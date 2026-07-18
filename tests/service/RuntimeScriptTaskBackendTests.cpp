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
    const runtime::RuntimeTaskRequest& value, const std::string_view selected)
{
    runtime::RuntimeScriptTaskIdentity identity;
    identity.config_id = value.config_id;
    identity.config_snapshot_id = value.config_id + "@config-generation-7";
    identity.profile = "zh-cn";
    identity.device_id = "emulator-5554";
    identity.runtime_generation = 19;
    identity.scripts_commit = "scripts-commit";
    identity.resources_commit = "resources-commit";
    identity.run_mode = value.run_mode;
    identity.requested_task = selected;
    identity.canonical_task = selected;
    return identity;
}

using Execute = std::function<runtime::RuntimeTaskTerminal(
    const runtime::RuntimeScriptTaskExecutionControl&)>;

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
        const runtime::RuntimeScriptTaskExecutionControl& control) override
    {
        return execute_(control);
    }

private:
    runtime::RuntimeScriptTaskIdentity identity_;
    Execute execute_;
};

using Create = std::function<std::unique_ptr<runtime::RuntimeScriptTaskRuntime>(
    const runtime::RuntimeTaskRequest&, std::string_view,
    const runtime::RuntimeScriptTaskExecutionControl&)>;

class TestFactory final : public runtime::RuntimeScriptTaskRuntimeFactory {
public:
    explicit TestFactory(Create create) : create_(std::move(create)) {}

    std::unique_ptr<runtime::RuntimeScriptTaskRuntime> create(
        const runtime::RuntimeTaskRequest& value,
        const std::string_view selected,
        const runtime::RuntimeScriptTaskExecutionControl& control)
        const override
    {
        return create_(value, selected, control);
    }

private:
    Create create_;
};

std::shared_ptr<TestFactory> successful_factory(
    Execute execute = [](const auto&) {
        return runtime::RuntimeTaskTerminal{false, 0};
    })
{
    return std::make_shared<TestFactory>(
        [execute = std::move(execute)](
            const runtime::RuntimeTaskRequest& value,
            const std::string_view selected, const auto&) {
            return std::make_unique<TestRuntime>(
                identity_for(value, selected), execute);
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
    std::atomic<int> executions{};
    const auto factory = std::make_shared<TestFactory>(
        [&](const runtime::RuntimeTaskRequest& value,
            const std::string_view selected, const auto&) {
            auto identity = identity_for(value, selected);
            identity.config_snapshot_id.clear();
            return std::make_unique<TestRuntime>(
                std::move(identity), [&](const auto&) {
                    ++executions;
                    return runtime::RuntimeTaskTerminal{false, 0};
                });
        });
    const auto terminal = runtime::make_runtime_script_task_backend(factory)(
        request(), {}, [](auto) { return true; });
    check(terminal.exit_code == runtime::runtime_script_task_failure_exit_code
              && executions.load() == 0,
          "missing exact config identity must fail before execution");
}

void test_pre_cancel_and_deadline()
{
    std::atomic<int> creates{};
    const auto factory = std::make_shared<TestFactory>(
        [&](const runtime::RuntimeTaskRequest& value,
            const std::string_view selected, const auto&) {
            ++creates;
            return std::make_unique<TestRuntime>(
                identity_for(value, selected), [](const auto&) {
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
    const auto factory = successful_factory([](const auto& control) {
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

void test_factory_and_runtime_exceptions_fail_closed()
{
    const auto throwing_factory = std::make_shared<TestFactory>(
        [](const runtime::RuntimeTaskRequest&, std::string_view, const auto&)
            -> std::unique_ptr<runtime::RuntimeScriptTaskRuntime> {
            throw std::bad_alloc{};
        });
    auto terminal = runtime::make_runtime_script_task_backend(throwing_factory)(
        request(), {}, [](auto) { return true; });
    check(terminal.exit_code == runtime::runtime_script_task_failure_exit_code,
          "factory allocation failure must be translated to terminal failure");

    terminal = runtime::make_runtime_script_task_backend(successful_factory(
        [](const auto&) -> runtime::RuntimeTaskTerminal {
            throw std::runtime_error{"execution failed"};
        }))(request(), {}, [](auto) { return true; });
    check(terminal.exit_code == runtime::runtime_script_task_failure_exit_code,
          "runtime exceptions must not escape the backend boundary");

    terminal = runtime::make_runtime_script_task_backend(successful_factory(
        [](const auto&) {
            return runtime::RuntimeTaskTerminal{true, std::nullopt};
        }))(request(), {}, [](auto) { return true; });
    check(terminal.exit_code == runtime::runtime_script_task_failure_exit_code,
          "a nonterminal runtime result must fail closed");
}

void test_different_configs_receive_isolated_concurrent_runtimes()
{
    std::mutex mutex;
    std::barrier rendezvous{2};
    int active{};
    int peak{};
    const auto factory = std::make_shared<TestFactory>(
        [&](const runtime::RuntimeTaskRequest& value,
            const std::string_view selected, const auto&) {
            const auto identity = identity_for(value, selected);
            return std::make_unique<TestRuntime>(identity, [&](const auto&) {
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
    test_pre_cancel_and_deadline();
    test_mid_execution_cancel();
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
