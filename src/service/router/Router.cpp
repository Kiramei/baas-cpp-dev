#include "service/router/Router.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace baas::service::router {
namespace {

constexpr std::size_t minimum_response_budget = 128;
constexpr std::size_t maximum_health_depth = 64;
constexpr std::string_view health_path = "/health";
constexpr std::string_view version_path = "/version";
constexpr std::string_view shutdown_path = "/shutdown";
constexpr std::string_view response_budget_error =
    R"({"error":{"code":"response_too_large","message":"response exceeds configured budget","status":500},"ok":false})";
static_assert(response_budget_error.size() <= minimum_response_budget);

enum class SnapshotValidation { valid, invalid, too_large };

[[nodiscard]] char hex_digit(const unsigned int value) noexcept
{
    return value < 10U ? static_cast<char>('0' + value)
                       : static_cast<char>('A' + (value - 10U));
}

[[nodiscard]] bool is_valid_utf8(const std::string_view value) noexcept
{
    std::size_t index = 0;
    while (index < value.size()) {
        const auto lead = static_cast<unsigned char>(value[index]);
        if (lead < 0x80U) {
            ++index;
            continue;
        }

        std::size_t continuation_count = 0;
        std::uint32_t code_point = 0;
        std::uint32_t minimum = 0;
        if ((lead & 0xE0U) == 0xC0U) {
            continuation_count = 1;
            code_point = lead & 0x1FU;
            minimum = 0x80U;
        } else if ((lead & 0xF0U) == 0xE0U) {
            continuation_count = 2;
            code_point = lead & 0x0FU;
            minimum = 0x800U;
        } else if ((lead & 0xF8U) == 0xF0U) {
            continuation_count = 3;
            code_point = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (index + continuation_count >= value.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if (code_point < minimum || code_point > 0x10FFFFU
            || (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            return false;
        }
        index += continuation_count + 1;
    }
    return true;
}

class BoundedWriter final {
public:
    explicit BoundedWriter(const std::size_t limit) : limit_(limit)
    {
        output_.reserve(std::min(limit, std::size_t{4'096}));
    }

    bool append(const std::string_view value)
    {
        if (exceeded_) return false;
        if (value.size() > limit_ - output_.size()) {
            exceeded_ = true;
            return false;
        }
        output_.append(value);
        return true;
    }

    bool append(const char value)
    {
        return append(std::string_view{&value, 1});
    }

    [[nodiscard]] bool exceeded() const noexcept { return exceeded_; }
    [[nodiscard]] std::string take() { return std::move(output_); }

private:
    std::size_t limit_;
    std::string output_;
    bool exceeded_ = false;
};

bool append_json_string(BoundedWriter& output, const std::string_view value)
{
    if (!output.append('"')) return false;
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"': if (!output.append("\\\"")) return false; break;
        case '\\': if (!output.append("\\\\")) return false; break;
        case '\b': if (!output.append("\\b")) return false; break;
        case '\f': if (!output.append("\\f")) return false; break;
        case '\n': if (!output.append("\\n")) return false; break;
        case '\r': if (!output.append("\\r")) return false; break;
        case '\t': if (!output.append("\\t")) return false; break;
        default:
            if (byte < 0x20U) {
                char escaped[] = {'\\', 'u', '0', '0', hex_digit((byte >> 4U) & 0x0FU),
                                  hex_digit(byte & 0x0FU)};
                if (!output.append(std::string_view{escaped, sizeof(escaped)})) return false;
            } else if (!output.append(static_cast<char>(byte))) {
                return false;
            }
            break;
        }
    }
    return output.append('"');
}

[[nodiscard]] std::string json_string(const std::string_view value)
{
    BoundedWriter output{std::numeric_limits<std::size_t>::max()};
    static_cast<void>(append_json_string(output, value));
    return output.take();
}

SnapshotValidation canonicalize_value(
    HealthValue& value,
    const std::size_t depth,
    std::size_t& nodes,
    const std::size_t node_limit
);

SnapshotValidation canonicalize_object(
    HealthObject& object,
    const std::size_t depth,
    std::size_t& nodes,
    const std::size_t node_limit
)
{
    if (depth > maximum_health_depth) return SnapshotValidation::invalid;
    if (object.size() > node_limit - std::min(nodes, node_limit)) {
        return SnapshotValidation::too_large;
    }
    for (const auto& [key, unused] : object) {
        static_cast<void>(unused);
        if (!is_valid_utf8(key)) return SnapshotValidation::invalid;
    }
    std::sort(object.begin(), object.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    for (std::size_t index = 1; index < object.size(); ++index) {
        if (object[index - 1].first == object[index].first) {
            return SnapshotValidation::invalid;
        }
    }
    for (auto& [key, value] : object) {
        static_cast<void>(key);
        const auto result = canonicalize_value(value, depth, nodes, node_limit);
        if (result != SnapshotValidation::valid) return result;
    }
    return SnapshotValidation::valid;
}

SnapshotValidation canonicalize_value(
    HealthValue& value,
    const std::size_t depth,
    std::size_t& nodes,
    const std::size_t node_limit
)
{
    if (nodes >= node_limit) return SnapshotValidation::too_large;
    ++nodes;
    switch (value.kind()) {
    case HealthValueKind::null:
    case HealthValueKind::boolean:
    case HealthValueKind::integer:
        return SnapshotValidation::valid;
    case HealthValueKind::floating:
        return std::isfinite(std::get<double>(value.storage))
            ? SnapshotValidation::valid : SnapshotValidation::invalid;
    case HealthValueKind::string:
        return is_valid_utf8(std::get<std::string>(value.storage))
            ? SnapshotValidation::valid : SnapshotValidation::invalid;
    case HealthValueKind::array:
        if (depth > maximum_health_depth) return SnapshotValidation::invalid;
        if (const auto& values = std::get<HealthArray>(value.storage);
            values.size() > node_limit - std::min(nodes, node_limit)) {
            return SnapshotValidation::too_large;
        }
        for (auto& child : std::get<HealthArray>(value.storage)) {
            const auto result = canonicalize_value(child, depth + 1, nodes, node_limit);
            if (result != SnapshotValidation::valid) return result;
        }
        return SnapshotValidation::valid;
    case HealthValueKind::object:
        return canonicalize_object(
            std::get<HealthObject>(value.storage), depth + 1, nodes, node_limit
        );
    }
    return SnapshotValidation::invalid;
}

SnapshotValidation canonicalize_snapshot(
    HealthSnapshot& snapshot,
    const std::size_t node_limit
)
{
    if (!is_valid_utf8(snapshot.auth.server_sign_public_key)) {
        return SnapshotValidation::invalid;
    }
    std::size_t nodes = 0;
    return canonicalize_object(snapshot.statuses, 1, nodes, node_limit);
}

bool append_health_value(BoundedWriter& output, const HealthValue& value);

bool append_health_object(BoundedWriter& output, const HealthObject& object)
{
    if (!output.append('{')) return false;
    for (std::size_t index = 0; index < object.size(); ++index) {
        if (index != 0 && !output.append(',')) return false;
        if (!append_json_string(output, object[index].first) || !output.append(':')
            || !append_health_value(output, object[index].second)) {
            return false;
        }
    }
    return output.append('}');
}

bool append_health_value(BoundedWriter& output, const HealthValue& value)
{
    switch (value.kind()) {
    case HealthValueKind::null: return output.append("null");
    case HealthValueKind::boolean:
        return output.append(std::get<bool>(value.storage) ? "true" : "false");
    case HealthValueKind::integer: {
        char buffer[32]{};
        const auto [end, error] = std::to_chars(
            std::begin(buffer), std::end(buffer), std::get<std::int64_t>(value.storage)
        );
        return error == std::errc{}
            && output.append(std::string_view{buffer, static_cast<std::size_t>(end - buffer)});
    }
    case HealthValueKind::floating: {
        char buffer[64]{};
        const auto [end, error] = std::to_chars(
            std::begin(buffer), std::end(buffer), std::get<double>(value.storage)
        );
        if (error != std::errc{}) return false;
        const std::string_view encoded{buffer, static_cast<std::size_t>(end - buffer)};
        if (!output.append(encoded)) return false;
        if (encoded.find_first_of(".eE") == std::string_view::npos) {
            return output.append(".0");
        }
        return true;
    }
    case HealthValueKind::string:
        return append_json_string(output, std::get<std::string>(value.storage));
    case HealthValueKind::array: {
        if (!output.append('[')) return false;
        const auto& values = std::get<HealthArray>(value.storage);
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0 && !output.append(',')) return false;
            if (!append_health_value(output, values[index])) return false;
        }
        return output.append(']');
    }
    case HealthValueKind::object:
        return append_health_object(output, std::get<HealthObject>(value.storage));
    }
    return false;
}

[[nodiscard]] std::optional<std::string> serialize_health(
    const HealthSnapshot& snapshot,
    const std::size_t limit
)
{
    BoundedWriter output{limit};
    if (!output.append(R"({"ok":true,"statuses":)")
        || !append_health_object(output, snapshot.statuses)
        || !output.append(R"(,"auth":{"initialized":)")
        || !output.append(snapshot.auth.initialized ? "true" : "false")
        || !output.append(R"(,"pwd_epoch":)")
        || !output.append(std::to_string(snapshot.auth.pwd_epoch))
        || !output.append(R"(,"server_sign_public_key":)")
        || !append_json_string(output, snapshot.auth.server_sign_public_key)
        || !output.append("}}") || output.exceeded()) {
        return std::nullopt;
    }
    return output.take();
}

[[nodiscard]] bool is_known_path(const std::string_view path) noexcept
{
    return path == health_path || path == version_path || path == shutdown_path;
}

[[nodiscard]] std::string_view allowed_method(const std::string_view path) noexcept
{
    return path == shutdown_path ? "POST" : "GET";
}

void add_allow_header(Response& response, const std::string_view method)
{
    response.headers.push_back(Header{"Allow", std::string{method}});
}

}  // namespace

HealthValueKind HealthValue::kind() const noexcept
{
    return static_cast<HealthValueKind>(storage.index());
}

Router::Router(
    ServiceInfo service,
    const SizeBudget budget,
    ShutdownIntent* shutdown_intent,
    std::shared_ptr<RouteExtension> extension
)
    : Router(
          std::move(service), budget, shutdown_intent, std::nullopt, nullptr,
          std::move(extension))
{}

Router Router::with_health_snapshot(
    ServiceInfo service,
    HealthSnapshot health,
    const SizeBudget budget,
    ShutdownIntent* shutdown_intent,
    std::shared_ptr<RouteExtension> extension
)
{
    return Router{
        std::move(service), budget, shutdown_intent, std::move(health), nullptr,
        std::move(extension)
    };
}

Router Router::with_health_provider(
    ServiceInfo service,
    std::shared_ptr<HealthSnapshotProvider> health_provider,
    const SizeBudget budget,
    ShutdownIntent* shutdown_intent,
    std::shared_ptr<RouteExtension> extension
)
{
    if (!health_provider) {
        throw std::invalid_argument("health snapshot provider must not be null");
    }
    return Router{
        std::move(service), budget, shutdown_intent, std::nullopt,
        std::move(health_provider), std::move(extension)
    };
}

Router::Router(
    ServiceInfo service,
    const SizeBudget budget,
    ShutdownIntent* shutdown_intent,
    std::optional<HealthSnapshot> health_snapshot,
    std::shared_ptr<HealthSnapshotProvider> health_provider,
    std::shared_ptr<RouteExtension> extension
)
    : service_(std::move(service)),
      budget_(budget),
      shutdown_intent_(shutdown_intent),
      health_snapshot_(std::move(health_snapshot)),
      health_provider_(std::move(health_provider)),
      extension_(std::move(extension))
{
    if (service_.name.empty() || service_.version.empty()) {
        throw std::invalid_argument("service name and version must not be empty");
    }
    if (!is_valid_utf8(service_.name) || !is_valid_utf8(service_.version)) {
        throw std::invalid_argument("service name and version must be valid UTF-8");
    }
    if (budget_.max_method_bytes == 0 || budget_.max_path_bytes == 0
        || budget_.max_request_body_bytes == 0
        || budget_.max_response_body_bytes < minimum_response_budget) {
        throw std::invalid_argument("service router size budget is invalid");
    }
    if (health_snapshot_.has_value()) {
        const auto validation = canonicalize_snapshot(
            *health_snapshot_, budget_.max_response_body_bytes
        );
        if (validation == SnapshotValidation::invalid) {
            throw std::invalid_argument("static health snapshot must be JSON-safe UTF-8");
        }
    }
}

const SizeBudget& Router::budget() const noexcept
{
    return budget_;
}

Response Router::handle(const Request& request) const
{
    if (request.method.size() > budget_.max_method_bytes) {
        return finish(error(400, "method_too_large", "request method exceeds configured budget"));
    }
    if (request.path.size() > budget_.max_path_bytes) {
        return finish(error(414, "path_too_large", "request path exceeds configured budget"));
    }
    if (request.body.size() > budget_.max_request_body_bytes) {
        return finish(error(413, "request_too_large", "request body exceeds configured budget"));
    }
    if (request.path.empty() || request.path.front() != '/'
        || request.path.find_first_of("?#") != std::string_view::npos) {
        return finish(error(400, "invalid_path", "path must be an absolute normalized route path"));
    }
    return finish(route(request));
}

Response Router::route(const Request& request) const
{
    if (extension_) {
        try {
            if (auto response = extension_->handle(request); response.has_value()) {
                return std::move(*response);
            }
        } catch (...) {
            return error(500, "route_extension_failed", "route extension failed");
        }
    }
    if (!is_known_path(request.path)) {
        return error(404, "route_not_found", "no route matches the request path");
    }
    const auto expected_method = allowed_method(request.path);
    if (request.method != expected_method) {
        auto response = error(405, "method_not_allowed", "request method is not allowed for this path");
        add_allow_header(response, expected_method);
        return response;
    }
    if (request.path == health_path) {
        return health();
    }
    if (request.path == version_path) {
        return json_response(
            200,
            std::string{"{\"api_version\":1,\"ok\":true,\"service\":"}
                + json_string(service_.name) + ",\"version\":" + json_string(service_.version) + "}"
        );
    }

    if (shutdown_intent_ == nullptr) {
        return error(503, "shutdown_unavailable", "no shutdown intent is configured");
    }
    if (shutdown_intent_->request_shutdown() == ShutdownDecision::rejected) {
        return error(409, "shutdown_rejected", "shutdown intent rejected the request");
    }
    return json_response(202, R"({"accepted":true,"api_version":1,"ok":true})");
}

Response Router::health() const
{
    HealthSnapshot dynamic_snapshot;
    const HealthSnapshot* snapshot = nullptr;
    if (health_provider_) {
        HealthReadinessSnapshot readiness;
        try {
            readiness = health_provider_->readiness_snapshot();
        } catch (...) {
            return error(503, "health_provider_failed", "health snapshot provider failed");
        }
        if (readiness.state == HealthReadinessState::starting) {
            return error(503, "health_starting", "service readiness is starting");
        }
        if (readiness.state == HealthReadinessState::failed) {
            return error(503, "health_failed", "service readiness failed");
        }
        if (readiness.state != HealthReadinessState::ready) {
            return error(500, "invalid_health_snapshot", "health readiness state is invalid");
        }
        dynamic_snapshot = std::move(readiness.health);
        const auto validation = canonicalize_snapshot(
            dynamic_snapshot, budget_.max_response_body_bytes
        );
        if (validation == SnapshotValidation::invalid) {
            return error(500, "invalid_health_snapshot", "health snapshot is not JSON-safe UTF-8");
        }
        if (validation == SnapshotValidation::too_large) {
            return json_response(500, std::string{response_budget_error});
        }
        snapshot = &dynamic_snapshot;
    } else if (health_snapshot_.has_value()) {
        snapshot = &*health_snapshot_;
    } else {
        return error(503, "health_unavailable", "no health snapshot is configured");
    }

    const auto body = serialize_health(*snapshot, budget_.max_response_body_bytes);
    if (!body.has_value()) {
        return json_response(500, std::string{response_budget_error});
    }
    return json_response(200, *body);
}

Response Router::finish(Response response) const
{
    if (response.body.size() <= budget_.max_response_body_bytes) {
        return response;
    }
    return json_response(500, std::string{response_budget_error});
}

Response Router::json_response(const int status, std::string body)
{
    return Response{
        status,
        {Header{"Content-Type", "application/json; charset=utf-8"}},
        std::move(body),
    };
}

Response Router::error(
    const int status,
    const std::string_view code,
    const std::string_view message
)
{
    return json_response(
        status,
        std::string{"{\"error\":{\"code\":"} + json_string(code)
            + ",\"message\":" + json_string(message) + ",\"status\":"
            + std::to_string(status) + "},\"ok\":false}"
    );
}

}  // namespace baas::service::router
