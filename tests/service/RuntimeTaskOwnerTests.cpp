#include "service/runtime/RuntimeTaskOwner.h"

#include <algorithm>
#include <array>
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

void test_start_reservation_commit_and_rollback()
{
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStartDecision::started) == 0);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStartDecision::already_running) == 1);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStartDecision::stopping) == 2);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStartDecision::owner_stopped) == 3);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStartDecision::capacity_exceeded) == 4);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStartDecision::invalid_request) == 5);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStartDecision::thread_start_failed) == 6);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStartDecision::reservation_conflict) == 7);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStartDecision::preparation_failed) == 8);
    static_assert(!std::is_copy_constructible_v<
                  runtime::RuntimeTaskStartReservation>);
    static_assert(std::is_nothrow_move_constructible_v<
                  runtime::RuntimeTaskStartReservation>);
    static_assert(std::is_nothrow_move_assignable_v<
                  runtime::RuntimeTaskStartReservation>);

    std::atomic<int> backend_calls{};
    runtime::RuntimeTaskOwner owner{
        [&backend_calls](
            const runtime::RuntimeTaskRequest&, std::stop_token,
            const runtime::RuntimeTaskProgressReporter&) {
            backend_calls.fetch_add(1);
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};

    {
        auto prepared = owner.prepare_start(request("rollback"));
        check(static_cast<bool>(prepared),
              "start reservation prepares a gated worker");
        check(prepared.snapshot && prepared.snapshot->running,
              "prepare returns the complete future start response");
        check(!owner.snapshot("rollback") && owner.snapshots().empty(),
              "uncommitted reservation is absent from public snapshots");
        std::this_thread::sleep_for(20ms);
        check(backend_calls.load() == 0,
              "gated worker cannot enter backend before commit");
    }
    check(backend_calls.load() == 0 && !owner.snapshot("rollback"),
          "reservation destruction rolls back and never invokes backend");

    auto committed = owner.prepare_start(request("commit"));
    check(static_cast<bool>(committed) && !owner.snapshot("commit"),
          "committed config remains invisible until ownership transfer");
    committed.reservation.commit();
    check(!static_cast<bool>(committed.reservation),
          "commit consumes the move-only reservation exactly once");
    check(wait_until([&backend_calls] { return backend_calls.load() == 1; }),
          "commit releases the gate and invokes backend once");
    check(owner.wait_for_idle("commit", 3s),
          "committed worker publishes terminal state");
    const auto terminal = owner.snapshot("commit");
    check(terminal && !terminal->running,
          "committed worker becomes the visible config generation");
}

void test_start_reservation_conflict_and_thread_failure()
{
    std::atomic<int> backend_calls{};
    runtime::RuntimeTaskLimits limits;
    limits.max_configs = 1;
    runtime::RuntimeTaskOwner owner{
        [&backend_calls](
            const runtime::RuntimeTaskRequest&, std::stop_token,
            const runtime::RuntimeTaskProgressReporter&) {
            backend_calls.fetch_add(1);
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        },
        limits};

    std::atomic<int> ready{};
    std::atomic<bool> release{};
    std::array<runtime::RuntimeTaskStartDecision, 2> decisions{};
    std::array<runtime::RuntimeTaskStartReservation, 2> reservations;
    std::array<std::thread, 2> contenders;
    for (std::size_t index = 0; index < contenders.size(); ++index) {
        contenders[index] = std::thread{[&, index] {
            ready.fetch_add(1);
            while (!release.load()) std::this_thread::yield();
            auto prepared = owner.prepare_start(request("contended"));
            decisions[index] = prepared.decision;
            reservations[index] = std::move(prepared.reservation);
        }};
    }
    check(wait_until([&ready] { return ready.load() == 2; }),
          "same-config contenders reach the prepare race");
    release = true;
    for (auto& contender : contenders) contender.join();
    const auto prepared_count = std::count(
        decisions.begin(), decisions.end(),
        runtime::RuntimeTaskStartDecision::started);
    const auto conflict_count = std::count(
        decisions.begin(), decisions.end(),
        runtime::RuntimeTaskStartDecision::reservation_conflict);
    check(prepared_count == 1 && conflict_count == 1,
          "same-config prepare race has one winner and deterministic conflict");
    check(runtime::runtime_task_start_decision_name(
              runtime::RuntimeTaskStartDecision::reservation_conflict)
              == "reservation-conflict",
          "reservation conflict has a stable decision spelling");
    check(!owner.snapshot("contended") && backend_calls.load() == 0,
          "contended uncommitted winner is neither visible nor executing");
    for (auto& reservation : reservations) {
        reservation = runtime::RuntimeTaskStartReservation{};
    }

    runtime::RuntimeTaskOwnerTestAccess::fail_next_thread_start(owner);
    auto failed = owner.prepare_start(request("thread-failure"));
    check(failed.decision
              == runtime::RuntimeTaskStartDecision::thread_start_failed
              && !failed.reservation && failed.snapshot
              && !failed.snapshot->running && failed.snapshot->exit_code == 1,
          "forced thread creation failure returns a terminal failure response");
    check(!owner.snapshot("thread-failure") && owner.snapshots().empty()
              && backend_calls.load() == 0,
          "failed reversible prepare publishes no config generation");

    auto capacity_reused = owner.prepare_start(request("capacity-reused"));
    check(static_cast<bool>(capacity_reused),
          "failed reversible prepare releases the only config slot");
    capacity_reused.reservation = runtime::RuntimeTaskStartReservation{};

    const auto prior_start = owner.start(request("prior", "prior-mode"));
    check(prior_start.decision == runtime::RuntimeTaskStartDecision::started
              && owner.wait_for_idle("prior", 3s),
          "prior completed generation is available for rollback evidence");
    const auto prior = owner.snapshot("prior");
    runtime::RuntimeTaskOwnerTestAccess::fail_next_thread_start(owner);
    auto failed_replacement = owner.prepare_start(
        request("prior", "replacement-mode"));
    const auto preserved = owner.snapshot("prior");
    check(failed_replacement.decision
              == runtime::RuntimeTaskStartDecision::thread_start_failed
              && prior && preserved
              && preserved->timestamp == prior->timestamp
              && preserved->run_mode == prior->run_mode
              && preserved->exit_code == prior->exit_code,
          "failed replacement prepare preserves the prior public generation");

    runtime::RuntimeTaskOwnerTestAccess::fail_next_thread_start(owner);
    const auto legacy_failure = owner.start(request("prior", "legacy-mode"));
    const auto legacy_snapshot = owner.snapshot("prior");
    check(legacy_failure.decision
              == runtime::RuntimeTaskStartDecision::thread_start_failed
              && legacy_snapshot && !legacy_snapshot->running
              && legacy_snapshot->exit_code == 1
              && legacy_snapshot->run_mode == "legacy-mode"
              && legacy_snapshot->timestamp > prior->timestamp,
          "legacy start alone preserves its visible thread-failure snapshot");
}

void test_reservation_abort_remains_owned_until_join()
{
    struct CancelGate {
        std::atomic<int> entered{};
        std::atomic<bool> release{};
    } gate;
    std::atomic<int> backend_calls{};
    runtime::RuntimeTaskLimits limits;
    limits.max_configs = 1;
    runtime::RuntimeTaskOwner owner{
        [&backend_calls](
            const runtime::RuntimeTaskRequest&, std::stop_token,
            const runtime::RuntimeTaskProgressReporter&) {
            backend_calls.fetch_add(1);
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        },
        limits};
    runtime::RuntimeTaskOwnerTestAccess::set_after_reservation_cancelled_gate_hook(
        owner,
        +[](void* context) noexcept {
            auto& value = *static_cast<CancelGate*>(context);
            value.entered.fetch_add(1);
            while (!value.release.load()) std::this_thread::yield();
        },
        &gate);

    auto prepared = owner.prepare_start(request("abort-owned"));
    check(static_cast<bool>(prepared),
          "abort ownership fixture prepares one gated worker");
    std::atomic<bool> abort_returned{};
    std::thread abort{[
        reservation = std::move(prepared.reservation),
        &abort_returned]() mutable {
        reservation = runtime::RuntimeTaskStartReservation{};
        abort_returned = true;
    }};
    check(wait_until([&gate] { return gate.entered.load() == 1; }),
          "cancelled worker reaches the deterministic pre-exit gate");
    check(!abort_returned.load(),
          "reservation abort remains in progress until worker exit");
    const auto same_config = owner.prepare_start(request("abort-owned"));
    check(same_config.decision
              == runtime::RuntimeTaskStartDecision::reservation_conflict,
          "same config remains reserved until cancelled worker joins");
    const auto other_config = owner.prepare_start(request("other"));
    check(other_config.decision
              == runtime::RuntimeTaskStartDecision::capacity_exceeded,
          "cancel-in-progress remains charged to the config capacity bound");
    gate.release = true;
    abort.join();
    check(abort_returned.load() && backend_calls.load() == 0
              && !owner.snapshot("abort-owned"),
          "abort joins without backend entry or public state");
    auto released_capacity = owner.prepare_start(request("other"));
    check(static_cast<bool>(released_capacity),
          "joined abort releases its charged config slot");
    released_capacity.reservation = runtime::RuntimeTaskStartReservation{};

    struct ShutdownCancelGate {
        std::atomic<bool> entered{};
        std::atomic<bool> release{};
    } shutdown_gate;
    std::atomic<int> shutdown_backend_calls{};
    runtime::RuntimeTaskOwner shutdown_owner{
        [&shutdown_backend_calls](
            const runtime::RuntimeTaskRequest&, std::stop_token,
            const runtime::RuntimeTaskProgressReporter&) {
            shutdown_backend_calls.fetch_add(1);
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    runtime::RuntimeTaskOwnerTestAccess::set_after_reservation_cancelled_gate_hook(
        shutdown_owner,
        +[](void* context) noexcept {
            auto& value = *static_cast<ShutdownCancelGate*>(context);
            value.entered = true;
            while (!value.release.load()) std::this_thread::yield();
        },
        &shutdown_gate);
    auto shutdown_prepared = shutdown_owner.prepare_start(
        request("shutdown-abort"));
    std::thread shutdown_abort{
        [reservation = std::move(shutdown_prepared.reservation)]() mutable {
            reservation = runtime::RuntimeTaskStartReservation{};
        }};
    check(wait_until([&shutdown_gate] { return shutdown_gate.entered.load(); }),
          "shutdown fixture blocks cancelled worker before exit");
    std::atomic<bool> shutdown_returned{};
    std::thread shutdown{[&] {
        shutdown_owner.shutdown();
        shutdown_returned = true;
    }};
    std::this_thread::sleep_for(20ms);
    check(!shutdown_returned.load(),
          "external shutdown waits for abort worker join, not slot removal");
    shutdown_gate.release = true;
    shutdown_abort.join();
    shutdown.join();
    check(shutdown_returned.load() && shutdown_backend_calls.load() == 0
              && !shutdown_owner.snapshot("shutdown-abort"),
          "shutdown returns only after the cancelled worker is joined");
}

void test_start_reservation_shutdown_linearization()
{
    struct ShutdownGate {
        std::atomic<bool> closed{};
        std::atomic<bool> release{};
    } shutdown_gate;
    std::atomic<int> backend_calls{};
    std::atomic<bool> observed_stop{};
    runtime::RuntimeTaskOwner owner{
        [&backend_calls, &observed_stop](
            const runtime::RuntimeTaskRequest&, const std::stop_token stop,
            const runtime::RuntimeTaskProgressReporter&) {
            backend_calls.fetch_add(1);
            observed_stop = stop.stop_requested();
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    runtime::RuntimeTaskOwnerTestAccess::set_after_shutdown_closed_hook(
        owner,
        +[](void* context) noexcept {
            auto& gate = *static_cast<ShutdownGate*>(context);
            gate.closed = true;
            while (!gate.release.load()) std::this_thread::yield();
        },
        &shutdown_gate);

    auto prepared = owner.prepare_start(request("shutdown-commit"));
    check(static_cast<bool>(prepared),
          "shutdown race has an outstanding prepared start");
    std::thread shutdown{[&owner] { owner.shutdown(); }};
    check(wait_until([&shutdown_gate] { return shutdown_gate.closed.load(); }),
          "shutdown closes admission before reservation commit");
    check(owner.request_stop("shutdown-commit").decision
              == runtime::RuntimeTaskStopDecision::unknown_config,
          "legacy stop sees no current job behind a pending start after shutdown");
    prepared.reservation.commit();
    shutdown_gate.release = true;
    shutdown.join();
    check(backend_calls.load() == 1 && observed_stop.load(),
          "commit after shutdown linearization executes once with stop requested");
    const auto stopped = owner.snapshot("shutdown-commit");
    check(stopped && !stopped->running && !stopped->stopping,
          "shutdown drains the late committed generation to terminal state");

    struct ClosedFlag { std::atomic<bool> value{}; } closed;
    std::atomic<int> cancelled_backend_calls{};
    runtime::RuntimeTaskOwner cancel_owner{
        [&cancelled_backend_calls](
            const runtime::RuntimeTaskRequest&, std::stop_token,
            const runtime::RuntimeTaskProgressReporter&) {
            cancelled_backend_calls.fetch_add(1);
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    runtime::RuntimeTaskOwnerTestAccess::set_after_shutdown_closed_hook(
        cancel_owner,
        +[](void* context) noexcept {
            static_cast<ClosedFlag*>(context)->value = true;
        },
        &closed);
    auto cancelled = cancel_owner.prepare_start(request("shutdown-cancel"));
    std::atomic<bool> shutdown_returned{};
    std::thread cancel_shutdown{[&] {
        cancel_owner.shutdown();
        shutdown_returned = true;
    }};
    check(wait_until([&closed] { return closed.value.load(); }),
          "second shutdown closes admission with a pending reservation");
    std::this_thread::sleep_for(20ms);
    check(!shutdown_returned.load(),
          "external shutdown waits for reservation resolution");
    cancelled.reservation = runtime::RuntimeTaskStartReservation{};
    cancel_shutdown.join();
    check(cancelled_backend_calls.load() == 0
              && !cancel_owner.snapshot("shutdown-cancel"),
          "rollback unblocks shutdown without invoking backend");

    struct ReplacementGate {
        std::atomic<bool> closed{};
        std::atomic<bool> release{};
    } replacement_gate;
    runtime::RuntimeTaskOwner replacement_owner{
        [](const runtime::RuntimeTaskRequest&, std::stop_token,
           const runtime::RuntimeTaskProgressReporter&) {
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    check(static_cast<bool>(replacement_owner.start(request("replacement")))
              && replacement_owner.wait_for_idle("replacement", 3s),
          "replacement shutdown fixture retains a completed generation");
    auto replacement = replacement_owner.prepare_start(
        request("replacement", "next"));
    runtime::RuntimeTaskOwnerTestAccess::set_after_shutdown_closed_hook(
        replacement_owner,
        +[](void* context) noexcept {
            auto& gate = *static_cast<ReplacementGate*>(context);
            gate.closed = true;
            while (!gate.release.load()) std::this_thread::yield();
        },
        &replacement_gate);
    std::thread replacement_shutdown{
        [&replacement_owner] { replacement_owner.shutdown(); }};
    check(wait_until([&replacement_gate] {
              return replacement_gate.closed.load();
          }),
          "shutdown closes with a replacement start pending");
    check(replacement_owner.request_stop("replacement").decision
              == runtime::RuntimeTaskStopDecision::already_stopped,
          "legacy stop preserves the completed current view behind replacement");
    check(replacement_owner.request_stop(std::string(1'024, 'x')).decision
              == runtime::RuntimeTaskStopDecision::unknown_config,
          "oversized legacy stop remains an allocation-free unknown lookup");
    replacement.reservation = runtime::RuntimeTaskStartReservation{};
    replacement_gate.release = true;
    replacement_shutdown.join();
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

void test_stop_reservation_prepare_abort_commit_and_capacity()
{
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStopDecision::stop_requested) == 0);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStopDecision::already_stopping) == 1);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStopDecision::already_stopped) == 2);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStopDecision::unknown_config) == 3);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStopDecision::owner_stopped) == 4);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStopDecision::capacity_exceeded) == 5);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStopDecision::reservation_conflict) == 6);
    static_assert(!std::is_copy_constructible_v<
                  runtime::RuntimeTaskStopReservation>);
    static_assert(std::is_nothrow_move_constructible_v<
                  runtime::RuntimeTaskStopReservation>);

    std::atomic<int> entered{};
    std::atomic<int> stop_callbacks{};
    std::atomic<int> progress_round{};
    std::atomic<int> progress_attempts{};
    std::atomic<int> accepted_progress{};
    std::atomic<bool> reporter_failed{};
    std::atomic<int> stopping_progress_round{};
    std::atomic<int> stopping_progress_attempts{};
    std::atomic<bool> allow_exit{};
    runtime::RuntimeTaskOwner owner{
        [&entered, &stop_callbacks, &progress_round, &progress_attempts,
         &accepted_progress, &reporter_failed, &stopping_progress_round,
         &stopping_progress_attempts, &allow_exit](
            const runtime::RuntimeTaskRequest&, const std::stop_token stop,
            const runtime::RuntimeTaskProgressReporter& report) {
            entered.fetch_add(1);
            auto callback = [&stop_callbacks]() noexcept {
                stop_callbacks.fetch_add(1);
            };
            std::stop_callback on_stop{stop, callback};
            for (int round = 1; round <= 3; ++round) {
                while (progress_round.load() < round) {
                    std::this_thread::yield();
                }
                if (round == 1) {
                    if (!report({
                            true, "prepared-progress",
                            std::string{"staged-old"}, {}})) {
                        reporter_failed = true;
                        return runtime::RuntimeTaskTerminal{false, 90};
                    }
                    accepted_progress.fetch_add(1);
                }
                if (report({
                        true, "prepared-progress",
                        std::string{"live-" + std::to_string(round)}, {}})) {
                    accepted_progress.fetch_add(1);
                } else {
                    reporter_failed = true;
                    return runtime::RuntimeTaskTerminal{false, 91};
                }
                progress_attempts.fetch_add(1);
            }
            while (!stop.stop_requested()) std::this_thread::yield();
            for (int round = 1; round <= 2; ++round) {
                while (stopping_progress_round.load() < round) {
                    std::this_thread::yield();
                }
                if (!report({
                        true, "stopping-progress",
                        std::string{"stopping-" + std::to_string(round)}, {}})) {
                    reporter_failed = true;
                    return runtime::RuntimeTaskTerminal{false, 95};
                }
                stopping_progress_attempts.fetch_add(1);
            }
            while (!allow_exit.load()) std::this_thread::yield();
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    check(static_cast<bool>(owner.start(request("reserved-stop"))),
          "stop reservation fixture starts");
    check(wait_until([&entered] { return entered.load() == 1; }),
          "stop reservation fixture enters backend");

    const auto before_prepare = owner.snapshot("reserved-stop");
    auto aborted = owner.prepare_stop("reserved-stop");
    check(aborted.decision == runtime::RuntimeTaskStopDecision::stop_requested
              && aborted.reservation && aborted.snapshot
              && aborted.snapshot->stopping,
          "prepare_stop returns a complete projected response");
    progress_round = 1;
    check(wait_until([&progress_attempts] {
              return progress_attempts.load() >= 1;
          }),
          "running backend is not blocked by stop prepare");
    const auto still_running = owner.snapshot("reserved-stop");
    check(still_running && still_running->running && !still_running->stopping
              && before_prepare
              && still_running->timestamp == before_prepare->timestamp
              && still_running->current_task == before_prepare->current_task
              && accepted_progress.load() == 2
              && stop_callbacks.load() == 0,
          "prepared stop stages accepted progress behind its ordered reply");
    check(owner.prepare_start(request("reserved-stop")).decision
              == runtime::RuntimeTaskStartDecision::reservation_conflict,
          "pending stop blocks same-config start");
    check(owner.prepare_stop("reserved-stop").decision
              == runtime::RuntimeTaskStopDecision::reservation_conflict,
          "pending stop blocks same-config stop");
    aborted.reservation = runtime::RuntimeTaskStopReservation{};
    const auto after_abort = owner.snapshot("reserved-stop");
    check(after_abort && after_abort->current_task == "live-1"
              && before_prepare
              && after_abort->timestamp > before_prepare->timestamp,
          "stop abort publishes the latest staged progress once");
    progress_round = 2;
    check(wait_until([&progress_attempts] {
              return progress_attempts.load() >= 2;
          }) && accepted_progress.load() == 3
              && owner.snapshot("reserved-stop")->current_task == "live-2"
              && stop_callbacks.load() == 0,
          "stop abort releases publication without stop side effects");

    auto committed = owner.prepare_stop("reserved-stop");
    const auto committed_reply = committed.snapshot;
    progress_round = 3;
    check(wait_until([&progress_attempts] {
              return progress_attempts.load() == 3;
          }) && accepted_progress.load() == 4,
          "concurrent progress cannot overtake the committing stop reply");
    check(owner.prepare_stop_all().decision
              == runtime::RuntimeTaskStopAllDecision::reservation_conflict,
          "stop-all atomically rejects an active keyed stop reservation");
    committed.reservation.commit();
    committed.reservation.commit();
    const auto committed_public = owner.snapshot("reserved-stop");
    check(stop_callbacks.load() == 1 && committed_reply && committed_public
              && committed_public->timestamp == committed_reply->timestamp
              && committed_public->stopping == committed_reply->stopping
              && committed_public->current_task == committed_reply->current_task,
          "stop commit publishes its exact prebuilt ordered reply exactly once");
    auto stopping_abort = owner.prepare_stop("reserved-stop");
    check(stopping_abort.decision
              == runtime::RuntimeTaskStopDecision::already_stopping
              && stopping_abort.reservation,
          "already-stopping stop still acquires a keyed no-op reservation");
    check(owner.prepare_start(request("reserved-stop")).decision
              == runtime::RuntimeTaskStartDecision::reservation_conflict,
          "already-stopping reservation blocks same-config restart");
    stopping_progress_round = 1;
    check(wait_until([&stopping_progress_attempts] {
              return stopping_progress_attempts.load() == 1;
          }),
          "already-stopping abort fixture stages one accepted report");
    stopping_abort.reservation = runtime::RuntimeTaskStopReservation{};
    check(owner.snapshot("reserved-stop")->current_task == "stopping-1"
              && !owner.snapshot("reserved-stop")->is_flag_run,
          "already-stopping abort publishes staged progress as stopping");

    auto stopping_commit = owner.prepare_stop("reserved-stop");
    stopping_progress_round = 2;
    check(wait_until([&stopping_progress_attempts] {
              return stopping_progress_attempts.load() == 2;
          }),
          "already-stopping commit fixture stages latest progress");
    stopping_commit.reservation.commit();
    check(owner.snapshot("reserved-stop")->current_task == "stopping-2"
              && !owner.snapshot("reserved-stop")->is_flag_run,
          "already-stopping no-op commit publishes accepted staged progress");
    allow_exit = true;
    check(owner.wait_for_idle("reserved-stop", 3s),
          "committed stop reaches terminal state");
    check(!reporter_failed.load()
              && !owner.snapshot("reserved-stop")->exit_code,
          "reporter fail-closed backend survives abort and commit staging");

    auto completed = owner.prepare_stop("reserved-stop");
    check(completed.decision == runtime::RuntimeTaskStopDecision::already_stopped
              && completed.reservation,
          "already-stopped stop still owns a keyed no-op reservation");
    check(owner.prepare_start(request("reserved-stop")).decision
              == runtime::RuntimeTaskStartDecision::reservation_conflict,
          "already-stopped reservation blocks restart until resolution");
    completed.reservation.commit();

    runtime::RuntimeTaskLimits limits;
    limits.max_configs = 1;
    runtime::RuntimeTaskOwner capacity_owner{
        [](const runtime::RuntimeTaskRequest&, std::stop_token,
           const runtime::RuntimeTaskProgressReporter&) {
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        },
        limits};
    auto unknown = capacity_owner.prepare_stop("unknown-a");
    check(unknown.decision == runtime::RuntimeTaskStopDecision::unknown_config
              && unknown.reservation && !capacity_owner.snapshot("unknown-a"),
          "unknown stop owns an invisible temporary keyed slot");
    check(capacity_owner.prepare_stop("unknown-b").decision
              == runtime::RuntimeTaskStopDecision::capacity_exceeded
              && capacity_owner.prepare_start(request("unknown-b")).decision
                  == runtime::RuntimeTaskStartDecision::capacity_exceeded,
          "unknown stop reservation is charged to max_configs");
    check(capacity_owner.request_stop("unknown-b").decision
              == runtime::RuntimeTaskStopDecision::unknown_config,
          "legacy unknown stop remains unknown when max_configs is full");
    unknown.reservation = runtime::RuntimeTaskStopReservation{};
    auto released = capacity_owner.prepare_start(request("unknown-b"));
    check(static_cast<bool>(released.reservation),
          "resolving unknown stop releases its temporary capacity slot");
    released.reservation = runtime::RuntimeTaskStartReservation{};
    check(static_cast<bool>(capacity_owner.start(request("retained")))
              && capacity_owner.wait_for_idle("retained", 3s)
              && capacity_owner.prepare_stop("other").decision
                  == runtime::RuntimeTaskStopDecision::capacity_exceeded
              && capacity_owner.request_stop("other").decision
                  == runtime::RuntimeTaskStopDecision::unknown_config,
          "legacy unknown decision survives a full retained status registry");
}

void test_stop_reservation_natural_completion_and_shutdown_race()
{
    std::atomic<bool> entered{};
    std::atomic<bool> release{};
    std::atomic<int> stop_callbacks{};
    std::atomic<bool> reporter_accepted{};
    runtime::RuntimeTaskOwner owner{
        [&entered, &release, &stop_callbacks, &reporter_accepted](
            const runtime::RuntimeTaskRequest&, const std::stop_token stop,
            const runtime::RuntimeTaskProgressReporter& report) {
            auto callback = [&stop_callbacks]() noexcept {
                stop_callbacks.fetch_add(1);
            };
            std::stop_callback on_stop{stop, callback};
            entered = true;
            while (!release.load()) std::this_thread::yield();
            reporter_accepted = report(
                {true, "natural", std::string{"natural-progress"}, {}});
            if (!reporter_accepted.load()) {
                return runtime::RuntimeTaskTerminal{false, 93};
            }
            return runtime::RuntimeTaskTerminal{true, 0};
        }};
    check(static_cast<bool>(owner.start(request("natural")))
              && wait_until([&entered] { return entered.load(); }),
          "natural-completion fixture starts");
    auto prepared = owner.prepare_stop("natural");
    release = true;
    check(owner.wait_for_idle("natural", 3s),
          "backend may complete naturally after prepare");
    prepared.reservation.commit();
    const auto terminal = owner.snapshot("natural");
    check(reporter_accepted.load() && stop_callbacks.load() == 0
              && terminal && terminal->is_flag_run
              && terminal->exit_code == 0,
          "natural completion clears staging without fail-closed or rollback");

    struct ShutdownGate {
        std::atomic<bool> closed{};
        std::atomic<bool> release{};
    } gate;
    std::atomic<bool> shutdown_entered{};
    std::atomic<bool> allow_shutdown_progress{};
    std::atomic<bool> shutdown_progress_attempted{};
    std::atomic<bool> shutdown_progress_accepted{};
    std::atomic<bool> allow_shutdown_exit{};
    runtime::RuntimeTaskOwner shutdown_owner{
        [&shutdown_entered, &allow_shutdown_progress,
         &shutdown_progress_attempted, &shutdown_progress_accepted,
         &allow_shutdown_exit](
            const runtime::RuntimeTaskRequest&, const std::stop_token stop,
            const runtime::RuntimeTaskProgressReporter& report) {
            shutdown_entered = true;
            while (!allow_shutdown_progress.load()) std::this_thread::yield();
            shutdown_progress_accepted = report(
                {true, "shutdown", std::string{"shutdown-progress"}, {}});
            shutdown_progress_attempted = true;
            if (!shutdown_progress_accepted.load()) {
                return runtime::RuntimeTaskTerminal{false, 94};
            }
            while (!stop.stop_requested()) std::this_thread::yield();
            while (!allow_shutdown_exit.load()) std::this_thread::yield();
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    runtime::RuntimeTaskOwnerTestAccess::set_after_shutdown_closed_hook(
        shutdown_owner,
        +[](void* context) noexcept {
            auto& value = *static_cast<ShutdownGate*>(context);
            value.closed = true;
            while (!value.release.load()) std::this_thread::yield();
        },
        &gate);
    check(static_cast<bool>(shutdown_owner.start(request("shutdown-stop")))
              && wait_until([&shutdown_entered] {
                     return shutdown_entered.load();
                 }),
          "shutdown-stop fixture starts");
    const auto shutdown_before = shutdown_owner.snapshot("shutdown-stop");
    auto shutdown_stop = shutdown_owner.prepare_stop("shutdown-stop");
    const auto shutdown_reply = shutdown_stop.snapshot;
    std::thread shutdown{[&shutdown_owner] { shutdown_owner.shutdown(); }};
    check(wait_until([&gate] { return gate.closed.load(); }),
          "shutdown closes admission with claimed stop outstanding");
    allow_shutdown_progress = true;
    check(wait_until([&shutdown_progress_attempted] {
              return shutdown_progress_attempted.load();
          }) && shutdown_progress_accepted.load()
              && shutdown_before
              && shutdown_owner.snapshot("shutdown-stop")->timestamp
                  == shutdown_before->timestamp,
          "post-close progress stages without overtaking prepared reply");
    check(shutdown_owner.request_stop("shutdown-stop").decision
              == runtime::RuntimeTaskStopDecision::reservation_conflict,
          "shutdown does not bypass an outstanding keyed stop gate");
    shutdown_stop.reservation.commit();
    check(shutdown_reply
              && shutdown_owner.snapshot("shutdown-stop")->timestamp
                  == shutdown_reply->timestamp,
          "post-close commit cannot roll the public timestamp backward");
    allow_shutdown_exit = true;
    gate.release = true;
    shutdown.join();
    check(shutdown_owner.prepare_stop("shutdown-stop").decision
              == runtime::RuntimeTaskStopDecision::owner_stopped,
          "prepared stop admission is closed after shutdown");
    const auto compatible = shutdown_owner.request_stop("shutdown-stop");
    check(compatible.decision
              == runtime::RuntimeTaskStopDecision::already_stopped,
          "legacy request_stop preserves terminal decision after shutdown");
}

void test_stop_all_reservation_gates_and_reentrant_delivery()
{
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStopAllDecision::stop_requested) == 0);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStopAllDecision::nothing_to_stop) == 1);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStopAllDecision::owner_stopped) == 2);
    static_assert(static_cast<std::uint8_t>(
                      runtime::RuntimeTaskStopAllDecision::reservation_conflict)
                  == 3);
    static_assert(!std::is_copy_constructible_v<
                  runtime::RuntimeTaskStopAllReservation>);
    static_assert(std::is_nothrow_move_constructible_v<
                  runtime::RuntimeTaskStopAllReservation>);

    runtime::RuntimeTaskOwner empty{
        [](const runtime::RuntimeTaskRequest&, std::stop_token,
           const runtime::RuntimeTaskProgressReporter&) {
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    auto empty_all = empty.prepare_stop_all();
    check(empty_all.decision
              == runtime::RuntimeTaskStopAllDecision::nothing_to_stop
              && empty_all.reservation,
          "empty stop-all still acquires the global gate");
    check(empty.prepare_start(request("blocked")).decision
              == runtime::RuntimeTaskStartDecision::reservation_conflict
              && empty.prepare_stop("blocked").decision
                  == runtime::RuntimeTaskStopDecision::reservation_conflict
              && empty.prepare_stop_all().decision
                  == runtime::RuntimeTaskStopAllDecision::reservation_conflict,
          "empty stop-all blocks every start and stop reservation");
    empty_all.reservation = runtime::RuntimeTaskStopAllReservation{};
    auto after_empty_abort = empty.prepare_start(request("after-abort"));
    check(static_cast<bool>(after_empty_abort.reservation),
          "aborting empty stop-all releases the global gate");
    check(empty.prepare_stop_all().decision
              == runtime::RuntimeTaskStopAllDecision::reservation_conflict,
          "stop-all atomically rejects an active start reservation");
    after_empty_abort.reservation = runtime::RuntimeTaskStartReservation{};
    const auto wrapped_empty = empty.request_stop_all();
    check(wrapped_empty.decision
              == runtime::RuntimeTaskStopAllDecision::nothing_to_stop
              && empty.prepare_start(request("after-wrapper")).reservation,
          "request_stop_all wraps prepare and commit without retaining its gate");

    runtime::RuntimeTaskOwner* owner_pointer = nullptr;
    std::atomic<int> entered{};
    std::atomic<int> callbacks{};
    std::atomic<bool> reentrant_returned{};
    std::atomic<bool> reentrant_shutdown_returned{};
    std::atomic<int> progress_round{};
    std::atomic<int> progress_attempts{};
    std::atomic<int> accepted_progress{};
    std::atomic<bool> reporter_failed{};
    std::atomic<bool> allow_exit{};
    runtime::RuntimeTaskOwner owner{
        [&owner_pointer, &entered, &callbacks, &reentrant_returned,
         &reentrant_shutdown_returned, &progress_round, &progress_attempts,
         &accepted_progress, &reporter_failed, &allow_exit](
            const runtime::RuntimeTaskRequest& value,
            const std::stop_token stop,
            const runtime::RuntimeTaskProgressReporter& report) {
            auto callback = [&]() noexcept {
                callbacks.fetch_add(1);
                if (value.config_id == "alpha") {
                    const auto nested = owner_pointer->request_stop("beta");
                    const auto restart = owner_pointer->start(request("alpha"));
                    reentrant_returned =
                        nested.decision
                            == runtime::RuntimeTaskStopDecision::already_stopping
                        && restart.decision
                            == runtime::RuntimeTaskStartDecision::stopping;
                    owner_pointer->shutdown();
                    reentrant_shutdown_returned = true;
                }
            };
            std::stop_callback on_stop{stop, callback};
            entered.fetch_add(1);
            for (int round = 1; round <= 3; ++round) {
                while (progress_round.load() < round) {
                    std::this_thread::yield();
                }
                if (report({
                        true, "late",
                        std::string{
                            value.config_id + std::to_string(round)},
                        {}})) {
                    accepted_progress.fetch_add(1);
                } else {
                    reporter_failed = true;
                    return runtime::RuntimeTaskTerminal{false, 92};
                }
                progress_attempts.fetch_add(1);
            }
            while (!stop.stop_requested()) std::this_thread::yield();
            while (!allow_exit.load()) std::this_thread::yield();
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    owner_pointer = &owner;
    check(static_cast<bool>(owner.start(request("beta")))
              && static_cast<bool>(owner.start(request("alpha")))
              && static_cast<bool>(owner.start(request("gamma")))
              && wait_until([&entered] { return entered.load() == 3; }),
          "stop-all fixtures are live");
    check(owner.request_stop("gamma").decision
              == runtime::RuntimeTaskStopDecision::stop_requested,
          "stop-all fixture has one preexisting stopping generation");
    const auto alpha_before = owner.snapshot("alpha");
    const auto beta_before = owner.snapshot("beta");
    const auto gamma_before = owner.snapshot("gamma");
    auto aborted = owner.prepare_stop_all();
    check(aborted.decision == runtime::RuntimeTaskStopAllDecision::stop_requested
              && aborted.snapshots.size() == 3
              && aborted.snapshots[0].config_id == "alpha"
              && aborted.snapshots[1].config_id == "beta"
              && aborted.snapshots[2].config_id == "gamma",
          "prepared stop-all response is complete and sorted");
    progress_round = 1;
    check(wait_until([&progress_attempts] {
              return progress_attempts.load() == 3;
          }),
          "backends remain nonblocking while stop-all reply is prepared");
    check(owner.snapshot("alpha")->running
              && !owner.snapshot("alpha")->stopping
              && alpha_before && beta_before && gamma_before
              && owner.snapshot("alpha")->timestamp == alpha_before->timestamp
              && owner.snapshot("beta")->timestamp == beta_before->timestamp
              && owner.snapshot("gamma")->timestamp == gamma_before->timestamp
              && accepted_progress.load() == 3
              && owner.prepare_start(request("other")).decision
                  == runtime::RuntimeTaskStartDecision::reservation_conflict
              && owner.prepare_stop("alpha").decision
                  == runtime::RuntimeTaskStopDecision::reservation_conflict,
          "prepared stop-all is invisible and globally exclusive");
    aborted.reservation = runtime::RuntimeTaskStopAllReservation{};
    check(owner.snapshot("alpha")->current_task == "alpha1"
              && owner.snapshot("beta")->current_task == "beta1"
              && owner.snapshot("gamma")->current_task == "gamma1"
              && !owner.snapshot("gamma")->is_flag_run
              && callbacks.load() == 1,
          "aborting stop-all publishes each latest staged progress");

    progress_round = 2;
    check(wait_until([&progress_attempts] {
              return progress_attempts.load() == 6;
          }) && accepted_progress.load() == 6
              && owner.snapshot("alpha")->current_task == "alpha2"
              && owner.snapshot("beta")->current_task == "beta2"
              && owner.snapshot("gamma")->current_task == "gamma2",
          "stop-all abort lets fail-closed reporters continue successfully");

    auto committed = owner.prepare_stop_all();
    const auto committed_replies = committed.snapshots;
    progress_round = 3;
    check(wait_until([&progress_attempts] {
              return progress_attempts.load() == 9;
          }) && accepted_progress.load() == 9,
          "concurrent reports cannot overtake committing stop-all replies");
    committed.reservation.commit();
    const auto committed_alpha = owner.snapshot("alpha");
    const auto committed_beta = owner.snapshot("beta");
    const auto committed_gamma = owner.snapshot("gamma");
    check(committed_replies.size() == 3 && committed_alpha && committed_beta
              && committed_gamma
              && committed_alpha->timestamp == committed_replies[0].timestamp
              && committed_beta->timestamp == committed_replies[1].timestamp
              && committed_gamma->current_task == "gamma3"
              && !committed_gamma->is_flag_run
              && callbacks.load() == 3 && reentrant_returned.load()
              && reentrant_shutdown_returned.load(),
          "stop-all publishes exact replies before reentrant delivery");
    allow_exit = true;
    check(owner.wait_for_idle("alpha", 3s)
              && owner.wait_for_idle("beta", 3s)
              && owner.wait_for_idle("gamma", 3s),
          "stop-all workers reach terminal state");
    check(!reporter_failed.load() && !owner.snapshot("alpha")->exit_code
              && !owner.snapshot("beta")->exit_code
              && !owner.snapshot("gamma")->exit_code,
          "stop-all staging never trips fail-closed backend behavior");
}

void test_stop_transactions_generation_exhaustion_and_stress()
{
    runtime::RuntimeTaskOwner exhausted{
        [](const runtime::RuntimeTaskRequest&, std::stop_token,
           const runtime::RuntimeTaskProgressReporter&) {
            return runtime::RuntimeTaskTerminal{false, std::nullopt};
        }};
    runtime::RuntimeTaskOwnerTestAccess::exhaust_generation_after_next_start(
        exhausted);
    check(static_cast<bool>(exhausted.start(request("last-generation")))
              && exhausted.wait_for_idle("last-generation", 3s),
          "maximum nonzero generation is usable once");
    check(exhausted.prepare_start(request("no-wrap")).decision
              == runtime::RuntimeTaskStartDecision::capacity_exceeded,
          "generation wrap fails closed instead of reusing an ABA identity");

    constexpr int rounds = 100;
    for (int round = 0; round < rounds; ++round) {
        std::atomic<bool> entered{};
        runtime::RuntimeTaskOwner owner{
            [&entered](
                const runtime::RuntimeTaskRequest&, const std::stop_token stop,
                const runtime::RuntimeTaskProgressReporter&) {
                entered = true;
                while (!stop.stop_requested()) std::this_thread::yield();
                return runtime::RuntimeTaskTerminal{false, std::nullopt};
            }};
        const std::string config = "transaction-" + std::to_string(round);
        check(static_cast<bool>(owner.start(request(config)))
                  && wait_until([&entered] { return entered.load(); }),
              "transaction stress worker starts");
        auto prepared = owner.prepare_stop(config);
        if ((round % 2) == 0) {
            prepared.reservation = runtime::RuntimeTaskStopReservation{};
            check(owner.request_stop(config).decision
                      == runtime::RuntimeTaskStopDecision::stop_requested,
                  "aborted stress reservation leaves wrapper usable");
        } else {
            prepared.reservation.commit();
        }
        check(owner.wait_for_idle(config, 3s),
              "transaction stress worker reaches terminal state");
    }
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

void test_prepared_backend_shutdown_reentry()
{
    struct SelfShutdownBackend final : runtime::RuntimeTaskPreparedBackend {
        runtime::RuntimeTaskOwner* owner{};
        std::atomic<int> prepare_calls{};
        std::atomic<int> shutdown_returns{};
        std::atomic<int> execute_calls{};

        [[nodiscard]] bool prepare(std::stop_token) noexcept override
        {
            prepare_calls.fetch_add(1);
            owner->shutdown();
            shutdown_returns.fetch_add(1);
            return false;
        }

        [[nodiscard]] runtime::RuntimeTaskTerminal execute(
            const runtime::RuntimeTaskRequest&, std::stop_token,
            const runtime::RuntimeTaskProgressReporter&) noexcept override
        {
            execute_calls.fetch_add(1);
            return {false, 91};
        }
    };

    auto self_backend = std::make_shared<SelfShutdownBackend>();
    runtime::RuntimeTaskOwner self_owner{
        [](const runtime::RuntimeTaskRequest&, std::stop_token,
           const runtime::RuntimeTaskProgressReporter&) {
            return runtime::RuntimeTaskTerminal{false, 90};
        }};
    self_backend->owner = &self_owner;
    auto self_request = request("prepare-self-shutdown");
    self_request.prepared_backend = self_backend;
    const auto self_result = self_owner.prepare_start(std::move(self_request));
    check(self_result.decision
              == runtime::RuntimeTaskStartDecision::preparation_failed
              && self_backend->prepare_calls.load() == 1
              && self_backend->shutdown_returns.load() == 1
              && self_backend->execute_calls.load() == 0,
          "prepared backend can synchronously initiate shutdown without deadlock");
    check(!self_owner.snapshot("prepare-self-shutdown")
              && self_owner.snapshots().empty(),
          "failed reentrant preparation publishes no task generation");
    check(self_owner.start(request("after-prepare-shutdown")).decision
              == runtime::RuntimeTaskStartDecision::owner_stopped,
          "prepare-time shutdown permanently closes admission");
    self_owner.shutdown();

    struct CallbackShutdownBackend final : runtime::RuntimeTaskPreparedBackend {
        runtime::RuntimeTaskOwner* owner{};
        std::atomic<bool> callback_registered{};
        std::atomic<int> callback_returns{};
        std::atomic<int> prepare_returns{};
        std::atomic<int> execute_calls{};

        [[nodiscard]] bool prepare(const std::stop_token stop) noexcept override
        {
            auto stop_action = [this]() noexcept {
                owner->shutdown();
                callback_returns.fetch_add(1);
            };
            static_assert(std::is_nothrow_invocable_v<decltype(stop_action)>);
            std::stop_callback callback{stop, stop_action};
            callback_registered = true;
            // Keep the registration alive until request_stop() has actually
            // invoked the callback. Merely observing the published stop bit
            // is insufficient: the standard permits this thread to win the
            // callback-unregistration race before the requester starts it.
            while (!stop.stop_requested() || callback_returns.load() == 0)
                std::this_thread::yield();
            prepare_returns.fetch_add(1);
            return false;
        }

        [[nodiscard]] runtime::RuntimeTaskTerminal execute(
            const runtime::RuntimeTaskRequest&, std::stop_token,
            const runtime::RuntimeTaskProgressReporter&) noexcept override
        {
            execute_calls.fetch_add(1);
            return {false, 92};
        }
    };

    auto callback_backend = std::make_shared<CallbackShutdownBackend>();
    runtime::RuntimeTaskOwner callback_owner{
        [](const runtime::RuntimeTaskRequest&, std::stop_token,
           const runtime::RuntimeTaskProgressReporter&) {
            return runtime::RuntimeTaskTerminal{false, 93};
        }};
    callback_backend->owner = &callback_owner;
    std::atomic<runtime::RuntimeTaskStartDecision> callback_decision{
        runtime::RuntimeTaskStartDecision::started};
    std::thread prepare_caller{[&] {
        auto callback_request = request("prepare-stop-callback");
        callback_request.prepared_backend = callback_backend;
        callback_decision = callback_owner.prepare_start(
                                std::move(callback_request))
                                .decision;
    }};
    check(wait_until([&callback_backend] {
              return callback_backend->callback_registered.load();
          }),
          "prepared backend registers its shutdown-reentrant stop callback");
    callback_owner.shutdown();
    prepare_caller.join();
    check(callback_decision.load()
              == runtime::RuntimeTaskStartDecision::preparation_failed
              && callback_backend->callback_returns.load() == 1
              && callback_backend->prepare_returns.load() == 1
              && callback_backend->execute_calls.load() == 0,
          "prepare stop callback can reenter shutdown while external drain completes");
    check(!callback_owner.snapshot("prepare-stop-callback")
              && callback_owner.snapshots().empty(),
          "cancelled preparation releases its reserved capacity before drain returns");
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
    test_start_reservation_commit_and_rollback();
    test_start_reservation_conflict_and_thread_failure();
    test_reservation_abort_remains_owned_until_join();
    test_start_reservation_shutdown_linearization();
    test_keyed_concurrency_and_stop_start_linearization();
    test_stop_reservation_prepare_abort_commit_and_capacity();
    test_stop_reservation_natural_completion_and_shutdown_race();
    test_stop_all_reservation_gates_and_reentrant_delivery();
    test_stop_transactions_generation_exhaustion_and_stress();
    test_explicit_terminal_outcomes_and_bounded_snapshots();
    test_self_shutdown_and_reentrant_stop_callback();
    test_prepared_backend_shutdown_reentry();
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
