#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace baas::scheduler {

using UnixSeconds = std::int64_t;

inline constexpr UnixSeconds maximum_unix_seconds = 253'402'300'799;
inline constexpr std::int64_t maximum_interval_seconds = 315'360'000;

struct ParseLimits {
    std::size_t maximum_json_bytes{1U * 1024U * 1024U};
    std::size_t maximum_depth{8};
    std::size_t maximum_nodes{16'384};
    std::size_t maximum_events{256};
    std::size_t maximum_items_per_event{64};
    std::size_t maximum_string_bytes{256};
    std::size_t maximum_total_string_bytes{128U * 1024U};
};

struct TimeOfDay {
    std::uint8_t hour{};
    std::uint8_t minute{};
    std::uint8_t second{};

    [[nodiscard]] std::uint32_t seconds_since_midnight() const noexcept;
    friend bool operator==(const TimeOfDay&, const TimeOfDay&) = default;
};

struct DisabledTimeRange {
    TimeOfDay start;
    TimeOfDay end;
    friend bool operator==(const DisabledTimeRange&, const DisabledTimeRange&) = default;
};

struct SchedulerEvent {
    bool enabled{};
    std::int32_t priority{};
    std::int64_t interval_seconds{};
    std::vector<TimeOfDay> daily_resets;
    UnixSeconds next_tick{};
    std::string event_name;
    std::string function_name;
    std::vector<DisabledTimeRange> disabled_time_ranges;
    std::vector<std::string> pre_tasks;
    std::vector<std::string> post_tasks;
};

struct SchedulerDocument {
    std::vector<SchedulerEvent> events;
};

enum class ParseError : std::uint8_t {
    none,
    input_too_large,
    invalid_utf8,
    invalid_json,
    limit_exceeded,
    invalid_schema,
    invalid_value,
};

struct ParseResult {
    std::optional<SchedulerDocument> document;
    ParseError error{ParseError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return document.has_value();
    }
};

// The caller obtains these values from an injected clock. Keeping local and
// UTC wall-clock positions explicit avoids platform timezone APIs in policy.
struct EvaluationTime {
    UnixSeconds unix_seconds{};
    std::uint32_t local_seconds_since_midnight{};
    std::uint32_t utc_seconds_since_midnight{};
};

enum class InvocationRole : std::uint8_t { pre_task, current_task, post_task };

struct PlannedInvocation {
    InvocationRole role{InvocationRole::current_task};
    std::string function_name;
};

struct ScheduledPlan {
    std::size_t event_index{};
    std::string event_name;
    std::string current_task;
    std::int32_t priority{};
    std::vector<PlannedInvocation> serial_invocations;
};

// Python snapshots self.funcs during Scheduler construction and keeps that
// inventory across event.json reloads. Callers retain this value to preserve
// the same pre/post-task filtering behavior.
struct FunctionInventory {
    std::vector<std::string> function_names;
};

enum class CompletionOutcome : std::uint8_t { success, failure };
enum class TransformError : std::uint8_t {
    none,
    invalid_document,
    invalid_time,
    invalid_event_index,
    invalid_delay,
    timestamp_overflow,
};

struct CompletionTransform {
    SchedulerDocument document;
    bool persist_required{};
    std::optional<UnixSeconds> next_tick;
    TransformError error{TransformError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == TransformError::none;
    }
};

[[nodiscard]] ParseResult parse_event_json(
    std::string_view text, const ParseLimits& limits = {});

[[nodiscard]] std::optional<FunctionInventory> snapshot_function_inventory(
    const SchedulerDocument& document);

// Returns nullopt for an invalid injected timestamp. The output is stable by
// priority: equal-priority events retain source JSON order.
[[nodiscard]] std::optional<std::vector<ScheduledPlan>> plan_due_events(
    const SchedulerDocument& document, const FunctionInventory& inventory,
    const EvaluationTime& now);

// This is the only next_tick transform. For a bounded valid document, failure
// returns an unchanged document and persist_required=false. A successful
// transform returns complete JSON data for the caller to durably replace
// event.json; this library performs no I/O.
[[nodiscard]] CompletionTransform apply_completion(
    const SchedulerDocument& document, std::size_t event_index,
    CompletionOutcome outcome, const EvaluationTime& now,
    std::optional<std::int64_t> requested_delay_seconds = std::nullopt);

[[nodiscard]] std::optional<std::string> serialize_event_json(
    const SchedulerDocument& document);

[[nodiscard]] std::string_view parse_error_name(ParseError error) noexcept;
[[nodiscard]] std::string_view invocation_role_name(InvocationRole role) noexcept;
[[nodiscard]] std::string_view transform_error_name(TransformError error) noexcept;

}  // namespace baas::scheduler
