#include "service/app/RuntimeTaskStatusJson.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace app = baas::service::app;
namespace runtime = baas::service::runtime;

namespace {

int failures{};

void check(const bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

runtime::RuntimeTaskSnapshot snapshot(std::string config_id)
{
    runtime::RuntimeTaskSnapshot value;
    value.config_id = std::move(config_id);
    value.timestamp = 7;
    return value;
}

void check_button_classification(
    std::string button, const bool expected_json,
    const app::RuntimeTaskStatusJsonLimits limits, const char* message)
{
    auto value = snapshot("button-case");
    value.button = button;
    const auto encoded = app::encode_runtime_task_status_json(
        std::span<const runtime::RuntimeTaskSnapshot>{&value, 1}, limits);
    const auto raw_marker = std::string{R"("button":)"} + button;
    check(encoded
              && (encoded.json.find(raw_marker) != std::string::npos)
                  == expected_json,
          message);
}

void test_empty_and_python_field_shape()
{
    const auto empty = app::encode_runtime_task_status_json({});
    check(empty && empty.json == "{}", "empty status must be an empty object");

    auto zeta = snapshot("zeta");
    zeta.running = true;
    zeta.stopping = true;
    zeta.is_flag_run = true;
    zeta.button = R"({"kind":"continue","count":2})";
    zeta.current_task = "daily";
    zeta.waiting_tasks = {"lesson", "shop"};
    zeta.exit_code = 0;
    zeta.run_mode = "scheduler";
    zeta.timestamp = 9'007'199'254'740'991ULL;

    auto alpha = snapshot("alpha");
    alpha.button = "legacy button";
    const std::array input{zeta, alpha};
    const auto encoded = app::encode_runtime_task_status_json(input);
    const std::string expected =
        R"({"alpha":{"config_id":"alpha","running":false,"is_flag_run":false,"button":"legacy button","current_task":null,"waiting_tasks":[],"exit_code":null,"run_mode":null,"timestamp":7},"zeta":{"config_id":"zeta","running":true,"is_flag_run":true,"button":{"kind":"continue","count":2},"current_task":"daily","waiting_tasks":["lesson","shop"],"exit_code":0,"run_mode":"scheduler","timestamp":9007199254740991}})";
    check(encoded && encoded.json == expected,
          "status must be sorted and preserve the exact Python-compatible fields");
    check(encoded.json.find("stopping") == std::string::npos,
          "internal stopping state must not leak onto the v1 wire shape");
}

void test_button_json_and_string_boundary()
{
    auto raw_string = snapshot("a");
    raw_string.button = R"("already-json")";
    auto number = snapshot("b");
    number.button = "1.25";
    auto malformed = snapshot("c");
    malformed.button = "{not-json";
    const std::array values{raw_string, number, malformed};
    const auto encoded = app::encode_runtime_task_status_json(values);
    check(encoded
              && encoded.json.find(R"("button":"already-json")")
                  != std::string::npos
              && encoded.json.find(R"("button":1.25)") != std::string::npos
              && encoded.json.find(R"("button":"{not-json")")
                  != std::string::npos,
          "button must retain bounded JSON values and encode legacy text");
}

void test_button_json_is_bounded_and_duplicate_free()
{
    app::RuntimeTaskStatusJsonLimits limits;
    limits.max_button_json_depth = 1;
    check_button_classification(
        "[[]]", true, limits,
        "max depth one must accept an empty child array at depth one");
    check_button_classification(
        R"({"child":{}})", true, limits,
        "max depth one must accept an empty child object at depth one");
    check_button_classification(
        "[[[]]]", false, limits,
        "max depth one must reject an array at depth two");
    check_button_classification(
        R"([{"child":[]}])", false, limits,
        "mixed array/object JSON must reject a container at depth two");

    limits.max_button_json_depth = 2;
    check_button_classification(
        R"([{}])", true, limits,
        "mixed JSON one level below the maximum depth must remain JSON");
    check_button_classification(
        R"([{"child":[]}])", true, limits,
        "mixed array/object JSON must accept its exact maximum depth");
    check_button_classification(
        R"([[{"child":[]}]])", false, limits,
        "mixed array/object JSON must reject one level above maximum depth");

    limits = {};
    limits.max_button_json_nodes = 1;
    check_button_classification(
        "[]", true, limits,
        "an empty root container must consume exactly one node");
    check_button_classification(
        "[1]", false, limits,
        "button JSON node overflow must use the legacy string boundary");

    limits = {};
    check_button_classification(
        R"({"a":1,"\u0061":2})", false, limits,
        "escaped equivalent duplicate keys must use the legacy string boundary");
}

void test_button_json_corpus()
{
    const app::RuntimeTaskStatusJsonLimits limits;
    check_button_classification("[]", true, limits, "empty array must be JSON");
    check_button_classification("{}", true, limits, "empty object must be JSON");
    check_button_classification(
        R"({"value":"\uD83D\uDE00"})", true, limits,
        "a valid surrogate pair must remain JSON");
    check_button_classification(
        R"("\uD800")", false, limits,
        "an unpaired high surrogate must use the legacy string boundary");
    check_button_classification(
        R"("\uDC00")", false, limits,
        "an unpaired low surrogate must use the legacy string boundary");

    for (const char* number : {
             "0", "-0", "9007199254740991", "-9007199254740991",
             "18446744073709551616", "1.25", "1e+2", "-1E-2"}) {
        check_button_classification(
            number, true, limits, "valid JSON number syntax must remain JSON");
    }
    for (const char* number : {"01", "1.", "1e", "+1", "--1"}) {
        check_button_classification(
            number, false, limits,
            "invalid JSON number syntax must use the legacy string boundary");
    }

    check_button_classification(
        "[1,]", false, limits,
        "an array trailing comma must use the legacy string boundary");
    check_button_classification(
        R"({"a":1,})", false, limits,
        "an object trailing comma must use the legacy string boundary");
    check_button_classification(
        " \t[1]\r\n", true, limits,
        "JSON surrounded by legal whitespace must remain JSON");
}

void test_public_hard_limits()
{
    app::RuntimeTaskStatusJsonLimits limits;
    limits.max_configs = app::runtime_task_status_json_hard_max_configs + 1;
    check(app::encode_runtime_task_status_json({}, limits).error
              == app::RuntimeTaskStatusJsonError::invalid_limits,
          "configured configs above the public hard ceiling must fail early");

    limits = {};
    limits.max_waiting_tasks =
        app::runtime_task_status_json_hard_max_waiting_tasks + 1;
    check(app::encode_runtime_task_status_json({}, limits).error
              == app::RuntimeTaskStatusJsonError::invalid_limits,
          "configured waiting tasks above the public hard ceiling must fail early");

    limits = {};
    limits.max_button_json_depth =
        app::runtime_task_status_json_hard_max_button_depth + 1;
    check(app::encode_runtime_task_status_json({}, limits).error
              == app::RuntimeTaskStatusJsonError::invalid_limits,
          "configured JSON depth above the public hard ceiling must fail early");

    limits = {};
    limits.max_button_json_bytes =
        app::runtime_task_status_json_hard_max_button_bytes + 1;
    check(app::encode_runtime_task_status_json({}, limits).error
              == app::RuntimeTaskStatusJsonError::invalid_limits,
          "configured JSON bytes above the public hard ceiling must fail early");

    limits = {};
    limits.max_button_json_nodes =
        app::runtime_task_status_json_hard_max_button_nodes + 1;
    check(app::encode_runtime_task_status_json({}, limits).error
              == app::RuntimeTaskStatusJsonError::invalid_limits,
          "configured JSON nodes above the public hard ceiling must fail early");

    limits = {};
    limits.max_output_bytes =
        app::runtime_task_status_json_hard_max_output_bytes + 1;
    check(app::encode_runtime_task_status_json({}, limits).error
              == app::RuntimeTaskStatusJsonError::invalid_limits,
          "configured output bytes above the public hard ceiling must fail early");

    limits = {};
    limits.max_button_json_depth =
        app::runtime_task_status_json_hard_max_button_depth;
    const auto accepted_levels =
        app::runtime_task_status_json_hard_max_button_depth + 1;
    check_button_classification(
        std::string(accepted_levels, '[') + std::string(accepted_levels, ']'),
        true, limits,
        "root depth zero must accept a container at the hard maximum depth");
    const auto rejected_levels = accepted_levels + 1;
    check_button_classification(
        std::string(rejected_levels, '[') + std::string(rejected_levels, ']'),
        false, limits,
        "nesting above the hard maximum must be rejected before deeper recursion");
}

void test_rejects_ambiguous_or_invalid_snapshots()
{
    auto duplicate_left = snapshot("same");
    auto duplicate_right = snapshot("same");
    const std::array duplicates{duplicate_left, duplicate_right};
    check(app::encode_runtime_task_status_json(duplicates).error
              == app::RuntimeTaskStatusJsonError::duplicate_config,
          "duplicate config ids must fail closed");

    auto invalid_utf8 = snapshot("bad");
    invalid_utf8.current_task = std::string{"\xC3\x28", 2};
    check(app::encode_runtime_task_status_json(
              std::span<const runtime::RuntimeTaskSnapshot>{&invalid_utf8, 1})
              .error == app::RuntimeTaskStatusJsonError::invalid_snapshot,
          "invalid UTF-8 must not reach provider JSON");

    auto unsafe_timestamp = snapshot("unsafe");
    unsafe_timestamp.timestamp = 9'007'199'254'740'992ULL;
    check(app::encode_runtime_task_status_json(
              std::span<const runtime::RuntimeTaskSnapshot>{&unsafe_timestamp, 1})
              .error == app::RuntimeTaskStatusJsonError::invalid_snapshot,
          "timestamps outside the JavaScript-safe range must be rejected");
}

void test_independent_bounds()
{
    auto value = snapshot("alpha");
    value.waiting_tasks = {"first", "second"};

    app::RuntimeTaskStatusJsonLimits limits;
    limits.max_waiting_tasks = 1;
    check(app::encode_runtime_task_status_json(
              std::span<const runtime::RuntimeTaskSnapshot>{&value, 1}, limits)
              .error == app::RuntimeTaskStatusJsonError::invalid_snapshot,
          "waiting task count must be bounded before encoding");

    limits = {};
    limits.max_output_bytes = 2;
    check(app::encode_runtime_task_status_json(
              std::span<const runtime::RuntimeTaskSnapshot>{&value, 1}, limits)
              .error == app::RuntimeTaskStatusJsonError::capacity,
          "output bytes must be bounded independently");

    limits = {};
    limits.max_configs = 1;
    const std::array two{snapshot("a"), snapshot("b")};
    check(app::encode_runtime_task_status_json(two, limits).error
              == app::RuntimeTaskStatusJsonError::capacity,
          "config count must be bounded before pointer allocation");

    limits = {};
    limits.max_output_bytes = 0;
    check(app::encode_runtime_task_status_json({}, limits).error
              == app::RuntimeTaskStatusJsonError::invalid_limits,
          "invalid limits must fail before encoding");
}

void test_resource_exhaustion_fails_closed()
{
    auto value = snapshot("alpha");
    value.button = R"({"kind":"continue"})";
    const auto input = std::span<const runtime::RuntimeTaskSnapshot>{&value, 1};

    app::RuntimeTaskStatusJsonTestAccess::fail_next_button_parse_allocation();
    const auto exhausted = app::encode_runtime_task_status_json(input);
    check(exhausted.error == app::RuntimeTaskStatusJsonError::resource_exhausted
              && exhausted.json.empty(),
          "button parse allocation failure must fail closed with a stable error");

    const auto recovered = app::encode_runtime_task_status_json(input);
    check(recovered
              && recovered.json.find(R"("button":{"kind":"continue"})")
                  != std::string::npos,
          "allocation injection must be one-shot and must not stringify JSON");
}

void test_error_names_are_stable()
{
    constexpr std::array errors{
        app::RuntimeTaskStatusJsonError::none,
        app::RuntimeTaskStatusJsonError::invalid_limits,
        app::RuntimeTaskStatusJsonError::invalid_snapshot,
        app::RuntimeTaskStatusJsonError::duplicate_config,
        app::RuntimeTaskStatusJsonError::capacity,
        app::RuntimeTaskStatusJsonError::resource_exhausted,
    };
    for (const auto error : errors) {
        check(app::runtime_task_status_json_error_name(error) != "unknown",
              "every status encoding error must have a stable name");
    }
}

}  // namespace

int main()
{
    test_empty_and_python_field_shape();
    test_button_json_and_string_boundary();
    test_button_json_is_bounded_and_duplicate_free();
    test_button_json_corpus();
    test_public_hard_limits();
    test_rejects_ambiguous_or_invalid_snapshots();
    test_independent_bounds();
    test_resource_exhaustion_fails_closed();
    test_error_names_are_stable();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "runtime task status JSON tests passed\n";
    return EXIT_SUCCESS;
}
