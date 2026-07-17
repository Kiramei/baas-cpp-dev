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

struct OperationMetrics {
    std::atomic<unsigned int> commit_calls{};
    std::atomic<unsigned int> transferred{};
    std::atomic<unsigned int> aborted{};
};

class TestPreparedOperation final : public app::RuntimeTaskPreparedOperation {
public:
    TestPreparedOperation(
        std::shared_ptr<OperationMetrics> metrics,
        std::function<app::RuntimeTaskControlResult()> commit)
        : metrics_(std::move(metrics)), commit_(std::move(commit))
    {}

    ~TestPreparedOperation() override
    {
        if (!committed_) metrics_->aborted.fetch_add(1);
    }

    app::RuntimeTaskControlResult commit() override
    {
        metrics_->commit_calls.fetch_add(1);
        auto result = commit_
            ? commit_() : app::RuntimeTaskControlResult{};
        if (result) {
            committed_ = true;
            metrics_->transferred.fetch_add(1);
        }
        return result;
    }

private:
    std::shared_ptr<OperationMetrics> metrics_;
    std::function<app::RuntimeTaskControlResult()> commit_;
    bool committed_{};
};

class RecordingControl final : public app::RuntimeTaskControl {
public:
    using ConfigPrepare =
        std::function<app::RuntimeTaskPrepareResult(std::string_view)>;
    using TaskPrepare = std::function<app::RuntimeTaskPrepareResult(
        std::string_view, std::string_view)>;
    using GlobalPrepare = std::function<app::RuntimeTaskPrepareResult()>;

    [[nodiscard]] app::RuntimeTaskPrepareResult prepared(std::string data)
    {
        return {
            std::move(data),
            std::make_unique<TestPreparedOperation>(metrics, commit_call)};
    }

    app::RuntimeTaskPrepareResult prepare_start_scheduler(
        const std::string_view config_id) override
    {
        ++start_scheduler_calls;
        last_config.assign(config_id);
        return start_scheduler_prepare
            ? start_scheduler_prepare(config_id)
            : prepared(R"({"status":"started","config_id":"default"})");
    }

    app::RuntimeTaskPrepareResult prepare_stop_scheduler(
        const std::string_view config_id) override
    {
        ++stop_scheduler_calls;
        last_config.assign(config_id);
        return stop_scheduler_prepare
            ? stop_scheduler_prepare(config_id)
            : prepared(R"({"status":"stopped","config_id":"default"})");
    }

    app::RuntimeTaskPrepareResult prepare_start_task(
        const std::string_view config_id,
        const std::string_view requested_task) override
    {
        ++start_task_calls;
        last_config.assign(config_id);
        last_task.assign(requested_task);
        return start_task_prepare
            ? start_task_prepare(config_id, requested_task)
            : prepared(R"({"status":"ok","task":"normalized","result":0})");
    }

    app::RuntimeTaskPrepareResult prepare_stop_all_tasks() override
    {
        ++stop_all_calls;
        return stop_all_prepare
            ? stop_all_prepare()
            : prepared(R"({"status":"stopped","results":[]})");
    }

    ConfigPrepare start_scheduler_prepare;
    ConfigPrepare stop_scheduler_prepare;
    TaskPrepare start_task_prepare;
    GlobalPrepare stop_all_prepare;
    std::function<app::RuntimeTaskControlResult()> commit_call;
    std::shared_ptr<OperationMetrics> metrics =
        std::make_shared<OperationMetrics>();
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

[[nodiscard]] protocol::OutboundBatch blocker_batch()
{
    protocol::CommandResponse response;
    response.command = "blocker";
    response.timestamp = 1;
    response.data_json = "{}";
    auto encoded = protocol::encode_command_response(std::move(response));
    if (!encoded) throw std::runtime_error("blocker encode failed");
    return std::move(encoded.batch);
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
    control->start_scheduler_prepare = [control](const std::string_view config) {
        return control->prepared(
            std::string{R"({"status":"already-running","config_id":")"}
            + std::string{config} + R"("})");
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
    check(control->metrics->commit_calls.load() == 5
              && control->metrics->transferred.load() == 5,
          "every successful descriptor must claim then commit exactly once");
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
          "payload depth must be rejected before control and secondary business DOM retention");

    app::RuntimeTaskTriggerLimits nodes;
    nodes.max_payload_nodes = 3;
    auto wide = execute_one(
        control, "solve", "default",
        R"({"task":"x","a":1,"b":2,"c":3})", nodes);
    check(wide.status == protocol::ResponseStatus::error
              && control->start_task_calls.load() == 0,
          "payload node count must be rejected before control admission");

    control->start_task_prepare = [control](std::string_view, std::string_view) {
        return control->prepared("[]");
    };
    auto non_object = execute_one(
        control, "solve", "default", R"({"task":"x"})");
    check(non_object.status == protocol::ResponseStatus::error
              && non_object.json.find("runtime_task_result_invalid_json")
                  != std::string::npos,
          "control success data must be a valid JSON object");
    check(control->metrics->commit_calls.load() == 0
              && control->metrics->aborted.load() == 1,
          "invalid prepared data must abort before claim and commit");

    control->start_task_prepare = [control](std::string_view, std::string_view) {
        return control->prepared(
            R"({"padding":"012345678901234567890123456789"})");
    };
    app::RuntimeTaskTriggerLimits small;
    small.max_result_json_bytes = 32;
    auto oversized = execute_one(
        control, "solve", "default", R"({"task":"x"})", small);
    check(oversized.status == protocol::ResponseStatus::error
              && oversized.json.find("runtime_task_result_capacity")
                  != std::string::npos,
          "control result JSON must be bounded before publication");
    check(control->metrics->commit_calls.load() == 0
              && control->metrics->aborted.load() == 2,
          "oversized prepared data must abort before claim and commit");
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
        control->stop_all_prepare = [error = errors[index]] {
            return app::RuntimeTaskPrepareResult{{}, nullptr, error};
        };
        auto observed = execute_one(control, "stop_all_tasks", std::nullopt);
        check(observed.status == protocol::ResponseStatus::error
                  && observed.json.find(wire_names[index]) != std::string::npos,
              "each control category must have one stable wire error");
        check(app::runtime_task_control_error_name(errors[index]) != "unknown",
              "each control category must have a stable diagnostic name");
    }

    auto throwing = std::make_shared<RecordingControl>();
    throwing->stop_all_prepare = []() -> app::RuntimeTaskPrepareResult {
        throw std::runtime_error("secret device path");
    };
    auto observed = execute_one(throwing, "stop_all_tasks", std::nullopt);
    check(observed.json.find("runtime_task_control_exception")
                  != std::string::npos
              && observed.json.find("secret device path") == std::string::npos,
          "control exceptions must map to a stable non-sensitive error");
}

void test_backpressure_cancel_before_claim_aborts_without_commit()
{
    auto control = std::make_shared<RecordingControl>();
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{};
    bool release{};
    control->start_task_prepare =
        [&](std::string_view, std::string_view) {
        {
            std::lock_guard lock(mutex);
            entered = true;
        }
        condition.notify_all();
        std::unique_lock lock(mutex);
        condition.wait(lock, [&] { return release; });
        return control->prepared(
            R"({"status":"ok","task":"x","result":0})");
    };

    auto dispatcher = dispatcher_for(control);
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    protocol::TriggerSessionLimits session_limits;
    session_limits.max_queued_batches = 1;
    auto trigger_session =
        std::make_shared<protocol::TriggerSession>(session_limits);
    protocol::CommandAdmission admission;
    admission.command = "blocker";
    admission.timestamp = 1;
    admission.payload_bytes = 2;
    auto blocker = trigger_session->admit(std::move(admission));
    check(blocker && trigger_session->publish(
                         *blocker.receipt, blocker_batch()),
          "backpressure fixture must fill the response queue");

    auto connection = executor.connect(trigger_session);
    auto item = ingress_item(
        "solve", 11, "default", R"({"task":"x"})");
    check(item && connection.submit(std::move(*item)),
          "cancel-before-claim fixture must submit");
    {
        std::unique_lock lock(mutex);
        check(condition.wait_for(lock, 3s, [&] { return entered; }),
              "reversible preparation must begin");
    }
    const auto cancelled = connection.request_cancel(11);
    check(cancelled.task_found && cancelled.stop_requested,
          "cancel-before-claim must mark the admitted handler");
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    condition.notify_all();
    check(wait_until([&] {
              return connection.stats().completed == 1;
          }),
          "cancelled terminal must remain pending under backpressure");
    check(control->metrics->commit_calls.load() == 0
              && control->metrics->transferred.load() == 0
              && control->metrics->aborted.load() == 1,
          "cancellation before claim must abort reservation without commit");

    auto first = trigger_session->begin_send();
    check(first && first.lease->batch().command() == "blocker",
          "blocker must be the first queued batch");
    if (first) {
        const auto completed = connection.complete_send(*first.lease);
        check(completed && completed.retry.published == 1,
              "draining blocker must publish retained cancellation");
    }
    auto terminal = trigger_session->begin_send();
    check(terminal
              && terminal.lease->batch().status()
                  == protocol::ResponseStatus::cancelled,
          "cancel-before-claim must retain a cancelled terminal");
    if (terminal) static_cast<void>(connection.complete_send(*terminal.lease));
}

void test_cancel_after_claim_cannot_replace_success_and_commits_once()
{
    auto control = std::make_shared<RecordingControl>();
    trigger::TriggerConnectionOwner* owner{};
    protocol::CancelDecision decision{protocol::CancelDecision::requested};
    control->commit_call = [&] {
        const auto cancelled = owner->request_cancel(12);
        decision = cancelled.session_decision;
        return app::RuntimeTaskControlResult{};
    };

    auto dispatcher = dispatcher_for(control);
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    auto trigger_session = session();
    auto connection = executor.connect(trigger_session);
    owner = &connection;
    auto item = ingress_item(
        "solve", 12, "default", R"({"task":"x"})");
    check(item && connection.submit(std::move(*item)),
          "claim-before-cancel fixture must submit");
    check(wait_until([&] { return trigger_session->stats().queued_batches == 1; }),
          "claimed terminal must be published");
    auto terminal = trigger_session->begin_send();
    check(terminal
              && terminal.lease->batch().status() == protocol::ResponseStatus::ok
              && decision == protocol::CancelDecision::terminal_already_queued,
          "cancellation after claim must not replace the success terminal");
    check(control->metrics->commit_calls.load() == 1
              && control->metrics->transferred.load() == 1
              && control->metrics->aborted.load() == 0,
          "claimed operation must transfer ownership exactly once");
    if (terminal) static_cast<void>(connection.complete_send(*terminal.lease));
}

void test_commit_failure_replaces_claimed_success_with_error()
{
    auto control = std::make_shared<RecordingControl>();
    control->commit_call = [] {
        return app::RuntimeTaskControlResult{
            app::RuntimeTaskControlError::unavailable};
    };
    auto failed = execute_one(
        control, "solve", "default", R"({"task":"x"})");
    check(failed.status == protocol::ResponseStatus::error
              && failed.json.find("runtime_task_control_unavailable")
                  != std::string::npos,
          "commit failure must replace claimed success with stable error");
    check(control->metrics->commit_calls.load() == 1
              && control->metrics->transferred.load() == 0
              && control->metrics->aborted.load() == 1,
          "failed commit must not transfer ownership and must release reservation");

    auto throwing = std::make_shared<RecordingControl>();
    throwing->commit_call = []() -> app::RuntimeTaskControlResult {
        throw std::runtime_error("private commit detail");
    };
    auto exception = execute_one(
        throwing, "solve", "default", R"({"task":"x"})");
    check(exception.status == protocol::ResponseStatus::error
              && exception.json.find("runtime_task_control_exception")
                  != std::string::npos
              && exception.json.find("private commit detail")
                  == std::string::npos,
          "commit exception must replace claimed success with redacted error");
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
    test_backpressure_cancel_before_claim_aborts_without_commit();
    test_cancel_after_claim_cannot_replace_success_and_commits_once();
    test_commit_failure_replaces_claimed_success_with_error();
    test_stable_registration_error_names();
    if (failures != 0) {
        std::cerr << failures << " runtime task trigger test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Runtime task trigger registration tests passed\n";
    return EXIT_SUCCESS;
}
