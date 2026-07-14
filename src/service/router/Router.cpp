#include "service/router/Router.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace baas::service::router {
namespace {

constexpr std::size_t minimum_response_budget = 128;
constexpr std::string_view health_path = "/api/v1/health";
constexpr std::string_view version_path = "/api/v1/version";
constexpr std::string_view shutdown_path = "/api/v1/shutdown";
constexpr std::string_view response_budget_error =
    R"({"error":{"code":"response_too_large","message":"response exceeds configured budget","status":500},"ok":false})";
static_assert(response_budget_error.size() <= minimum_response_budget);

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

[[nodiscard]] std::string json_string(const std::string_view value)
{
    std::string output;
    output.reserve(value.size() + 2);
    output.push_back('"');
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (byte < 0x20U) {
                output += "\\u00";
                output.push_back(hex_digit((byte >> 4U) & 0x0FU));
                output.push_back(hex_digit(byte & 0x0FU));
            } else {
                output.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    output.push_back('"');
    return output;
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

Router::Router(ServiceInfo service, const SizeBudget budget, ShutdownIntent* shutdown_intent)
    : service_(std::move(service)), budget_(budget), shutdown_intent_(shutdown_intent)
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
        return json_response(200, R"({"api_version":1,"ok":true,"status":"healthy"})");
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
