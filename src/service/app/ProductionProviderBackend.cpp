#include "service/app/ProductionProviderBackend.h"

#include "service/auth/CanonicalJson.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iterator>
#include <list>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace baas::service::app {
namespace {

using channels::ProviderBackendError;

enum class JsonKind : std::uint8_t { null, boolean, number, string, array, object };

struct JsonValidation {
    bool valid{};
    JsonKind kind{JsonKind::null};
    std::optional<std::string> root_scope;
};

class JsonValidator final {
public:
    JsonValidator(std::string_view input, const ProductionProviderBackendLimits& limits)
        : input_(input), limits_(limits)
    {}

    [[nodiscard]] JsonValidation parse(const bool capture_scope)
    {
        result_.valid = input_.size() <= limits_.max_json_bytes
            && auth::is_valid_utf8(input_);
        if (!result_.valid) return result_;
        capture_scope_ = capture_scope;
        skip_space();
        const auto parsed = value(0, true);
        skip_space();
        result_.valid = parsed.has_value() && offset_ == input_.size();
        if (parsed) result_.kind = *parsed;
        return result_;
    }

private:
    [[nodiscard]] bool consume(const char expected) noexcept
    {
        if (offset_ >= input_.size() || input_[offset_] != expected) return false;
        ++offset_;
        return true;
    }

    void skip_space() noexcept
    {
        while (offset_ < input_.size()) {
            const char value = input_[offset_];
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
            const int digit = hex(input_[offset_++]);
            if (digit < 0) return std::nullopt;
            value = (value << 4U) | static_cast<std::uint32_t>(digit);
        }
        if (value >= 0xD800U && value <= 0xDBFFU) {
            if (offset_ + 6 > input_.size() || input_[offset_] != '\\'
                || input_[offset_ + 1] != 'u') return std::nullopt;
            offset_ += 2;
            std::uint32_t low = 0;
            for (int index = 0; index < 4; ++index) {
                const int digit = hex(input_[offset_++]);
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
        std::string decoded;
        while (offset_ < input_.size()) {
            const auto value = static_cast<unsigned char>(input_[offset_++]);
            if (value == '"') return decoded;
            if (value < 0x20U) return std::nullopt;
            if (value != '\\') {
                decoded.push_back(static_cast<char>(value));
                continue;
            }
            if (offset_ >= input_.size()) return std::nullopt;
            switch (input_[offset_++]) {
                case '"': decoded.push_back('"'); break;
                case '\\': decoded.push_back('\\'); break;
                case '/': decoded.push_back('/'); break;
                case 'b': decoded.push_back('\b'); break;
                case 'f': decoded.push_back('\f'); break;
                case 'n': decoded.push_back('\n'); break;
                case 'r': decoded.push_back('\r'); break;
                case 't': decoded.push_back('\t'); break;
                case 'u': {
                    const auto codepoint = unicode_escape();
                    if (!codepoint) return std::nullopt;
                    append_utf8(decoded, *codepoint);
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool literal(const std::string_view literal) noexcept
    {
        if (input_.substr(offset_, literal.size()) != literal) return false;
        offset_ += literal.size();
        return true;
    }

    [[nodiscard]] bool number() noexcept
    {
        const std::size_t begin = offset_;
        if (offset_ < input_.size() && input_[offset_] == '-') ++offset_;
        if (offset_ >= input_.size()) return false;
        if (input_[offset_] == '0') ++offset_;
        else if (input_[offset_] >= '1' && input_[offset_] <= '9') {
            while (offset_ < input_.size() && input_[offset_] >= '0'
                   && input_[offset_] <= '9') ++offset_;
        }
        else return false;
        if (offset_ < input_.size() && input_[offset_] == '.') {
            ++offset_;
            const std::size_t fraction = offset_;
            while (offset_ < input_.size() && input_[offset_] >= '0'
                   && input_[offset_] <= '9') ++offset_;
            if (fraction == offset_) return false;
        }
        if (offset_ < input_.size()
            && (input_[offset_] == 'e' || input_[offset_] == 'E')) {
            ++offset_;
            if (offset_ < input_.size()
                && (input_[offset_] == '+' || input_[offset_] == '-')) ++offset_;
            const std::size_t exponent = offset_;
            while (offset_ < input_.size() && input_[offset_] >= '0'
                   && input_[offset_] <= '9') ++offset_;
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
            if (root && capture_scope_ && *key == "scope"
                && offset_ < input_.size() && input_[offset_] == '"') {
                auto scope = string();
                if (!scope || !node()) return std::nullopt;
                result_.root_scope = std::move(*scope);
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
    const ProductionProviderBackendLimits& limits_;
    JsonValidation result_;
    std::size_t offset_{};
    std::size_t nodes_{};
    bool capture_scope_{};
};

[[nodiscard]] std::string encode_json_string(const std::string_view value)
{
    static constexpr char hex[] = "0123456789abcdef";
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
                    output.push_back(hex[byte >> 4U]);
                    output.push_back(hex[byte & 0x0FU]);
                }
                else output.push_back(static_cast<char>(byte));
                break;
        }
    }
    output.push_back('"');
    return output;
}

[[nodiscard]] bool invalid_limits(const ProductionProviderBackendLimits& limits) noexcept
{
    return limits.max_json_bytes == 0 || limits.max_json_depth == 0
        || limits.max_json_nodes == 0 || limits.max_scope_bytes == 0
        || limits.max_scopes == 0 || limits.max_history_entries == 0
        || limits.max_history_bytes == 0 || limits.max_snapshot_json_bytes < 4
        || limits.max_subscriptions_per_stream == 0;
}

}  // namespace

class ProductionProviderBackend::Impl final
    : public std::enable_shared_from_this<ProductionProviderBackend::Impl> {
public:
    explicit Impl(ProductionProviderBackendLimits limits)
        : limits_(limits), status_(std::make_shared<const std::string>("{}")),
          timestamp_(std::make_shared<const std::string>("0")),
          static_data_(std::make_shared<const std::string>("null"))
    {}

    ~Impl() { close(); }

    enum class Stream : std::uint8_t { logs, status };

    class CallbackSlot final {
    public:
        explicit CallbackSlot(PushCallback callback) : callback_(std::move(callback)) {}

        enum class InvokeResult : std::uint8_t { skipped, delivered, failed };

        [[nodiscard]] InvokeResult invoke(const std::string& payload) noexcept
        {
            {
                std::lock_guard lock{mutex_};
                if (!accepting_) return InvokeResult::skipped;
                ++active_calls_;
            }
            CallbackSlot* previous = current_;
            current_ = this;
            bool failed = false;
            try { callback_(payload); }
            catch (...) { failed = true; }
            current_ = previous;
            {
                std::lock_guard lock{mutex_};
                --active_calls_;
                if (active_calls_ == 0) idle_.notify_all();
            }
            return failed ? InvokeResult::failed : InvokeResult::delivered;
        }

        void close() noexcept
        {
            std::unique_lock lock{mutex_};
            accepting_ = false;
            // Self-unsubscribe cannot wait for its own callback. Admission is
            // still closed, and the publisher retains this slot through return.
            if (current_ == this) return;
            idle_.wait(lock, [this] { return active_calls_ == 0; });
        }

    private:
        inline static thread_local CallbackSlot* current_{};
        std::mutex mutex_;
        std::condition_variable idle_;
        PushCallback callback_;
        std::size_t active_calls_{};
        bool accepting_{true};
    };

    class Subscription final : public channels::ProviderSubscription {
    public:
        Subscription(
            std::shared_ptr<Impl> owner, const Stream stream, const std::uint64_t id,
            std::shared_ptr<CallbackSlot> slot) noexcept
            : owner_(std::move(owner)), stream_(stream), id_(id), slot_(std::move(slot))
        {}
        ~Subscription() override
        {
            owner_->unsubscribe(stream_, id_, slot_);
        }
    private:
        std::shared_ptr<Impl> owner_;
        Stream stream_;
        std::uint64_t id_{};
        std::shared_ptr<CallbackSlot> slot_;
    };

    [[nodiscard]] channels::ProviderBackendResult<channels::ProviderLogsFull>
    logs_full(const std::stop_token stop)
    {
        if (stop.stop_requested()) return {{}, ProviderBackendError::internal_error};
        try {
            std::lock_guard lock{mutex_};
            if (closed_ || stop.stop_requested()) {
                return {{}, ProviderBackendError::internal_error};
            }
            std::string scopes;
            scopes.reserve(scopes_json_bytes_);
            scopes.push_back('[');
            bool first = true;
            for (const auto& scope : scope_order_) {
                if (!first) scopes.push_back(',');
                first = false;
                scopes.append(scopes_.at(scope).encoded);
            }
            scopes.push_back(']');

            std::string entries;
            entries.reserve(entries_json_bytes_);
            entries.push_back('[');
            first = true;
            for (const auto& entry : history_) {
                if (!first) entries.push_back(',');
                first = false;
                entries.append(*entry.payload);
            }
            entries.push_back(']');
            if (stop.stop_requested()) return {{}, ProviderBackendError::internal_error};
            return {channels::ProviderLogsFull{std::move(scopes), std::move(entries)},
                    ProviderBackendError::none};
        }
        catch (...) { return {{}, ProviderBackendError::capacity}; }
    }

    [[nodiscard]] channels::ProviderBackendResult<std::string>
    status(const std::stop_token stop)
    {
        if (stop.stop_requested()) return {{}, ProviderBackendError::internal_error};
        try {
            std::shared_ptr<const std::string> value;
            {
                std::lock_guard lock{mutex_};
                if (closed_) return {{}, ProviderBackendError::internal_error};
                value = status_;
            }
            if (stop.stop_requested()) return {{}, ProviderBackendError::internal_error};
            return {std::string{*value}, ProviderBackendError::none};
        }
        catch (...) { return {{}, ProviderBackendError::capacity}; }
    }

    [[nodiscard]] channels::ProviderBackendResult<std::optional<bool>>
    all_data_initialized(const std::stop_token stop)
    {
        if (stop.stop_requested()) return {{}, ProviderBackendError::internal_error};
        std::lock_guard lock{mutex_};
        if (closed_ || stop.stop_requested()) return {{}, ProviderBackendError::internal_error};
        return {std::optional<std::optional<bool>>{initialized_}, ProviderBackendError::none};
    }

    [[nodiscard]] channels::ProviderBackendResult<channels::ProviderStaticSnapshot>
    static_snapshot(const std::stop_token stop)
    {
        if (stop.stop_requested()) return {{}, ProviderBackendError::internal_error};
        try {
            std::shared_ptr<const std::string> timestamp;
            std::shared_ptr<const std::string> data;
            {
                std::lock_guard lock{mutex_};
                if (closed_) return {{}, ProviderBackendError::internal_error};
                timestamp = timestamp_;
                data = static_data_;
            }
            if (stop.stop_requested()) return {{}, ProviderBackendError::internal_error};
            return {channels::ProviderStaticSnapshot{*timestamp, *data},
                    ProviderBackendError::none};
        }
        catch (...) { return {{}, ProviderBackendError::capacity}; }
    }

    [[nodiscard]] channels::ProviderSubscribeResult subscribe(
        const Stream stream, PushCallback callback)
    {
        if (!callback) return {nullptr, ProviderBackendError::internal_error};
        try {
            auto slot = std::make_shared<CallbackSlot>(std::move(callback));
            std::lock_guard lock{mutex_};
            if (closed_) return {nullptr, ProviderBackendError::internal_error};
            auto& subscriptions = stream == Stream::logs ? log_subscriptions_
                                                          : status_subscriptions_;
            if (subscriptions.size() >= limits_.max_subscriptions_per_stream) {
                return {nullptr, ProviderBackendError::capacity};
            }
            if (next_subscription_id_ == 0) {
                return {nullptr, ProviderBackendError::capacity};
            }
            const std::uint64_t id = next_subscription_id_++;
            if (!subscriptions.emplace(id, slot).second) {
                return {nullptr, ProviderBackendError::capacity};
            }
            std::unique_ptr<Subscription> token;
            try {
                token = std::make_unique<Subscription>(
                    shared_from_this(), stream, id, std::move(slot));
            }
            catch (...) {
                subscriptions.erase(id);
                throw;
            }
            return {std::move(token), ProviderBackendError::none};
        }
        catch (...) { return {nullptr, ProviderBackendError::capacity}; }
    }

    [[nodiscard]] ProviderBackendError publish_log(std::string payload) noexcept
    {
        try {
            if (payload.size() > limits_.max_json_bytes
                || payload.size() > limits_.max_history_bytes) {
                return ProviderBackendError::capacity;
            }
            auto parsed = JsonValidator{payload, limits_}.parse(true);
            if (!parsed.valid || parsed.kind != JsonKind::object
                || !parsed.root_scope || parsed.root_scope->empty()) {
                return ProviderBackendError::internal_error;
            }
            if (parsed.root_scope->size() > limits_.max_scope_bytes) {
                return ProviderBackendError::capacity;
            }
            auto encoded_scope = encode_json_string(*parsed.root_scope);
            if (payload.size() + encoded_scope.size() + 4
                > limits_.max_snapshot_json_bytes) {
                return ProviderBackendError::capacity;
            }
            auto retained = std::make_shared<const std::string>(std::move(payload));
            std::vector<std::pair<std::uint64_t, std::shared_ptr<CallbackSlot>>> callbacks;
            {
                std::lock_guard lock{mutex_};
                if (closed_) return ProviderBackendError::internal_error;
                collect_callbacks(log_subscriptions_, callbacks);
                append_log(retained, std::move(*parsed.root_scope), std::move(encoded_scope));
            }
            return notify(Stream::logs, *retained, callbacks);
        }
        catch (...) { return ProviderBackendError::capacity; }
    }

    [[nodiscard]] ProviderBackendError publish_status(std::string payload) noexcept
    {
        try {
            if (payload.size() > limits_.max_json_bytes
                || payload.size() > limits_.max_snapshot_json_bytes) {
                return ProviderBackendError::capacity;
            }
            const auto parsed = JsonValidator{payload, limits_}.parse(false);
            if (!parsed.valid || parsed.kind != JsonKind::object) {
                return ProviderBackendError::internal_error;
            }
            auto retained = std::make_shared<const std::string>(std::move(payload));
            std::vector<std::pair<std::uint64_t, std::shared_ptr<CallbackSlot>>> callbacks;
            {
                std::lock_guard lock{mutex_};
                if (closed_) return ProviderBackendError::internal_error;
                collect_callbacks(status_subscriptions_, callbacks);
                status_ = retained;
            }
            return notify(Stream::status, *retained, callbacks);
        }
        catch (...) { return ProviderBackendError::capacity; }
    }

    [[nodiscard]] ProviderBackendError set_initialized(
        const std::optional<bool> initialized) noexcept
    {
        try {
            std::shared_ptr<const std::string> notification;
            if (initialized.has_value()) {
                notification = std::make_shared<const std::string>(
                    *initialized ? R"({"is_all_data_initialized":true})"
                                 : R"({"is_all_data_initialized":false})");
            }
            std::vector<std::pair<std::uint64_t, std::shared_ptr<CallbackSlot>>> callbacks;
            {
                std::lock_guard lock{mutex_};
                if (closed_) return ProviderBackendError::internal_error;
                if (notification) collect_callbacks(status_subscriptions_, callbacks);
                initialized_ = initialized;
            }
            return notification ? notify(Stream::status, *notification, callbacks)
                                : ProviderBackendError::none;
        }
        catch (...) { return ProviderBackendError::capacity; }
    }

    [[nodiscard]] ProviderBackendError replace_static(
        std::string timestamp, std::string data) noexcept
    {
        try {
            if (timestamp.size() > limits_.max_json_bytes
                || data.size() > limits_.max_json_bytes
                || timestamp.size() + data.size() > limits_.max_snapshot_json_bytes) {
                return ProviderBackendError::capacity;
            }
            const auto parsed_timestamp = JsonValidator{timestamp, limits_}.parse(false);
            const auto parsed_data = JsonValidator{data, limits_}.parse(false);
            if (!parsed_timestamp.valid || parsed_timestamp.kind != JsonKind::number
                || !parsed_data.valid) return ProviderBackendError::internal_error;
            auto retained_timestamp =
                std::make_shared<const std::string>(std::move(timestamp));
            auto retained_data = std::make_shared<const std::string>(std::move(data));
            std::lock_guard lock{mutex_};
            if (closed_) return ProviderBackendError::internal_error;
            timestamp_ = std::move(retained_timestamp);
            static_data_ = std::move(retained_data);
            return ProviderBackendError::none;
        }
        catch (...) { return ProviderBackendError::capacity; }
    }

    void close() noexcept
    {
        while (true) {
            std::shared_ptr<CallbackSlot> slot;
            {
                std::lock_guard lock{mutex_};
                closed_ = true;
                if (!log_subscriptions_.empty()) {
                    auto iterator = log_subscriptions_.begin();
                    slot = std::move(iterator->second);
                    log_subscriptions_.erase(iterator);
                }
                else if (!status_subscriptions_.empty()) {
                    auto iterator = status_subscriptions_.begin();
                    slot = std::move(iterator->second);
                    status_subscriptions_.erase(iterator);
                }
                else return;
            }
            slot->close();
        }
    }

    [[nodiscard]] const ProductionProviderBackendLimits& limits() const noexcept
    {
        return limits_;
    }

private:
    struct LogEntry {
        std::shared_ptr<const std::string> payload;
        std::string scope;
    };

    struct ScopeState {
        std::size_t references{};
        std::string encoded;
        std::list<std::string>::iterator order;
    };

    using SubscriptionMap =
        std::unordered_map<std::uint64_t, std::shared_ptr<CallbackSlot>>;

    static void collect_callbacks(
        const SubscriptionMap& subscriptions,
        std::vector<std::pair<std::uint64_t, std::shared_ptr<CallbackSlot>>>& output)
    {
        output.reserve(subscriptions.size());
        for (const auto& entry : subscriptions) output.push_back(entry);
    }

    void append_log(
        std::shared_ptr<const std::string> payload,
        std::string scope,
        std::string encoded_scope)
    {
        auto scope_iterator = scopes_.find(scope);
        const bool new_scope = scope_iterator == scopes_.end();
        if (new_scope) {
            scope_order_.push_back(scope);
            const auto order = std::prev(scope_order_.end());
            try {
                auto inserted = scopes_.emplace(
                    scope, ScopeState{0, std::move(encoded_scope), order});
                scope_iterator = inserted.first;
            }
            catch (...) {
                scope_order_.pop_back();
                throw;
            }
        }
        try {
            history_.push_back(LogEntry{std::move(payload), std::move(scope)});
        }
        catch (...) {
            if (new_scope) {
                scope_order_.erase(scope_iterator->second.order);
                scopes_.erase(scope_iterator);
            }
            throw;
        }

        const std::size_t payload_size = history_.back().payload->size();
        history_payload_bytes_ += payload_size;
        entries_json_bytes_ += payload_size + (history_.size() > 1 ? 1U : 0U);
        if (new_scope) {
            scopes_json_bytes_ += scope_iterator->second.encoded.size()
                + (scopes_.size() > 1 ? 1U : 0U);
        }
        ++scope_iterator->second.references;

        while (history_.size() > limits_.max_history_entries
               || history_payload_bytes_ > limits_.max_history_bytes
               || scopes_.size() > limits_.max_scopes
               || entries_json_bytes_ + scopes_json_bytes_
                    > limits_.max_snapshot_json_bytes) {
            evict_oldest();
        }
    }

    void evict_oldest() noexcept
    {
        const LogEntry& entry = history_.front();
        history_payload_bytes_ -= entry.payload->size();
        entries_json_bytes_ -= entry.payload->size() + (history_.size() > 1 ? 1U : 0U);
        auto scope = scopes_.find(entry.scope);
        --scope->second.references;
        if (scope->second.references == 0) {
            scopes_json_bytes_ -= scope->second.encoded.size()
                + (scopes_.size() > 1 ? 1U : 0U);
            scope_order_.erase(scope->second.order);
            scopes_.erase(scope);
        }
        history_.pop_front();
    }

    [[nodiscard]] ProviderBackendError notify(
        const Stream stream,
        const std::string& payload,
        const std::vector<std::pair<std::uint64_t, std::shared_ptr<CallbackSlot>>>& callbacks)
        noexcept
    {
        bool failed = false;
        for (const auto& [id, slot] : callbacks) {
            if (slot->invoke(payload) == CallbackSlot::InvokeResult::failed) {
                failed = true;
                unsubscribe(stream, id, slot);
            }
        }
        return failed ? ProviderBackendError::internal_error : ProviderBackendError::none;
    }

    void unsubscribe(
        const Stream stream, const std::uint64_t id,
        const std::shared_ptr<CallbackSlot>& slot) noexcept
    {
        {
            std::lock_guard lock{mutex_};
            auto& subscriptions = stream == Stream::logs ? log_subscriptions_
                                                          : status_subscriptions_;
            const auto iterator = subscriptions.find(id);
            if (iterator != subscriptions.end() && iterator->second == slot) {
                subscriptions.erase(iterator);
            }
        }
        slot->close();
    }

    const ProductionProviderBackendLimits limits_;
    mutable std::mutex mutex_;
    std::deque<LogEntry> history_;
    std::unordered_map<std::string, ScopeState> scopes_;
    std::list<std::string> scope_order_;
    std::shared_ptr<const std::string> status_;
    std::optional<bool> initialized_;
    std::shared_ptr<const std::string> timestamp_;
    std::shared_ptr<const std::string> static_data_;
    SubscriptionMap log_subscriptions_;
    SubscriptionMap status_subscriptions_;
    std::size_t history_payload_bytes_{};
    std::size_t scopes_json_bytes_{2};
    std::size_t entries_json_bytes_{2};
    std::uint64_t next_subscription_id_{1};
    bool closed_{};
};

ProductionProviderBackend::ProductionProviderBackend(
    ProductionProviderBackendLimits limits)
{
    if (invalid_limits(limits)) {
        throw std::invalid_argument("production provider backend limits must be positive");
    }
    impl_ = std::make_shared<Impl>(limits);
}

ProductionProviderBackend::~ProductionProviderBackend()
{
    if (impl_) impl_->close();
}

channels::ProviderBackendResult<channels::ProviderLogsFull>
ProductionProviderBackend::logs_full(const std::stop_token stop)
{
    return impl_->logs_full(stop);
}

channels::ProviderBackendResult<std::string>
ProductionProviderBackend::status(const std::stop_token stop)
{
    return impl_->status(stop);
}

channels::ProviderBackendResult<std::optional<bool>>
ProductionProviderBackend::all_data_initialized(const std::stop_token stop)
{
    return impl_->all_data_initialized(stop);
}

channels::ProviderBackendResult<channels::ProviderStaticSnapshot>
ProductionProviderBackend::static_snapshot(const std::stop_token stop)
{
    return impl_->static_snapshot(stop);
}

channels::ProviderSubscribeResult ProductionProviderBackend::subscribe_logs(
    PushCallback callback)
{
    return impl_->subscribe(Impl::Stream::logs, std::move(callback));
}

channels::ProviderSubscribeResult ProductionProviderBackend::subscribe_status(
    PushCallback callback)
{
    return impl_->subscribe(Impl::Stream::status, std::move(callback));
}

ProviderBackendError ProductionProviderBackend::publish_log(std::string entry_json) noexcept
{
    return impl_->publish_log(std::move(entry_json));
}

ProviderBackendError ProductionProviderBackend::publish_status(std::string status_json) noexcept
{
    return impl_->publish_status(std::move(status_json));
}

ProviderBackendError ProductionProviderBackend::set_initialized(
    const std::optional<bool> initialized) noexcept
{
    return impl_->set_initialized(initialized);
}

ProviderBackendError ProductionProviderBackend::replace_static(
    std::string timestamp_json, std::string data_json) noexcept
{
    return impl_->replace_static(std::move(timestamp_json), std::move(data_json));
}

const ProductionProviderBackendLimits& ProductionProviderBackend::limits() const noexcept
{
    return impl_->limits();
}

}  // namespace baas::service::app
