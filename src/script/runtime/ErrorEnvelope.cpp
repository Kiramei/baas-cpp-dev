#include "script/runtime/ErrorEnvelope.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <system_error>

namespace baas::script::runtime {
namespace {

constexpr std::size_t kHardDepthLimit = 256;

bool add_with_limit(std::size_t& used, const std::size_t amount,
                    const std::size_t limit) noexcept
{
    if (amount > limit - std::min(used, limit)) return false;
    used += amount;
    return true;
}

std::size_t saturating_add(const std::size_t left, const std::size_t right) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left)
        return std::numeric_limits<std::size_t>::max();
    return left + right;
}

std::size_t utf8_prefix(const std::string_view value, const std::size_t limit) noexcept
{
    if (value.size() <= limit) return value.size();
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto lead = static_cast<unsigned char>(value[offset]);
        std::size_t width = 1;
        if ((lead & 0xE0U) == 0xC0U) width = 2;
        else if ((lead & 0xF0U) == 0xE0U) width = 3;
        else if ((lead & 0xF8U) == 0xF0U) width = 4;
        if (width > limit - std::min(offset, limit)) break;
        offset += width;
    }
    return offset;
}

class Writer {
public:
    Writer(const std::span<char> output, const std::size_t limit) noexcept
        : output_(output), capacity_(std::min(output.size(), limit)) {}

    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    void rewind(const std::size_t position = 0) noexcept { position_ = position; }
    void set_counting(const bool value) noexcept { counting_ = value; }

    bool raw(const std::string_view value) noexcept
    {
        if (counting_) {
            if (value.size() > std::numeric_limits<std::size_t>::max() - position_)
                return false;
            position_ += value.size();
            return true;
        }
        if (value.size() > capacity_ - std::min(position_, capacity_)) return false;
        if (!value.empty()) std::memcpy(output_.data() + position_, value.data(), value.size());
        position_ += value.size();
        return true;
    }

    bool character(const char value) noexcept
    {
        if (counting_) {
            if (position_ == std::numeric_limits<std::size_t>::max()) return false;
            ++position_;
            return true;
        }
        if (position_ >= capacity_) return false;
        output_[position_++] = value;
        return true;
    }

    bool quoted(const std::string_view value) noexcept
    {
        static constexpr char hex[] = "0123456789abcdef";
        if (!character('"')) return false;
        for (const unsigned char byte : value) {
            switch (byte) {
                case '"': if (!raw("\\\"")) return false; break;
                case '\\': if (!raw("\\\\")) return false; break;
                case '\b': if (!raw("\\b")) return false; break;
                case '\f': if (!raw("\\f")) return false; break;
                case '\n': if (!raw("\\n")) return false; break;
                case '\r': if (!raw("\\r")) return false; break;
                case '\t': if (!raw("\\t")) return false; break;
                default:
                    if (byte < 0x20U) {
                        const std::array<char, 6> escaped{
                            '\\', 'u', '0', '0', hex[byte >> 4U], hex[byte & 0x0FU]};
                        if (!raw(std::string_view(escaped.data(), escaped.size()))) return false;
                    } else if (!character(static_cast<char>(byte))) {
                        return false;
                    }
            }
        }
        return character('"');
    }

    template <typename Integer>
    bool integer(const Integer value) noexcept
    {
        std::array<char, 32> bytes{};
        const auto result = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
        return result.ec == std::errc{} && raw(
            std::string_view(bytes.data(), static_cast<std::size_t>(result.ptr - bytes.data())));
    }

    bool floating(const double value) noexcept
    {
        if (!std::isfinite(value)) return false;
        if (value == 0.0) return character('0');
        std::array<char, 64> bytes{};
        const auto result = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value,
                                          std::chars_format::general,
                                          std::numeric_limits<double>::max_digits10);
        if (result.ec != std::errc{}) return false;

        // Normalize the only implementation latitude relevant to JSON: an
        // exponent plus sign and leading zeroes. The mantissa is already the
        // locale-independent max_digits10 representation.
        auto end = result.ptr;
        auto exponent = std::find(bytes.data(), end, 'e');
        if (exponent == end) exponent = std::find(bytes.data(), end, 'E');
        if (exponent != end) {
            *exponent = 'e';
            auto read = exponent + 1;
            auto write = read;
            if (read != end && *read == '+') ++read;
            else if (read != end && *read == '-') *write++ = *read++;
            while (read + 1 < end && *read == '0') ++read;
            while (read != end) *write++ = *read++;
            end = write;
        }
        return raw(std::string_view(bytes.data(), static_cast<std::size_t>(end - bytes.data())));
    }

private:
    std::span<char> output_;
    std::size_t capacity_{};
    std::size_t position_{};
    bool counting_{false};
};

class CountingScope {
public:
    explicit CountingScope(Writer& writer) noexcept
        : writer_(writer), saved_position_(writer.position())
    {
        writer_.set_counting(true);
        writer_.rewind();
    }
    CountingScope(const CountingScope&) = delete;
    CountingScope& operator=(const CountingScope&) = delete;
    ~CountingScope()
    {
        writer_.set_counting(false);
        writer_.rewind(saved_position_);
    }

private:
    Writer& writer_;
    std::size_t saved_position_{};
};

struct Budget {
    explicit Budget(const ErrorEnvelopeLimits& value) noexcept : limits(value) {}

    bool node(const std::size_t depth) noexcept
    {
        if (depth == 0 || depth > limits.max_depth || depth > kHardDepthLimit) return false;
        return add_with_limit(nodes, 1, limits.max_nodes) && work_item();
    }
    bool edge() noexcept { return work_item(); }
    bool snapshot() noexcept { return work_item(); }
    bool string(const std::size_t bytes) noexcept {
        return add_with_limit(strings, bytes, limits.max_string_bytes);
    }

    ErrorEnvelopeLimits limits;
    std::size_t nodes{};
    std::size_t strings{};
    std::size_t work{};

private:
    bool work_item() noexcept { return add_with_limit(work, 1, limits.max_work); }
};

std::string_view origin_name(const ErrorOrigin origin) noexcept
{
    switch (origin) {
        case ErrorOrigin::Script: return "script";
        case ErrorOrigin::Runtime: return "runtime";
        case ErrorOrigin::Host: return "host";
    }
    return {};
}

std::string_view frame_kind_name(const ErrorFrameKind kind) noexcept
{
    switch (kind) {
        case ErrorFrameKind::Script: return "script";
        case ErrorFrameKind::Host: return "host";
    }
    return {};
}

std::string_view frame_phase_name(const ErrorFramePhase phase) noexcept
{
    switch (phase) {
        case ErrorFramePhase::Body: return "body";
        case ErrorFramePhase::ModuleInit: return "module_init";
        case ErrorFramePhase::Cleanup: return "cleanup";
        case ErrorFramePhase::Host: return "host";
    }
    return {};
}

std::string_view detail_kind_name(const ValueKind kind) noexcept
{
    switch (kind) {
        case ValueKind::Null: return "null";
        case ValueKind::Boolean: return "boolean";
        case ValueKind::Integer: return "integer";
        case ValueKind::Float: return "nonfinite";
        case ValueKind::HeapReference: return "invalid_reference";
        case ValueKind::String: return "string";
        case ValueKind::List: return "list";
        case ValueKind::OrderedMap: return "ordered_map";
        case ValueKind::Function: return "function";
        case ValueKind::Module: return "module";
        case ValueKind::Error: return "error";
        case ValueKind::Task: return "task";
        case ValueKind::HostHandle: return "host_handle";
    }
    return "invalid";
}

struct FallbackInfo {
    LanguageErrorCode code{LanguageErrorCode::InternalInvariant};
    ErrorOrigin origin{ErrorOrigin::Runtime};
    std::array<char, 256> message{};
    std::size_t message_size{};
    std::size_t omitted_message_bytes{};
    std::array<char, 128> correlation{};
    std::size_t correlation_size{};
    bool has_snapshot{false};
};

class Serializer {
public:
    Serializer(const Heap& heap, Writer& writer, const ErrorEnvelopeLimits& limits) noexcept
        : heap_(heap), writer_(writer), budget_(limits), limits_(limits) {}

    [[nodiscard]] const FallbackInfo& fallback_info() const noexcept { return fallback_; }

    bool write(const Value error)
    {
        return write_error(error, 1, 0);
    }

private:
    bool begin_object(const std::size_t depth) noexcept {
        return budget_.node(depth) && writer_.character('{');
    }
    bool begin_array(const std::size_t depth) noexcept {
        return budget_.node(depth) && writer_.character('[');
    }
    bool field(const std::string_view literal, const bool first) noexcept {
        return budget_.edge() && (first || writer_.character(',')) && writer_.raw(literal) &&
               writer_.character(':');
    }
    bool element(const bool first) noexcept {
        return budget_.edge() && (first || writer_.character(','));
    }
    bool null_value(const std::size_t depth) noexcept {
        return budget_.node(depth) && writer_.raw("null");
    }
    bool bool_value(const bool value, const std::size_t depth) noexcept {
        return budget_.node(depth) && writer_.raw(value ? "true" : "false");
    }
    bool size_value(const std::size_t value, const std::size_t depth) noexcept {
        return budget_.node(depth) && writer_.integer(value);
    }
    bool integer_value(const std::int64_t value, const std::size_t depth) noexcept {
        return budget_.node(depth) && writer_.integer(value);
    }
    bool float_value(const double value, const std::size_t depth) noexcept {
        return budget_.node(depth) && writer_.floating(value);
    }
    bool fixed_string(const std::string_view value, const std::size_t depth) noexcept {
        return budget_.node(depth) && writer_.quoted(value);
    }
    bool dynamic_string(const std::string_view value, const std::size_t depth) noexcept {
        return budget_.node(depth) && budget_.string(value.size()) && writer_.quoted(value);
    }

    bool write_location(const ::baas::script::SourceLocation& location,
                        const std::size_t depth) noexcept
    {
        return begin_object(depth) &&
               field("\"byte_offset\"", true) && size_value(location.byte_offset, depth + 1) &&
               field("\"line\"", false) && size_value(location.line, depth + 1) &&
               field("\"column\"", false) && size_value(location.column, depth + 1) &&
               writer_.character('}');
    }

    bool write_source(const SourceReference& source, const std::size_t depth) noexcept
    {
        return begin_object(depth) &&
               field("\"snapshot_id\"", true) && dynamic_string(source.snapshot_id, depth + 1) &&
               field("\"module\"", false) && dynamic_string(source.module, depth + 1) &&
               field("\"span\"", false) && begin_object(depth + 1) &&
               field("\"begin\"", true) && write_location(source.span.begin, depth + 2) &&
               field("\"end\"", false) && write_location(source.span.end, depth + 2) &&
               writer_.character('}') && writer_.character('}');
    }

    bool write_optional_source(const std::optional<SourceReference>& source,
                               const std::size_t depth) noexcept
    {
        return source ? write_source(*source, depth) : null_value(depth);
    }

    bool write_frame(const ErrorStackFrame& frame, const std::size_t depth) noexcept
    {
        const auto kind = frame_kind_name(frame.kind);
        const auto phase = frame_phase_name(frame.phase);
        if (kind.empty() || phase.empty()) return false;
        return begin_object(depth) &&
               field("\"kind\"", true) && fixed_string(kind, depth + 1) &&
               field("\"module\"", false) && dynamic_string(frame.module, depth + 1) &&
               field("\"function\"", false) && dynamic_string(frame.function, depth + 1) &&
               field("\"phase\"", false) && fixed_string(phase, depth + 1) &&
               field("\"call_source\"", false) && write_optional_source(frame.call_source, depth + 1) &&
               field("\"definition_source\"", false) &&
                    write_optional_source(frame.definition_source, depth + 1) &&
               field("\"defer_source\"", false) && write_optional_source(frame.defer_source, depth + 1) &&
               writer_.character('}');
    }

    bool write_context_value(const std::optional<std::string>& value,
                             const std::size_t depth) noexcept
    {
        return value ? dynamic_string(*value, depth) : null_value(depth);
    }

    bool write_context(const ErrorContext& context, const std::size_t depth) noexcept
    {
        return begin_object(depth) &&
               field("\"task_id\"", true) && write_context_value(context.task_id, depth + 1) &&
               field("\"session_id\"", false) && write_context_value(context.session_id, depth + 1) &&
               field("\"package_id\"", false) && write_context_value(context.package_id, depth + 1) &&
               field("\"snapshot_id\"", false) && write_context_value(context.snapshot_id, depth + 1) &&
               field("\"language_version\"", false) &&
                    write_context_value(context.language_version, depth + 1) &&
               field("\"correlation_id\"", false) &&
                    write_context_value(context.correlation_id, depth + 1) &&
               writer_.character('}');
    }

    bool write_truncation(const ErrorTruncation& value, const std::size_t depth) noexcept
    {
        return begin_object(depth) &&
               field("\"stack_frames\"", true) && size_value(value.stack_frames, depth + 1) &&
               field("\"cause_errors\"", false) && size_value(value.cause_errors, depth + 1) &&
               field("\"suppressed_errors\"", false) &&
                    size_value(value.suppressed_errors, depth + 1) &&
               field("\"message_bytes\"", false) && size_value(value.message_bytes, depth + 1) &&
               field("\"detail_bytes\"", false) && size_value(value.detail_bytes, depth + 1) &&
               field("\"details_replaced\"", false) &&
                    bool_value(value.details_replaced, depth + 1) &&
               field("\"fallback\"", false) && bool_value(value.fallback, depth + 1) &&
               writer_.character('}');
    }

    bool active_contains(const std::array<HeapRef, kHardDepthLimit>& active,
                         const std::size_t count, const HeapRef reference) const noexcept
    {
        for (std::size_t index = 0; index < count; ++index)
            if (active[index] == reference) return true;
        return false;
    }

    bool write_marker(const std::string_view kind, const std::size_t depth,
                      ErrorTruncation& truncation) noexcept
    {
        truncation.details_replaced = true;
        return begin_object(depth) && field("\"kind\"", true) &&
               fixed_string(kind, depth + 1) && writer_.character('}');
    }

    bool write_detail(const Value value, const std::size_t depth,
                      ErrorTruncation& truncation)
    {
        const auto inline_kind = value.inline_kind();
        switch (inline_kind) {
            case ValueKind::Null: return null_value(depth);
            case ValueKind::Boolean: return bool_value(value.as_boolean(), depth);
            case ValueKind::Integer: return integer_value(value.as_integer(), depth);
            case ValueKind::Float:
                if (!std::isfinite(value.as_float()))
                    return write_marker("nonfinite", depth, truncation);
                return float_value(value.as_float(), depth);
            case ValueKind::HeapReference: break;
            default: return write_marker("invalid", depth, truncation);
        }

        const auto reference = value.as_heap_ref();
        const auto kind = heap_.kind(reference);
        if (kind == ValueKind::String)
            return dynamic_string(heap_.string_view(reference), depth);
        if (kind != ValueKind::List && kind != ValueKind::OrderedMap)
            return write_marker(detail_kind_name(kind), depth, truncation);
        if (active_contains(detail_active_, detail_active_count_, reference))
            return write_marker("cycle", depth, truncation);
        if (detail_active_count_ >= detail_active_.size()) return false;

        detail_active_[detail_active_count_++] = reference;
        struct Pop {
            std::size_t& count;
            ~Pop() { --count; }
        } pop{detail_active_count_};

        if (kind == ValueKind::List) {
            const auto size = heap_.list_size(reference);
            if (!begin_array(depth)) return false;
            for (std::size_t index = 0; index < size; ++index) {
                if (!element(index == 0) ||
                    !write_detail(heap_.list_value_at(reference, index), depth + 1, truncation))
                    return false;
            }
            return writer_.character(']');
        }

        const auto size = heap_.map_size(reference);
        for (std::size_t index = 0; index < size; ++index) {
            const auto key = heap_.map_entry_at(reference, index).first;
            for (std::size_t other = 0; other < index; ++other) {
                if (!budget_.edge()) return false;
                if (key == heap_.map_entry_at(reference, other).first) return false;
            }
        }
        if (!begin_object(depth)) return false;
        for (std::size_t index = 0; index < size; ++index) {
            const auto [key, child] = heap_.map_entry_at(reference, index);
            if (!element(index == 0) || !budget_.string(key.size()) ||
                !writer_.quoted(key) || !writer_.character(':') ||
                !write_detail(child, depth + 1, truncation))
                return false;
        }
        return writer_.character('}');
    }

    bool write_details(const ErrorMetadata& metadata, const std::size_t depth,
                       ErrorTruncation& truncation)
    {
        for (std::size_t index = 0; index < metadata.details.size(); ++index) {
            for (std::size_t other = 0; other < index; ++other) {
                if (!budget_.edge()) return false;
                if (metadata.details[index].first == metadata.details[other].first) return false;
            }
        }

        const auto saved_budget = budget_;
        const auto saved_replaced = truncation.details_replaced;
        auto write_object = [&]() {
            if (!begin_object(depth)) return false;
            for (std::size_t index = 0; index < metadata.details.size(); ++index) {
                const auto& [key, value] = metadata.details[index];
                if (!element(index == 0) || !budget_.string(key.size()) ||
                    !writer_.quoted(key) || !writer_.character(':') ||
                    !write_detail(value, depth + 1, truncation))
                    return false;
            }
            return writer_.character('}');
        };

        std::size_t bytes = 0;
        budget_.limits.max_depth = kHardDepthLimit;
        budget_.limits.max_nodes = std::numeric_limits<std::size_t>::max();
        budget_.limits.max_string_bytes = std::numeric_limits<std::size_t>::max();
        {
            CountingScope counting(writer_);
            if (!write_object()) return false;
            bytes = writer_.position();
        }
        const auto probe_work = budget_.work;
        budget_ = saved_budget;
        budget_.work = probe_work;
        truncation.details_replaced = saved_replaced;
        if (bytes <= limits_.max_detail_bytes) return write_object();

        truncation.detail_bytes = saturating_add(truncation.detail_bytes, bytes);
        return write_marker("limit", depth, truncation);
    }

    bool count_omitted_causes(Value value, std::size_t& count)
    {
        std::array<HeapRef, kHardDepthLimit> seen{};
        std::size_t seen_count = 0;
        while (true) {
            if (!budget_.snapshot() || seen_count >= seen.size()) return false;
            const auto reference = value.as_heap_ref();
            if (active_contains(seen, seen_count, reference)) return false;
            seen[seen_count++] = reference;
            const auto& metadata = heap_.error_metadata_view(reference);
            count = saturating_add(count, 1);
            count = saturating_add(count, metadata.truncated.cause_errors);
            if (!metadata.cause) return true;
            value = *metadata.cause;
        }
    }

    void capture_fallback(const ErrorMetadata& metadata) noexcept
    {
        fallback_.has_snapshot = true;
        fallback_.code = metadata.code;
        fallback_.origin = metadata.origin;
        const auto message_size = utf8_prefix(metadata.message, fallback_.message.size());
        if (message_size != 0)
            std::memcpy(fallback_.message.data(), metadata.message.data(), message_size);
        fallback_.message_size = message_size;
        fallback_.omitted_message_bytes = metadata.message.size() - message_size;
        if (metadata.context.correlation_id &&
            metadata.context.correlation_id->size() <= fallback_.correlation.size()) {
            const auto size = metadata.context.correlation_id->size();
            if (size != 0)
                std::memcpy(fallback_.correlation.data(),
                            metadata.context.correlation_id->data(), size);
            fallback_.correlation_size = size;
        }
    }

    bool write_error(const Value value, const std::size_t depth,
                     const std::size_t cause_depth)
    {
        if (!budget_.snapshot()) return false;
        const auto reference = value.as_heap_ref();
        if (active_contains(error_active_, error_active_count_, reference) ||
            error_active_count_ >= error_active_.size())
            return false;
        const auto& metadata = heap_.error_metadata_view(reference);
        if (error_active_count_ == 0) capture_fallback(metadata);
        const auto code = metadata.code_name();
        const auto origin = origin_name(metadata.origin);
        if (code.empty() || origin.empty()) return false;

        error_active_[error_active_count_++] = reference;
        struct Pop {
            std::size_t& count;
            ~Pop() { --count; }
        } pop{error_active_count_};

        auto truncation = metadata.truncated;
        const auto message_size = utf8_prefix(metadata.message, limits_.max_message_bytes);
        truncation.message_bytes = saturating_add(
            truncation.message_bytes, metadata.message.size() - message_size);
        const auto message = std::string_view(metadata.message.data(), message_size);
        const auto suppressed_count = std::min(metadata.suppressed.size(),
                                               limits_.max_suppressed_errors);
        truncation.suppressed_errors = saturating_add(
            truncation.suppressed_errors, metadata.suppressed.size() - suppressed_count);

        bool omit_cause = false;
        if (metadata.cause && cause_depth >= limits_.max_cause_depth) {
            omit_cause = true;
            std::size_t omitted = 0;
            if (!count_omitted_causes(*metadata.cause, omitted)) return false;
            truncation.cause_errors = saturating_add(truncation.cause_errors, omitted);
        }

        if (!begin_object(depth) ||
            !field("\"schema\"", true) || !fixed_string("baas.script.error/v1", depth + 1) ||
            !field("\"code\"", false) || !fixed_string(code, depth + 1) ||
            !field("\"message\"", false) || !dynamic_string(message, depth + 1) ||
            !field("\"origin\"", false) || !fixed_string(origin, depth + 1) ||
            !field("\"catchable\"", false) || !bool_value(metadata.catchable(), depth + 1) ||
            !field("\"source\"", false) || !write_optional_source(metadata.source, depth + 1) ||
            !field("\"stack\"", false) || !begin_array(depth + 1))
            return false;
        for (std::size_t index = 0; index < metadata.stack.size(); ++index) {
            if (!element(index == 0) || !write_frame(metadata.stack[index], depth + 2))
                return false;
        }
        if (!writer_.character(']') || !field("\"cause\"", false)) return false;
        if (!metadata.cause || omit_cause) {
            if (!null_value(depth + 1)) return false;
        } else if (!write_error(*metadata.cause, depth + 1, cause_depth + 1)) {
            return false;
        }

        if (!field("\"suppressed\"", false) || !begin_array(depth + 1)) return false;
        for (std::size_t index = 0; index < suppressed_count; ++index) {
            if (!element(index == 0) ||
                !write_error(metadata.suppressed[index], depth + 2, 0))
                return false;
        }
        return writer_.character(']') &&
               field("\"details\"", false) && write_details(metadata, depth + 1, truncation) &&
               field("\"context\"", false) && write_context(metadata.context, depth + 1) &&
               field("\"truncated\"", false) && write_truncation(truncation, depth + 1) &&
               writer_.character('}');
    }

    const Heap& heap_;
    Writer& writer_;
    Budget budget_;
    ErrorEnvelopeLimits limits_;
    std::array<HeapRef, kHardDepthLimit> error_active_{};
    std::size_t error_active_count_{};
    std::array<HeapRef, kHardDepthLimit> detail_active_{};
    std::size_t detail_active_count_{};
    FallbackInfo fallback_{};
};

bool emergency_field(Writer& writer, const std::string_view literal,
                     const bool first) noexcept
{
    return (first || writer.character(',')) && writer.raw(literal) && writer.character(':');
}

bool write_fallback(Writer& writer, const FallbackInfo& info) noexcept
{
    auto code = language_error_code_name(info.code);
    auto origin = origin_name(info.origin);
    if (code.empty()) code = "InternalInvariant";
    if (origin.empty()) origin = "runtime";
    const auto message = info.has_snapshot
        ? std::string_view(info.message.data(), info.message_size)
        : std::string_view("Error envelope serialization failed");

    writer.rewind();
    return writer.character('{') &&
           emergency_field(writer, "\"schema\"", true) && writer.quoted("baas.script.error/v1") &&
           emergency_field(writer, "\"code\"", false) && writer.quoted(code) &&
           emergency_field(writer, "\"message\"", false) && writer.quoted(message) &&
           emergency_field(writer, "\"origin\"", false) && writer.quoted(origin) &&
           emergency_field(writer, "\"catchable\"", false) &&
                writer.raw(language_error_code_catchable(info.code) ? "true" : "false") &&
           emergency_field(writer, "\"source\"", false) && writer.raw("null") &&
           emergency_field(writer, "\"stack\"", false) && writer.raw("[]") &&
           emergency_field(writer, "\"cause\"", false) && writer.raw("null") &&
           emergency_field(writer, "\"suppressed\"", false) && writer.raw("[]") &&
           emergency_field(writer, "\"details\"", false) && writer.raw("{}") &&
           emergency_field(writer, "\"context\"", false) && writer.raw("{") &&
                emergency_field(writer, "\"task_id\"", true) && writer.raw("null") &&
                emergency_field(writer, "\"session_id\"", false) && writer.raw("null") &&
                emergency_field(writer, "\"package_id\"", false) && writer.raw("null") &&
                emergency_field(writer, "\"snapshot_id\"", false) && writer.raw("null") &&
                emergency_field(writer, "\"language_version\"", false) && writer.raw("null") &&
                emergency_field(writer, "\"correlation_id\"", false) &&
                    (info.correlation_size == 0
                        ? writer.raw("null")
                        : writer.quoted(std::string_view(info.correlation.data(),
                                                        info.correlation_size))) &&
                writer.character('}') &&
           emergency_field(writer, "\"truncated\"", false) && writer.raw("{") &&
                emergency_field(writer, "\"stack_frames\"", true) && writer.integer(0U) &&
                emergency_field(writer, "\"cause_errors\"", false) && writer.integer(0U) &&
                emergency_field(writer, "\"suppressed_errors\"", false) && writer.integer(0U) &&
                emergency_field(writer, "\"message_bytes\"", false) &&
                    writer.integer(info.omitted_message_bytes) &&
                emergency_field(writer, "\"detail_bytes\"", false) && writer.integer(0U) &&
                emergency_field(writer, "\"details_replaced\"", false) && writer.raw("false") &&
                emergency_field(writer, "\"fallback\"", false) && writer.raw("true") &&
                writer.character('}') && writer.character('}');
}

}  // namespace

ErrorEnvelopeResult serialize_error_envelope(const Heap& heap, const Value error,
                                             const std::span<char> output,
                                             const ErrorEnvelopeLimits& limits) noexcept
{
    Writer writer(output, limits.max_output_bytes);
    FallbackInfo fallback;
    Serializer serializer(heap, writer, limits);
    try {
        if (serializer.write(error))
            return {ErrorEnvelopeStatus::Complete, writer.position()};
        fallback = serializer.fallback_info();
    } catch (...) {
        // The only permitted observation is the bounded redacted fallback.
        fallback = serializer.fallback_info();
    }

    if (write_fallback(writer, fallback))
        return {ErrorEnvelopeStatus::Fallback, writer.position()};
    writer.rewind();
    return {ErrorEnvelopeStatus::InsufficientCapacity, 0};
}

}  // namespace baas::script::runtime
