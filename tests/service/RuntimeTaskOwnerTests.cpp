#include "service/runtime/RuntimeTaskOwner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

std::size_t runtime_task_owner_size_from_non_hook_tu() noexcept;

namespace {

namespace runtime = baas::service::runtime;
using namespace std::chrono_literals;

std::atomic<int> failures{};

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        failures.fetch_add(1);
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
        bool allow_exit{false};
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
            auto stop_action = [&gate, &report]() noexcept {
                try {
                    if (report({
                            false, "{\"action\":\"stopping\"}",
                            std::string{"live"}, {}})) {
                        gate.stop_callbacks.fetch_add(1);
                    }
                } catch (...) {
                    failures.fetch_add(1);
                }
            };
            static_assert(std::is_nothrow_invocable_v<decltype(stop_action)>);
            std::stop_callback on_stop{stop, stop_action};
            while (!stop.stop_requested()) std::this_thread::sleep_for(1ms);
            std::unique_lock lock{gate.mutex};
            gate.condition.wait(lock, [&gate] { return gate.allow_exit; });
            gate.active.fetch_sub(1);
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
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
    check(!owner.wait_for_idle("alpha", 0ms),
          "wait_for_idle is false before terminal publication");

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
          "synchronous stop callback can report without owner-lock deadlock");
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
        gate.allow_exit = true;
    }
    gate.condition.notify_all();
    check(owner.wait_for_idle("alpha", 3s),
          "first backend publishes terminal state");
    check(owner.wait_for_idle("beta", 3s),
          "second backend publishes terminal state");
    const auto stopped = owner.snapshot("alpha");
    check(stopped && !stopped->running && !stopped->stopping
              && !stopped->exit_code,
          "backend explicitly publishes normal manual-stop terminal state");

    const auto restarted = owner.start(request("alpha", "single"));
    check(restarted.decision == runtime::RuntimeTaskStartDecision::started,
          "restart reaps the completed old thread before admission");
    static_cast<void>(owner.request_stop("alpha"));
    check(owner.wait_for_idle("alpha", 3s), "restarted backend completes");
}

void test_explicit_terminal_outcomes_and_bounded_snapshots()
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
                return runtime::RuntimeTaskTerminal{false, std::nullopt};
            }
            if (value.run_mode == "failure") {
                return runtime::runtime_task_terminal_from_result(false);
            }
            if (value.run_mode == "zero") {
                return runtime::RuntimeTaskTerminal{true, 0};
            }
            if (value.run_mode == "throw") {
                throw std::runtime_error{"backend failure"};
            }
            return runtime::runtime_task_terminal_from_result(true);
        },
        limits};

    auto plain = [](std::string config, std::string mode) {
        return runtime::RuntimeTaskRequest{
            std::move(config), std::move(mode), std::nullopt, {}};
    };
    for (const auto& [config, mode] : {
             std::pair{"success", "success"},
             std::pair{"failure", "failure"},
             std::pair{"zero", "zero"},
             std::pair{"throw", "throw"},
             std::pair{"progress", "progress"}}) {
        check(static_cast<bool>(owner.start(plain(config, mode))),
              "terminal-outcome task starts");
    }
    for (const auto name : {
             "success", "failure", "zero", "throw", "progress"}) {
        check(owner.wait_for_idle(name, 3s), "natural task reaches terminal");
    }

    check(!owner.snapshot("success")->exit_code,
          "legacy true maps to null exit code");
    check(owner.snapshot("failure")->exit_code == 1,
          "legacy false maps to exit code one");
    check(owner.snapshot("throw")->exit_code == 1,
          "backend exception maps to exit code one");
    const auto explicit_zero = owner.snapshot("zero");
    check(explicit_zero && explicit_zero->is_flag_run
              && explicit_zero->exit_code == 0,
          "backend terminal preserves final flag and explicit exit code zero");
    check(valid_progress.load(), "bounded progress update is accepted");
    check(invalid_progress_rejected.load(),
          "oversized waiting list is rejected");
    check(oversized_button_rejected.load(),
          "oversized raw button payload is rejected");
    check(owner.snapshot("progress")->button == "button",
          "last bounded raw button payload remains in terminal snapshot");

    const auto all = owner.snapshots();
    check(all.size() == 5, "snapshot retains one bounded entry per config");
    check(std::is_sorted(all.begin(), all.end(), [](const auto& left, const auto& right) {
              return left.config_id < right.config_id;
          }),
          "multi-config snapshot ordering is deterministic");
    check(std::all_of(all.begin(), all.end(), [](const auto& value) {
              return !value.running && !value.current_task
                  && value.waiting_tasks.empty() && value.run_mode
                  && value.timestamp != 0
                  && value.timestamp <= 9'007'199'254'740'991ULL;
          }),
          "terminal snapshots are bounded and JSON-safe");
}

void test_self_shutdown_and_reentrant_stop_callback()
{
    runtime::RuntimeTaskOwner* self_owner = nullptr;
    std::atomic<int> ready{};
    std::atomic<bool> release{};
    std::atomic<int> self_shutdown_returned{};
    runtime::RuntimeTaskOwner owner{
        [&self_owner, &ready, &release, &self_shutdown_returned](
            const runtime::RuntimeTaskRequest&, std::stop_token,
            const runtime::RuntimeTaskProgressReporter&) {
            ready.fetch_add(1);
            while (!release.load()) std::this_thread::yield();
            self_owner->shutdown();
            self_shutdown_returned.fetch_add(1);
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    self_owner = &owner;
    check(static_cast<bool>(owner.start(request("self-a"))),
          "first self-shutdown worker starts");
    check(static_cast<bool>(owner.start(request("self-b"))),
          "second self-shutdown worker starts");
    check(wait_until([&ready] { return ready.load() == 2; }),
          "both self-shutdown workers are live");
    release = true;
    check(wait_until([&self_shutdown_returned] {
              return self_shutdown_returned.load() == 2;
          }),
          "concurrent worker shutdown calls return without self/mutual join");
    check(owner.wait_for_idle("self-a", 3s)
              && owner.wait_for_idle("self-b", 3s),
          "self-initiated shutdown publishes both terminals");
    owner.shutdown();

    runtime::RuntimeTaskOwner* callback_owner = nullptr;
    std::atomic<bool> callback_registered{};
    std::atomic<bool> callback_returned{};
    std::atomic<bool> allow_callback_worker_exit{};
    runtime::RuntimeTaskOwner reentrant_owner{
        [&callback_owner, &callback_registered, &callback_returned,
         &allow_callback_worker_exit](
            const runtime::RuntimeTaskRequest&, const std::stop_token stop,
            const runtime::RuntimeTaskProgressReporter&) {
            auto stop_action = [&]() noexcept {
                callback_owner->shutdown();
                callback_returned = true;
            };
            static_assert(std::is_nothrow_invocable_v<decltype(stop_action)>);
            std::stop_callback callback{stop, stop_action};
            callback_registered = true;
            while (!stop.stop_requested()) std::this_thread::yield();
            while (!allow_callback_worker_exit.load()) std::this_thread::yield();
            return runtime::RuntimeTaskTerminal{true, 0};
        }};
    callback_owner = &reentrant_owner;
    check(static_cast<bool>(reentrant_owner.start(request("callback"))),
          "reentrant-callback worker starts");
    check(wait_until([&callback_registered] { return callback_registered.load(); }),
          "stop callback is registered");
    const auto stopped = reentrant_owner.request_stop("callback");
    check(stopped.decision == runtime::RuntimeTaskStopDecision::stop_requested,
          "outer request_stop returns after reentrant shutdown");
    check(wait_until([&callback_returned] { return callback_returned.load(); }),
          "stop callback reentrant shutdown returns without deadlock");
    allow_callback_worker_exit = true;
    check(reentrant_owner.wait_for_idle("callback", 3s),
          "reentrant shutdown drains backend");
    const auto terminal = reentrant_owner.snapshot("callback");
    check(terminal && terminal->is_flag_run && terminal->exit_code == 0,
          "manual stop does not overwrite explicit backend terminal fields");
}

void test_nested_stop_delivery_and_join_lock_order()
{
    runtime::RuntimeTaskOwner* nested_owner = nullptr;
    std::atomic<int> registered{};
    std::atomic<bool> nested_returned{};
    std::atomic<bool> nested_failed{};
    std::atomic<bool> second_received_stop{};
    std::atomic<bool> allow_exit{};
    runtime::RuntimeTaskOwner owner{
        [&nested_owner, &registered, &nested_returned, &nested_failed,
         &second_received_stop, &allow_exit](
            const runtime::RuntimeTaskRequest& value,
            const std::stop_token stop,
            const runtime::RuntimeTaskProgressReporter&) {
            const bool first = value.config_id == "nested-a";
            auto stop_action = [&]() noexcept {
                if (first) {
                    try {
                        const auto result =
                            nested_owner->request_stop("nested-b");
                        nested_returned =
                            result.decision
                            == runtime::RuntimeTaskStopDecision::stop_requested;
                    } catch (...) {
                        nested_failed = true;
                    }
                } else {
                    second_received_stop = true;
                }
            };
            static_assert(std::is_nothrow_invocable_v<decltype(stop_action)>);
            std::stop_callback callback{stop, stop_action};
            registered.fetch_add(1);
            while (!stop.stop_requested()) std::this_thread::yield();
            while (!allow_exit.load()) std::this_thread::yield();
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    nested_owner = &owner;
    check(static_cast<bool>(owner.start(request("nested-a"))),
          "nested first worker starts");
    check(static_cast<bool>(owner.start(request("nested-b"))),
          "nested second worker starts");
    check(wait_until([&registered] { return registered.load() == 2; }),
          "both nested callbacks are registered");
    const auto first_stop = owner.request_stop("nested-a");
    check(first_stop.decision == runtime::RuntimeTaskStopDecision::stop_requested,
          "first nested stop is delivered");
    check(nested_returned.load() && !nested_failed.load(),
          "stop callback can synchronously request_stop another config");
    check(second_received_stop.load(), "second config receives nested stop");
    allow_exit = true;
    check(owner.wait_for_idle("nested-a", 3s)
              && owner.wait_for_idle("nested-b", 3s),
          "nested-stop workers publish terminal state");

    struct HookState {
        std::atomic<bool> stop_linearized{};
        std::atomic<bool> allow_stop_delivery{};
        std::atomic<bool> drain_locked{};
        std::atomic<bool> allow_drain{};
    } hooks;
    runtime::RuntimeTaskOwner* ordering_owner = nullptr;
    std::atomic<int> ordering_ready{};
    std::atomic<bool> call_second_stop{};
    std::atomic<bool> second_stop_returned{};
    std::atomic<bool> ordering_allow_exit{};
    runtime::RuntimeTaskOwner ordering{
        [&ordering_owner, &ordering_ready, &call_second_stop,
         &second_stop_returned, &ordering_allow_exit](
            const runtime::RuntimeTaskRequest& value,
            const std::stop_token stop,
            const runtime::RuntimeTaskProgressReporter&) {
            ordering_ready.fetch_add(1);
            if (value.config_id == "join-a") {
                while (!call_second_stop.load()) std::this_thread::yield();
                static_cast<void>(ordering_owner->request_stop("join-b"));
                second_stop_returned = true;
            } else {
                while (!stop.stop_requested()) std::this_thread::yield();
            }
            while (!ordering_allow_exit.load()) std::this_thread::yield();
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    ordering_owner = &ordering;
    runtime::RuntimeTaskOwnerTestAccess::set_after_stop_linearized_hook(
        ordering,
        +[](void* context) noexcept {
            auto& state = *static_cast<HookState*>(context);
            state.stop_linearized = true;
            while (!state.allow_stop_delivery.load()) std::this_thread::yield();
        },
        &hooks);
    runtime::RuntimeTaskOwnerTestAccess::set_before_drain_hook(
        ordering,
        +[](void* context) noexcept {
            auto& state = *static_cast<HookState*>(context);
            state.drain_locked = true;
            while (!state.allow_drain.load()) std::this_thread::yield();
        },
        &hooks);
    check(static_cast<bool>(ordering.start(request("join-a"))),
          "join-order first worker starts");
    check(static_cast<bool>(ordering.start(request("join-b"))),
          "join-order second worker starts");
    check(wait_until([&ordering_ready] { return ordering_ready.load() == 2; }),
          "join-order workers are running");
    call_second_stop = true;
    check(wait_until([&hooks] { return hooks.stop_linearized.load(); }),
          "worker linearizes second-config stop before delivery");
    std::thread external_shutdown{[&ordering] { ordering.shutdown(); }};
    check(wait_until([&hooks] { return hooks.drain_locked.load(); }),
          "external shutdown holds exclusive drain ownership");
    hooks.allow_stop_delivery = true;
    const bool returned_without_drain_mutex =
        wait_until([&second_stop_returned] { return second_stop_returned.load(); });
    check(returned_without_drain_mutex,
          "worker request_stop returns while external shutdown is joining");
    ordering_allow_exit = true;
    hooks.allow_drain = true;
    external_shutdown.join();
}

void test_concurrent_external_and_worker_shutdown_stress()
{
    constexpr int rounds = 8;
    constexpr int worker_count = 4;
    constexpr int external_count = 4;
    for (int round = 0; round < rounds; ++round) {
        runtime::RuntimeTaskOwner* owner_pointer = nullptr;
        std::atomic<int> ready{};
        std::atomic<int> worker_shutdown_returned{};
        std::atomic<bool> release{};
        runtime::RuntimeTaskOwner owner{
            [&owner_pointer, &ready, &worker_shutdown_returned, &release](
                const runtime::RuntimeTaskRequest&, std::stop_token,
                const runtime::RuntimeTaskProgressReporter&) {
                ready.fetch_add(1);
                while (!release.load()) std::this_thread::yield();
                owner_pointer->shutdown();
                worker_shutdown_returned.fetch_add(1);
                return runtime::RuntimeTaskTerminal{false, std::nullopt};
            }};
        owner_pointer = &owner;
        for (int worker = 0; worker < worker_count; ++worker) {
            check(static_cast<bool>(owner.start(request(
                      "stress-" + std::to_string(round) + "-"
                      + std::to_string(worker)))),
                  "stress worker starts");
        }
        check(wait_until([&ready] { return ready.load() == worker_count; }),
              "all stress workers enter backend");
        std::vector<std::thread> external;
        external.reserve(external_count);
        for (int index = 0; index < external_count; ++index) {
            external.emplace_back([&owner, &release] {
                while (!release.load()) std::this_thread::yield();
                owner.shutdown();
            });
        }
        release = true;
        for (auto& thread : external) thread.join();
        check(worker_shutdown_returned.load() == worker_count,
              "worker shutdown calls return during concurrent external drains");
        owner.shutdown();
    }
}

void test_escaped_reporter_and_timestamp_ordering()
{
    runtime::RuntimeTaskProgressReporter escaped;
    std::mutex escaped_mutex;
    std::condition_variable escaped_condition;
    bool escaped_ready = false;
    {
        runtime::RuntimeTaskOwner owner{
            [&escaped, &escaped_mutex, &escaped_condition, &escaped_ready](
                const runtime::RuntimeTaskRequest&, std::stop_token,
                const runtime::RuntimeTaskProgressReporter& report) {
                {
                    std::lock_guard lock{escaped_mutex};
                    escaped = report;
                    escaped_ready = true;
                }
                escaped_condition.notify_one();
                return runtime::RuntimeTaskTerminal{false, std::nullopt};
            }};
        check(static_cast<bool>(owner.start(request("escaped"))),
              "escaped-reporter task starts");
        {
            std::unique_lock lock{escaped_mutex};
            escaped_condition.wait_for(lock, 3s, [&escaped_ready] {
                return escaped_ready;
            });
        }
        check(owner.wait_for_idle("escaped", 3s),
              "escaped-reporter task publishes terminal");
        check(!escaped({true, std::nullopt, std::string{"late"}, {}}),
              "reporter called after backend completion returns false");
    }
    check(!escaped({true, std::nullopt, std::string{"later"}, {}}),
          "reporter called after owner destruction returns false without UAF");

    std::atomic<int> phase{};
    std::atomic<int> advance{};
    runtime::RuntimeTaskOwner timestamp_owner{
        [&phase, &advance](
            const runtime::RuntimeTaskRequest&, const std::stop_token stop,
            const runtime::RuntimeTaskProgressReporter& report) {
            check(report({true, std::nullopt, std::string{"one"}, {}}),
                  "first timestamp progress is accepted");
            phase = 1;
            while (advance.load() < 1) std::this_thread::yield();
            check(report({true, std::nullopt, std::string{"two"}, {}}),
                  "second timestamp progress is accepted");
            phase = 2;
            while (!stop.stop_requested()) std::this_thread::yield();
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    const auto started = timestamp_owner.start(request("timestamps"));
    const auto start_timestamp = started.snapshot->timestamp;
    check(wait_until([&phase] { return phase.load() == 1; }),
          "first progress is visible");
    const auto first_timestamp =
        timestamp_owner.snapshot("timestamps")->timestamp;
    advance = 1;
    check(wait_until([&phase] { return phase.load() == 2; }),
          "second progress is visible");
    const auto second_timestamp =
        timestamp_owner.snapshot("timestamps")->timestamp;
    const auto stop = timestamp_owner.request_stop("timestamps");
    const auto stop_timestamp = stop.snapshot->timestamp;
    check(timestamp_owner.wait_for_idle("timestamps", 3s),
          "timestamp task publishes terminal");
    const auto terminal_timestamp =
        timestamp_owner.snapshot("timestamps")->timestamp;
    check(start_timestamp < first_timestamp
              && first_timestamp < second_timestamp
              && second_timestamp < stop_timestamp
              && stop_timestamp < terminal_timestamp,
          "start/progress/stop/terminal timestamps advance monotonically");
}

void test_limits_and_external_shutdown_drain()
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
            while (!stop.stop_requested()) std::this_thread::yield();
            exited.fetch_add(1);
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
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
        "config status registry has fixed capacity");

    owner.shutdown();
    check(exited.load() == 3,
          "external shutdown stops and joins every backend worker");
    const auto stopped = owner.snapshots();
    check(std::all_of(stopped.begin(), stopped.end(), [](const auto& value) {
              return !value.running && !value.exit_code;
          }),
          "shutdown preserves backend-provided normal terminal state");
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
    check(
        runtime_task_owner_size_from_non_hook_tu()
            == sizeof(runtime::RuntimeTaskOwner),
        "hook and non-hook translation units agree on owner layout");
    test_keyed_concurrency_and_stop_start_linearization();
    test_explicit_terminal_outcomes_and_bounded_snapshots();
    test_self_shutdown_and_reentrant_stop_callback();
    test_nested_stop_delivery_and_join_lock_order();
    test_concurrent_external_and_worker_shutdown_stress();
    test_escaped_reporter_and_timestamp_ordering();
    test_limits_and_external_shutdown_drain();
    if (failures.load() != 0) {
        std::cerr << failures.load() << " runtime task owner test(s) failed\n";
        return 1;
    }
    std::cout << "Runtime task owner tests passed\n";
    return 0;
}
