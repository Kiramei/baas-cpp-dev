#include "service/app/RuntimeTaskStatusJson.h"

#include "service/adapters/BoundedJson.h"

#include <algorithm>
#include <charconv>
#include <iterator>
#include <limits>
#include <new>
#include <vector>

namespace baas::service::app {
namespace {

constexpr std::uint64_t json_safe_integer_max = 9'007'199'254'740'991ULL;

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
        if (!adapters::bounded_json::is_valid_utf8(value) || !character('"')) {
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
        && limits.max_output_bytes >= 2;
}

[[nodiscard]] bool no_nul(const std::string_view value) noexcept
{
    return value.find('\0') == std::string_view::npos;
}

[[nodiscard]] bool valid_text(const std::string_view value) noexcept
{
    return no_nul(value) && adapters::bounded_json::is_valid_utf8(value);
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

[[nodiscard]] bool button(
    Output& output, const std::optional<std::string>& value,
    const RuntimeTaskStatusJsonLimits& limits)
{
    if (!value) return output.append("null");
    if (!valid_text(*value)) return false;
    const auto parsed = adapters::bounded_json::parse_json(
        *value,
        {limits.max_button_json_bytes,
         limits.max_button_json_depth,
         limits.max_button_json_nodes});
    if (parsed) return output.append(*value);
    return output.string(*value);
}

[[nodiscard]] bool snapshot(
    Output& output, const runtime::RuntimeTaskSnapshot& value,
    const RuntimeTaskStatusJsonLimits& limits)
{
    if (!output.append("{\"config_id\":") || !output.string(value.config_id)
        || !output.append(",\"running\":")
        || !output.append(value.running ? "true" : "false")
        || !output.append(",\"is_flag_run\":")
        || !output.append(value.is_flag_run ? "true" : "false")
        || !output.append(",\"button\":")
        || !button(output, value.button, limits)
        || !output.append(",\"current_task\":")
        || !optional_string(output, value.current_task)
        || !output.append(",\"waiting_tasks\":[")) {
        return false;
    }
    for (std::size_t index = 0; index < value.waiting_tasks.size(); ++index) {
        if ((index != 0 && !output.character(','))
            || !output.string(value.waiting_tasks[index])) {
            return false;
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
        return false;
    }
    return true;
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
            if (!snapshot(output, *ordered[index], limits)) {
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

}  // namespace baas::service::app
