#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::runtime {

struct RuntimeTaskLimits {
    std::size_t max_configs{256};
    std::size_t max_config_id_bytes{256};
    std::size_t max_run_mode_bytes{64};
    std::size_t max_task_name_bytes{512};
    std::size_t max_button_bytes{64U * 1'024U};
    std::size_t max_waiting_tasks{256};
    std::size_t max_waiting_task_bytes{512};
};

struct RuntimeTaskRequest {
    std::string config_id;
    std::string run_mode;
    std::optional<std::string> current_task;
    std::vector<std::string> waiting_tasks;
};

struct RuntimeTaskProgress {
    bool is_flag_run{true};
    // Raw JSON or legacy string. Interpretation belongs to the protocol layer.
    std::optional<std::string> button;
    std::optional<std::string> current_task;
    std::vector<std::string> waiting_tasks;
};

using RuntimeTaskProgressReporter =
    std::function<bool(RuntimeTaskProgress progress)>;

// Exact terminal state supplied by the real runtime backend. An absent exit
// code is distinct from an explicit zero and both are preserved in snapshots.
struct RuntimeTaskTerminal {
    bool is_flag_run{false};
    std::optional<int> exit_code;
};

// Converts legacy bool behavior without losing the richer terminal contract:
// true -> null exit code, false -> exit code 1.
[[nodiscard]] RuntimeTaskTerminal runtime_task_terminal_from_result(
    bool succeeded, bool is_flag_run = false) noexcept;

// The backend is injected by the composition root that owns the real BAAS
// runtime. Different configs invoke it concurrently, so the implementation and
// everything it captures must be thread-safe. Exceptions are caught and
// published as {is_flag_run=false, exit_code=1}.
//
// A cooperative manual stop is observable through stop_token. Production
// backends must provide provable stop-safe points and return an intentional
// RuntimeTaskTerminal after cancellation. C++ cannot safely force-terminate an
// arbitrary worker thread, so external shutdown drains by joining cooperative
// backends. Every std::stop_callback registered by a backend must be noexcept:
// std::stop_source::request_stop() is noexcept and the standard terminates the
// process if a callback lets an exception escape, before this owner can catch it.
using RuntimeTaskBackend = std::function<RuntimeTaskTerminal(
    const RuntimeTaskRequest& request, std::stop_token stop_token,
    const RuntimeTaskProgressReporter& report_progress)>;

struct RuntimeTaskSnapshot {
    std::string config_id;
    bool running{false};
    bool stopping{false};
    bool is_flag_run{false};
    std::optional<std::string> button;
    std::optional<std::string> current_task;
    std::vector<std::string> waiting_tasks;
    std::optional<int> exit_code;
    std::optional<std::string> run_mode;
    std::uint64_t timestamp{};
};

struct RuntimeTaskStartReservationAccess;

// Move-only ownership of a fully prepared start. The worker thread and every
// allocation required by a successful start already exist, but the backend is
// gated until commit(). Destroying a pending reservation rolls it back and
// joins the gated worker without invoking the backend.
class RuntimeTaskStartReservation final {
public:
    RuntimeTaskStartReservation() noexcept = default;
    ~RuntimeTaskStartReservation() noexcept;

    RuntimeTaskStartReservation(const RuntimeTaskStartReservation&) = delete;
    RuntimeTaskStartReservation& operator=(
        const RuntimeTaskStartReservation&) = delete;
    RuntimeTaskStartReservation(RuntimeTaskStartReservation&& other) noexcept;
    RuntimeTaskStartReservation& operator=(
        RuntimeTaskStartReservation&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;

    // Exactly-once, allocation-free ownership transfer. A successful prepare
    // guarantees this operation cannot fail. If shutdown linearized first,
    // the backend still runs once with an already-requested stop token.
    void commit() noexcept;

private:
    using Action = void (*)(void*) noexcept;

    RuntimeTaskStartReservation(
        std::shared_ptr<void> state, Action commit, Action cancel) noexcept;
    void cancel() noexcept;

    std::shared_ptr<void> state_;
    Action commit_{nullptr};
    Action cancel_{nullptr};

    friend struct RuntimeTaskStartReservationAccess;
};

enum class RuntimeTaskStartDecision : std::uint8_t {
    started = 0,
    already_running = 1,
    stopping = 2,
    owner_stopped = 3,
    capacity_exceeded = 4,
    invalid_request = 5,
    thread_start_failed = 6,
    reservation_conflict = 7,
};

[[nodiscard]] std::string_view runtime_task_start_decision_name(
    RuntimeTaskStartDecision decision) noexcept;

struct RuntimeTaskStartResult {
    RuntimeTaskStartDecision decision{RuntimeTaskStartDecision::invalid_request};
    std::optional<RuntimeTaskSnapshot> snapshot;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return decision == RuntimeTaskStartDecision::started;
    }
};

struct RuntimeTaskPrepareStartResult {
    RuntimeTaskStartDecision decision{RuntimeTaskStartDecision::invalid_request};
    std::optional<RuntimeTaskSnapshot> snapshot;
    RuntimeTaskStartReservation reservation;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return decision == RuntimeTaskStartDecision::started
            && static_cast<bool>(reservation);
    }
};

enum class RuntimeTaskStopDecision : std::uint8_t {
    stop_requested,
    already_stopping,
    already_stopped,
    unknown_config,
};

[[nodiscard]] std::string_view runtime_task_stop_decision_name(
    RuntimeTaskStopDecision decision) noexcept;

struct RuntimeTaskStopResult {
    RuntimeTaskStopDecision decision{RuntimeTaskStopDecision::unknown_config};
    std::optional<RuntimeTaskSnapshot> snapshot;
};

class RuntimeTaskOwner;
struct RuntimeTaskOwnerTestAccess;

#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
struct RuntimeTaskOwnerTestAccess final {
    using Hook = void (*)(void*) noexcept;
    static void set_after_stop_linearized_hook(
        RuntimeTaskOwner& owner, Hook hook, void* context) noexcept;
    static void set_before_drain_hook(
        RuntimeTaskOwner& owner, Hook hook, void* context) noexcept;
    static void set_after_shutdown_closed_hook(
        RuntimeTaskOwner& owner, Hook hook, void* context) noexcept;
    static void set_after_reservation_cancelled_gate_hook(
        RuntimeTaskOwner& owner, Hook hook, void* context) noexcept;
    static void fail_next_thread_start(RuntimeTaskOwner& owner) noexcept;
};
#endif

// Service-lifetime owner for long-running BAAS jobs. Jobs are keyed by config,
// so a config has at most one live worker while different configs may execute
// concurrently. Trigger/WebSocket request lifetimes are deliberately absent
// from this API. All public operations are thread-safe.
class RuntimeTaskOwner final {
public:
    explicit RuntimeTaskOwner(
        RuntimeTaskBackend backend, RuntimeTaskLimits limits = {});

    // Destruction is an external-owner operation and synchronously drains all
    // workers. Destroying this object from one of its own backend callbacks is
    // a contract violation and is fail-fast enforced with std::terminate().
    ~RuntimeTaskOwner() noexcept;

    RuntimeTaskOwner(const RuntimeTaskOwner&) = delete;
    RuntimeTaskOwner& operator=(const RuntimeTaskOwner&) = delete;
    RuntimeTaskOwner(RuntimeTaskOwner&&) = delete;
    RuntimeTaskOwner& operator=(RuntimeTaskOwner&&) = delete;

    // Validates and copies the request, reserves the keyed/capacity slot, and
    // creates a worker blocked before backend entry. A successful result must
    // be committed or destroyed before external shutdown can finish draining.
    [[nodiscard]] RuntimeTaskPrepareStartResult prepare_start(
        RuntimeTaskRequest request);

    // Compatibility wrapper: prepare followed immediately by commit.
    [[nodiscard]] RuntimeTaskStartResult start(RuntimeTaskRequest request);
    [[nodiscard]] RuntimeTaskStopResult request_stop(std::string_view config_id);
    [[nodiscard]] std::optional<RuntimeTaskSnapshot> snapshot(
        std::string_view config_id) const;
    [[nodiscard]] std::vector<RuntimeTaskSnapshot> snapshots() const;

    // Waits for terminal state publication by the keyed backend, not thread
    // reaping. Unknown/invalid configs are already idle. A subsequent restart,
    // external shutdown, or destruction reaps the completed std::thread.
    [[nodiscard]] bool wait_for_idle(
        std::string_view config_id, std::chrono::milliseconds timeout) const;

    // Stops admission and requests cooperative stop for every worker. An
    // external caller additionally joins all workers before returning. A call
    // from an owned worker, or reentered synchronously from stop delivery, is
    // intentionally initiation-only so it can never self-join or mutually
    // wait; a later external shutdown/destruction performs the drain. Repeated
    // and reentrant calls are safe.
    void shutdown() noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;

    friend struct RuntimeTaskOwnerTestAccess;
};

}  // namespace baas::service::runtime
