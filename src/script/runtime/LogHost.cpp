#include "script/runtime/LogHost.h"

#include "script/runtime/BoundedExecutor.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace baas::script::runtime {
namespace {

constexpr std::string_view redacted = "[REDACTED]";

[[nodiscard]] bool valid_utf8(const std::string_view value) noexcept
{
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<unsigned char>(value[offset]);
        std::size_t width{};
        std::uint32_t scalar{};
        if (first <= 0x7F) { width = 1; scalar = first; }
        else if (first >= 0xC2 && first <= 0xDF) { width = 2; scalar = first & 0x1FU; }
        else if (first >= 0xE0 && first <= 0xEF) { width = 3; scalar = first & 0x0FU; }
        else if (first >= 0xF0 && first <= 0xF4) { width = 4; scalar = first & 0x07U; }
        else return false;
        if (width > value.size() - offset) return false;
        for (std::size_t index = 1; index < width; ++index) {
            const auto next = static_cast<unsigned char>(value[offset + index]);
            if ((next & 0xC0U) != 0x80U) return false;
            scalar = (scalar << 6U) | (next & 0x3FU);
        }
        if ((width == 2 && scalar < 0x80U) || (width == 3 && scalar < 0x800U) ||
            (width == 4 && scalar < 0x10000U) || scalar > 0x10FFFFU ||
            (scalar >= 0xD800U && scalar <= 0xDFFFU)) return false;
        offset += width;
    }
    return true;
}

[[nodiscard]] std::optional<StructuredLogLevel> parse_level(
    const std::string_view level) noexcept
{
    if (level == "trace") return StructuredLogLevel::Trace;
    if (level == "debug") return StructuredLogLevel::Debug;
    if (level == "info") return StructuredLogLevel::Info;
    if (level == "warning" || level == "warn") return StructuredLogLevel::Warning;
    if (level == "error") return StructuredLogLevel::Error;
    if (level == "critical") return StructuredLogLevel::Critical;
    return std::nullopt;
}

[[nodiscard]] bool valid_level(const StructuredLogLevel level) noexcept
{
    return level >= StructuredLogLevel::Trace && level <= StructuredLogLevel::Critical;
}

[[nodiscard]] HostResult host_failure(
    const HostErrorCode code, std::string message,
    const bool retryable = false,
    const HostEffectState effect = HostEffectState::NotStarted,
    std::optional<JsonValue> details = std::nullopt) noexcept
{
    return HostResult::failure(
        {code, std::move(message), retryable, effect, std::move(details)});
}

[[nodiscard]] HostResult budget_failure(std::string message)
{
    return host_failure(
        HostErrorCode::BudgetExceeded, std::move(message), false,
        HostEffectState::NotStarted,
        JsonValue(JsonObject{{"budget_scope", JsonValue("host_operation")}}));
}

[[nodiscard]] bool replace_secret(
    std::string& value, const std::string_view secret,
    const std::size_t maximum, std::size_t& work,
    const std::size_t max_work)
{
    std::size_t offset{};
    for (;;) {
        const auto search_work = value.size() - std::min(offset, value.size()) + 1;
        if (search_work > max_work || work > max_work - search_work) return false;
        work += search_work;
        offset = value.find(secret, offset);
        if (offset == std::string::npos) break;
        const auto shift_work = value.size() - offset;
        if (shift_work > max_work || work > max_work - shift_work) return false;
        work += shift_work;
        if (redacted.size() > secret.size()) {
            const auto growth = redacted.size() - secret.size();
            if (value.size() > maximum || growth > maximum - value.size()) return false;
        }
        value.replace(offset, secret.size(), redacted);
        offset += redacted.size();
    }
    return value.size() <= maximum;
}

[[nodiscard]] bool redact_string(
    std::string& value, const std::vector<std::string>& secrets,
    const std::size_t maximum, std::size_t& work,
    const std::size_t max_work)
{
    for (const auto& secret : secrets) {
        if (!replace_secret(value, secret, maximum, work, max_work)) return false;
    }
    return true;
}

enum class RedactionStatus { Ok, LimitExceeded, DuplicateKey };

[[nodiscard]] RedactionStatus redact_json(
    JsonValue& root, const std::vector<std::string>& secrets,
    const QueuedLogHostLimits& limits, std::size_t& work)
{
    std::vector<JsonValue*> pending{&root};
    std::size_t nodes{};
    while (!pending.empty()) {
        auto* current = pending.back();
        pending.pop_back();
        if (nodes >= limits.max_field_nodes || work >= limits.max_redaction_work)
            return RedactionStatus::LimitExceeded;
        ++nodes;
        ++work;
        switch (current->kind()) {
            case JsonKind::String:
                if (!redact_string(
                        std::get<std::string>(current->value()), secrets,
                        limits.max_event_bytes, work, limits.max_redaction_work))
                    return RedactionStatus::LimitExceeded;
                break;
            case JsonKind::Array:
                for (auto& value : std::get<JsonArray>(current->value()))
                    pending.push_back(&value);
                break;
            case JsonKind::Object:
            {
                std::unordered_set<std::string_view> redacted_keys;
                for (auto& [key, value] : std::get<JsonObject>(current->value())) {
                    if (!redact_string(
                            key, secrets, limits.max_event_bytes, work,
                            limits.max_redaction_work))
                        return RedactionStatus::LimitExceeded;
                    if (!redacted_keys.insert(key).second)
                        return RedactionStatus::DuplicateKey;
                    pending.push_back(&value);
                }
                break;
            }
            default: break;
        }
    }
    return RedactionStatus::Ok;
}

class BoundedAppender final {
public:
    explicit BoundedAppender(const std::size_t maximum) : maximum_(maximum)
    {
        value_.reserve(std::min<std::size_t>(maximum, 4'096));
    }

    bool append(const std::string_view value)
    {
        if (value.size() > maximum_ || value_.size() > maximum_ - value.size())
            return false;
        value_.append(value);
        return true;
    }

    bool character(const char value)
    {
        return append(std::string_view(&value, 1));
    }

    [[nodiscard]] std::string take() && { return std::move(value_); }

private:
    std::size_t maximum_;
    std::string value_;
};

[[nodiscard]] bool append_json_string(BoundedAppender& output, const std::string_view value)
{
    if (!valid_utf8(value) || !output.character('"')) return false;
    constexpr char hex[] = "0123456789abcdef";
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
                    const char escaped[]{'\\', 'u', '0', '0',
                                         hex[(byte >> 4U) & 0x0FU], hex[byte & 0x0FU]};
                    if (!output.append(std::string_view(escaped, sizeof(escaped)))) return false;
                } else if (!output.character(static_cast<char>(byte))) {
                    return false;
                }
        }
    }
    return output.character('"');
}

[[nodiscard]] bool append_json(
    BoundedAppender& output, const JsonValue& value, const std::size_t depth)
{
    if (depth > 256) return false;
    switch (value.kind()) {
        case JsonKind::Null: return output.append("null");
        case JsonKind::Boolean:
            return output.append(std::get<bool>(value.value()) ? "true" : "false");
        case JsonKind::Integer: {
            char buffer[32];
            const auto rendered = std::to_chars(
                std::begin(buffer), std::end(buffer),
                std::get<std::int64_t>(value.value()));
            return rendered.ec == std::errc{} &&
                output.append(std::string_view(
                    buffer, static_cast<std::size_t>(rendered.ptr - buffer)));
        }
        case JsonKind::Float: {
            const auto number = std::get<double>(value.value());
            if (!std::isfinite(number)) return false;
            char buffer[64];
            const auto rendered = std::to_chars(
                std::begin(buffer), std::end(buffer), number,
                std::chars_format::general, std::numeric_limits<double>::max_digits10);
            return rendered.ec == std::errc{} &&
                output.append(std::string_view(
                    buffer, static_cast<std::size_t>(rendered.ptr - buffer)));
        }
        case JsonKind::String:
            return append_json_string(output, std::get<std::string>(value.value()));
        case JsonKind::Array: {
            if (!output.character('[')) return false;
            const auto& values = std::get<JsonArray>(value.value());
            for (std::size_t index = 0; index < values.size(); ++index) {
                if ((index != 0 && !output.character(',')) ||
                    !append_json(output, values[index], depth + 1))
                    return false;
            }
            return output.character(']');
        }
        case JsonKind::Object: {
            if (!output.character('{')) return false;
            const auto& entries = std::get<JsonObject>(value.value());
            for (std::size_t index = 0; index < entries.size(); ++index) {
                if ((index != 0 && !output.character(',')) ||
                    !append_json_string(output, entries[index].first) ||
                    !output.character(':') ||
                    !append_json(output, entries[index].second, depth + 1))
                    return false;
            }
            return output.character('}');
        }
    }
    return false;
}

}  // namespace

std::string_view structured_log_level_name(const StructuredLogLevel level) noexcept
{
    switch (level) {
        case StructuredLogLevel::Trace: return "trace";
        case StructuredLogLevel::Debug: return "debug";
        case StructuredLogLevel::Info: return "info";
        case StructuredLogLevel::Warning: return "warning";
        case StructuredLogLevel::Error: return "error";
        case StructuredLogLevel::Critical: return "critical";
    }
    return "info";
}

struct QueuedLogHost::Impl final {
    struct Counters final {
        std::atomic<std::size_t> accepted{};
        std::atomic<std::size_t> delivered{};
        std::atomic<std::size_t> sink_failures{};
        std::atomic<std::size_t> backpressure_rejections{};
        std::atomic<std::size_t> unavailable_rejections{};
    };

    Impl(
        std::shared_ptr<StructuredLogSink> sink_value,
        LogHostIdentity identity_value,
        std::vector<std::string> secrets_value,
        const QueuedLogHostLimits limits_value)
        : sink(std::move(sink_value)),
          identity(std::move(identity_value)),
          secrets(std::move(secrets_value)),
          limits(limits_value),
          counters(std::make_shared<Counters>()),
          executor(1, limits.queue_capacity)
    {
    }

    std::shared_ptr<StructuredLogSink> sink;
    LogHostIdentity identity;
    std::vector<std::string> secrets;
    QueuedLogHostLimits limits;
    std::shared_ptr<Counters> counters;
    BoundedExecutor executor;
};

QueuedLogHost::QueuedLogHost(
    std::shared_ptr<StructuredLogSink> sink,
    LogHostIdentity identity,
    std::vector<std::string> secrets,
    const QueuedLogHostLimits limits)
{
    if (limits.queue_capacity == 0 || limits.max_secret_count == 0 ||
        limits.max_secret_bytes == 0 || limits.max_event_bytes == 0 ||
        limits.max_field_nodes == 0 || limits.max_redaction_work == 0)
        throw LogHostConfigError(
            LogHostConfigErrorCode::InvalidLimits, "LogHost limits must be positive");
    if (!sink)
        throw LogHostConfigError(LogHostConfigErrorCode::MissingSink, "LogHost sink is absent");

    std::size_t identity_bytes{};
    for (const auto* value : {&identity.task_id, &identity.session_id, &identity.config_name}) {
        if (!valid_utf8(*value) || value->size() > limits.max_event_bytes -
                std::min(identity_bytes, limits.max_event_bytes))
            throw LogHostConfigError(
                LogHostConfigErrorCode::InvalidIdentity, "LogHost identity is invalid or oversized");
        identity_bytes += value->size();
    }
    if (secrets.size() > limits.max_secret_count)
        throw LogHostConfigError(
            LogHostConfigErrorCode::SecretLimitExceeded, "LogHost secret count exceeded");
    std::size_t secret_bytes{};
    for (const auto& secret : secrets) {
        if (secret.empty() || !valid_utf8(secret))
            throw LogHostConfigError(
                LogHostConfigErrorCode::InvalidSecret, "LogHost secrets must be non-empty UTF-8");
        if (secret.size() > limits.max_secret_bytes -
                std::min(secret_bytes, limits.max_secret_bytes))
            throw LogHostConfigError(
                LogHostConfigErrorCode::SecretLimitExceeded, "LogHost secret byte budget exceeded");
        secret_bytes += secret.size();
    }
    std::sort(secrets.begin(), secrets.end(), [](const auto& left, const auto& right) {
        return left.size() != right.size() ? left.size() > right.size() : left < right;
    });
    secrets.erase(std::unique(secrets.begin(), secrets.end()), secrets.end());
    impl_ = std::make_unique<Impl>(
        std::move(sink), std::move(identity), std::move(secrets), limits);
}

QueuedLogHost::~QueuedLogHost() { shutdown(); }

void QueuedLogHost::shutdown() noexcept
{
    if (impl_) impl_->executor.Shutdown(ShutdownMode::Drain);
}

QueuedLogHostStats QueuedLogHost::stats() const noexcept
{
    return {
        impl_->counters->accepted.load(std::memory_order_relaxed),
        impl_->counters->delivered.load(std::memory_order_relaxed),
        impl_->counters->sink_failures.load(std::memory_order_relaxed),
        impl_->counters->backpressure_rejections.load(std::memory_order_relaxed),
        impl_->counters->unavailable_rejections.load(std::memory_order_relaxed),
    };
}

HostResult QueuedLogHost::emit(const HostCallContext&, const HostArguments& arguments)
{
    if (arguments.size() != 3 || !arguments[0] || !arguments[1] ||
        arguments[0]->type() != HostValueType::String ||
        arguments[1]->type() != HostValueType::String ||
        (arguments[2] && (arguments[2]->type() != HostValueType::Json ||
            std::get<JsonValue>(arguments[2]->storage()).kind() != JsonKind::Object)))
        return host_failure(HostErrorCode::InvalidArgument, "invalid log arguments");

    const auto level = parse_level(std::get<std::string>(arguments[0]->storage()));
    if (!level)
        return host_failure(HostErrorCode::InvalidArgument, "unsupported log level");

    StructuredLogEvent event;
    event.level = *level;
    event.message = std::get<std::string>(arguments[1]->storage());
    event.identity = impl_->identity;
    if (arguments[2]) {
        event.fields = std::get<JsonObject>(
            std::get<JsonValue>(arguments[2]->storage()).value());
    }

    std::size_t work{};
    for (auto* value : {&event.message, &event.identity.task_id,
                        &event.identity.session_id, &event.identity.config_name}) {
        if (!redact_string(
                *value, impl_->secrets, impl_->limits.max_event_bytes,
                work, impl_->limits.max_redaction_work))
            return budget_failure("log redaction budget exceeded");
    }
    if (event.fields) {
        JsonValue fields(*event.fields);
        const auto redaction = redact_json(fields, impl_->secrets, impl_->limits, work);
        if (redaction == RedactionStatus::DuplicateKey)
            return host_failure(
                HostErrorCode::InvalidArgument,
                "log redaction produced duplicate field keys");
        if (redaction != RedactionStatus::Ok)
            return budget_failure("log field redaction budget exceeded");
        event.fields = std::get<JsonObject>(std::move(fields.value()));
    }
    if (!serialize_structured_log_event(event, impl_->limits.max_event_bytes))
        return budget_failure("structured log event byte budget exceeded");

    const auto counters = impl_->counters;
    try {
        auto accepted = impl_->executor.TrySubmit(
            [sink = impl_->sink, event = std::move(event), counters] {
                try {
                    sink->write(event);
                    counters->delivered.fetch_add(1, std::memory_order_relaxed);
                } catch (...) {
                    counters->sink_failures.fetch_add(1, std::memory_order_relaxed);
                }
            });
        if (!accepted) {
            counters->backpressure_rejections.fetch_add(1, std::memory_order_relaxed);
            return host_failure(
                HostErrorCode::Backpressure, "structured log queue is full", true);
        }
        counters->accepted.fetch_add(1, std::memory_order_relaxed);
        return HostResult::success();
    } catch (const ExecutorShutdown&) {
        counters->unavailable_rejections.fetch_add(1, std::memory_order_relaxed);
        return host_failure(HostErrorCode::Unavailable, "structured log sink is shut down");
    }
}

SynchronousNativeBinding make_queued_log_binding(std::shared_ptr<QueuedLogHost> host)
{
    if (!host)
        throw HostBindingError(HostBindingErrorCode::MissingCallback, "queued LogHost is absent");
    return {
        "host.log.emit.v1",
        {{{"level", HostValueType::String, true},
          {"message", HostValueType::String, true},
          {"fields", HostValueType::OrderedStringJsonMap, false}},
         HostValueType::Null,
         "log_events",
         HostExecutionMode::ThreadSafe,
         HostCancellationMode::Preflight},
        [host = std::move(host)](
            const HostCallContext& context, const HostArguments& arguments) {
            return host->emit(context, arguments);
        }};
}

std::optional<std::string> serialize_structured_log_event(
    const StructuredLogEvent& event, const std::size_t max_bytes)
{
    if (max_bytes == 0 || !valid_level(event.level)) return std::nullopt;
    BoundedAppender output(max_bytes);
    if (!output.append("{\"level\":") ||
        !append_json_string(output, structured_log_level_name(event.level)) ||
        !output.append(",\"message\":") || !append_json_string(output, event.message) ||
        !output.append(",\"task_id\":") ||
        !append_json_string(output, event.identity.task_id) ||
        !output.append(",\"session_id\":") ||
        !append_json_string(output, event.identity.session_id) ||
        !output.append(",\"config_name\":") ||
        !append_json_string(output, event.identity.config_name) ||
        !output.append(",\"fields\":"))
        return std::nullopt;
    if (event.fields) {
        if (!append_json(output, JsonValue(*event.fields), 1)) return std::nullopt;
    } else if (!output.append("null")) {
        return std::nullopt;
    }
    if (!output.character('}')) return std::nullopt;
    return std::move(output).take();
}

}  // namespace baas::script::runtime
