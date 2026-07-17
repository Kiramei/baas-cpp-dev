#include "service/app/RuntimeTaskStatusJson.h"

#include <algorithm>
#if defined(BAAS_SERVICE_RUNTIME_TASK_STATUS_JSON_TEST_HOOKS)
#include <atomic>
#endif
#include <charconv>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace baas::service::app {
namespace {

constexpr std::uint64_t json_safe_integer_max = 9'007'199'254'740'991ULL;

#if defined(BAAS_SERVICE_RUNTIME_TASK_STATUS_JSON_TEST_HOOKS)
std::atomic_bool fail_next_button_parse_allocation_for_test{};
#endif

enum class EncodeStatus : std::uint8_t {
    success,
    capacity,
    resource_exhausted,
};

enum class JsonSyntaxStatus : std::uint8_t {
    valid,
    invalid,
    resource_exhausted,
};

[[nodiscard]] bool valid_utf8(const std::string_view input) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(input.data());
    std::size_t index = 0;
    while (index < input.size()) {
        const auto lead = bytes[index++];
        if (lead <= 0x7fU) continue;

        std::size_t trailing{};
        std::uint32_t code_point{};
        std::uint32_t minimum{};
        if (lead >= 0xc2U && lead <= 0xdfU) {
            trailing = 1;
            code_point = lead & 0x1fU;
            minimum = 0x80U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            trailing = 2;
            code_point = lead & 0x0fU;
            minimum = 0x800U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            trailing = 3;
            code_point = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (trailing > input.size() - index) return false;
        for (std::size_t offset = 0; offset < trailing; ++offset) {
            const auto byte = bytes[index++];
            if ((byte & 0xc0U) != 0x80U) return false;
            code_point = (code_point << 6U) | (byte & 0x3fU);
        }
        if (code_point < minimum || code_point > 0x10ffffU
            || (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            return false;
        }
    }
    return true;
}

class BoundedJsonSyntax final {
public:
    BoundedJsonSyntax(
        const std::string_view input, const std::size_t maximum_depth,
        const std::size_t maximum_nodes) noexcept
        : input_(input), maximum_depth_(maximum_depth),
          maximum_nodes_(maximum_nodes)
    {}

    [[nodiscard]] bool parse()
    {
        skip_space();
        if (!value(0)) return false;
        skip_space();
        return offset_ == input_.size();
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
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
                break;
            }
            ++offset_;
        }
    }

    [[nodiscard]] bool node() noexcept
    {
        if (nodes_ >= maximum_nodes_) return false;
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

    [[nodiscard]] std::optional<std::uint32_t> unicode_escape() noexcept
    {
        if (offset_ + 4 > input_.size()) return std::nullopt;
        std::uint32_t value{};
        for (int index = 0; index < 4; ++index) {
            const int digit = hex(input_[offset_++]);
            if (digit < 0) return std::nullopt;
            value = (value << 4U) | static_cast<std::uint32_t>(digit);
        }
        if (value >= 0xd800U && value <= 0xdbffU) {
            if (offset_ + 6 > input_.size() || input_[offset_] != '\\'
                || input_[offset_ + 1] != 'u') {
                return std::nullopt;
            }
            offset_ += 2;
            std::uint32_t low{};
            for (int index = 0; index < 4; ++index) {
                const int digit = hex(input_[offset_++]);
                if (digit < 0) return std::nullopt;
                low = (low << 4U) | static_cast<std::uint32_t>(digit);
            }
            if (low < 0xdc00U || low > 0xdfffU) return std::nullopt;
            return 0x10000U + ((value - 0xd800U) << 10U) + (low - 0xdc00U);
        }
        if (value >= 0xdc00U && value <= 0xdfffU) return std::nullopt;
        return value;
    }

    static void append_utf8(std::string& output, const std::uint32_t value)
    {
        if (value <= 0x7fU) {
            output.push_back(static_cast<char>(value));
        } else if (value <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (value >> 6U)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
        } else if (value <= 0xffffU) {
            output.push_back(static_cast<char>(0xe0U | (value >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
        } else {
            output.push_back(static_cast<char>(0xf0U | (value >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
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
                    const auto code_point = unicode_escape();
                    if (!code_point) return std::nullopt;
                    append_utf8(decoded, *code_point);
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
        if (offset_ < input_.size() && input_[offset_] == '-') ++offset_;
        if (offset_ >= input_.size()) return false;
        if (input_[offset_] == '0') {
            ++offset_;
        } else if (input_[offset_] >= '1' && input_[offset_] <= '9') {
            do {
                ++offset_;
            } while (offset_ < input_.size() && input_[offset_] >= '0'
                     && input_[offset_] <= '9');
        } else {
            return false;
        }
        if (offset_ < input_.size() && input_[offset_] == '.') {
            ++offset_;
            const auto fraction = offset_;
            while (offset_ < input_.size() && input_[offset_] >= '0'
                   && input_[offset_] <= '9') {
                ++offset_;
            }
            if (offset_ == fraction) return false;
        }
        if (offset_ < input_.size()
            && (input_[offset_] == 'e' || input_[offset_] == 'E')) {
            ++offset_;
            if (offset_ < input_.size()
                && (input_[offset_] == '+' || input_[offset_] == '-')) {
                ++offset_;
            }
            const auto exponent = offset_;
            while (offset_ < input_.size() && input_[offset_] >= '0'
                   && input_[offset_] <= '9') {
                ++offset_;
            }
            if (offset_ == exponent) return false;
        }
        return true;
    }

    [[nodiscard]] bool array(const std::size_t depth)
    {
        if (!consume('[')) return false;
        skip_space();
        if (consume(']')) return true;
        if (depth == maximum_depth_) return false;
        while (true) {
            if (!value(depth + 1)) return false;
            skip_space();
            if (consume(']')) return true;
            if (!consume(',')) return false;
            skip_space();
        }
    }

    [[nodiscard]] bool object(const std::size_t depth)
    {
        if (!consume('{')) return false;
        std::unordered_set<std::string> keys;
        skip_space();
        if (consume('}')) return true;
        if (depth == maximum_depth_) return false;
        while (true) {
            auto key = string();
            if (!key || !keys.insert(*key).second) return false;
            skip_space();
            if (!consume(':')) return false;
            skip_space();
            if (!value(depth + 1)) return false;
            skip_space();
            if (consume('}')) return true;
            if (!consume(',')) return false;
            skip_space();
        }
    }

    [[nodiscard]] bool value(const std::size_t depth)
    {
        // Match the established bounded-JSON contract: the root value is at
        // depth zero, so max_depth=1 accepts one child container/value level.
        // Check before dispatching another recursive parse to keep the public
        // hard ceiling an actual call-stack bound.
        if (depth > maximum_depth_ || !node() || offset_ >= input_.size()) {
            return false;
        }
        switch (input_[offset_]) {
            case 'n': return literal("null");
            case 't': return literal("true");
            case 'f': return literal("false");
            case '"': return string().has_value();
            case '[': return array(depth);
            case '{': return object(depth);
            default: return number();
        }
    }

    std::string_view input_;
    std::size_t maximum_depth_{};
    std::size_t maximum_nodes_{};
    std::size_t offset_{};
    std::size_t nodes_{};
};

[[nodiscard]] JsonSyntaxStatus validate_json(
    const std::string_view text, const RuntimeTaskStatusJsonLimits& limits)
{
    if (text.size() > limits.max_button_json_bytes || !valid_utf8(text)) {
        return JsonSyntaxStatus::invalid;
    }
    try {
#if defined(BAAS_SERVICE_RUNTIME_TASK_STATUS_JSON_TEST_HOOKS)
        if (fail_next_button_parse_allocation_for_test.exchange(
                false, std::memory_order_relaxed)) {
            throw std::bad_alloc{};
        }
#endif
        return BoundedJsonSyntax{
                   text, limits.max_button_json_depth,
                   limits.max_button_json_nodes}.parse()
            ? JsonSyntaxStatus::valid
            : JsonSyntaxStatus::invalid;
    } catch (const std::bad_alloc&) {
        return JsonSyntaxStatus::resource_exhausted;
    } catch (const std::length_error&) {
        return JsonSyntaxStatus::resource_exhausted;
    } catch (...) {
        return JsonSyntaxStatus::invalid;
    }
}

[[nodiscard]] EncodeStatus append_status(const bool appended) noexcept
{
    return appended ? EncodeStatus::success : EncodeStatus::capacity;
}

class Output final {
public:
    explicit Output(const std::size_t maximum) : maximum_(maximum)
    {
        value_.reserve(std::min<std::size_t>(maximum, 4U * 1'024U));
    }

    [[nodiscard]] bool append(const std::string_view value)
    {
        if (value.size() > maximum_ - value_.size()) return false;
        value_.append(value);
        return true;
    }

    [[nodiscard]] bool character(const char value)
    {
        if (value_.size() == maximum_) return false;
        value_.push_back(value);
        return true;
    }

    [[nodiscard]] bool string(const std::string_view value)
    {
        if (!valid_utf8(value) || !character('"')) {
            return false;
        }
        static constexpr char hex[] = "0123456789abcdef";
        for (const unsigned char byte : value) {
            switch (byte) {
                case '"': if (!append("\\\"")) return false; break;
                case '\\': if (!append("\\\\")) return false; break;
                case '\b': if (!append("\\b")) return false; break;
                case '\f': if (!append("\\f")) return false; break;
                case '\n': if (!append("\\n")) return false; break;
                case '\r': if (!append("\\r")) return false; break;
                case '\t': if (!append("\\t")) return false; break;
                default:
                    if (byte < 0x20U) {
                        char escaped[]{'\\', 'u', '0', '0',
                                       hex[byte >> 4U], hex[byte & 0x0FU]};
                        if (!append(std::string_view{escaped, sizeof(escaped)})) {
                            return false;
                        }
                    } else if (!character(static_cast<char>(byte))) {
                        return false;
                    }
                    break;
            }
        }
        return character('"');
    }

    [[nodiscard]] bool integer(const std::uint64_t value)
    {
        char buffer[32]{};
        const auto converted = std::to_chars(
            std::begin(buffer), std::end(buffer), value);
        return converted.ec == std::errc{}
            && append(std::string_view{
                buffer, static_cast<std::size_t>(converted.ptr - buffer)});
    }

    [[nodiscard]] bool signed_integer(const int value)
    {
        char buffer[32]{};
        const auto converted = std::to_chars(
            std::begin(buffer), std::end(buffer), value);
        return converted.ec == std::errc{}
            && append(std::string_view{
                buffer, static_cast<std::size_t>(converted.ptr - buffer)});
    }

    [[nodiscard]] std::string take() && { return std::move(value_); }

private:
    std::string value_;
    std::size_t maximum_{};
};

[[nodiscard]] bool valid_limits(
    const RuntimeTaskStatusJsonLimits& limits) noexcept
{
    return limits.max_configs != 0 && limits.max_waiting_tasks != 0
        && limits.max_button_json_bytes != 0
        && limits.max_button_json_depth != 0
        && limits.max_button_json_nodes != 0
        && limits.max_output_bytes >= 2
        && limits.max_configs <= runtime_task_status_json_hard_max_configs
        && limits.max_waiting_tasks
            <= runtime_task_status_json_hard_max_waiting_tasks
        && limits.max_button_json_bytes
            <= runtime_task_status_json_hard_max_button_bytes
        && limits.max_button_json_depth
            <= runtime_task_status_json_hard_max_button_depth
        && limits.max_button_json_nodes
            <= runtime_task_status_json_hard_max_button_nodes
        && limits.max_output_bytes
            <= runtime_task_status_json_hard_max_output_bytes;
}

[[nodiscard]] bool no_nul(const std::string_view value) noexcept
{
    return value.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool valid_text(const std::string_view value) noexcept
{
    return no_nul(value) && valid_utf8(value);
}

[[nodiscard]] bool optional_string(
    Output& output, const std::optional<std::string>& value)
{
    return value ? output.string(*value) : output.append("null");
}

[[nodiscard]] bool valid_snapshot(
    const runtime::RuntimeTaskSnapshot& value,
    const RuntimeTaskStatusJsonLimits& limits) noexcept
{
    if (value.config_id.empty() || !valid_text(value.config_id)
        || value.timestamp > json_safe_integer_max
        || value.waiting_tasks.size() > limits.max_waiting_tasks
        || (value.button
            && (value.button->size() > limits.max_button_json_bytes
                || !valid_text(*value.button)))
        || (value.current_task && !valid_text(*value.current_task))
        || (value.run_mode && !valid_text(*value.run_mode))) {
        return false;
    }
    return std::all_of(
        value.waiting_tasks.begin(), value.waiting_tasks.end(),
        [](const std::string& task) { return valid_text(task); });
}

[[nodiscard]] EncodeStatus button(
    Output& output, const std::optional<std::string>& value,
    const RuntimeTaskStatusJsonLimits& limits)
{
    if (!value) return append_status(output.append("null"));
    if (!valid_text(*value)) return EncodeStatus::capacity;
    const auto parsed = validate_json(*value, limits);
    if (parsed == JsonSyntaxStatus::resource_exhausted) {
        return EncodeStatus::resource_exhausted;
    }
    if (parsed == JsonSyntaxStatus::valid) {
        return append_status(output.append(*value));
    }
    return append_status(output.string(*value));
}

[[nodiscard]] EncodeStatus snapshot(
    Output& output, const runtime::RuntimeTaskSnapshot& value,
    const RuntimeTaskStatusJsonLimits& limits)
{
    if (!output.append("{\"config_id\":") || !output.string(value.config_id)
        || !output.append(",\"running\":")
        || !output.append(value.running ? "true" : "false")
        || !output.append(",\"is_flag_run\":")
        || !output.append(value.is_flag_run ? "true" : "false")
        || !output.append(",\"button\":")) {
        return EncodeStatus::capacity;
    }
    const auto button_status = button(output, value.button, limits);
    if (button_status != EncodeStatus::success) return button_status;
    if (!output.append(",\"current_task\":")
        || !optional_string(output, value.current_task)
        || !output.append(",\"waiting_tasks\":[")) {
        return EncodeStatus::capacity;
    }
    for (std::size_t index = 0; index < value.waiting_tasks.size(); ++index) {
        if ((index != 0 && !output.character(','))
            || !output.string(value.waiting_tasks[index])) {
            return EncodeStatus::capacity;
        }
    }
    if (!output.append("],\"exit_code\":")
        || (value.exit_code ? !output.signed_integer(*value.exit_code)
                            : !output.append("null"))
        || !output.append(",\"run_mode\":")
        || !optional_string(output, value.run_mode)
        || !output.append(",\"timestamp\":")
        || !output.integer(value.timestamp)
        || !output.character('}')) {
        return EncodeStatus::capacity;
    }
    return EncodeStatus::success;
}

}  // namespace

std::string_view runtime_task_status_json_error_name(
    const RuntimeTaskStatusJsonError error) noexcept
{
    using enum RuntimeTaskStatusJsonError;
    switch (error) {
        case none: return "none";
        case invalid_limits: return "invalid_limits";
        case invalid_snapshot: return "invalid_snapshot";
        case duplicate_config: return "duplicate_config";
        case capacity: return "capacity";
        case resource_exhausted: return "resource_exhausted";
    }
    return "unknown";
}

RuntimeTaskStatusJsonResult encode_runtime_task_status_json(
    const std::span<const runtime::RuntimeTaskSnapshot> snapshots,
    const RuntimeTaskStatusJsonLimits limits) noexcept
{
    if (!valid_limits(limits)) {
        return {{}, RuntimeTaskStatusJsonError::invalid_limits};
    }
    if (snapshots.size() > limits.max_configs) {
        return {{}, RuntimeTaskStatusJsonError::capacity};
    }
    try {
        std::vector<const runtime::RuntimeTaskSnapshot*> ordered;
        ordered.reserve(snapshots.size());
        for (const auto& item : snapshots) ordered.push_back(&item);
        std::sort(
            ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
                return left->config_id < right->config_id;
            });
        for (std::size_t index = 0; index < ordered.size(); ++index) {
            if (!valid_snapshot(*ordered[index], limits)) {
                return {{}, RuntimeTaskStatusJsonError::invalid_snapshot};
            }
            if (index != 0
                    && ordered[index - 1]->config_id
                        == ordered[index]->config_id) {
                return {{}, RuntimeTaskStatusJsonError::duplicate_config};
            }
        }

        Output output{limits.max_output_bytes};
        if (!output.character('{')) {
            return {{}, RuntimeTaskStatusJsonError::capacity};
        }
        for (std::size_t index = 0; index < ordered.size(); ++index) {
            if ((index != 0 && !output.character(','))
                || !output.string(ordered[index]->config_id)
                || !output.character(':')) {
                return {{}, RuntimeTaskStatusJsonError::capacity};
            }
            const auto encoded = snapshot(output, *ordered[index], limits);
            if (encoded == EncodeStatus::resource_exhausted) {
                return {{}, RuntimeTaskStatusJsonError::resource_exhausted};
            }
            if (encoded != EncodeStatus::success) {
                return {{}, RuntimeTaskStatusJsonError::capacity};
            }
        }
        if (!output.character('}')) {
            return {{}, RuntimeTaskStatusJsonError::capacity};
        }
        return {std::move(output).take(), RuntimeTaskStatusJsonError::none};
    } catch (const std::bad_alloc&) {
        return {{}, RuntimeTaskStatusJsonError::resource_exhausted};
    } catch (...) {
        return {{}, RuntimeTaskStatusJsonError::invalid_snapshot};
    }
}

#if defined(BAAS_SERVICE_RUNTIME_TASK_STATUS_JSON_TEST_HOOKS)
void RuntimeTaskStatusJsonTestAccess::fail_next_button_parse_allocation() noexcept
{
    fail_next_button_parse_allocation_for_test.store(
        true, std::memory_order_relaxed);
}
#endif

}  // namespace baas::service::app
