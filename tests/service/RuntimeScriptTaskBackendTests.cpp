#include "service/runtime/RuntimeScriptTaskBackend.h"

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace runtime = baas::service::runtime;
using namespace std::chrono_literals;

namespace {

int failures{};

void check(const bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

runtime::RuntimeTaskRequest request(
    std::string config_id = "config-a", std::string task = "group")
{
    runtime::RuntimeTaskRequest value;
    value.config_id = std::move(config_id);
    value.run_mode = "solve";
    value.current_task = std::move(task);
    value.waiting_tasks = {"mail", "daily-task-power"};
    return value;
}

runtime::RuntimeScriptTaskIdentity identity_for(
    const runtime::RuntimeTaskRequest& value,
    const std::span<const std::string> requested_plan)
{
    runtime::RuntimeScriptTaskIdentity identity;
    identity.config_id = value.config_id;
    identity.config_snapshot_id = value.config_id + "@config-generation-7";
    identity.profile = "zh-cn";
    identity.device_id = "emulator-5554";
    identity.runtime_generation = std::string(64, 'a');
    identity.scripts_commit = std::string(40, 'b');
    identity.resources_commit = std::string(64, 'c');
    identity.run_mode = value.run_mode;
    identity.requested_task_plan.assign(
        requested_plan.begin(), requested_plan.end());
    identity.canonical_task_plan = identity.requested_task_plan;
    return identity;
}

using Execute = std::function<runtime::RuntimeTaskTerminal(
    const runtime::RuntimeScriptTaskExecutionControl&,
    const runtime::RuntimeTaskProgressReporter&)>;

class TestRuntime final : public runtime::RuntimeScriptTaskRuntime {
public:
    TestRuntime(runtime::RuntimeScriptTaskIdentity identity, Execute execute)
        : identity_(std::move(identity)), execute_(std::move(execute))
    {
    }

    const runtime::RuntimeScriptTaskIdentity& identity() const noexcept override
    {
        return identity_;
    }

    runtime::RuntimeTaskTerminal execute(
        const runtime::RuntimeScriptTaskExecutionControl& control,
        const runtime::RuntimeTaskProgressReporter& report_progress) override
    {
        return execute_(control, report_progress);
    }

private:
    runtime::RuntimeScriptTaskIdentity identity_;
    Execute execute_;
};

using Create = std::function<std::unique_ptr<runtime::RuntimeScriptTaskRuntime>(
    const runtime::RuntimeTaskRequest&, std::span<const std::string>,
    const runtime::RuntimeScriptTaskExecutionControl&)>;

class TestFactory final : public runtime::RuntimeScriptTaskRuntimeFactory {
public:
    explicit TestFactory(Create create) : create_(std::move(create)) {}

    std::unique_ptr<runtime::RuntimeScriptTaskRuntime> create(
        const runtime::RuntimeTaskRequest& value,
        const std::span<const std::string> requested_plan,
        const runtime::RuntimeScriptTaskExecutionControl& control)
        const override
    {
        return create_(value, requested_plan, control);
    }

private:
    Create create_;
};

std::shared_ptr<TestFactory> successful_factory(
    Execute execute = [](const auto&, const auto&) {
        return runtime::RuntimeTaskTerminal{false, 0};
    })
{
    return std::make_shared<TestFactory>(
        [execute = std::move(execute)](
            const runtime::RuntimeTaskRequest& value,
            const std::span<const std::string> requested_plan, const auto&) {
            return std::make_unique<TestRuntime>(
                identity_for(value, requested_plan), execute);
        });
}

void test_success_and_stable_progress()
{
    const auto backend = runtime::make_runtime_script_task_backend(
        successful_factory());
    auto value = request();
    int reports{};
    runtime::RuntimeTaskProgress progress;
    const auto terminal = backend(
        value, {}, [&](runtime::RuntimeTaskProgress update) {
            ++reports;
            progress = std::move(update);
            return true;
        });

    check(!terminal.is_flag_run && terminal.exit_code == 0,
          "a successful runtime must preserve its explicit terminal zero");
    check(reports == 1 && progress.is_flag_run
              && progress.current_task == "group"
              && progress.waiting_tasks == value.waiting_tasks,
          "the backend must publish one stable initial task projection");
}

void test_waiting_task_selection_and_stale_reporter()
{
    const auto backend = runtime::make_runtime_script_task_backend(
        successful_factory());
    auto value = request();
    value.current_task.reset();
    value.waiting_tasks = {"mail", "daily-task-power"};
    runtime::RuntimeTaskProgress progress;
    const auto terminal = backend(
        value, {}, [&](runtime::RuntimeTaskProgress update) {
            progress = std::move(update);
            return false;
        });

    check(terminal.exit_code == 0,
          "an expired progress lease must not fail an otherwise valid task");
    check(progress.current_task == "mail"
              && progress.waiting_tasks
                  == std::vector<std::string>{"daily-task-power"},
          "an absent current task must select and remove the first waiting task");
}

void test_identity_mismatch_fails_closed()
{
    using Mutate = std::function<void(runtime::RuntimeScriptTaskIdentity&)>;
    const auto rejected = [](Mutate mutate) {
        std::atomic<int> executions{};
        const auto factory = std::make_shared<TestFactory>(
            [mutate = std::move(mutate), &executions](
                const runtime::RuntimeTaskRequest& value,
                const std::span<const std::string> requested_plan,
                const auto&) {
                auto identity = identity_for(value, requested_plan);
                mutate(identity);
                return std::make_unique<TestRuntime>(
                    std::move(identity), [&executions](const auto&, const auto&) {
                        ++executions;
                        return runtime::RuntimeTaskTerminal{false, 0};
                    });
            });
        const auto terminal = runtime::make_runtime_script_task_backend(factory)(
            request(), {}, [](auto) { return true; });
        return terminal.exit_code
                == runtime::runtime_script_task_failure_exit_code
            && executions.load() == 0;
    };

    check(rejected([](auto& value) { value.config_snapshot_id.clear(); }),
          "missing exact config identity must fail before execution");
    check(rejected([](auto& value) { value.runtime_generation = "19"; }),
          "runtime generation must be exact lowercase SHA-256");
    check(rejected([](auto& value) { value.runtime_generation.assign(64, 'A'); }),
          "uppercase runtime generation must fail closed");
    check(rejected([](auto& value) { value.scripts_commit = "scripts-commit"; }),
          "scripts commit must be exact lowercase Git identity");
    check(rejected([](auto& value) { value.resources_commit.assign(39, 'c'); }),
          "resources commit width must fail closed");
    check(rejected([](auto& value) {
              std::swap(
                  value.requested_task_plan[0],
                  value.requested_task_plan[1]);
          }),
          "requested task plan ordering must match the request exactly");
    check(rejected([](auto& value) { value.canonical_task_plan.pop_back(); }),
          "canonical task plan cardinality must match the requested plan");
    check(rejected([](auto& value) {
              value.canonical_task_plan[0].push_back('\0');
          }),
          "identity task names must reject embedded NUL");
    check(rejected([](auto& value) { value.profile.push_back('\0'); }),
          "identity text must reject embedded NUL");

    std::atomic<int> creates{};
    const auto factory = std::make_shared<TestFactory>(
        [&](const runtime::RuntimeTaskRequest& value,
            const std::span<const std::string> requested_plan, const auto&) {
            ++creates;
            return std::make_unique<TestRuntime>(
                identity_for(value, requested_plan),
                [](const auto&, const auto&) {
                    return runtime::RuntimeTaskTerminal{false, 0};
                });
        });
    auto invalid_request = request();
    invalid_request.current_task->push_back('\0');
    const auto terminal = runtime::make_runtime_script_task_backend(factory)(
        invalid_request, {}, [](auto) { return true; });
    check(terminal.exit_code == runtime::runtime_script_task_failure_exit_code
              && creates.load() == 0,
          "requested task plan must reject embedded NUL before factory entry");
}

void test_complete_task_plan_and_intermediate_progress()
{
    std::vector<std::string> factory_plan;
    std::vector<std::string> executed;
    std::vector<runtime::RuntimeTaskProgress> progress;
    const auto factory = std::make_shared<TestFactory>(
        [&](const runtime::RuntimeTaskRequest& value,
            const std::span<const std::string> requested_plan, const auto&) {
            factory_plan.assign(requested_plan.begin(), requested_plan.end());
            auto identity = identity_for(value, requested_plan);
            identity.canonical_task_plan = {
                "group", "mail", "collect_reward"};
            const auto owned_plan = identity.requested_task_plan;
            return std::make_unique<TestRuntime>(
                std::move(identity),
                [owned_plan, &executed](const auto&, const auto& report) {
                    for (std::size_t index{}; index < owned_plan.size(); ++index) {
                        executed.push_back(owned_plan[index]);
                        if (index + 1 < owned_plan.size()) {
                            runtime::RuntimeTaskProgress update;
                            update.is_flag_run = true;
                            update.current_task = owned_plan[index + 1];
                            update.waiting_tasks.assign(
                                owned_plan.begin() + index + 2,
                                owned_plan.end());
                            (void)report(std::move(update));
                        }
                    }
                    return runtime::RuntimeTaskTerminal{true, 0};
                });
        });
    const auto terminal = runtime::make_runtime_script_task_backend(factory)(
        request(), {}, [&](runtime::RuntimeTaskProgress update) {
            progress.push_back(std::move(update));
            return true;
        });
    const std::vector<std::string> expected{"group", "mail", "daily-task-power"};
    check(factory_plan == expected && executed == expected,
          "factory/runtime must receive and execute the complete ordered task plan");
    check(progress.size() == 3
              && progress[0].current_task == "group"
              && progress[0].waiting_tasks
                  == std::vector<std::string>{"mail", "daily-task-power"}
              && progress[1].current_task == "mail"
              && progress[1].waiting_tasks
                  == std::vector<std::string>{"daily-task-power"}
              && progress[2].current_task == "daily-task-power"
              && progress[2].waiting_tasks.empty(),
          "runtime must be able to publish ordered intermediate progress");
    check(terminal.is_flag_run && terminal.exit_code == 0,
          "complete-plan execution must preserve the exact terminal state");
}

void test_pre_cancel_and_deadline()
{
    std::atomic<int> creates{};
    const auto factory = std::make_shared<TestFactory>(
        [&](const runtime::RuntimeTaskRequest& value,
            const std::span<const std::string> requested_plan, const auto&) {
            ++creates;
            return std::make_unique<TestRuntime>(
                identity_for(value, requested_plan), [](const auto&, const auto&) {
                    std::this_thread::sleep_for(20ms);
                    return runtime::RuntimeTaskTerminal{false, 0};
                });
        });

    std::stop_source stopped;
    stopped.request_stop();
    auto backend = runtime::make_runtime_script_task_backend(factory);
    auto terminal = backend(
        request(), stopped.get_token(), [](auto) { return true; });
    check(terminal.exit_code
                  == runtime::runtime_script_task_cancelled_exit_code
              && creates.load() == 0,
          "a pre-cancelled task must not enter the runtime factory");

    runtime::RuntimeScriptTaskBackendOptions options;
    options.task_deadline = 5ms;
    backend = runtime::make_runtime_script_task_backend(factory, options);
    terminal = backend(request(), {}, [](auto) { return true; });
    check(terminal.exit_code
              == runtime::runtime_script_task_deadline_exit_code,
          "a runtime returning after its deadline must fail as deadline exceeded");
}

void test_mid_execution_cancel()
{
    const auto factory = successful_factory([](const auto& control, const auto&) {
        while (!control.stop_requested()) {
            std::this_thread::yield();
        }
        return runtime::RuntimeTaskTerminal{false, std::nullopt};
    });
    const auto backend = runtime::make_runtime_script_task_backend(factory);
    std::atomic<int> exit_code{};
    std::jthread worker([&](const std::stop_token token) {
        const auto terminal = backend(
            request(), token, [](auto) { return true; });
        exit_code.store(terminal.exit_code.value_or(-1));
    });
    std::this_thread::sleep_for(2ms);
    worker.request_stop();
    worker.join();
    check(exit_code.load() == runtime::runtime_script_task_cancelled_exit_code,
          "cooperative mid-execution stop must publish the stable cancel code");
}

void test_deadline_precedes_simultaneous_stop_and_throw_boundaries()
{
    runtime::RuntimeScriptTaskBackendOptions options;
    options.task_deadline = 5ms;
    std::atomic<bool> entered{};
    const auto simultaneous = runtime::make_runtime_script_task_backend(
        successful_factory([&](const auto& control, const auto&) {
            entered = true;
            while (!control.stop_requested()) std::this_thread::yield();
            while (!control.deadline_exceeded()) std::this_thread::yield();
            return runtime::RuntimeTaskTerminal{false, 0};
        }),
        options);
    std::atomic<int> simultaneous_exit{};
    std::jthread worker([&](const std::stop_token stop) {
        simultaneous_exit = simultaneous(
            request(), stop, [](auto) { return true; })
                                .exit_code.value_or(-1);
    });
    while (!entered.load()) std::this_thread::yield();
    worker.request_stop();
    worker.join();
    check(simultaneous_exit.load()
              == runtime::runtime_script_task_deadline_exit_code,
          "deadline must precede a simultaneous stop at the terminal boundary");

    options.task_deadline = 1ms;
    auto terminal = runtime::make_runtime_script_task_backend(
        successful_factory([](const auto&, const auto&)
            -> runtime::RuntimeTaskTerminal {
            std::this_thread::sleep_for(5ms);
            throw std::runtime_error{"deadline boundary"};
        }),
        options)(request(), {}, [](auto) { return true; });
    check(terminal.exit_code
              == runtime::runtime_script_task_deadline_exit_code,
          "an exception after deadline must preserve deadline priority");

    entered = false;
    const auto throwing_on_stop = runtime::make_runtime_script_task_backend(
        successful_factory([&](const auto& control, const auto&)
            -> runtime::RuntimeTaskTerminal {
            entered = true;
            while (!control.stop_requested()) std::this_thread::yield();
            throw std::runtime_error{"cancel boundary"};
        }));
    std::atomic<int> cancel_exit{};
    std::jthread cancel_worker([&](const std::stop_token stop) {
        cancel_exit = throwing_on_stop(
            request(), stop, [](auto) { return true; })
                          .exit_code.value_or(-1);
    });
    while (!entered.load()) std::this_thread::yield();
    cancel_worker.request_stop();
    cancel_worker.join();
    check(cancel_exit.load()
              == runtime::runtime_script_task_cancelled_exit_code,
          "an exception after stop must preserve cancellation priority");
}

void test_reporter_boundary_rechecks_control()
{
    std::atomic<int> executions{};
    const auto factory = successful_factory(
        [&](const auto&, const auto&) {
            ++executions;
            return runtime::RuntimeTaskTerminal{false, 0};
        });

    std::stop_source stopped_by_reporter;
    auto terminal = runtime::make_runtime_script_task_backend(factory)(
        request(), stopped_by_reporter.get_token(), [&](auto) {
            stopped_by_reporter.request_stop();
            return true;
        });
    check(terminal.exit_code
                  == runtime::runtime_script_task_cancelled_exit_code
              && executions.load() == 0,
          "stop observed after initial progress must prevent runtime execution");

    runtime::RuntimeScriptTaskBackendOptions options;
    options.task_deadline = 1ms;
    terminal = runtime::make_runtime_script_task_backend(factory, options)(
        request(), {}, [](auto) {
            std::this_thread::sleep_for(5ms);
            return true;
        });
    check(terminal.exit_code
                  == runtime::runtime_script_task_deadline_exit_code
              && executions.load() == 0,
          "deadline observed after initial progress must prevent execution");

    std::stop_source throwing_reporter_stop;
    terminal = runtime::make_runtime_script_task_backend(factory)(
        request(), throwing_reporter_stop.get_token(), [&](auto)
            -> bool {
            throwing_reporter_stop.request_stop();
            throw std::bad_alloc{};
        });
    check(terminal.exit_code
                  == runtime::runtime_script_task_cancelled_exit_code
              && executions.load() == 0,
          "reporter exceptions must still honor the active control boundary");

    std::stop_source stopped_by_intermediate_report;
    bool intermediate_returned_true{true};
    const auto intermediate_factory = successful_factory(
        [&](const auto& control, const auto& report) {
            runtime::RuntimeTaskProgress update;
            update.is_flag_run = true;
            update.current_task = "mail";
            intermediate_returned_true = report(std::move(update));
            check(control.stop_requested(),
                  "runtime must observe stop after intermediate progress");
            return runtime::RuntimeTaskTerminal{false, 0};
        });
    int reports{};
    terminal = runtime::make_runtime_script_task_backend(intermediate_factory)(
        request(), stopped_by_intermediate_report.get_token(), [&](auto) {
            if (++reports == 2) stopped_by_intermediate_report.request_stop();
            return true;
        });
    check(terminal.exit_code
                  == runtime::runtime_script_task_cancelled_exit_code
              && reports == 2 && !intermediate_returned_true,
          "controlled intermediate reports must expose and preserve stop");
}

void test_factory_and_runtime_exceptions_fail_closed()
{
    const auto throwing_factory = std::make_shared<TestFactory>(
        [](const runtime::RuntimeTaskRequest&,
           std::span<const std::string>, const auto&)
            -> std::unique_ptr<runtime::RuntimeScriptTaskRuntime> {
            throw std::bad_alloc{};
        });
    auto terminal = runtime::make_runtime_script_task_backend(throwing_factory)(
        request(), {}, [](auto) { return true; });
    check(terminal.exit_code == runtime::runtime_script_task_failure_exit_code,
          "factory allocation failure must be translated to terminal failure");

    terminal = runtime::make_runtime_script_task_backend(successful_factory(
        [](const auto&, const auto&) -> runtime::RuntimeTaskTerminal {
            throw std::runtime_error{"execution failed"};
        }))(request(), {}, [](auto) { return true; });
    check(terminal.exit_code == runtime::runtime_script_task_failure_exit_code,
          "runtime exceptions must not escape the backend boundary");

    terminal = runtime::make_runtime_script_task_backend(successful_factory(
        [](const auto&, const auto&) {
            return runtime::RuntimeTaskTerminal{true, 0};
        }))(request(), {}, [](auto) { return true; });
    check(terminal.is_flag_run && terminal.exit_code == 0,
          "the backend must preserve the runtime's exact terminal state");
}

void test_different_configs_receive_isolated_concurrent_runtimes()
{
    std::mutex mutex;
    std::barrier rendezvous{2};
    int active{};
    int peak{};
    const auto factory = std::make_shared<TestFactory>(
        [&](const runtime::RuntimeTaskRequest& value,
            const std::span<const std::string> requested_plan, const auto&) {
            const auto identity = identity_for(value, requested_plan);
            return std::make_unique<TestRuntime>(identity, [&](const auto&, const auto&) {
                {
                    std::lock_guard lock{mutex};
                    ++active;
                    peak = active > peak ? active : peak;
                }
                rendezvous.arrive_and_wait();
                {
                    std::lock_guard lock{mutex};
                    --active;
                }
                return runtime::RuntimeTaskTerminal{false, 0};
            });
        });
    const auto backend = runtime::make_runtime_script_task_backend(factory);
    std::atomic<int> first{-1};
    std::atomic<int> second{-1};
    std::jthread a([&] {
        first.store(backend(request("config-a"), {}, [](auto) { return true; })
                        .exit_code.value_or(-1));
    });
    std::jthread b([&] {
        second.store(backend(request("config-b"), {}, [](auto) { return true; })
                         .exit_code.value_or(-1));
    });
    a.join();
    b.join();
    check(first.load() == 0 && second.load() == 0 && peak == 2,
          "different configs must execute concurrently in task-local runtimes");
}

void test_composition_options_are_fail_fast()
{
    bool rejected_null{};
    try {
        (void)runtime::make_runtime_script_task_backend(nullptr);
    }
    catch (const std::invalid_argument&) {
        rejected_null = true;
    }
    check(rejected_null, "a null production factory must be rejected");

    bool rejected_deadline{};
    try {
        runtime::RuntimeScriptTaskBackendOptions options;
        options.task_deadline = 0ms;
        (void)runtime::make_runtime_script_task_backend(
            successful_factory(), options);
    }
    catch (const std::invalid_argument&) {
        rejected_deadline = true;
    }
    check(rejected_deadline, "a nonpositive deadline must be rejected");
}

} // namespace

int main()
{
    test_success_and_stable_progress();
    test_waiting_task_selection_and_stale_reporter();
    test_identity_mismatch_fails_closed();
    test_complete_task_plan_and_intermediate_progress();
    test_pre_cancel_and_deadline();
    test_mid_execution_cancel();
    test_deadline_precedes_simultaneous_stop_and_throw_boundaries();
    test_reporter_boundary_rechecks_control();
    test_factory_and_runtime_exceptions_fail_closed();
    test_different_configs_receive_isolated_concurrent_runtimes();
    test_composition_options_are_fail_fast();
    if (failures != 0) {
        std::cerr << failures << " runtime script task backend test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "runtime script task backend tests passed\n";
    return EXIT_SUCCESS;
}
