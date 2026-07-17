#include "scheduler/SchedulerPolicy.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace baas::scheduler {
namespace {

using Json = nlohmann::json;

constexpr std::size_t hard_maximum_json_bytes = 4U * 1024U * 1024U;
constexpr std::size_t hard_maximum_depth = 16;
constexpr std::size_t hard_maximum_nodes = 65'536;
constexpr std::size_t hard_maximum_events = 1'024;
constexpr std::size_t hard_maximum_items_per_event = 256;
constexpr std::size_t hard_maximum_string_bytes = 1'024;
constexpr std::size_t hard_maximum_total_string_bytes = 512U * 1'024U;
constexpr UnixSeconds seconds_per_day = 86'400;

struct EffectiveLimits {
    std::size_t json_bytes;
    std::size_t depth;
    std::size_t nodes;
    std::size_t events;
    std::size_t items_per_event;
    std::size_t string_bytes;
    std::size_t total_string_bytes;
};

[[nodiscard]] EffectiveLimits effective_limits(const ParseLimits& limits) noexcept
{
    return {
        std::min(limits.maximum_json_bytes, hard_maximum_json_bytes),
        std::min(limits.maximum_depth, hard_maximum_depth),
        std::min(limits.maximum_nodes, hard_maximum_nodes),
        std::min(limits.maximum_events, hard_maximum_events),
        std::min(limits.maximum_items_per_event, hard_maximum_items_per_event),
        std::min(limits.maximum_string_bytes, hard_maximum_string_bytes),
        std::min(limits.maximum_total_string_bytes,
                 hard_maximum_total_string_bytes),
    };
}

[[nodiscard]] bool is_valid_utf8(const std::string_view input) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(input.data());
    std::size_t index{};
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
        for (std::size_t offset{}; offset < trailing; ++offset) {
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

class ShapeSax final : public nlohmann::json_sax<Json> {
public:
    ShapeSax(const std::size_t maximum_depth,
             const std::size_t maximum_nodes) noexcept
        : maximum_depth_(maximum_depth), maximum_nodes_(maximum_nodes)
    {}

    bool null() override { return scalar(); }
    bool boolean(bool) override { return scalar(); }
    bool number_integer(Json::number_integer_t) override { return scalar(); }
    bool number_unsigned(Json::number_unsigned_t) override { return scalar(); }
    bool number_float(Json::number_float_t, const Json::string_t&) override
    {
        return scalar();
    }
    bool string(Json::string_t&) override { return scalar(); }
    bool binary(Json::binary_t&) override { return scalar(); }
    bool start_object(std::size_t) override { return container_start(); }
    bool key(Json::string_t&) override { return scalar(); }
    bool end_object() override { return container_end(); }
    bool start_array(std::size_t) override { return container_start(); }
    bool end_array() override { return container_end(); }
    bool parse_error(std::size_t, const std::string&,
                     const nlohmann::detail::exception&) override
    {
        syntax_error_ = true;
        return false;
    }

    [[nodiscard]] bool exceeded() const noexcept { return exceeded_; }
    [[nodiscard]] bool syntax_error() const noexcept { return syntax_error_; }

private:
    [[nodiscard]] bool node() noexcept
    {
        if (depth_ > maximum_depth_ || ++nodes_ > maximum_nodes_) {
            exceeded_ = true;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool scalar() noexcept { return node(); }

    [[nodiscard]] bool container_start() noexcept
    {
        if (!node()) return false;
        ++depth_;
        if (depth_ > maximum_depth_) {
            exceeded_ = true;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool container_end() noexcept
    {
        if (depth_ == 0) return false;
        --depth_;
        return true;
    }

    std::size_t maximum_depth_{};
    std::size_t maximum_nodes_{};
    std::size_t depth_{};
    std::size_t nodes_{};
    bool exceeded_{};
    bool syntax_error_{};
};

[[nodiscard]] bool json_integer(const Json& value, std::int64_t& output)
{
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(
                         std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        output = static_cast<std::int64_t>(number);
        return true;
    }
    if (!value.is_number_integer()) return false;
    output = value.get<std::int64_t>();
    return true;
}

[[nodiscard]] bool exact_event_shape(const Json& value)
{
    constexpr std::array required{
        "enabled", "priority", "interval", "daily_reset", "next_tick",
        "event_name", "func_name", "disabled_time_range", "pre_task",
        "post_task",
    };
    if (!value.is_object() || value.size() != required.size()) return false;
    return std::ranges::all_of(required, [&value](const char* key) {
        return value.contains(key);
    });
}

[[nodiscard]] bool take_string(const Json& value, const EffectiveLimits& limits,
                               std::size_t& total_string_bytes,
                               std::string& output, const bool nonempty)
{
    if (!value.is_string()) return false;
    const auto& text = value.get_ref<const std::string&>();
    if ((nonempty && text.empty()) || text.size() > limits.string_bytes
        || text.size() > limits.total_string_bytes - total_string_bytes) {
        return false;
    }
    total_string_bytes += text.size();
    output = text;
    return true;
}

[[nodiscard]] bool parse_time_of_day(const Json& value, TimeOfDay& output)
{
    if (!value.is_array() || value.size() != 3) return false;
    std::array<std::int64_t, 3> parts{};
    for (std::size_t index{}; index < parts.size(); ++index) {
        if (!json_integer(value[index], parts[index])) return false;
    }
    if (parts[0] < 0 || parts[0] > 23 || parts[1] < 0 || parts[1] > 59
        || parts[2] < 0 || parts[2] > 59) {
        return false;
    }
    output = {static_cast<std::uint8_t>(parts[0]),
              static_cast<std::uint8_t>(parts[1]),
              static_cast<std::uint8_t>(parts[2])};
    return true;
}

[[nodiscard]] bool parse_time_list(const Json& value,
                                   const EffectiveLimits& limits,
                                   std::vector<TimeOfDay>& output)
{
    if (!value.is_array() || value.size() > limits.items_per_event) return false;
    output.reserve(value.size());
    for (const auto& item : value) {
        TimeOfDay parsed;
        if (!parse_time_of_day(item, parsed)) return false;
        output.push_back(parsed);
    }
    return true;
}

[[nodiscard]] bool parse_disabled_ranges(
    const Json& value, const EffectiveLimits& limits,
    std::vector<DisabledTimeRange>& output)
{
    if (!value.is_array() || value.size() > limits.items_per_event) return false;
    output.reserve(value.size());
    for (const auto& item : value) {
        if (!item.is_array() || item.size() != 2) return false;
        DisabledTimeRange range;
        if (!parse_time_of_day(item[0], range.start)
            || !parse_time_of_day(item[1], range.end)) {
            return false;
        }
        output.push_back(range);
    }
    return true;
}

[[nodiscard]] bool parse_task_list(const Json& value,
                                   const EffectiveLimits& limits,
                                   std::size_t& total_string_bytes,
                                   std::vector<std::string>& output)
{
    if (!value.is_array() || value.size() > limits.items_per_event) return false;
    output.reserve(value.size());
    for (const auto& item : value) {
        std::string task;
        if (!take_string(item, limits, total_string_bytes, task, true)) return false;
        output.push_back(std::move(task));
    }
    return true;
}

[[nodiscard]] bool parse_event(const Json& value,
                               const EffectiveLimits& limits,
                               std::size_t& total_string_bytes,
                               SchedulerEvent& event)
{
    if (!exact_event_shape(value) || !value["enabled"].is_boolean()) return false;
    event.enabled = value["enabled"].get<bool>();

    std::int64_t priority{};
    if (!json_integer(value["priority"], priority)
        || priority < std::numeric_limits<std::int32_t>::min()
        || priority > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    event.priority = static_cast<std::int32_t>(priority);

    if (!json_integer(value["interval"], event.interval_seconds)
        || event.interval_seconds < -maximum_interval_seconds
        || event.interval_seconds > maximum_interval_seconds
        || !json_integer(value["next_tick"], event.next_tick)
        || event.next_tick < 0 || event.next_tick > maximum_unix_seconds) {
        return false;
    }

    return take_string(value["event_name"], limits, total_string_bytes,
                       event.event_name, true)
        && take_string(value["func_name"], limits, total_string_bytes,
                       event.function_name, true)
        && parse_time_list(value["daily_reset"], limits, event.daily_resets)
        && parse_disabled_ranges(value["disabled_time_range"], limits,
                                 event.disabled_time_ranges)
        && parse_task_list(value["pre_task"], limits, total_string_bytes,
                           event.pre_tasks)
        && parse_task_list(value["post_task"], limits, total_string_bytes,
                           event.post_tasks);
}

[[nodiscard]] bool valid_evaluation_time(const EvaluationTime& now) noexcept
{
    return now.unix_seconds >= 0 && now.unix_seconds <= maximum_unix_seconds
        && now.local_seconds_since_midnight < seconds_per_day
        && now.utc_seconds_since_midnight < seconds_per_day
        && static_cast<std::uint32_t>(now.unix_seconds % seconds_per_day)
            == now.utc_seconds_since_midnight;
}

[[nodiscard]] bool valid_document(const SchedulerDocument& document)
{
    if (document.events.size() > hard_maximum_events) return false;
    std::size_t total_string_bytes{};
    for (const auto& event : document.events) {
        if (event.event_name.empty() || event.function_name.empty()
            || event.event_name.size() > hard_maximum_string_bytes
            || event.function_name.size() > hard_maximum_string_bytes
            || event.interval_seconds < -maximum_interval_seconds
            || event.interval_seconds > maximum_interval_seconds
            || event.next_tick < 0 || event.next_tick > maximum_unix_seconds
            || event.daily_resets.size() > hard_maximum_items_per_event
            || event.disabled_time_ranges.size() > hard_maximum_items_per_event
            || event.pre_tasks.size() > hard_maximum_items_per_event
            || event.post_tasks.size() > hard_maximum_items_per_event) {
            return false;
        }
        const auto count_string = [&total_string_bytes](const std::string& text) {
            if (text.empty() || text.size() > hard_maximum_string_bytes
                || text.size()
                    > hard_maximum_total_string_bytes - total_string_bytes) {
                return false;
            }
            total_string_bytes += text.size();
            return true;
        };
        if (!count_string(event.event_name)
            || !count_string(event.function_name)) {
            return false;
        }
        for (const auto& task : event.pre_tasks) {
            if (!count_string(task)) return false;
        }
        for (const auto& task : event.post_tasks) {
            if (!count_string(task)) return false;
        }
        for (const auto& reset : event.daily_resets) {
            if (reset.hour > 23 || reset.minute > 59 || reset.second > 59) {
                return false;
            }
        }
        for (const auto& range : event.disabled_time_ranges) {
            for (const auto& time : {range.start, range.end}) {
                if (time.hour > 23 || time.minute > 59 || time.second > 59) {
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] bool valid_inventory(const FunctionInventory& inventory)
{
    if (inventory.function_names.size() > hard_maximum_events) return false;
    if (!inventory.initialized && !inventory.function_names.empty()) return false;
    if (inventory.initialized && inventory.function_names.empty()) return false;
    std::size_t total_string_bytes{};
    for (const auto& name : inventory.function_names) {
        if (name.empty() || name.size() > hard_maximum_string_bytes
            || name.size() > hard_maximum_total_string_bytes - total_string_bytes) {
            return false;
        }
        total_string_bytes += name.size();
    }
    return true;
}

[[nodiscard]] bool disabled_now(const SchedulerEvent& event,
                                const std::uint32_t local_seconds) noexcept
{
    for (const auto& range : event.disabled_time_ranges) {
        const auto start = range.start.seconds_since_midnight();
        const auto end = range.end.seconds_since_midnight();
        // Deliberately mirrors Python's inclusive, same-day comparison. A
        // reverse range is therefore empty rather than wrapping midnight.
        if (start <= local_seconds && local_seconds <= end) return true;
    }
    return false;
}

[[nodiscard]] std::optional<UnixSeconds> checked_add(
    const UnixSeconds left, const std::int64_t right) noexcept
{
    if (right < 0 || left > maximum_unix_seconds - right) return std::nullopt;
    return left + right;
}

[[nodiscard]] Json encode_time(const TimeOfDay& value)
{
    return Json::array({value.hour, value.minute, value.second});
}

[[nodiscard]] Json encode_event(const SchedulerEvent& event)
{
    Json resets = Json::array();
    for (const auto& reset : event.daily_resets) resets.push_back(encode_time(reset));
    Json disabled = Json::array();
    for (const auto& range : event.disabled_time_ranges) {
        disabled.push_back(Json::array({encode_time(range.start),
                                        encode_time(range.end)}));
    }
    return Json{
        {"enabled", event.enabled},
        {"priority", event.priority},
        {"interval", event.interval_seconds},
        {"daily_reset", std::move(resets)},
        {"next_tick", event.next_tick},
        {"event_name", event.event_name},
        {"func_name", event.function_name},
        {"disabled_time_range", std::move(disabled)},
        {"pre_task", event.pre_tasks},
        {"post_task", event.post_tasks},
    };
}

}  // namespace

std::uint32_t TimeOfDay::seconds_since_midnight() const noexcept
{
    return static_cast<std::uint32_t>(hour) * 3'600U
        + static_cast<std::uint32_t>(minute) * 60U
        + static_cast<std::uint32_t>(second);
}

ParseResult parse_event_json(const std::string_view text,
                             const ParseLimits& requested_limits)
{
    const auto limits = effective_limits(requested_limits);
    if (text.size() > limits.json_bytes) {
        return {std::nullopt, ParseError::input_too_large};
    }
    if (!is_valid_utf8(text)) return {std::nullopt, ParseError::invalid_utf8};

    try {
        ShapeSax shape{limits.depth, limits.nodes};
        if (!Json::sax_parse(text, &shape)) {
            return {std::nullopt, shape.exceeded() ? ParseError::limit_exceeded
                                                   : ParseError::invalid_json};
        }

        bool duplicate_key{};
        std::vector<std::unordered_set<std::string>> object_keys;
        const auto callback = [&duplicate_key, &object_keys](
                                  int, const Json::parse_event_t event,
                                  Json& parsed) {
            if (event == Json::parse_event_t::object_start) {
                object_keys.emplace_back();
            } else if (event == Json::parse_event_t::key
                       && !object_keys.empty()) {
                if (!object_keys.back().insert(parsed.get<std::string>()).second) {
                    duplicate_key = true;
                }
            } else if (event == Json::parse_event_t::object_end
                       && !object_keys.empty()) {
                object_keys.pop_back();
            }
            return !duplicate_key;
        };
        Json root = Json::parse(text, callback, false);
        if (duplicate_key || root.is_discarded()) {
            return {std::nullopt, ParseError::invalid_schema};
        }
        if (!root.is_array()) return {std::nullopt, ParseError::invalid_schema};
        if (root.size() > limits.events) {
            return {std::nullopt, ParseError::limit_exceeded};
        }

        SchedulerDocument document;
        document.events.reserve(root.size());
        std::size_t total_string_bytes{};
        for (const auto& raw_event : root) {
            SchedulerEvent event;
            if (!parse_event(raw_event, limits, total_string_bytes, event)) {
                return {std::nullopt, ParseError::invalid_value};
            }
            document.events.push_back(std::move(event));
        }
        return {std::move(document), ParseError::none};
    } catch (...) {
        return {std::nullopt, ParseError::invalid_json};
    }
}

std::optional<FunctionInventory> snapshot_function_inventory(
    const SchedulerDocument& document)
{
    if (!valid_document(document)) return std::nullopt;
    FunctionInventory inventory;
    inventory.function_names.reserve(document.events.size());
    for (const auto& event : document.events) {
        inventory.function_names.push_back(event.function_name);
    }
    inventory.initialized = !document.events.empty();
    return inventory;
}

std::optional<FunctionInventory> refresh_function_inventory(
    const FunctionInventory& inventory, const SchedulerDocument& document)
{
    if (!valid_inventory(inventory) || !valid_document(document)) {
        return std::nullopt;
    }
    if (inventory.initialized || document.events.empty()) return inventory;
    return snapshot_function_inventory(document);
}

std::optional<std::vector<ScheduledPlan>> plan_due_events(
    const SchedulerDocument& document, const FunctionInventory& inventory,
    const EvaluationTime& now)
{
    if (!valid_evaluation_time(now) || !valid_document(document)
        || !valid_inventory(inventory)
        || (!inventory.initialized && !document.events.empty())) {
        return std::nullopt;
    }

    std::unordered_set<std::string_view> known_functions;
    known_functions.reserve(inventory.function_names.size());
    for (const auto& name : inventory.function_names) {
        known_functions.insert(name);
    }

    std::vector<std::size_t> due_indices;
    due_indices.reserve(document.events.size());
    for (std::size_t index{}; index < document.events.size(); ++index) {
        const auto& event = document.events[index];
        if (event.enabled && event.next_tick <= now.unix_seconds
            && !disabled_now(event, now.local_seconds_since_midnight)) {
            due_indices.push_back(index);
        }
    }
    std::stable_sort(due_indices.begin(), due_indices.end(),
                     [&document](const auto left, const auto right) {
                         return document.events[left].priority
                             < document.events[right].priority;
                     });

    std::vector<ScheduledPlan> plans;
    plans.reserve(due_indices.size());
    for (const auto index : due_indices) {
        const auto& event = document.events[index];
        ScheduledPlan plan{index, event.event_name, event.function_name,
                           event.priority, {}};
        plan.serial_invocations.reserve(event.pre_tasks.size() + 1
                                        + event.post_tasks.size());
        for (const auto& task : event.pre_tasks) {
            if (known_functions.contains(task)) {
                plan.serial_invocations.push_back(
                    {InvocationRole::pre_task, task});
            }
        }
        plan.serial_invocations.push_back(
            {InvocationRole::current_task, event.function_name});
        for (const auto& task : event.post_tasks) {
            if (known_functions.contains(task)) {
                plan.serial_invocations.push_back(
                    {InvocationRole::post_task, task});
            }
        }
        plans.push_back(std::move(plan));
    }
    return plans;
}

CompletionTransform apply_completion(
    const SchedulerDocument& document, const std::size_t event_index,
    const CompletionOutcome outcome, const EvaluationTime& now,
    const std::optional<std::int64_t> requested_delay_seconds)
{
    CompletionTransform result;
    if (!valid_document(document)) {
        result.error = TransformError::invalid_document;
        return result;
    }
    result.document = document;
    if (!valid_evaluation_time(now)) {
        result.error = TransformError::invalid_time;
        return result;
    }
    if (event_index >= document.events.size()) {
        result.error = TransformError::invalid_event_index;
        return result;
    }
    if (requested_delay_seconds
        && (*requested_delay_seconds < -maximum_interval_seconds
            || *requested_delay_seconds > maximum_interval_seconds)) {
        result.error = TransformError::invalid_delay;
        return result;
    }

    if (outcome == CompletionOutcome::failure) return result;

    const auto& event = document.events[event_index];
    std::optional<UnixSeconds> next_tick;
    if (requested_delay_seconds && *requested_delay_seconds > 0) {
        next_tick = checked_add(now.unix_seconds, *requested_delay_seconds);
    } else {
        const auto interval = event.interval_seconds > 0
            ? event.interval_seconds
            : seconds_per_day;
        const auto interval_tick = checked_add(now.unix_seconds, interval);

        std::optional<UnixSeconds> nearest_reset;
        const auto utc_day_start = now.unix_seconds
            - static_cast<UnixSeconds>(now.utc_seconds_since_midnight);
        for (const auto& reset : event.daily_resets) {
            auto reset_tick = checked_add(
                utc_day_start,
                static_cast<std::int64_t>(reset.seconds_since_midnight()));
            if (!reset_tick) continue;
            if (now.utc_seconds_since_midnight
                > reset.seconds_since_midnight()) {
                reset_tick = checked_add(*reset_tick, seconds_per_day);
                if (!reset_tick) continue;
            }
            if (!nearest_reset || *reset_tick < *nearest_reset) {
                nearest_reset = reset_tick;
            }
        }
        if (nearest_reset && (!interval_tick || *interval_tick >= *nearest_reset)) {
            next_tick = nearest_reset;
        } else {
            next_tick = interval_tick;
        }
    }

    if (!next_tick) {
        result.error = TransformError::timestamp_overflow;
        return result;
    }
    result.document.events[event_index].next_tick = *next_tick;
    result.persist_required = true;
    result.next_tick = next_tick;
    return result;
}

std::optional<std::string> serialize_event_json(
    const SchedulerDocument& document)
{
    if (!valid_document(document)) return std::nullopt;
    try {
        Json root = Json::array();
        for (const auto& event : document.events) root.push_back(encode_event(event));
        auto encoded = root.dump(2);
        if (encoded.size() > hard_maximum_json_bytes) return std::nullopt;
        return encoded;
    } catch (...) {
        return std::nullopt;
    }
}

std::string_view parse_error_name(const ParseError error) noexcept
{
    switch (error) {
        case ParseError::none: return "none";
        case ParseError::input_too_large: return "input_too_large";
        case ParseError::invalid_utf8: return "invalid_utf8";
        case ParseError::invalid_json: return "invalid_json";
        case ParseError::limit_exceeded: return "limit_exceeded";
        case ParseError::invalid_schema: return "invalid_schema";
        case ParseError::invalid_value: return "invalid_value";
    }
    return "unknown";
}

std::string_view invocation_role_name(const InvocationRole role) noexcept
{
    switch (role) {
        case InvocationRole::pre_task: return "pre_task";
        case InvocationRole::current_task: return "current_task";
        case InvocationRole::post_task: return "post_task";
    }
    return "unknown";
}

std::string_view transform_error_name(const TransformError error) noexcept
{
    switch (error) {
        case TransformError::none: return "none";
        case TransformError::invalid_document: return "invalid_document";
        case TransformError::invalid_time: return "invalid_time";
        case TransformError::invalid_event_index: return "invalid_event_index";
        case TransformError::invalid_delay: return "invalid_delay";
        case TransformError::timestamp_overflow: return "timestamp_overflow";
    }
    return "unknown";
}

}  // namespace baas::scheduler
