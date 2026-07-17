#include "service/app/RuntimeTaskTriggerRegistration.h"
#include "service/trigger/TriggerExecutor.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
namespace app = baas::service::app;
namespace trigger = baas::service::trigger;
namespace protocol = baas::service::protocol::trigger;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Predicate>
bool wait_until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

class RecordingControl final : public app::RuntimeTaskControl {
public:
    using ConfigCall =
        std::function<app::RuntimeTaskControlResult(std::string_view)>;
    using TaskCall = std::function<app::RuntimeTaskControlResult(
        std::string_view, std::string_view)>;
    using GlobalCall = std::function<app::RuntimeTaskControlResult()>;

    app::RuntimeTaskControlResult start_scheduler(
        const std::string_view config_id) override
    {
        ++start_scheduler_calls;
        last_config.assign(config_id);
        return start_scheduler_call
            ? start_scheduler_call(config_id)
            : app::RuntimeTaskControlResult{
                  R"({"status":"started","config_id":"default"})"};
    }

    app::RuntimeTaskControlResult stop_scheduler(
        const std::string_view config_id) override
    {
        ++stop_scheduler_calls;
        last_config.assign(config_id);
        return stop_scheduler_call
            ? stop_scheduler_call(config_id)
            : app::RuntimeTaskControlResult{
                  R"({"status":"stopped","config_id":"default"})"};
    }

    app::RuntimeTaskControlResult start_task(
        const std::string_view config_id,
        const std::string_view requested_task) override
    {
        ++start_task_calls;
        last_config.assign(config_id);
        last_task.assign(requested_task);
        return start_task_call
            ? start_task_call(config_id, requested_task)
            : app::RuntimeTaskControlResult{
                  R"({"status":"ok","task":"normalized","result":0})"};
    }

    app::RuntimeTaskControlResult stop_all_tasks() override
    {
        ++stop_all_calls;
        return stop_all_call
            ? stop_all_call()
            : app::RuntimeTaskControlResult{
                  R"({"status":"stopped","results":[]})"};
    }

    ConfigCall start_scheduler_call;
    ConfigCall stop_scheduler_call;
    TaskCall start_task_call;
    GlobalCall stop_all_call;
    std::atomic<unsigned int> start_scheduler_calls{};
    std::atomic<unsigned int> stop_scheduler_calls{};
    std::atomic<unsigned int> start_task_calls{};
    std::atomic<unsigned int> stop_all_calls{};
    std::string last_config;
    std::string last_task;
};

[[nodiscard]] std::shared_ptr<protocol::TriggerSession> session()
{
    return std::make_shared<protocol::TriggerSession>();
}

[[nodiscard]] std::optional<protocol::TriggerIngressItem> ingress_item(
    const std::string_view command,
    const protocol::Timestamp timestamp,
    const std::optional<std::string_view> config_id,
    const std::string_view payload = "{}",
    protocol::TriggerIngressLimits ingress_limits = {})
{
    std::string json = R"({"type":"command","command":")";
    json.append(command);
    json.append(R"(","timestamp":)");
    json.append(std::to_string(timestamp));
    if (config_id) {
        json.append(R"(,"config_id":")");
        json.append(*config_id);
        json.push_back('"');
    }
    json.append(R"(,"payload":)");
    json.append(payload);
    json.push_back('}');
    protocol::TriggerIngress ingress(ingress_limits);
    if (!ingress.receive_json_frame(json)) return std::nullopt;
    return ingress.take_ready();
}

[[nodiscard]] std::shared_ptr<const trigger::TriggerDispatcher> dispatcher_for(
    std::shared_ptr<app::RuntimeTaskControl> control,
    const app::RuntimeTaskTriggerLimits limits = {},
    const trigger::TriggerDispatchLimits dispatch_limits = {})
{
    auto made = app::make_runtime_task_trigger_registrations(
        std::move(control), limits);
    if (!made) throw std::runtime_error("runtime task registration build failed");
    auto built = trigger::TriggerDispatcher::create(
        std::move(made.registrations), dispatch_limits);
    if (!built) throw std::runtime_error("runtime task dispatcher build failed");
    return std::make_shared<const trigger::TriggerDispatcher>(
        std::move(*built.dispatcher));
}

struct ObservedResponse {
    protocol::ResponseStatus status{protocol::ResponseStatus::error};
    std::string json;
};

[[nodiscard]] ObservedResponse execute_one(
    std::shared_ptr<app::RuntimeTaskControl> control,
    const std::string_view command,
    const std::optional<std::string_view> config_id,
    const std::string_view payload = "{}",
    const app::RuntimeTaskTriggerLimits limits = {},
    const trigger::TriggerDispatchLimits dispatch_limits = {})
{
    auto dispatcher = dispatcher_for(control, limits, dispatch_limits);
    trigger::TriggerExecutor executor(dispatcher, {1, 2, 2, 2});
    auto trigger_session = session();
    auto connection = executor.connect(trigger_session);
    auto item = ingress_item(command, 7, config_id, payload);
    if (!item || !connection.submit(std::move(*item))) {
        throw std::runtime_error("runtime task submit failed");
    }
    if (!wait_until([&] { return trigger_session->stats().queued_batches == 1; })) {
        throw std::runtime_error("runtime task response timeout");
    }
    auto begun = trigger_session->begin_send();
    if (!begun) throw std::runtime_error("runtime task response lease failed");
    ObservedResponse result{
        begun.lease->batch().status(), begun.lease->batch().json()};
    if (!connection.complete_send(*begun.lease)) {
        throw std::runtime_error("runtime task response completion failed");
    }
    return result;
}

void test_factory_is_exact_and_fail_closed()
{
    auto missing = app::make_runtime_task_trigger_registrations(nullptr);
    check(!missing
              && missing.error
                  == app::RuntimeTaskTriggerRegistrationError::missing_control,
          "factory must reject a missing production control owner");

    auto control = std::make_shared<RecordingControl>();
    app::RuntimeTaskTriggerLimits invalid;
    invalid.max_payload_depth = 0;
    check(app::make_runtime_task_trigger_registrations(control, invalid).error
              == app::RuntimeTaskTriggerRegistrationError::invalid_limits,
          "factory must reject zero payload depth");
    invalid = {};
    invalid.max_payload_nodes =
        app::runtime_task_trigger_hard_max_json_nodes + 1;
    check(app::make_runtime_task_trigger_registrations(control, invalid).error
              == app::RuntimeTaskTriggerRegistrationError::invalid_limits,
          "factory must enforce the hard payload node ceiling");

    auto made = app::make_runtime_task_trigger_registrations(control);
    constexpr std::array<std::string_view, 5> expected{
        "start_scheduler", "stop_scheduler", "solve", "start_*",
        "stop_all_tasks"};
    check(made && made.registrations.size() == expected.size(),
          "factory must return exactly five runtime task registrations");
    for (std::size_t index = 0;
         index < expected.size() && index < made.registrations.size(); ++index) {
        check(made.registrations[index].descriptor_name == expected[index],
              "registration descriptor order must remain deterministic");
    }
}

void test_ingress_and_adapter_validate_required_fields()
{
    protocol::TriggerIngress ingress;
    const auto missing_config = ingress.receive_json_frame(
        R"({"type":"command","command":"solve","timestamp":1,"payload":{"task":"x"}})");
    check(!missing_config
              && missing_config.error
                  == protocol::TriggerIngressError::config_id_required,
          "catalog ingress must reject solve without config_id");

    auto control = std::make_shared<RecordingControl>();
    auto missing_task = execute_one(control, "solve", "default", "{}");
    check(missing_task.status == protocol::ResponseStatus::error
              && missing_task.json.find("task is required for solve command")
                  != std::string::npos
              && control->start_task_calls.load() == 0,
          "solve must reject a missing task before control admission");
    auto wrong_task = execute_one(
        control, "solve", "default", R"({"task":42})");
    check(wrong_task.status == protocol::ResponseStatus::error
              && control->start_task_calls.load() == 0,
          "solve must reject a non-string task before control admission");
}

void test_python_wire_success_and_alias_handoff()
{
    auto control = std::make_shared<RecordingControl>();
    control->start_scheduler_call = [](const std::string_view config) {
        return app::RuntimeTaskControlResult{
            std::string{R"({"status":"already-running","config_id":")"}
            + std::string{config} + R"("})"};
    };
    auto started = execute_one(control, "start_scheduler", "alpha");
    check(started.status == protocol::ResponseStatus::ok
              && started.json
                  == R"({"type":"command_response","command":"start_scheduler","status":"ok","data":{"status":"already-running","config_id":"alpha"},"timestamp":7})",
          "already-running must remain successful Python-compatible data");

    auto stopped = execute_one(control, "stop_scheduler", "default");
    check(stopped.status == protocol::ResponseStatus::ok
              && control->stop_scheduler_calls.load() == 1,
          "stop_scheduler must transfer one stop request");

    auto solved = execute_one(
        control, "solve", "default", R"({"task":"daily"})");
    check(solved.status == protocol::ResponseStatus::ok
              && control->last_task == "daily",
          "solve must hand the payload task to the control owner");

    auto aliased = execute_one(control, "start_hard_task", "default");
    check(aliased.status == protocol::ResponseStatus::ok
              && control->last_task == "start_hard_task",
          "start_* must preserve the original alias for owner normalization");

    auto all = execute_one(control, "stop_all_tasks", std::nullopt);
    check(all.status == protocol::ResponseStatus::ok
              && control->stop_all_calls.load() == 1,
          "stop_all_tasks must transfer one global stop request");
}

void test_bounded_payload_before_dom_and_result_validation()
{
    auto control = std::make_shared<RecordingControl>();
    app::RuntimeTaskTriggerLimits depth;
    depth.max_payload_depth = 2;
    auto deep = execute_one(
        control, "solve", "default",
        R"({"task":"x","a":{"b":{"c":1}}})", depth);
    check(deep.status == protocol::ResponseStatus::error
              && control->start_task_calls.load() == 0,
          "payload depth must be rejected before control and DOM retention");

    app::RuntimeTaskTriggerLimits nodes;
    nodes.max_payload_nodes = 3;
    auto wide = execute_one(
        control, "solve", "default",
        R"({"task":"x","a":1,"b":2,"c":3})", nodes);
    check(wide.status == protocol::ResponseStatus::error
              && control->start_task_calls.load() == 0,
          "payload node count must be rejected before control admission");

    control->start_task_call = [](std::string_view, std::string_view) {
        return app::RuntimeTaskControlResult{"[]"};
    };
    auto non_object = execute_one(
        control, "solve", "default", R"({"task":"x"})");
    check(non_object.status == protocol::ResponseStatus::error
              && non_object.json.find("runtime_task_result_invalid_json")
                  != std::string::npos,
          "control success data must be a valid JSON object");

    control->start_task_call = [](std::string_view, std::string_view) {
        return app::RuntimeTaskControlResult{
            R"({"padding":"012345678901234567890123456789"})"};
    };
    app::RuntimeTaskTriggerLimits small;
    small.max_result_json_bytes = 32;
    auto oversized = execute_one(
        control, "solve", "default", R"({"task":"x"})", small);
    check(oversized.status == protocol::ResponseStatus::error
              && oversized.json.find("runtime_task_result_capacity")
                  != std::string::npos,
          "control result JSON must be bounded before publication");
}

void test_stable_control_errors_and_exception_redaction()
{
    constexpr std::array errors{
        app::RuntimeTaskControlError::invalid_config_id,
        app::RuntimeTaskControlError::invalid_task,
        app::RuntimeTaskControlError::conflict,
        app::RuntimeTaskControlError::capacity,
        app::RuntimeTaskControlError::unavailable,
        app::RuntimeTaskControlError::internal_error,
    };
    constexpr std::array<std::string_view, 6> wire_names{
        "runtime_task_invalid_config_id", "runtime_task_invalid_task",
        "runtime_task_conflict", "runtime_task_control_capacity",
        "runtime_task_control_unavailable", "runtime_task_internal_error"};
    for (std::size_t index = 0; index < errors.size(); ++index) {
        auto control = std::make_shared<RecordingControl>();
        control->stop_all_call = [error = errors[index]] {
            return app::RuntimeTaskControlResult{{}, error};
        };
        auto observed = execute_one(control, "stop_all_tasks", std::nullopt);
        check(observed.status == protocol::ResponseStatus::error
                  && observed.json.find(wire_names[index]) != std::string::npos,
              "each control category must have one stable wire error");
        check(app::runtime_task_control_error_name(errors[index]) != "unknown",
              "each control category must have a stable diagnostic name");
    }

    auto throwing = std::make_shared<RecordingControl>();
    throwing->stop_all_call = []() -> app::RuntimeTaskControlResult {
        throw std::runtime_error("secret device path");
    };
    auto observed = execute_one(throwing, "stop_all_tasks", std::nullopt);
    check(observed.json.find("runtime_task_control_exception")
                  != std::string::npos
              && observed.json.find("secret device path") == std::string::npos,
          "control exceptions must map to a stable non-sensitive error");
}

void test_disconnect_does_not_cancel_transferred_work()
{
    auto control = std::make_shared<RecordingControl>();
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{};
    bool release{};
    std::atomic<bool> returned{};
    control->start_task_call = [&](std::string_view, std::string_view) {
        {
            std::lock_guard lock(mutex);
            entered = true;
        }
        condition.notify_all();
        std::unique_lock lock(mutex);
        condition.wait(lock, [&] { return release; });
        returned.store(true, std::memory_order_release);
        return app::RuntimeTaskControlResult{
            R"({"status":"ok","task":"x","result":0})"};
    };

    auto dispatcher = dispatcher_for(control);
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    auto trigger_session = session();
    auto connection = executor.connect(trigger_session);
    auto item = ingress_item(
        "solve", 11, "default", R"({"task":"x"})");
    check(item && connection.submit(std::move(*item)),
          "disconnect fixture must submit");
    {
        std::unique_lock lock(mutex);
        check(condition.wait_for(lock, 3s, [&] { return entered; }),
              "control ownership transfer must begin");
    }
    const auto closed = connection.close();
    check(closed.cancellations_consumed == 1,
          "closing Trigger must consume the admitted correlation");
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    condition.notify_all();
    check(wait_until([&] {
              return returned.load(std::memory_order_acquire)
                  && executor.stats().active_tasks == 0;
          }),
          "connection stop must not interrupt service-owned control transfer");
    check(control->start_task_calls.load() == 1,
          "disconnect must not retry or cancel transferred runtime work");
}

void test_stable_registration_error_names()
{
    constexpr std::array errors{
        app::RuntimeTaskTriggerRegistrationError::none,
        app::RuntimeTaskTriggerRegistrationError::missing_control,
        app::RuntimeTaskTriggerRegistrationError::invalid_limits,
        app::RuntimeTaskTriggerRegistrationError::resource_exhausted,
    };
    for (const auto error : errors) {
        check(app::runtime_task_trigger_registration_error_name(error)
                  != "unknown",
              "every registration error must have a stable name");
    }
}

}  // namespace

int main()
{
    test_factory_is_exact_and_fail_closed();
    test_ingress_and_adapter_validate_required_fields();
    test_python_wire_success_and_alias_handoff();
    test_bounded_payload_before_dom_and_result_validation();
    test_stable_control_errors_and_exception_redaction();
    test_disconnect_does_not_cancel_transferred_work();
    test_stable_registration_error_names();
    if (failures != 0) {
        std::cerr << failures << " runtime task trigger test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Runtime task trigger registration tests passed\n";
    return EXIT_SUCCESS;
}
