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

class RuntimeTaskPreparedBackend;

struct RuntimeTaskRequest {
    std::string config_id;
    std::string run_mode;
    std::optional<std::string> current_task;
    std::vector<std::string> waiting_tasks;
    // Optional request-local backend prepared by the protocol composition
    // before its irreversible success claim. RuntimeTaskOwner retains this
    // exact object with the gated worker and never performs provider/catalog
    // discovery after commit.
    std::shared_ptr<RuntimeTaskPreparedBackend> prepared_backend;
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
// While a prepared keyed/global stop owns the next ordered snapshot, a valid
// report is accepted into move-only latest-value staging without blocking.
using RuntimeTaskBackend = std::function<RuntimeTaskTerminal(
    const RuntimeTaskRequest& request, std::stop_token stop_token,
    const RuntimeTaskProgressReporter& report_progress)>;

class RuntimeTaskPreparedBackend {
public:
    virtual ~RuntimeTaskPreparedBackend() = default;
    // Runs on the gated worker thread. RuntimeTaskOwner waits for completion
    // before returning a successful reservation, so the protocol still has
    // not claimed success. This preserves thread-affine evaluator ownership.
    [[nodiscard]] virtual bool prepare(std::stop_token) noexcept { return true; }
    [[nodiscard]] virtual RuntimeTaskTerminal execute(
        const RuntimeTaskRequest& request,
        std::stop_token stop_token,
        const RuntimeTaskProgressReporter& report_progress) noexcept = 0;
};

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
    preparation_failed = 8,
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
    stop_requested = 0,
    already_stopping = 1,
    already_stopped = 2,
    unknown_config = 3,
    owner_stopped = 4,
    capacity_exceeded = 5,
    reservation_conflict = 6,
};

[[nodiscard]] std::string_view runtime_task_stop_decision_name(
    RuntimeTaskStopDecision decision) noexcept;

struct RuntimeTaskStopResult {
    RuntimeTaskStopDecision decision{RuntimeTaskStopDecision::unknown_config};
    std::optional<RuntimeTaskSnapshot> snapshot;
};

struct RuntimeTaskStopReservationAccess;

// Move-only ownership of one prepared keyed stop, including unknown and
// already-completed no-op decisions. Preparation owns the per-config operation
// gate and every response/source copy. Destruction aborts without changing the
// public task phase or requesting stop.
class RuntimeTaskStopReservation final {
public:
    RuntimeTaskStopReservation() noexcept = default;
    ~RuntimeTaskStopReservation() noexcept;

    RuntimeTaskStopReservation(const RuntimeTaskStopReservation&) = delete;
    RuntimeTaskStopReservation& operator=(
        const RuntimeTaskStopReservation&) = delete;
    RuntimeTaskStopReservation(RuntimeTaskStopReservation&& other) noexcept;
    RuntimeTaskStopReservation& operator=(
        RuntimeTaskStopReservation&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    void commit() noexcept;

private:
    using Action = void (*)(void*) noexcept;
    RuntimeTaskStopReservation(
        std::shared_ptr<void> state, Action commit, Action abort) noexcept;
    void abort() noexcept;

    std::shared_ptr<void> state_;
    Action commit_{nullptr};
    Action abort_{nullptr};

    friend struct RuntimeTaskStopReservationAccess;
};

struct RuntimeTaskPrepareStopResult {
    RuntimeTaskStopDecision decision{RuntimeTaskStopDecision::unknown_config};
    std::optional<RuntimeTaskSnapshot> snapshot;
    RuntimeTaskStopReservation reservation;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(reservation);
    }
};

enum class RuntimeTaskStopAllDecision : std::uint8_t {
    stop_requested = 0,
    nothing_to_stop = 1,
    owner_stopped = 2,
    reservation_conflict = 3,
};

[[nodiscard]] std::string_view runtime_task_stop_all_decision_name(
    RuntimeTaskStopAllDecision decision) noexcept;

struct RuntimeTaskStopAllResult {
    RuntimeTaskStopAllDecision decision{
        RuntimeTaskStopAllDecision::nothing_to_stop};
    std::vector<RuntimeTaskSnapshot> snapshots;
};

struct RuntimeTaskStopAllReservationAccess;

// Global counterpart of RuntimeTaskStopReservation. Even an empty prepared
// stop-all owns the global operation gate until commit or destruction.
class RuntimeTaskStopAllReservation final {
public:
    RuntimeTaskStopAllReservation() noexcept = default;
    ~RuntimeTaskStopAllReservation() noexcept;

    RuntimeTaskStopAllReservation(const RuntimeTaskStopAllReservation&) = delete;
    RuntimeTaskStopAllReservation& operator=(
        const RuntimeTaskStopAllReservation&) = delete;
    RuntimeTaskStopAllReservation(
        RuntimeTaskStopAllReservation&& other) noexcept;
    RuntimeTaskStopAllReservation& operator=(
        RuntimeTaskStopAllReservation&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    void commit() noexcept;

private:
    using Action = void (*)(void*) noexcept;
    RuntimeTaskStopAllReservation(
        std::shared_ptr<void> state, Action commit, Action abort) noexcept;
    void abort() noexcept;

    std::shared_ptr<void> state_;
    Action commit_{nullptr};
    Action abort_{nullptr};

    friend struct RuntimeTaskStopAllReservationAccess;
};

struct RuntimeTaskPrepareStopAllResult {
    RuntimeTaskStopAllDecision decision{
        RuntimeTaskStopAllDecision::nothing_to_stop};
    std::vector<RuntimeTaskSnapshot> snapshots;
    RuntimeTaskStopAllReservation reservation;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(reservation);
    }
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
    static void exhaust_generation_after_next_start(
        RuntimeTaskOwner& owner) noexcept;
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
    // creates a worker blocked before execution. When prepared_backend is
    // present, its fallible prepare() runs on that worker and completes before
    // this call returns. A successful result must be committed or destroyed
    // before external shutdown can finish draining.
    [[nodiscard]] RuntimeTaskPrepareStartResult prepare_start(
        RuntimeTaskRequest request);

    // Compatibility wrapper: prepare followed immediately by commit.
    [[nodiscard]] RuntimeTaskStartResult start(RuntimeTaskRequest request);

    // Reserves one config operation without publishing stopping or requesting
    // cancellation. Unknown and completed configs also return a keyed no-op
    // reservation so the protocol layer can make its reply/commit atomic.
    [[nodiscard]] RuntimeTaskPrepareStopResult prepare_stop(
        std::string_view config_id);

    // Compatibility wrapper: prepare followed immediately by commit. After
    // shutdown it preserves the legacy already-stopping/already-stopped view.
    [[nodiscard]] RuntimeTaskStopResult request_stop(std::string_view config_id);

    // Captures every current generation and a sorted response under one global
    // operation gate. Even an empty result must be resolved.
    [[nodiscard]] RuntimeTaskPrepareStopAllResult prepare_stop_all();
    [[nodiscard]] RuntimeTaskStopAllResult request_stop_all();
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
