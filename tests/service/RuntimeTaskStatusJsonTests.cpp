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
    auto duplicate = snapshot("duplicate");
    duplicate.button = R"({"a":1,"\u0061":2})";
    const auto duplicate_encoded = app::encode_runtime_task_status_json(
        std::span<const runtime::RuntimeTaskSnapshot>{&duplicate, 1});
    check(duplicate_encoded
              && duplicate_encoded.json.find(
                     R"("button":"{\"a\":1,\"\\u0061\":2}")")
                  != std::string::npos,
          "duplicate decoded object keys must use the legacy string boundary");

    app::RuntimeTaskStatusJsonLimits limits;
    limits.max_button_json_depth = 1;
    auto deep = snapshot("deep");
    deep.button = "[[]]";
    const auto depth_encoded = app::encode_runtime_task_status_json(
        std::span<const runtime::RuntimeTaskSnapshot>{&deep, 1}, limits);
    check(depth_encoded
              && depth_encoded.json.find(R"("button":"[[]]")")
                  != std::string::npos,
          "button JSON depth overflow must use the legacy string boundary");

    limits = {};
    limits.max_button_json_nodes = 1;
    auto nodes = snapshot("nodes");
    nodes.button = "[1]";
    const auto node_encoded = app::encode_runtime_task_status_json(
        std::span<const runtime::RuntimeTaskSnapshot>{&nodes, 1}, limits);
    check(node_encoded
              && node_encoded.json.find(R"("button":"[1]")")
                  != std::string::npos,
          "button JSON node overflow must use the legacy string boundary");
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
