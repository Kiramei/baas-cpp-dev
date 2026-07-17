#include "service/runtime/RuntimeTaskOwner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace runtime = baas::service::runtime;
using namespace std::chrono_literals;

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Predicate>
[[nodiscard]] bool wait_until(Predicate&& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

[[nodiscard]] runtime::RuntimeTaskRequest request(
    std::string config_id, std::string run_mode = "scheduler")
{
    return {
        std::move(config_id), std::move(run_mode), std::string{"first"},
        {"second", "third"}};
}

void test_keyed_concurrency_and_stop_start_linearization()
{
    struct Gate {
        std::atomic<int> active{};
        std::atomic<int> maximum{};
        std::atomic<int> stop_callbacks{};
        std::mutex mutex;
        std::condition_variable condition;
        bool allow_stopped_workers_to_exit{false};
    } gate;

    runtime::RuntimeTaskOwner owner{
        [&gate](const runtime::RuntimeTaskRequest&, const std::stop_token stop,
                const runtime::RuntimeTaskProgressReporter& report) {
            const int active = gate.active.fetch_add(1) + 1;
            int maximum = gate.maximum.load();
            while (active > maximum
                   && !gate.maximum.compare_exchange_weak(maximum, active)) {
            }
            check(
                report({
                    true, "{\"action\":\"live\"}", std::string{"live"},
                    {"queued"}}),
                "live worker progress is accepted");
            std::stop_callback on_stop{stop, [&gate, &report] {
                if (report({
                        false, "{\"action\":\"stopping\"}",
                        std::string{"live"}, {}})) {
                    gate.stop_callbacks.fetch_add(1);
                }
            }};
            while (!stop.stop_requested()) std::this_thread::sleep_for(1ms);
            std::unique_lock lock{gate.mutex};
            gate.condition.wait(lock, [&gate] {
                return gate.allow_stopped_workers_to_exit;
            });
            gate.active.fetch_sub(1);
            return true;
        }};

    const auto alpha = owner.start(request("alpha"));
    const auto beta = owner.start(request("beta"));
    check(alpha.decision == runtime::RuntimeTaskStartDecision::started,
          "first config starts");
    check(beta.decision == runtime::RuntimeTaskStartDecision::started,
          "second config starts independently");
    check(wait_until([&gate] { return gate.active.load() == 2; }),
          "different configs execute concurrently");
    check(gate.maximum.load() >= 2, "backend observed concurrent execution");

    const auto duplicate = owner.start(request("alpha"));
    check(
        duplicate.decision
            == runtime::RuntimeTaskStartDecision::already_running,
        "same config has at most one running worker");
    check(
        runtime::runtime_task_start_decision_name(duplicate.decision)
            == "already-running",
        "legacy-facing already-running spelling is stable");

    const auto stop = owner.request_stop("alpha");
    check(stop.decision == runtime::RuntimeTaskStopDecision::stop_requested,
          "stop requests cooperative cancellation");
    check(gate.stop_callbacks.load() == 1,
          "synchronous stop callback can reenter progress without deadlock");
    const auto while_stopping = owner.start(request("alpha"));
    check(
        while_stopping.decision == runtime::RuntimeTaskStartDecision::stopping,
        "restart is refused until old worker exits");
    check(while_stopping.snapshot && while_stopping.snapshot->running
              && while_stopping.snapshot->stopping
              && !while_stopping.snapshot->is_flag_run,
          "stopping snapshot distinguishes owned live thread");

    static_cast<void>(owner.request_stop("beta"));
    {
        std::lock_guard lock{gate.mutex};
        gate.allow_stopped_workers_to_exit = true;
    }
    gate.condition.notify_all();
    check(owner.wait_for_idle("alpha", 3s), "first stopped worker drains");
    check(owner.wait_for_idle("beta", 3s), "second stopped worker drains");
    const auto stopped = owner.snapshot("alpha");
    check(stopped && !stopped->running && !stopped->stopping
              && !stopped->exit_code,
          "manual stop preserves the Python null exit code");

    const auto restarted = owner.start(request("alpha", "single"));
    check(restarted.decision == runtime::RuntimeTaskStartDecision::started,
          "same config can restart after prior worker is joined");
    static_cast<void>(owner.request_stop("alpha"));
    check(owner.wait_for_idle("alpha", 3s), "restarted worker drains");
}

void test_natural_outcomes_and_stable_snapshots()
{
    std::atomic<bool> valid_progress{};
    std::atomic<bool> invalid_progress_rejected{};
    std::atomic<bool> oversized_button_rejected{};
    runtime::RuntimeTaskLimits limits;
    limits.max_waiting_tasks = 2;
    limits.max_waiting_task_bytes = 8;
    limits.max_button_bytes = 8;
    runtime::RuntimeTaskOwner owner{
        [&valid_progress, &invalid_progress_rejected,
         &oversized_button_rejected](
            const runtime::RuntimeTaskRequest& value, std::stop_token,
            const runtime::RuntimeTaskProgressReporter& report) {
            if (value.run_mode == "progress") {
                valid_progress = report(
                    {true, "button", std::string{"task"}, {"a", "b"}});
                invalid_progress_rejected = !report(
                    {true, "button", std::string{"task"}, {"a", "b", "c"}});
                oversized_button_rejected = !report(
                    {true, "123456789", std::string{"task"}, {"a"}});
                return true;
            }
            if (value.run_mode == "false") return false;
            if (value.run_mode == "throw") {
                throw std::runtime_error{"backend failure"};
            }
            return true;
        },
        limits};

    auto plain = [](std::string config, std::string mode) {
        return runtime::RuntimeTaskRequest{
            std::move(config), std::move(mode), std::nullopt, {}};
    };
    check(static_cast<bool>(owner.start(plain("success", "success"))),
          "success task starts");
    check(static_cast<bool>(owner.start(plain("false", "false"))),
          "false task starts");
    check(static_cast<bool>(owner.start(plain("throw", "throw"))),
          "throwing task starts");
    check(static_cast<bool>(owner.start(plain("progress", "progress"))),
          "progress task starts");
    for (const auto name : {"success", "false", "throw", "progress"}) {
        check(owner.wait_for_idle(name, 3s), "natural task reaches idle");
    }

    check(!owner.snapshot("success")->exit_code,
          "true backend result preserves the Python null exit code");
    check(owner.snapshot("false")->exit_code == 1,
          "false backend result maps to exit code one");
    check(owner.snapshot("throw")->exit_code == 1,
          "backend exception maps to exit code one");
    check(valid_progress.load(), "bounded progress update is accepted");
    check(invalid_progress_rejected.load(),
          "oversized progress update is rejected");
    check(oversized_button_rejected.load(),
          "oversized raw button payload is rejected");
    check(owner.snapshot("progress")->button == "button",
          "bounded raw button payload remains in the stable snapshot");

    const auto all = owner.snapshots();
    check(all.size() == 4, "snapshot retains one bounded entry per config");
    check(std::is_sorted(all.begin(), all.end(), [](const auto& left, const auto& right) {
              return left.config_id < right.config_id;
          }),
          "multi-config snapshot ordering is deterministic");
    check(std::all_of(all.begin(), all.end(), [](const auto& value) {
              return !value.running && !value.is_flag_run
                  && !value.current_task && value.waiting_tasks.empty()
                  && value.run_mode && value.timestamp != 0
                  && value.timestamp <= 9'007'199'254'740'991ULL;
          }),
          "terminal snapshots are complete and stable");
}

void test_limits_and_shutdown_drain()
{
    runtime::RuntimeTaskLimits limits;
    limits.max_configs = 3;
    limits.max_config_id_bytes = 8;
    limits.max_run_mode_bytes = 8;
    limits.max_task_name_bytes = 8;
    limits.max_button_bytes = 8;
    limits.max_waiting_tasks = 2;
    limits.max_waiting_task_bytes = 8;

    std::atomic<int> entered{};
    std::atomic<int> exited{};
    runtime::RuntimeTaskOwner owner{
        [&entered, &exited](
            const runtime::RuntimeTaskRequest&, const std::stop_token stop,
            const runtime::RuntimeTaskProgressReporter&) {
            entered.fetch_add(1);
            while (!stop.stop_requested()) std::this_thread::sleep_for(1ms);
            exited.fetch_add(1);
            return false;
        },
        limits};

    check(
        owner.start({"", "mode", std::nullopt, {}}).decision
            == runtime::RuntimeTaskStartDecision::invalid_request,
        "empty config id is rejected");
    check(
        owner.start({"123456789", "mode", std::nullopt, {}}).decision
            == runtime::RuntimeTaskStartDecision::invalid_request,
        "oversized config id is rejected");
    check(!owner.snapshot(std::string(1'024, 'x')),
          "oversized snapshot lookup is rejected before key allocation");
    check(
        owner.request_stop(std::string(1'024, 'x')).decision
            == runtime::RuntimeTaskStopDecision::unknown_config,
        "oversized stop lookup is rejected before key allocation");
    for (const auto config : {"one", "two", "three"}) {
        check(static_cast<bool>(
                  owner.start({config, "mode", std::nullopt, {}})),
              "bounded shutdown worker starts");
    }
    check(wait_until([&entered] { return entered.load() == 3; }),
          "all shutdown workers enter backend");
    check(
        owner.start({"four", "mode", std::nullopt, {}}).decision
            == runtime::RuntimeTaskStartDecision::capacity_exceeded,
        "config status registry has a fixed capacity");

    owner.shutdown();
    check(exited.load() == 3, "shutdown reliably joins every backend worker");
    const auto stopped = owner.snapshots();
    check(std::all_of(stopped.begin(), stopped.end(), [](const auto& value) {
              return !value.running && !value.exit_code;
          }),
          "shutdown cancellation remains a normal completion");
    check(
        owner.start({"one", "mode", std::nullopt, {}}).decision
            == runtime::RuntimeTaskStartDecision::owner_stopped,
        "shutdown permanently closes admission");
    owner.shutdown();
    check(owner.wait_for_idle("unknown", 0ms),
          "unknown config is already idle");
}

}  // namespace

int main()
{
    test_keyed_concurrency_and_stop_start_linearization();
    test_natural_outcomes_and_stable_snapshots();
    test_limits_and_shutdown_drain();
    if (failures != 0) {
        std::cerr << failures << " runtime task owner test(s) failed\n";
        return 1;
    }
    std::cout << "Runtime task owner tests passed\n";
    return 0;
}
