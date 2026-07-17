#include "scheduler/SchedulerPolicy.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace baas::scheduler;

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] SchedulerEvent event(std::string name, std::string function,
                                   const std::int32_t priority,
                                   const UnixSeconds next_tick)
{
    SchedulerEvent value;
    value.enabled = true;
    value.priority = priority;
    value.interval_seconds = 10'800;
    value.daily_resets = {{19, 0, 0}};
    value.next_tick = next_tick;
    value.event_name = std::move(name);
    value.function_name = std::move(function);
    return value;
}

[[nodiscard]] FunctionInventory inventory(const SchedulerDocument& document)
{
    const auto result = snapshot_function_inventory(document);
    check(result.has_value(), "valid test document must produce an inventory");
    check(document.events.empty() || (result && result->initialized),
          "non-empty initialization must freeze its function inventory");
    return result.value_or(FunctionInventory{});
}

constexpr std::string_view valid_json = R"json(
[
  {
    "enabled": true,
    "priority": -2,
    "interval": 10800,
    "daily_reset": [[7, 0, 0], [19, 0, 0]],
    "next_tick": 1700000000,
    "event_name": "咖啡厅",
    "func_name": "cafe_reward",
    "disabled_time_range": [[[1, 2, 3], [4, 5, 6]]],
    "pre_task": ["restart"],
    "post_task": []
  },
  {
    "enabled": false,
    "priority": 4,
    "interval": 0,
    "daily_reset": [],
    "next_tick": 0,
    "event_name": "重启",
    "func_name": "restart",
    "disabled_time_range": [],
    "pre_task": [],
    "post_task": []
  }
]
)json";

void test_parser_and_round_trip()
{
    const auto parsed = parse_event_json(valid_json);
    check(static_cast<bool>(parsed), "default event.json shape must parse");
    if (!parsed) return;
    check(parsed.document->events.size() == 2,
          "parser must retain the event array");
    const auto& first = parsed.document->events.front();
    check(first.enabled && first.priority == -2
              && first.interval_seconds == 10'800
              && first.daily_resets.size() == 2
              && first.next_tick == 1'700'000'000
              && first.event_name == "咖啡厅"
              && first.function_name == "cafe_reward"
              && first.disabled_time_ranges.size() == 1
              && first.pre_tasks == std::vector<std::string>{"restart"}
              && first.post_tasks.empty(),
          "parser must preserve every Python scheduler field");

    const auto encoded = serialize_event_json(*parsed.document);
    check(encoded.has_value(), "valid document must serialize for caller persistence");
    if (!encoded) return;
    const auto reparsed = parse_event_json(*encoded);
    check(reparsed && reparsed.document->events.size() == 2
              && reparsed.document->events.front().event_name == "咖啡厅"
              && reparsed.document->events.front().daily_resets.size() == 2,
          "serialized completion document must round-trip through strict parser");
}

void test_parser_fails_closed_and_is_bounded()
{
    check(parse_event_json("{").error == ParseError::invalid_json,
          "malformed JSON must fail closed");
    check(parse_event_json("{}").error == ParseError::invalid_schema,
          "non-array root must fail closed");
    check(parse_event_json(std::string{"[\"\xFF\"]", 5}).error
              == ParseError::invalid_utf8,
          "invalid UTF-8 must fail before DOM allocation");

    std::string duplicate{valid_json};
    const auto enabled = duplicate.find("\"enabled\": true");
    duplicate.replace(enabled, std::string{"\"enabled\": true"}.size(),
                      "\"enabled\": true, \"enabled\": false");
    check(parse_event_json(duplicate).error == ParseError::invalid_schema,
          "duplicate object keys must be rejected");

    std::string unknown{valid_json};
    const auto marker = unknown.find("\"priority\": -2");
    unknown.replace(marker, std::string{"\"priority\": -2"}.size(),
                    "\"priority\": -2, \"unknown\": 1");
    check(parse_event_json(unknown).error == ParseError::invalid_value,
          "unknown fields must be rejected instead of lost on persistence");

    std::string invalid_time{valid_json};
    const auto time = invalid_time.find("[7, 0, 0]");
    invalid_time.replace(time, std::string{"[7, 0, 0]"}.size(), "[24, 0, 0]");
    check(parse_event_json(invalid_time).error == ParseError::invalid_value,
          "out-of-range wall-clock components must fail closed");

    ParseLimits tiny;
    tiny.maximum_json_bytes = 8;
    check(parse_event_json(valid_json, tiny).error == ParseError::input_too_large,
          "caller byte bound must be enforced");
    tiny = {};
    tiny.maximum_events = 1;
    check(parse_event_json(valid_json, tiny).error == ParseError::limit_exceeded,
          "caller event-count bound must be enforced");

    std::string oversized(4U * 1024U * 1024U + 1U, ' ');
    check(parse_event_json(oversized,
                           ParseLimits{std::numeric_limits<std::size_t>::max(),
                                       std::numeric_limits<std::size_t>::max(),
                                       std::numeric_limits<std::size_t>::max(),
                                       std::numeric_limits<std::size_t>::max(),
                                       std::numeric_limits<std::size_t>::max(),
                                       std::numeric_limits<std::size_t>::max(),
                                       std::numeric_limits<std::size_t>::max()})
              .error == ParseError::input_too_large,
          "hard byte ceiling must cap hostile caller-supplied limits");
}

void test_due_filter_and_stable_priority()
{
    SchedulerDocument document;
    document.events = {
        event("equal first", "equal_first", 5, 86'500),
        event("disabled flag", "disabled_flag", -10, 86'500),
        event("future", "future", -9, 86'501),
        event("disabled start", "disabled_start", -8, 86'500),
        event("disabled end", "disabled_end", -7, 86'500),
        event("reverse range", "reverse", 1, 86'500),
        event("equal second", "equal_second", 5, 86'500),
    };
    document.events[1].enabled = false;
    document.events[3].disabled_time_ranges = {{{1, 0, 0}, {2, 0, 0}}};
    document.events[4].disabled_time_ranges = {{{0, 0, 0}, {1, 0, 0}}};
    document.events[5].disabled_time_ranges = {{{2, 0, 0}, {1, 0, 0}}};

    const auto plans = plan_due_events(document, inventory(document),
                                       EvaluationTime{86'500, 3'600, 100});
    check(plans.has_value(), "valid explicit time must produce a plan");
    if (!plans) return;
    check(plans->size() == 3,
          "enabled, due, non-disabled predicates must all hold");
    check((*plans)[0].current_task == "reverse"
              && (*plans)[1].current_task == "equal_first"
              && (*plans)[2].current_task == "equal_second",
          "priority ordering must be stable for equal-priority source entries");
    check((*plans)[0].event_index == 5 && (*plans)[1].event_index == 0
              && (*plans)[2].event_index == 6,
          "plans must retain source indices for the success transform");
}

void test_serial_plan_and_frozen_inventory()
{
    SchedulerDocument initial;
    initial.events = {
        event("helper", "helper", 0, 100),
        event("current", "current", 0, 100),
        event("post", "post", 0, 100),
    };
    const auto initial_inventory = inventory(initial);

    SchedulerDocument reloaded = initial;
    reloaded.events[1].pre_tasks = {"missing", "helper", "new_task"};
    reloaded.events[1].post_tasks = {"post", "missing"};
    reloaded.events.push_back(event("new", "new_task", 9, 86'501));

    const auto plans = plan_due_events(reloaded, initial_inventory,
                                       EvaluationTime{86'500, 0, 100});
    check(plans && plans->size() == 3,
          "due events must plan against the retained initialization inventory");
    if (!plans || plans->size() < 2) return;
    const auto& current = (*plans)[1];
    check(current.serial_invocations.size() == 3,
          "unknown pre/post references must be skipped");
    check(current.serial_invocations[0].role == InvocationRole::pre_task
              && current.serial_invocations[0].function_name == "helper"
              && current.serial_invocations[1].role == InvocationRole::current_task
              && current.serial_invocations[1].function_name == "current"
              && current.serial_invocations[2].role == InvocationRole::post_task
              && current.serial_invocations[2].function_name == "post",
          "pre/current/post invocations must form one deterministic serial plan");
    check(invocation_role_name(current.serial_invocations[0].role) == "pre_task",
          "invocation role vocabulary must remain stable");
}

void test_empty_initialization_defers_inventory_freeze()
{
    const SchedulerDocument empty;
    const auto initial = snapshot_function_inventory(empty);
    check(initial && !initial->initialized && initial->function_names.empty(),
          "empty construction must leave Python's function inventory open");

    const auto still_empty = initial
        ? refresh_function_inventory(*initial, empty)
        : std::optional<FunctionInventory>{};
    check(still_empty && !still_empty->initialized,
          "empty reload must keep the inventory open");

    SchedulerDocument first_nonempty;
    first_nonempty.events = {
        event("helper", "helper", 0, 10),
        event("current", "current", 1, 10),
    };
    first_nonempty.events[1].pre_tasks = {"helper"};
    const auto initialized = still_empty
        ? refresh_function_inventory(*still_empty, first_nonempty)
        : std::optional<FunctionInventory>{};
    check(initialized && initialized->initialized
              && initialized->function_names
                  == std::vector<std::string>({"helper", "current"}),
          "first non-empty reload must initialize funcs like Python");
    const auto plans = initialized
        ? plan_due_events(first_nonempty, *initialized,
                          EvaluationTime{86'500, 0, 100})
        : std::optional<std::vector<ScheduledPlan>>{};
    check(plans && plans->size() == 2
              && (*plans)[1].serial_invocations.size() == 2,
          "newly initialized inventory must admit first-reload pre tasks");

    SchedulerDocument later = first_nonempty;
    later.events.push_back(event("new", "new", 2, 10));
    const auto frozen = initialized
        ? refresh_function_inventory(*initialized, later)
        : std::optional<FunctionInventory>{};
    check(frozen && frozen->function_names == initialized->function_names,
          "inventory must freeze after the first non-empty reload");
}

void test_success_only_next_tick_transform()
{
    SchedulerDocument document;
    document.events = {event("current", "current", 0, 100)};
    document.events[0].interval_seconds = 10'800;
    document.events[0].daily_resets = {{7, 0, 0}, {19, 0, 0}};
    constexpr UnixSeconds day_start = 1'728'000'000;
    constexpr UnixSeconds now_tick = day_start + 17 * 3'600;
    const EvaluationTime now{now_tick, 3 * 3'600, 17 * 3'600};

    const auto failed = apply_completion(document, 0, CompletionOutcome::failure, now);
    check(failed && !failed.persist_required && !failed.next_tick
              && failed.document.events[0].next_tick == 100,
          "failed execution must not advance or request persistence");

    const auto succeeded = apply_completion(document, 0, CompletionOutcome::success, now);
    const auto expected_reset = day_start + 19 * 3'600;
    check(succeeded && succeeded.persist_required
              && succeeded.next_tick == expected_reset
              && succeeded.document.events[0].next_tick == expected_reset,
          "success must choose an earlier UTC daily reset over the interval");
    check(document.events[0].next_tick == 100,
          "completion must be a pure transform and leave caller state unchanged");
    const auto persisted = serialize_event_json(succeeded.document);
    const auto reparsed = persisted ? parse_event_json(*persisted) : ParseResult{};
    check(reparsed && reparsed.document->events[0].next_tick == expected_reset,
          "success output must contain the complete persistable next_tick document");

    const auto override_delay = apply_completion(
        document, 0, CompletionOutcome::success, now, 45);
    check(override_delay && override_delay.next_tick == now_tick + 45,
          "positive task-requested delay must override interval and daily reset");

    document.events[0].interval_seconds = 0;
    document.events[0].daily_resets.clear();
    const auto daily_default = apply_completion(
        document, 0, CompletionOutcome::success, now, 0);
    check(daily_default && daily_default.next_tick == now_tick + 86'400,
          "non-positive delay and interval must use Python's one-day fallback");
}

void test_utc_reset_and_local_disable_are_independent()
{
    SchedulerDocument document;
    document.events = {event("current", "current", 0, 10)};
    document.events[0].disabled_time_ranges = {{{1, 0, 0}, {2, 0, 0}}};
    document.events[0].daily_resets = {{19, 0, 0}};
    constexpr UnixSeconds day_start = 1'987'200;
    const EvaluationTime now{day_start + 18 * 3'600, 90 * 60, 18 * 3'600};

    const auto plans = plan_due_events(document, inventory(document), now);
    check(plans && plans->empty(),
          "disabled range must use injected local seconds and inclusive endpoints");
    const auto completion = apply_completion(
        document, 0, CompletionOutcome::success, now);
    check(completion && completion.next_tick == day_start + 19 * 3'600,
          "daily reset must independently use injected UTC seconds");
}

void test_invalid_policy_inputs_fail_closed()
{
    SchedulerDocument document;
    document.events = {event("current", "current", 0, 10)};
    const auto known = inventory(document);
    check(!plan_due_events(document, known,
                           EvaluationTime{100, 86'400, 100}),
          "invalid local clock input must not produce a plan");
    check(!plan_due_events(document, known,
                           EvaluationTime{100, 0, 86'400}),
          "invalid UTC clock input must not produce a plan");
    const auto bad_index = apply_completion(
        document, 1, CompletionOutcome::success, EvaluationTime{100, 0, 100});
    check(!bad_index && bad_index.error == TransformError::invalid_event_index,
          "unknown event index must fail closed");
    check(bad_index.document.events[0].next_tick == document.events[0].next_tick
              && !bad_index.persist_required,
          "invalid index must return the unchanged bounded document");
    const auto bad_delay = apply_completion(
        document, 0, CompletionOutcome::success, EvaluationTime{100, 0, 100},
        maximum_interval_seconds + 1);
    check(!bad_delay && bad_delay.error == TransformError::invalid_delay,
          "task-requested delay must remain bounded");
    check(bad_delay.document.events[0].next_tick == document.events[0].next_tick
              && !bad_delay.persist_required,
          "invalid delay must return the unchanged bounded document");
    const auto invalid_time = apply_completion(
        document, 0, CompletionOutcome::success,
        EvaluationTime{100, 86'400, 100});
    check(!invalid_time && invalid_time.error == TransformError::invalid_time
              && invalid_time.document.events[0].next_tick
                  == document.events[0].next_tick
              && !invalid_time.persist_required,
          "invalid time must return the unchanged bounded document");
    const auto overflow = apply_completion(
        document, 0, CompletionOutcome::success,
        EvaluationTime{maximum_unix_seconds, 0,
                       static_cast<std::uint32_t>(maximum_unix_seconds % 86'400)},
        1);
    check(!overflow && overflow.error == TransformError::timestamp_overflow,
          "next_tick overflow must fail closed");
    check(overflow.document.events[0].next_tick == document.events[0].next_tick
              && !overflow.persist_required,
          "unsuccessful transform must return the unchanged bounded document");

    const auto inconsistent = apply_completion(
        document, 0, CompletionOutcome::success, EvaluationTime{100, 0, 99});
    check(!inconsistent && inconsistent.error == TransformError::invalid_time
              && inconsistent.document.events[0].next_tick
                  == document.events[0].next_tick
              && !inconsistent.persist_required
              && !plan_due_events(document, known, EvaluationTime{100, 0, 99}),
          "unix timestamp and UTC wall-clock mismatch must fail closed unchanged");

    SchedulerDocument reset_before_overflow;
    reset_before_overflow.events = {event("edge", "edge", 0, 10)};
    reset_before_overflow.events[0].interval_seconds = 1;
    reset_before_overflow.events[0].daily_resets = {{23, 59, 59}};
    const auto reset_wins = apply_completion(
        reset_before_overflow, 0, CompletionOutcome::success,
        EvaluationTime{maximum_unix_seconds, 0, 86'399});
    check(reset_wins && reset_wins.persist_required
              && reset_wins.next_tick == maximum_unix_seconds,
          "legal earlier daily reset must win when interval deadline overflows");

    SchedulerDocument oversized;
    oversized.events.assign(1'025, event("x", "x", 0, 0));
    check(!snapshot_function_inventory(oversized)
              && !plan_due_events(oversized, FunctionInventory{},
                                  EvaluationTime{0, 0, 0})
              && !serialize_event_json(oversized),
          "directly constructed policy documents must obey hard bounds too");

    SchedulerDocument oversized_output;
    oversized_output.events.assign(1'024, event("x", "x", 0, 0));
    for (auto& item : oversized_output.events) {
        item.disabled_time_ranges.assign(
            256, DisabledTimeRange{{0, 0, 0}, {23, 59, 59}});
    }
    check(!serialize_event_json(oversized_output),
          "serialized persistence output must obey the hard byte ceiling");
    check(parse_error_name(static_cast<ParseError>(255)) == "unknown"
              && invocation_role_name(static_cast<InvocationRole>(255)) == "unknown"
              && transform_error_name(static_cast<TransformError>(255)) == "unknown",
          "stable diagnostic-name functions must be total");
}

}  // namespace

int main()
{
    test_parser_and_round_trip();
    test_parser_fails_closed_and_is_bounded();
    test_due_filter_and_stable_priority();
    test_serial_plan_and_frozen_inventory();
    test_empty_initialization_defers_inventory_freeze();
    test_success_only_next_tick_transform();
    test_utc_reset_and_local_disable_are_independent();
    test_invalid_policy_inputs_fail_closed();
    if (failures != 0) {
        std::cerr << failures << " scheduler policy test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "scheduler policy tests passed\n";
    return EXIT_SUCCESS;
}
