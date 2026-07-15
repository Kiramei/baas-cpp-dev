#include "service/channels/ProviderHandler.h"

#include "service/auth/CanonicalJson.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace baas::service::channels {
namespace {

namespace ws = websocket;

enum class JsonKind : std::uint8_t { null, boolean, number, string, array, object };

struct ParsedJson {
    bool valid{};
    JsonKind kind{JsonKind::null};
    std::optional<std::string> request_type;
    bool resource_id_valid{true};
};

class JsonValidator final {
public:
    JsonValidator(
        std::string_view input,
        const ProviderHandlerLimits& limits,
        const std::size_t maximum_bytes)
        : input_(input), limits_(limits), maximum_bytes_(maximum_bytes)
    {}

    [[nodiscard]] ParsedJson parse(const bool capture_request)
    {
        result_.valid = auth::is_valid_utf8(input_)
            && input_.size() <= maximum_bytes_;
        if (!result_.valid) return result_;
        capture_request_ = capture_request;
        skip_space();
        const auto kind = value(0, true);
        skip_space();
        result_.valid = kind.has_value() && offset_ == input_.size();
        if (kind) result_.kind = *kind;
        return result_;
    }

private:
    [[nodiscard]] bool consume(const char value) noexcept
    {
        if (offset_ >= input_.size() || input_[offset_] != value) return false;
        ++offset_;
        return true;
    }

    void skip_space() noexcept
    {
        while (offset_ < input_.size()) {
            const auto value = input_[offset_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') break;
            ++offset_;
        }
    }

    [[nodiscard]] bool node() noexcept
    {
        if (nodes_ >= limits_.max_json_nodes) return false;
        ++nodes_;
        return true;
    }

    [[nodiscard]] static int hex(const char value) noexcept
    {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    [[nodiscard]] std::optional<std::uint32_t> unicode_escape()
    {
        if (offset_ + 4 > input_.size()) return std::nullopt;
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index) {
            const auto digit = hex(input_[offset_++]);
            if (digit < 0) return std::nullopt;
            value = (value << 4U) | static_cast<std::uint32_t>(digit);
        }
        if (value >= 0xD800U && value <= 0xDBFFU) {
            if (offset_ + 6 > input_.size() || input_[offset_] != '\\'
                || input_[offset_ + 1] != 'u') return std::nullopt;
            offset_ += 2;
            std::uint32_t low = 0;
            for (int index = 0; index < 4; ++index) {
                const auto digit = hex(input_[offset_++]);
                if (digit < 0) return std::nullopt;
                low = (low << 4U) | static_cast<std::uint32_t>(digit);
            }
            if (low < 0xDC00U || low > 0xDFFFU) return std::nullopt;
            return 0x10000U + ((value - 0xD800U) << 10U) + (low - 0xDC00U);
        }
        if (value >= 0xDC00U && value <= 0xDFFFU) return std::nullopt;
        return value;
    }

    static void append_utf8(std::string& output, const std::uint32_t value)
    {
        if (value <= 0x7FU) output.push_back(static_cast<char>(value));
        else if (value <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
        else if (value <= 0xFFFFU) {
            output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
        else {
            output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
    }

    [[nodiscard]] std::optional<std::string> string()
    {
        if (!consume('"')) return std::nullopt;
        std::string output;
        while (offset_ < input_.size()) {
            const auto value = static_cast<unsigned char>(input_[offset_++]);
            if (value == '"') return output;
            if (value < 0x20U) return std::nullopt;
            if (value != '\\') {
                output.push_back(static_cast<char>(value));
                continue;
            }
            if (offset_ >= input_.size()) return std::nullopt;
            const auto escaped = input_[offset_++];
            switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    auto codepoint = unicode_escape();
                    if (!codepoint) return std::nullopt;
                    append_utf8(output, *codepoint);
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool literal(const std::string_view text) noexcept
    {
        if (input_.substr(offset_, text.size()) != text) return false;
        offset_ += text.size();
        return true;
    }

    [[nodiscard]] bool number() noexcept
    {
        const auto begin = offset_;
        if (offset_ < input_.size() && input_[offset_] == '-') ++offset_;
        if (offset_ >= input_.size()) return false;
        if (input_[offset_] == '0') ++offset_;
        else if (input_[offset_] >= '1' && input_[offset_] <= '9') {
            while (offset_ < input_.size()
                   && input_[offset_] >= '0' && input_[offset_] <= '9') ++offset_;
        }
        else return false;
        if (offset_ < input_.size() && input_[offset_] == '.') {
            ++offset_;
            const auto fraction = offset_;
            while (offset_ < input_.size()
                   && input_[offset_] >= '0' && input_[offset_] <= '9') ++offset_;
            if (fraction == offset_) return false;
        }
        if (offset_ < input_.size()
            && (input_[offset_] == 'e' || input_[offset_] == 'E')) {
            ++offset_;
            if (offset_ < input_.size()
                && (input_[offset_] == '+' || input_[offset_] == '-')) ++offset_;
            const auto exponent = offset_;
            while (offset_ < input_.size()
                   && input_[offset_] >= '0' && input_[offset_] <= '9') ++offset_;
            if (exponent == offset_) return false;
        }
        return offset_ != begin;
    }

    [[nodiscard]] std::optional<JsonKind> array(const std::size_t depth)
    {
        if (!consume('[') || depth >= limits_.max_json_depth) return std::nullopt;
        skip_space();
        if (consume(']')) return JsonKind::array;
        while (true) {
            if (!value(depth + 1, false)) return std::nullopt;
            skip_space();
            if (consume(']')) return JsonKind::array;
            if (!consume(',')) return std::nullopt;
            skip_space();
        }
    }

    [[nodiscard]] std::optional<JsonKind> object(
        const std::size_t depth, const bool root)
    {
        if (!consume('{') || depth >= limits_.max_json_depth) return std::nullopt;
        std::unordered_set<std::string> keys;
        skip_space();
        if (consume('}')) return JsonKind::object;
        while (true) {
            auto key = string();
            if (!key || !keys.insert(*key).second) return std::nullopt;
            skip_space();
            if (!consume(':')) return std::nullopt;
            skip_space();
            if (root && capture_request_ && *key == "type") {
                auto type = string();
                if (!type) return std::nullopt;
                result_.request_type = std::move(*type);
                if (!node()) return std::nullopt;
            }
            else if (root && capture_request_ && *key == "resource_id") {
                if (offset_ < input_.size() && input_[offset_] == '"') {
                    if (!string() || !node()) return std::nullopt;
                }
                else if (!literal("null") || !node()) {
                    result_.resource_id_valid = false;
                    return std::nullopt;
                }
            }
            else if (!value(depth + 1, false)) return std::nullopt;
            skip_space();
            if (consume('}')) return JsonKind::object;
            if (!consume(',')) return std::nullopt;
            skip_space();
        }
    }

    [[nodiscard]] std::optional<JsonKind> value(
        const std::size_t depth, const bool root)
    {
        if (!node() || offset_ >= input_.size()) return std::nullopt;
        switch (input_[offset_]) {
            case 'n': return literal("null") ? std::optional{JsonKind::null} : std::nullopt;
            case 't': return literal("true") ? std::optional{JsonKind::boolean} : std::nullopt;
            case 'f': return literal("false") ? std::optional{JsonKind::boolean} : std::nullopt;
            case '"': return string() ? std::optional{JsonKind::string} : std::nullopt;
            case '[': return array(depth);
            case '{': return object(depth, root);
            default: return number() ? std::optional{JsonKind::number} : std::nullopt;
        }
    }

    std::string_view input_;
    const ProviderHandlerLimits& limits_;
    std::size_t maximum_bytes_{};
    ParsedJson result_;
    std::size_t offset_{};
    std::size_t nodes_{};
    bool capture_request_{};
};

[[nodiscard]] ParsedJson parse_json(
    const std::string_view input,
    const ProviderHandlerLimits& limits,
    const bool request = false)
{
    return JsonValidator{input, limits, limits.max_input_json_bytes}.parse(request);
}

[[nodiscard]] ParsedJson parse_output_json(
    const std::string_view input,
    const ProviderHandlerLimits& limits)
{
    return JsonValidator{input, limits, limits.max_output_json_bytes}.parse(false);
}

[[nodiscard]] bool append_bounded(
    std::string& output, const std::string_view value, const std::size_t maximum)
{
    if (output.size() > maximum || value.size() > maximum - output.size()) return false;
    output.append(value);
    return true;
}

[[nodiscard]] std::optional<std::string> envelope(
    const std::string_view prefix,
    const std::string_view value,
    const std::string_view suffix,
    const ProviderHandlerLimits& limits)
{
    try {
        std::string output;
        if (!append_bounded(output, prefix, limits.max_output_json_bytes)
            || !append_bounded(output, value, limits.max_output_json_bytes)
            || !append_bounded(output, suffix, limits.max_output_json_bytes)) {
            return std::nullopt;
        }
        return output;
    }
    catch (...) { return std::nullopt; }
}

[[nodiscard]] ws::BusinessHandlerStatus backend_status(
    const ProviderBackendError error) noexcept
{
    return error == ProviderBackendError::capacity
        ? ws::BusinessHandlerStatus::capacity
        : ws::BusinessHandlerStatus::internal_error;
}

class ProviderCore final : public std::enable_shared_from_this<ProviderCore> {
public:
    ProviderCore(
        std::shared_ptr<ws::BusinessPlaintextSink> output,
        ProviderHandlerLimits limits)
        : output_(std::move(output)), limits_(limits)
    {}

    void log(std::string payload) noexcept
    {
        try { push("{\"type\":\"log\",\"entry\":", payload); }
        catch (...) { static_cast<void>(fail()); }
    }
    void status(std::string payload) noexcept
    {
        try { push("{\"type\":\"status\",\"status\":", payload); }
        catch (...) { static_cast<void>(fail()); }
    }

    [[nodiscard]] bool initial(std::vector<ws::BusinessOutboundMessage> messages)
    {
        if (messages.size() < 2 || messages.size() > 3) return fail();
        {
            std::lock_guard lock{mutex_};
            if (phase_ == Phase::failed || phase_ == Phase::closed) return false;
        }
        std::vector<ws::BusinessOutboundMessage> first;
        first.push_back(std::move(messages[0]));
        first.push_back(std::move(messages[1]));
        if (output_->emit_batch(std::move(first)) != ws::BusinessEmitResult::accepted) {
            return fail();
        }
        if (messages.size() == 3
            && output_->emit(std::move(messages[2])) != ws::BusinessEmitResult::accepted) {
            return fail();
        }
        return drain();
    }

    [[nodiscard]] bool failed() const noexcept
    {
        std::lock_guard lock{mutex_};
        return phase_ == Phase::failed;
    }

    void stop_admission() noexcept
    {
        std::lock_guard lock{mutex_};
        phase_ = Phase::closed;
        pending_.clear();
        pending_bytes_ = 0;
    }

    void wait_idle() noexcept
    {
        std::unique_lock lock{mutex_};
        idle_.wait(lock, [&] { return in_flight_ == 0; });
    }

private:
    enum class Phase : std::uint8_t { initializing, draining, live, failed, closed };

    void push(const std::string_view prefix, const std::string& payload)
    {
        const auto parsed = parse_output_json(payload, limits_);
        if (!parsed.valid || parsed.kind != JsonKind::object) {
            static_cast<void>(fail());
            return;
        }
        auto message = envelope(prefix, payload, "}", limits_);
        if (!message) {
            static_cast<void>(fail());
            return;
        }
        {
            std::lock_guard lock{mutex_};
            if (phase_ == Phase::failed || phase_ == Phase::closed) return;
            if (phase_ != Phase::live) {
                if (pending_.size() >= limits_.max_pending_pushes
                    || message->size() > limits_.max_pending_push_bytes - pending_bytes_) {
                    phase_ = Phase::failed;
                    return;
                }
                pending_bytes_ += message->size();
                pending_.push_back(std::move(*message));
                return;
            }
            ++in_flight_;
        }
        const auto emitted = output_->emit({std::move(*message), false});
        {
            std::lock_guard lock{mutex_};
            if (emitted != ws::BusinessEmitResult::accepted
                && phase_ != Phase::closed) phase_ = Phase::failed;
            --in_flight_;
        }
        idle_.notify_all();
    }

    [[nodiscard]] bool drain()
    {
        {
            std::lock_guard lock{mutex_};
            if (phase_ == Phase::failed || phase_ == Phase::closed) return false;
            phase_ = Phase::draining;
        }
        while (true) {
            std::string message;
            {
                std::lock_guard lock{mutex_};
                if (phase_ == Phase::failed || phase_ == Phase::closed) return false;
                if (pending_.empty()) {
                    phase_ = Phase::live;
                    return true;
                }
                message = std::move(pending_.front());
                pending_.pop_front();
                pending_bytes_ -= message.size();
            }
            if (output_->emit({std::move(message), false})
                != ws::BusinessEmitResult::accepted) return fail();
        }
    }

    [[nodiscard]] bool fail() noexcept
    {
        std::lock_guard lock{mutex_};
        if (phase_ != Phase::closed) phase_ = Phase::failed;
        pending_.clear();
        pending_bytes_ = 0;
        return false;
    }

    std::shared_ptr<ws::BusinessPlaintextSink> output_;
    ProviderHandlerLimits limits_;
    mutable std::mutex mutex_;
    std::condition_variable idle_;
    Phase phase_{Phase::initializing};
    std::deque<std::string> pending_;
    std::size_t pending_bytes_{};
    std::size_t in_flight_{};
};

class ProviderHandler final : public ws::BusinessChannelHandler {
public:
    ProviderHandler(
        std::shared_ptr<ProviderBackend> backend,
        std::shared_ptr<ws::BusinessPlaintextSink> output,
        ProviderHandlerLimits limits)
        : backend_(std::move(backend)), limits_(limits),
          core_(std::make_shared<ProviderCore>(std::move(output), limits))
    {}

    ~ProviderHandler() override { shutdown(); }

    ws::BusinessHandlerResult ready(const std::stop_token stop) override
    {
        try {
            if (stop.stop_requested()) return {{}, ws::BusinessHandlerStatus::complete};
            std::weak_ptr<ProviderCore> weak = core_;
            auto logs_subscription = backend_->subscribe_logs(
                [weak](std::string value) {
                    if (auto core = weak.lock()) core->log(std::move(value));
                });
            if (!logs_subscription) return failure(logs_subscription.error);
            log_subscription_ = std::move(logs_subscription.subscription);
            auto status_subscription = backend_->subscribe_status(
                [weak](std::string value) {
                    if (auto core = weak.lock()) core->status(std::move(value));
                });
            if (!status_subscription) return failure(status_subscription.error);
            status_subscription_ = std::move(status_subscription.subscription);

            auto logs = backend_->logs_full(stop);
            auto status = backend_->status(stop);
            auto initialized = backend_->all_data_initialized(stop);
            if (!logs) return failure(logs.error);
            if (!status) return failure(status.error);
            if (!initialized) return failure(initialized.error);
            const auto scopes = parse_output_json(logs->scopes_json, limits_);
            const auto entries = parse_output_json(logs->entries_json, limits_);
            const auto status_value = parse_output_json(*status.value, limits_);
            if (!scopes.valid || scopes.kind != JsonKind::array
                || !entries.valid || entries.kind != JsonKind::array
                || !status_value.valid || status_value.kind != JsonKind::object) {
                return failure(ProviderBackendError::internal_error);
            }
            auto logs_message = logs_full(*logs.value);
            auto status_message = envelope(
                "{\"type\":\"status\",\"status\":", *status.value, "}", limits_);
            if (!logs_message || !status_message) {
                return failure(ProviderBackendError::capacity);
            }
            std::vector<ws::BusinessOutboundMessage> messages;
            messages.push_back({std::move(*logs_message), false});
            messages.push_back({std::move(*status_message), false});
            if (initialized.value->value_or(false)) {
                messages.push_back({
                    "{\"type\":\"status\",\"status\":{\"is_all_data_initialized\":true}}",
                    false});
            }
            if (!core_->initial(std::move(messages))) {
                return failure(ProviderBackendError::internal_error);
            }
            return {};
        }
        catch (...) { return failure(ProviderBackendError::internal_error); }
    }

    ws::BusinessHandlerResult input(
        auth::SecretBuffer plaintext,
        const bool peer_final,
        const std::stop_token stop) override
    {
        try {
            if (core_->failed()) return {{}, ws::BusinessHandlerStatus::internal_error};
            if (stop.stop_requested()) return {{}, ws::BusinessHandlerStatus::complete};
            if (peer_final && plaintext.empty()) return {};
            const std::string_view text{
                reinterpret_cast<const char*>(plaintext.bytes().data()),
                plaintext.size()};
            const auto request = parse_json(text, limits_, true);
            if (!request.valid || request.kind != JsonKind::object
                || !request.request_type || !request.resource_id_valid) {
                return {{}, ws::BusinessHandlerStatus::protocol_failed};
            }
            if (*request.request_type == "status_request") {
                auto status = backend_->status(stop);
                if (!status) return failure(status.error);
                const auto parsed = parse_output_json(*status.value, limits_);
                if (!parsed.valid || parsed.kind != JsonKind::object) {
                    return failure(ProviderBackendError::internal_error);
                }
                auto message = envelope(
                    "{\"type\":\"status\",\"status\":", *status.value, "}", limits_);
                return response(std::move(message));
            }
            if (*request.request_type == "static_request") {
                auto snapshot = backend_->static_snapshot(stop);
                if (!snapshot) return failure(snapshot.error);
                const auto timestamp = parse_output_json(snapshot->timestamp_json, limits_);
                const auto data = parse_output_json(snapshot->data_json, limits_);
                if (!timestamp.valid || timestamp.kind != JsonKind::number || !data.valid) {
                    return failure(ProviderBackendError::internal_error);
                }
                std::string message{"{\"type\":\"static_snapshot\",\"timestamp\":"};
                if (!append_bounded(message, snapshot->timestamp_json,
                                    limits_.max_output_json_bytes)
                    || !append_bounded(message, ",\"data\":",
                                       limits_.max_output_json_bytes)
                    || !append_bounded(message, snapshot->data_json,
                                       limits_.max_output_json_bytes)
                    || !append_bounded(message, "}", limits_.max_output_json_bytes)) {
                    return failure(ProviderBackendError::capacity);
                }
                return {{{std::move(message), false}}, ws::BusinessHandlerStatus::ok};
            }
            return {{}, ws::BusinessHandlerStatus::protocol_failed};
        }
        catch (...) { return failure(ProviderBackendError::internal_error); }
    }

    ws::BusinessHandlerResult heartbeat(const std::stop_token stop) override
    {
        if (stop.stop_requested()) {
            return {{}, ws::BusinessHandlerStatus::complete};
        }
        return core_->failed()
            ? ws::BusinessHandlerResult{{}, ws::BusinessHandlerStatus::internal_error}
            : ws::BusinessHandlerResult{};
    }

    void closed(ws::BusinessCloseReason) noexcept override { shutdown(); }

private:
    [[nodiscard]] std::optional<std::string> logs_full(
        const ProviderLogsFull& logs) const
    {
        std::string message{"{\"type\":\"logs_full\",\"scopes\":"};
        if (!append_bounded(message, logs.scopes_json, limits_.max_output_json_bytes)
            || !append_bounded(message, ",\"entries\":",
                               limits_.max_output_json_bytes)
            || !append_bounded(message, logs.entries_json,
                               limits_.max_output_json_bytes)
            || !append_bounded(message, "}", limits_.max_output_json_bytes)) {
            return std::nullopt;
        }
        return message;
    }

    [[nodiscard]] ws::BusinessHandlerResult response(
        std::optional<std::string> message)
    {
        if (!message) return failure(ProviderBackendError::capacity);
        return {{{std::move(*message), false}}, ws::BusinessHandlerStatus::ok};
    }

    [[nodiscard]] ws::BusinessHandlerResult failure(const ProviderBackendError error)
    {
        shutdown();
        return {{}, backend_status(error)};
    }

    void shutdown() noexcept
    {
        if (!core_) return;
        core_->stop_admission();
        log_subscription_.reset();
        status_subscription_.reset();
        core_->wait_idle();
    }

    std::shared_ptr<ProviderBackend> backend_;
    ProviderHandlerLimits limits_;
    std::shared_ptr<ProviderCore> core_;
    std::unique_ptr<ProviderSubscription> log_subscription_;
    std::unique_ptr<ProviderSubscription> status_subscription_;
};

[[nodiscard]] bool valid_limits(const ProviderHandlerLimits& limits) noexcept
{
    return limits.max_input_json_bytes != 0
        && limits.max_output_json_bytes != 0
        && limits.max_json_depth != 0 && limits.max_json_nodes != 0
        && limits.max_pending_pushes != 0
        && limits.max_pending_push_bytes >= limits.max_output_json_bytes;
}

}  // namespace

ProviderHandlerFactory::ProviderHandlerFactory(
    std::shared_ptr<ProviderBackend> backend,
    const ProviderHandlerLimits limits)
    : backend_(std::move(backend)), limits_(limits)
{
    if (!backend_ || !valid_limits(limits_)) {
        throw std::invalid_argument("invalid provider handler dependencies or bounds");
    }
}

ws::BusinessHandlerCreateResult ProviderHandlerFactory::create(
    ws::BusinessSessionContext context,
    std::shared_ptr<ws::BusinessPlaintextSink> output,
    const std::stop_token stop)
{
    if (!output || stop.stop_requested()
        || context.channel != auth::BusinessChannel::provider) {
        return {nullptr, ws::BusinessHandlerCreateError::internal_error};
    }
    try {
        return {std::make_unique<ProviderHandler>(backend_, std::move(output), limits_),
                ws::BusinessHandlerCreateError::none};
    }
    catch (...) { return {nullptr, ws::BusinessHandlerCreateError::internal_error}; }
}

}  // namespace baas::service::channels
