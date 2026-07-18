#include "service/runtime/RuntimeTaskOwner.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace baas::service::runtime {
namespace {

enum class JobPhase : std::uint8_t { completed, running, stopping };
enum class StartGate : std::uint8_t { pending, committed, cancelled };

inline constexpr std::uint64_t json_safe_integer_max =
    9'007'199'254'740'991ULL;

[[nodiscard]] bool bounded_text(
    const std::string_view value, const std::size_t maximum,
    const bool allow_empty = false) noexcept
{
    return (allow_empty || !value.empty()) && value.size() <= maximum
        && value.find('\0') == std::string_view::npos;
}

[[nodiscard]] std::uint64_t unix_timestamp_ms() noexcept
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    if (elapsed.count() <= 0) return 0;
    return std::min(
        static_cast<std::uint64_t>(elapsed.count()), json_safe_integer_max);
}

[[nodiscard]] std::uint64_t next_timestamp(
    const std::uint64_t previous) noexcept
{
    const auto now = unix_timestamp_ms();
    if (now > previous) return now;
    if (previous < json_safe_integer_max) return previous + 1;
    return json_safe_integer_max;
}

}  // namespace

struct RuntimeTaskStartReservationAccess final {
    [[nodiscard]] static RuntimeTaskStartReservation create(
        std::shared_ptr<void> state,
        const RuntimeTaskStartReservation::Action commit,
        const RuntimeTaskStartReservation::Action cancel) noexcept
    {
        return RuntimeTaskStartReservation{
            std::move(state), commit, cancel};
    }
};

struct RuntimeTaskStopReservationAccess final {
    [[nodiscard]] static RuntimeTaskStopReservation create(
        std::shared_ptr<void> state,
        const RuntimeTaskStopReservation::Action commit,
        const RuntimeTaskStopReservation::Action abort) noexcept
    {
        return RuntimeTaskStopReservation{
            std::move(state), commit, abort};
    }
};

struct RuntimeTaskStopAllReservationAccess final {
    [[nodiscard]] static RuntimeTaskStopAllReservation create(
        std::shared_ptr<void> state,
        const RuntimeTaskStopAllReservation::Action commit,
        const RuntimeTaskStopAllReservation::Action abort) noexcept
    {
        return RuntimeTaskStopAllReservation{
            std::move(state), commit, abort};
    }
};

RuntimeTaskStartReservation::RuntimeTaskStartReservation(
    std::shared_ptr<void> state, const Action commit,
    const Action cancel) noexcept
    : state_(std::move(state)), commit_(commit), cancel_(cancel)
{}

RuntimeTaskStartReservation::~RuntimeTaskStartReservation() noexcept
{
    cancel();
}

RuntimeTaskStartReservation::RuntimeTaskStartReservation(
    RuntimeTaskStartReservation&& other) noexcept
    : state_(std::move(other.state_)),
      commit_(std::exchange(other.commit_, nullptr)),
      cancel_(std::exchange(other.cancel_, nullptr))
{}

RuntimeTaskStartReservation& RuntimeTaskStartReservation::operator=(
    RuntimeTaskStartReservation&& other) noexcept
{
    if (this == &other) return *this;
    cancel();
    state_ = std::move(other.state_);
    commit_ = std::exchange(other.commit_, nullptr);
    cancel_ = std::exchange(other.cancel_, nullptr);
    return *this;
}

RuntimeTaskStartReservation::operator bool() const noexcept
{
    return state_ != nullptr;
}

void RuntimeTaskStartReservation::commit() noexcept
{
    auto state = std::move(state_);
    const auto action = std::exchange(commit_, nullptr);
    cancel_ = nullptr;
    if (state && action != nullptr) action(state.get());
}

void RuntimeTaskStartReservation::cancel() noexcept
{
    auto state = std::move(state_);
    const auto action = std::exchange(cancel_, nullptr);
    commit_ = nullptr;
    if (state && action != nullptr) action(state.get());
}

RuntimeTaskStopReservation::RuntimeTaskStopReservation(
    std::shared_ptr<void> state, const Action commit,
    const Action abort) noexcept
    : state_(std::move(state)), commit_(commit), abort_(abort)
{}

RuntimeTaskStopReservation::~RuntimeTaskStopReservation() noexcept
{
    abort();
}

RuntimeTaskStopReservation::RuntimeTaskStopReservation(
    RuntimeTaskStopReservation&& other) noexcept
    : state_(std::move(other.state_)),
      commit_(std::exchange(other.commit_, nullptr)),
      abort_(std::exchange(other.abort_, nullptr))
{}

RuntimeTaskStopReservation& RuntimeTaskStopReservation::operator=(
    RuntimeTaskStopReservation&& other) noexcept
{
    if (this == &other) return *this;
    abort();
    state_ = std::move(other.state_);
    commit_ = std::exchange(other.commit_, nullptr);
    abort_ = std::exchange(other.abort_, nullptr);
    return *this;
}

RuntimeTaskStopReservation::operator bool() const noexcept
{
    return state_ != nullptr;
}

void RuntimeTaskStopReservation::commit() noexcept
{
    auto state = std::move(state_);
    const auto action = std::exchange(commit_, nullptr);
    abort_ = nullptr;
    if (state && action != nullptr) action(state.get());
}

void RuntimeTaskStopReservation::abort() noexcept
{
    auto state = std::move(state_);
    const auto action = std::exchange(abort_, nullptr);
    commit_ = nullptr;
    if (state && action != nullptr) action(state.get());
}

RuntimeTaskStopAllReservation::RuntimeTaskStopAllReservation(
    std::shared_ptr<void> state, const Action commit,
    const Action abort) noexcept
    : state_(std::move(state)), commit_(commit), abort_(abort)
{}

RuntimeTaskStopAllReservation::~RuntimeTaskStopAllReservation() noexcept
{
    abort();
}

RuntimeTaskStopAllReservation::RuntimeTaskStopAllReservation(
    RuntimeTaskStopAllReservation&& other) noexcept
    : state_(std::move(other.state_)),
      commit_(std::exchange(other.commit_, nullptr)),
      abort_(std::exchange(other.abort_, nullptr))
{}

RuntimeTaskStopAllReservation& RuntimeTaskStopAllReservation::operator=(
    RuntimeTaskStopAllReservation&& other) noexcept
{
    if (this == &other) return *this;
    abort();
    state_ = std::move(other.state_);
    commit_ = std::exchange(other.commit_, nullptr);
    abort_ = std::exchange(other.abort_, nullptr);
    return *this;
}

RuntimeTaskStopAllReservation::operator bool() const noexcept
{
    return state_ != nullptr;
}

void RuntimeTaskStopAllReservation::commit() noexcept
{
    auto state = std::move(state_);
    const auto action = std::exchange(commit_, nullptr);
    abort_ = nullptr;
    if (state && action != nullptr) action(state.get());
}

void RuntimeTaskStopAllReservation::abort() noexcept
{
    auto state = std::move(state_);
    const auto action = std::exchange(abort_, nullptr);
    commit_ = nullptr;
    if (state && action != nullptr) action(state.get());
}

RuntimeTaskTerminal runtime_task_terminal_from_result(
    const bool succeeded, const bool is_flag_run) noexcept
{
    return {is_flag_run, succeeded ? std::nullopt : std::optional<int>{1}};
}

std::string_view runtime_task_start_decision_name(
    const RuntimeTaskStartDecision decision) noexcept
{
    switch (decision) {
        case RuntimeTaskStartDecision::started: return "started";
        case RuntimeTaskStartDecision::reservation_conflict:
            return "reservation-conflict";
        case RuntimeTaskStartDecision::already_running:
            return "already-running";
        case RuntimeTaskStartDecision::stopping: return "stopping";
        case RuntimeTaskStartDecision::owner_stopped: return "owner-stopped";
        case RuntimeTaskStartDecision::capacity_exceeded:
            return "capacity-exceeded";
        case RuntimeTaskStartDecision::invalid_request:
            return "invalid-request";
        case RuntimeTaskStartDecision::thread_start_failed:
            return "thread-start-failed";
        case RuntimeTaskStartDecision::preparation_failed:
            return "preparation-failed";
    }
    return "invalid-request";
}

std::string_view runtime_task_stop_decision_name(
    const RuntimeTaskStopDecision decision) noexcept
{
    switch (decision) {
        case RuntimeTaskStopDecision::stop_requested: return "stop-requested";
        case RuntimeTaskStopDecision::already_stopping:
            return "already-stopping";
        case RuntimeTaskStopDecision::already_stopped:
            return "already-stopped";
        case RuntimeTaskStopDecision::unknown_config: return "unknown-config";
        case RuntimeTaskStopDecision::owner_stopped: return "owner-stopped";
        case RuntimeTaskStopDecision::capacity_exceeded:
            return "capacity-exceeded";
        case RuntimeTaskStopDecision::reservation_conflict:
            return "reservation-conflict";
    }
    return "unknown-config";
}

std::string_view runtime_task_stop_all_decision_name(
    const RuntimeTaskStopAllDecision decision) noexcept
{
    switch (decision) {
        case RuntimeTaskStopAllDecision::stop_requested:
            return "stop-requested";
        case RuntimeTaskStopAllDecision::nothing_to_stop:
            return "nothing-to-stop";
        case RuntimeTaskStopAllDecision::owner_stopped:
            return "owner-stopped";
        case RuntimeTaskStopAllDecision::reservation_conflict:
            return "reservation-conflict";
    }
    return "nothing-to-stop";
}

class RuntimeTaskOwner::Impl final
    : public std::enable_shared_from_this<RuntimeTaskOwner::Impl> {
private:
    static_assert(std::is_nothrow_move_constructible_v<RuntimeTaskProgress>);
    static_assert(std::is_nothrow_move_assignable_v<RuntimeTaskProgress>);

    struct Job {
        RuntimeTaskSnapshot snapshot;
        std::optional<RuntimeTaskProgress> pending_progress;
        std::uint64_t generation{};
        JobPhase phase{JobPhase::completed};
        std::stop_source stop_source;
        std::mutex gate_mutex;
        std::condition_variable gate_condition;
        StartGate gate{StartGate::pending};
        bool preparation_complete{};
        bool preparation_succeeded{};
        std::thread worker;
    };

    struct ConfigSlot {
        std::shared_ptr<Job> current;
        std::shared_ptr<Job> reserved;
        bool stop_reserved{};
    };

    struct ReservationState {
        std::shared_ptr<Impl> owner;
        std::shared_ptr<Job> job;
    };

    struct StopReservationState {
        std::shared_ptr<Impl> owner;
        std::string config_id;
        std::shared_ptr<Job> job;
        std::uint64_t generation{};
        std::uint64_t commit_timestamp{};
        bool transition_running{};
        std::stop_source source{std::nostopstate};
    };

    struct StopAllItem {
        std::shared_ptr<Job> job;
        std::uint64_t generation{};
        std::uint64_t commit_timestamp{};
        bool transition_running{};
        std::stop_source source{std::nostopstate};
        bool deliver{};
    };

    struct StopAllReservationState {
        std::shared_ptr<Impl> owner;
        std::vector<StopAllItem> items;
    };

public:
    Impl(RuntimeTaskBackend backend, RuntimeTaskLimits limits)
        : backend_(std::move(backend)), limits_(limits)
    {
        if (!backend_) throw std::invalid_argument("runtime backend is required");
        if (limits_.max_configs == 0 || limits_.max_config_id_bytes == 0
            || limits_.max_run_mode_bytes == 0
            || limits_.max_task_name_bytes == 0
            || limits_.max_button_bytes == 0
            || limits_.max_waiting_task_bytes == 0) {
            throw std::invalid_argument("runtime task limits must be non-zero");
        }
    }

    ~Impl() noexcept
    {
        // RuntimeTaskOwner's external destructor must have drained every
        // std::thread before releasing its shared ownership.
        if (has_joinable_worker()) std::terminate();
    }

    [[nodiscard]] RuntimeTaskPrepareStartResult prepare_start(
        RuntimeTaskRequest request, const bool publish_thread_failure)
    {
        if (!valid_request(request)) {
            return {
                RuntimeTaskStartDecision::invalid_request, std::nullopt, {}};
        }

        std::unique_lock lock{mutex_};
        if (!accepting_) {
            return {
                RuntimeTaskStartDecision::owner_stopped, std::nullopt, {}};
        }
        if (stop_all_reserved_) {
            return {
                RuntimeTaskStartDecision::reservation_conflict,
                std::nullopt, {}};
        }

        auto found = jobs_.find(request.config_id);
        std::shared_ptr<Job> previous;
        if (found != jobs_.end()) {
            if (found->second.reserved || found->second.stop_reserved) {
                return {
                    RuntimeTaskStartDecision::reservation_conflict,
                    std::nullopt, {}};
            }
            previous = found->second.current;
            if (previous && previous->phase == JobPhase::running) {
                return {
                    RuntimeTaskStartDecision::already_running,
                    previous->snapshot, {}};
            }
            if (previous && previous->phase == JobPhase::stopping) {
                return {
                    RuntimeTaskStartDecision::stopping, previous->snapshot, {}};
            }
            // completed is published only after the worker's final use of the
            // owner mutex, so this join cannot wait for the same lock.
            if (previous && previous->worker.joinable()) {
                previous->worker.join();
            }
        } else if (jobs_.size() >= limits_.max_configs) {
            return {
                RuntimeTaskStartDecision::capacity_exceeded, std::nullopt, {}};
        }

        // Prepare every allocation/copy needed by the successful response and
        // worker before changing registry state or starting a thread. If an
        // allocation throws, no task has been admitted.
        RuntimeTaskSnapshot next;
        next.config_id = request.config_id;
        next.running = true;
        next.is_flag_run = true;
        if (previous) next.button = previous->snapshot.button;
        next.current_task = request.current_task;
        next.waiting_tasks = request.waiting_tasks;
        next.run_mode = request.run_mode;
        next.timestamp = next_timestamp(
            previous ? previous->snapshot.timestamp : 0);

        RuntimeTaskPrepareStartResult result;
        result.decision = RuntimeTaskStartDecision::started;
        result.snapshot = next;
        if (next_job_generation_ == 0) {
            return {
                RuntimeTaskStartDecision::capacity_exceeded,
                std::nullopt, {}};
        }
        auto job = std::make_shared<Job>();
        job->generation = next_job_generation_++;
        job->snapshot = std::move(next);
        auto reservation_state = std::make_shared<ReservationState>(
            ReservationState{shared_from_this(), job});

        if (found == jobs_.end()) {
            found = jobs_.emplace(
                job->snapshot.config_id, ConfigSlot{nullptr, job}).first;
        } else {
            found->second.reserved = job;
        }
        ++active_reservations_;

        bool create_thread = true;
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
        if (fail_next_thread_start_) {
            fail_next_thread_start_ = false;
            create_thread = false;
        }
#endif
        const bool has_prepared_backend = request.prepared_backend != nullptr;
        try {
            if (!create_thread) {
                throw std::system_error{
                    std::make_error_code(std::errc::resource_unavailable_try_again)};
            }
            auto self = shared_from_this();
            job->worker = std::thread{
                [self = std::move(self), job,
                 request = std::move(request)]() mutable {
                    self->await_commit_and_run(job, request);
                }};
        } catch (...) {
            job->phase = JobPhase::completed;
            set_thread_start_failure(*job);
            found->second.reserved.reset();
            --active_reservations_;
            if (publish_thread_failure) {
                // The compatibility start() API historically publishes a
                // terminal thread-start failure. Reversible prepare_start()
                // must instead leave the prior public generation untouched.
                found->second.current = job;
            } else if (!found->second.current) {
                jobs_.erase(found);
            }
            result.decision = RuntimeTaskStartDecision::thread_start_failed;
            if (result.snapshot) {
                result.snapshot->running = false;
                result.snapshot->stopping = false;
                result.snapshot->is_flag_run = false;
                result.snapshot->current_task.reset();
                result.snapshot->waiting_tasks.clear();
                result.snapshot->exit_code = 1;
                result.snapshot->timestamp = job->snapshot.timestamp;
            }
            condition_.notify_all();
            return result;
        }
        if (has_prepared_backend) {
            lock.unlock();
            {
                std::unique_lock gate_lock{job->gate_mutex};
                job->gate_condition.wait(gate_lock, [&job] {
                    return job->preparation_complete;
                });
            }
            lock.lock();
            if (!job->preparation_succeeded) {
                const auto key = job->snapshot.config_id;
                auto failed = jobs_.find(key);
                if (failed != jobs_.end()
                    && failed->second.reserved.get() == job.get()) {
                    failed->second.reserved.reset();
                    --active_reservations_;
                    if (!failed->second.current) jobs_.erase(failed);
                }
                lock.unlock();
                if (job->worker.joinable()) job->worker.join();
                condition_.notify_all();
                result.decision = RuntimeTaskStartDecision::preparation_failed;
                result.snapshot.reset();
                return result;
            }
        }
        result.reservation = RuntimeTaskStartReservationAccess::create(
            std::move(reservation_state), &commit_reservation_action,
            &cancel_reservation_action);
        return result;
    }

    [[nodiscard]] RuntimeTaskStartResult start(RuntimeTaskRequest request)
    {
        auto prepared = prepare_start(std::move(request), true);
        RuntimeTaskStartResult result{
            prepared.decision, std::move(prepared.snapshot)};
        if (prepared) prepared.reservation.commit();
        return result;
    }

    [[nodiscard]] RuntimeTaskPrepareStopResult prepare_stop(
        const std::string_view config_id)
    {
        if (!bounded_text(config_id, limits_.max_config_id_bytes)) {
            return {RuntimeTaskStopDecision::unknown_config, std::nullopt, {}};
        }
        std::string key{config_id};
        std::unique_lock lock{mutex_};
        if (!accepting_) {
            return {RuntimeTaskStopDecision::owner_stopped, std::nullopt, {}};
        }
        if (stop_all_reserved_) {
            return {
                RuntimeTaskStopDecision::reservation_conflict,
                std::nullopt, {}};
        }

        auto found = jobs_.find(key);
        if (found != jobs_.end()
            && (found->second.reserved || found->second.stop_reserved)) {
            return {
                RuntimeTaskStopDecision::reservation_conflict,
                std::nullopt, {}};
        }
        if (found == jobs_.end() && jobs_.size() >= limits_.max_configs) {
            return {
                RuntimeTaskStopDecision::capacity_exceeded,
                std::nullopt, {}};
        }

        RuntimeTaskPrepareStopResult result;
        auto state = std::make_shared<StopReservationState>();
        state->owner = shared_from_this();
        state->config_id = key;
        if (found == jobs_.end() || !found->second.current) {
            result.decision = RuntimeTaskStopDecision::unknown_config;
        } else {
            state->job = found->second.current;
            state->generation = state->job->generation;
            result.snapshot = state->job->snapshot;
            if (state->job->phase == JobPhase::running) {
                result.decision = RuntimeTaskStopDecision::stop_requested;
                state->transition_running = true;
                result.snapshot->stopping = true;
                result.snapshot->is_flag_run = false;
                result.snapshot->timestamp =
                    next_timestamp(state->job->snapshot.timestamp);
                state->commit_timestamp = result.snapshot->timestamp;
                state->source = state->job->stop_source;
            } else if (state->job->phase == JobPhase::stopping) {
                result.decision = RuntimeTaskStopDecision::already_stopping;
                state->source = state->job->stop_source;
            } else {
                result.decision = RuntimeTaskStopDecision::already_stopped;
            }
        }

        if (found == jobs_.end()) {
            found = jobs_.emplace(key, ConfigSlot{}).first;
        }
        found->second.stop_reserved = true;
        ++active_reservations_;
        result.reservation = RuntimeTaskStopReservationAccess::create(
            std::move(state), &commit_stop_reservation_action,
            &abort_stop_reservation_action);
        return result;
    }

    [[nodiscard]] RuntimeTaskStopResult request_stop(
        const std::string_view config_id)
    {
        auto prepared = prepare_stop(config_id);
        if (prepared.decision == RuntimeTaskStopDecision::owner_stopped) {
            return request_stop_after_shutdown(config_id);
        }
        if (prepared.decision == RuntimeTaskStopDecision::capacity_exceeded) {
            return {
                RuntimeTaskStopDecision::unknown_config, std::nullopt};
        }
        RuntimeTaskStopResult result{
            prepared.decision, std::move(prepared.snapshot)};
        if (prepared) prepared.reservation.commit();
        return result;
    }

    [[nodiscard]] RuntimeTaskStopResult request_stop_after_shutdown(
        const std::string_view config_id)
    {
        std::stop_source source{std::nostopstate};
        RuntimeTaskStopResult result;
        bool deliver = false;
        {
            std::lock_guard lock{mutex_};
            if (stop_all_reserved_) {
                result.decision =
                    RuntimeTaskStopDecision::reservation_conflict;
                return result;
            }
            const auto found = jobs_.find(std::string{config_id});
            if (found == jobs_.end() || !found->second.current) {
                result.decision = RuntimeTaskStopDecision::unknown_config;
                return result;
            }
            if (found->second.stop_reserved) {
                result.decision =
                    RuntimeTaskStopDecision::reservation_conflict;
                return result;
            }
            auto& job = found->second.current;
            result.snapshot = job->snapshot;
            if (job->phase == JobPhase::running) {
                job->phase = JobPhase::stopping;
                job->snapshot.stopping = true;
                job->snapshot.is_flag_run = false;
                job->snapshot.timestamp = next_timestamp(job->snapshot.timestamp);
                result.snapshot = job->snapshot;
                result.decision = RuntimeTaskStopDecision::stop_requested;
                source = job->stop_source;
                deliver = true;
            } else if (job->phase == JobPhase::stopping) {
                result.decision = RuntimeTaskStopDecision::already_stopping;
            } else {
                result.decision = RuntimeTaskStopDecision::already_stopped;
            }
        }
        if (deliver) {
            StopDeliveryGuard delivery{this};
            static_cast<void>(source.request_stop());
        }
        return result;
    }

    [[nodiscard]] RuntimeTaskPrepareStopAllResult prepare_stop_all()
    {
        std::unique_lock lock{mutex_};
        if (!accepting_) {
            return {RuntimeTaskStopAllDecision::owner_stopped, {}, {}};
        }
        if (stop_all_reserved_ || active_reservations_ != 0) {
            return {
                RuntimeTaskStopAllDecision::reservation_conflict,
                {}, {}};
        }

        RuntimeTaskPrepareStopAllResult result;
        auto state = std::make_shared<StopAllReservationState>();
        state->owner = shared_from_this();
        state->items.reserve(jobs_.size());
        result.snapshots.reserve(jobs_.size());
        bool has_running = false;
        for (const auto& [config_id, slot] : jobs_) {
            static_cast<void>(config_id);
            if (!slot.current) continue;
            StopAllItem item;
            item.job = slot.current;
            item.generation = item.job->generation;
            if (item.job->phase != JobPhase::completed) {
                item.source = item.job->stop_source;
            }
            auto response = item.job->snapshot;
            if (item.job->phase == JobPhase::running) {
                has_running = true;
                item.transition_running = true;
                response.stopping = true;
                response.is_flag_run = false;
                response.timestamp = next_timestamp(response.timestamp);
                item.commit_timestamp = response.timestamp;
            }
            state->items.push_back(std::move(item));
            result.snapshots.push_back(std::move(response));
        }
        const auto by_job_config = [](const StopAllItem& left,
                                      const StopAllItem& right) {
            return left.job->snapshot.config_id < right.job->snapshot.config_id;
        };
        std::sort(state->items.begin(), state->items.end(), by_job_config);
        std::sort(
            result.snapshots.begin(), result.snapshots.end(),
            [](const RuntimeTaskSnapshot& left,
               const RuntimeTaskSnapshot& right) {
                return left.config_id < right.config_id;
            });
        result.decision = has_running
            ? RuntimeTaskStopAllDecision::stop_requested
            : RuntimeTaskStopAllDecision::nothing_to_stop;
        stop_all_reserved_ = true;
        ++active_reservations_;
        result.reservation = RuntimeTaskStopAllReservationAccess::create(
            std::move(state), &commit_stop_all_reservation_action,
            &abort_stop_all_reservation_action);
        return result;
    }

    [[nodiscard]] RuntimeTaskStopAllResult request_stop_all()
    {
        auto prepared = prepare_stop_all();
        RuntimeTaskStopAllResult result{
            prepared.decision, std::move(prepared.snapshots)};
        if (prepared) prepared.reservation.commit();
        return result;
    }

    [[nodiscard]] std::optional<RuntimeTaskSnapshot> snapshot(
        const std::string_view config_id) const
    {
        if (!bounded_text(config_id, limits_.max_config_id_bytes)) {
            return std::nullopt;
        }
        std::lock_guard lock{mutex_};
        const auto found = jobs_.find(std::string{config_id});
        if (found == jobs_.end()) return std::nullopt;
        if (!found->second.current) return std::nullopt;
        return found->second.current->snapshot;
    }

    [[nodiscard]] std::vector<RuntimeTaskSnapshot> snapshots() const
    {
        std::lock_guard lock{mutex_};
        std::vector<RuntimeTaskSnapshot> result;
        result.reserve(jobs_.size());
        for (const auto& [config_id, slot] : jobs_) {
            static_cast<void>(config_id);
            if (slot.current) result.push_back(slot.current->snapshot);
        }
        std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
            return left.config_id < right.config_id;
        });
        return result;
    }

    [[nodiscard]] bool wait_for_idle(
        const std::string_view config_id,
        const std::chrono::milliseconds timeout) const
    {
        if (!bounded_text(config_id, limits_.max_config_id_bytes)) return true;
        std::unique_lock lock{mutex_};
        const std::string key{config_id};
        return condition_.wait_for(lock, timeout, [this, &key] {
            const auto found = jobs_.find(key);
            return found == jobs_.end() || !found->second.current
                || found->second.current->phase == JobPhase::completed;
        });
    }

    [[nodiscard]] bool called_from_worker() const noexcept
    {
        return WorkerGuard::active(this);
    }

#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
    void set_after_stop_linearized_hook(
        const RuntimeTaskOwnerTestAccess::Hook hook, void* const context) noexcept
    {
        std::lock_guard lock{mutex_};
        after_stop_linearized_hook_ = hook;
        after_stop_linearized_context_ = context;
    }

    void set_before_drain_hook(
        const RuntimeTaskOwnerTestAccess::Hook hook, void* const context) noexcept
    {
        std::lock_guard lock{mutex_};
        before_drain_hook_ = hook;
        before_drain_context_ = context;
    }

    void set_after_shutdown_closed_hook(
        const RuntimeTaskOwnerTestAccess::Hook hook, void* const context) noexcept
    {
        std::lock_guard lock{mutex_};
        after_shutdown_closed_hook_ = hook;
        after_shutdown_closed_context_ = context;
    }

    void set_after_reservation_cancelled_gate_hook(
        const RuntimeTaskOwnerTestAccess::Hook hook, void* const context) noexcept
    {
        std::lock_guard lock{mutex_};
        after_reservation_cancelled_gate_hook_ = hook;
        after_reservation_cancelled_gate_context_ = context;
    }

    void fail_next_thread_start() noexcept
    {
        std::lock_guard lock{mutex_};
        fail_next_thread_start_ = true;
    }

    void exhaust_generation_after_next_start() noexcept
    {
        std::lock_guard lock{mutex_};
        next_job_generation_ = std::numeric_limits<std::uint64_t>::max();
    }
#endif

    void shutdown() noexcept
    {
        ShutdownGuard recursion{this};
        if (recursion.reentrant()) return;

        initiate_shutdown();
        if (called_from_worker() || StopDeliveryGuard::active(this)) return;

        // Only external callers drain. Serialization prevents two callers from
        // concurrently joining the same std::thread; the recursion guard keeps
        // synchronous stop callbacks from trying to acquire this mutex again.
        std::lock_guard drain_lock{drain_mutex_};
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
        RuntimeTaskOwnerTestAccess::Hook before_drain_hook = nullptr;
        void* before_drain_context = nullptr;
        {
            std::lock_guard lock{mutex_};
            before_drain_hook = before_drain_hook_;
            before_drain_context = before_drain_context_;
        }
        if (before_drain_hook != nullptr) {
            before_drain_hook(before_drain_context);
        }
#endif
        wait_for_reservations();
        drain_workers();
    }

private:
    class ShutdownGuard final {
    public:
        explicit ShutdownGuard(Impl* owner) noexcept
            : owner_(owner), previous_(top_)
        {
            for (auto* current = top_; current != nullptr;
                 current = current->previous_) {
                if (current->owner_ == owner_) {
                    reentrant_ = true;
                    return;
                }
            }
            top_ = this;
        }

        ~ShutdownGuard()
        {
            if (!reentrant_) top_ = previous_;
        }

        [[nodiscard]] bool reentrant() const noexcept { return reentrant_; }

    private:
        Impl* owner_;
        ShutdownGuard* previous_;
        bool reentrant_{false};
        inline static thread_local ShutdownGuard* top_{nullptr};
    };

    // A stop_callback may destroy itself on its backend worker and wait for a
    // callback currently executing on the requesting thread. A shutdown
    // reentered from that callback must never join the worker, or both threads
    // would wait on each other. The outer stop delivery remains responsible for
    // returning; a later ordinary external shutdown performs the drain.
    class StopDeliveryGuard final {
    public:
        explicit StopDeliveryGuard(Impl* owner) noexcept
            : owner_(owner), previous_(top_)
        {
            top_ = this;
        }

        ~StopDeliveryGuard() { top_ = previous_; }

        [[nodiscard]] static bool active(const Impl* owner) noexcept
        {
            for (auto* current = top_; current != nullptr;
                 current = current->previous_) {
                if (current->owner_ == owner) return true;
            }
            return false;
        }

    private:
        Impl* owner_;
        StopDeliveryGuard* previous_;
        inline static thread_local StopDeliveryGuard* top_{nullptr};
    };

    // Worker identity is execution context, not mutable std::thread state.
    // TLS avoids racing external join() with joinable()/get_id() reads.
    class WorkerGuard final {
    public:
        explicit WorkerGuard(Impl* owner) noexcept
            : owner_(owner), previous_(top_)
        {
            top_ = this;
        }

        ~WorkerGuard() { top_ = previous_; }

        [[nodiscard]] static bool active(const Impl* owner) noexcept
        {
            for (auto* current = top_; current != nullptr;
                 current = current->previous_) {
                if (current->owner_ == owner) return true;
            }
            return false;
        }

    private:
        Impl* owner_;
        WorkerGuard* previous_;
        inline static thread_local WorkerGuard* top_{nullptr};
    };

    [[nodiscard]] bool valid_request(const RuntimeTaskRequest& request) const
        noexcept
    {
        if (!bounded_text(request.config_id, limits_.max_config_id_bytes)
            || !bounded_text(request.run_mode, limits_.max_run_mode_bytes)
            || request.waiting_tasks.size() > limits_.max_waiting_tasks) {
            return false;
        }
        if (request.current_task
            && !bounded_text(*request.current_task, limits_.max_task_name_bytes)) {
            return false;
        }
        return std::all_of(
            request.waiting_tasks.begin(), request.waiting_tasks.end(),
            [this](const std::string& task) {
                return bounded_text(task, limits_.max_waiting_task_bytes);
            });
    }

    [[nodiscard]] bool valid_progress(const RuntimeTaskProgress& progress) const
        noexcept
    {
        if (progress.waiting_tasks.size() > limits_.max_waiting_tasks) {
            return false;
        }
        if (progress.current_task
            && !bounded_text(*progress.current_task, limits_.max_task_name_bytes)) {
            return false;
        }
        if (progress.button
            && !bounded_text(
                *progress.button, limits_.max_button_bytes, true)) {
            return false;
        }
        return std::all_of(
            progress.waiting_tasks.begin(), progress.waiting_tasks.end(),
            [this](const std::string& task) {
                return bounded_text(task, limits_.max_waiting_task_bytes);
            });
    }

    void set_thread_start_failure(Job& job) noexcept
    {
        job.snapshot.running = false;
        job.snapshot.stopping = false;
        job.snapshot.is_flag_run = false;
        job.snapshot.current_task.reset();
        job.snapshot.waiting_tasks.clear();
        job.snapshot.exit_code = 1;
        job.snapshot.timestamp = next_timestamp(job.snapshot.timestamp);
    }

    static void commit_reservation_action(void* const opaque) noexcept
    {
        auto& state = *static_cast<ReservationState*>(opaque);
        state.owner->commit_reservation(state.job);
    }

    static void cancel_reservation_action(void* const opaque) noexcept
    {
        auto& state = *static_cast<ReservationState*>(opaque);
        state.owner->cancel_reservation(state.job);
    }

    static void commit_stop_reservation_action(void* const opaque) noexcept
    {
        auto& state = *static_cast<StopReservationState*>(opaque);
        state.owner->commit_stop_reservation(state);
    }

    static void abort_stop_reservation_action(void* const opaque) noexcept
    {
        auto& state = *static_cast<StopReservationState*>(opaque);
        state.owner->abort_stop_reservation(state);
    }

    static void commit_stop_all_reservation_action(void* const opaque) noexcept
    {
        auto& state = *static_cast<StopAllReservationState*>(opaque);
        state.owner->commit_stop_all_reservation(state);
    }

    static void abort_stop_all_reservation_action(void* const opaque) noexcept
    {
        auto& state = *static_cast<StopAllReservationState*>(opaque);
        state.owner->abort_stop_all_reservation(state);
    }

    void resolve_stop_gate_locked(const std::string& config_id) noexcept
    {
        const auto found = jobs_.find(config_id);
        if (found == jobs_.end() || !found->second.stop_reserved
            || active_reservations_ == 0) {
            std::terminate();
        }
        found->second.stop_reserved = false;
        if (!found->second.current && !found->second.reserved) {
            jobs_.erase(found);
        }
        --active_reservations_;
        condition_.notify_all();
    }

    static void discard_pending_progress(Job& job) noexcept
    {
        job.pending_progress.reset();
    }

    static void publish_pending_progress(Job& job) noexcept
    {
        if (!job.pending_progress) return;
        auto& progress = *job.pending_progress;
        job.snapshot.is_flag_run = job.phase == JobPhase::stopping
            ? false
            : progress.is_flag_run;
        job.snapshot.button = std::move(progress.button);
        job.snapshot.current_task = std::move(progress.current_task);
        job.snapshot.waiting_tasks = std::move(progress.waiting_tasks);
        job.snapshot.timestamp = next_timestamp(job.snapshot.timestamp);
        job.pending_progress.reset();
    }

    void commit_stop_reservation(StopReservationState& state) noexcept
    {
        bool deliver = false;
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
        RuntimeTaskOwnerTestAccess::Hook after_stop_hook = nullptr;
        void* after_stop_context = nullptr;
#endif
        {
            std::lock_guard lock{mutex_};
            const auto found = jobs_.find(state.config_id);
            if (found == jobs_.end() || !found->second.stop_reserved) {
                std::terminate();
            }
            const bool same_generation = state.job
                && found->second.current.get() == state.job.get()
                && state.job->generation == state.generation;
            if (same_generation && state.transition_running
                && state.job->phase == JobPhase::running) {
                state.job->phase = JobPhase::stopping;
                state.job->snapshot.stopping = true;
                state.job->snapshot.is_flag_run = false;
                state.job->snapshot.timestamp = state.commit_timestamp;
                deliver = true;
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
                after_stop_hook = after_stop_linearized_hook_;
                after_stop_context = after_stop_linearized_context_;
#endif
            }
            if (same_generation && !state.transition_running
                && state.job->phase == JobPhase::stopping) {
                publish_pending_progress(*state.job);
            } else if (state.job) {
                discard_pending_progress(*state.job);
            }
            resolve_stop_gate_locked(state.config_id);
        }
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
        if (deliver && after_stop_hook != nullptr) {
            after_stop_hook(after_stop_context);
        }
#endif
        if (deliver) {
            StopDeliveryGuard delivery{this};
            static_cast<void>(state.source.request_stop());
        }
    }

    void abort_stop_reservation(StopReservationState& state) noexcept
    {
        std::lock_guard lock{mutex_};
        const auto found = jobs_.find(state.config_id);
        if (state.job && found != jobs_.end()
            && found->second.current.get() == state.job.get()
            && state.job->generation == state.generation
            && state.job->phase != JobPhase::completed) {
            publish_pending_progress(*state.job);
        } else if (state.job) {
            discard_pending_progress(*state.job);
        }
        resolve_stop_gate_locked(state.config_id);
    }

    void commit_stop_all_reservation(
        StopAllReservationState& state) noexcept
    {
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
        bool delivered_any = false;
        RuntimeTaskOwnerTestAccess::Hook after_stop_hook = nullptr;
        void* after_stop_context = nullptr;
#endif
        {
            std::lock_guard lock{mutex_};
            if (!stop_all_reserved_ || active_reservations_ == 0) {
                std::terminate();
            }
            for (auto& item : state.items) {
                const auto found = jobs_.find(item.job->snapshot.config_id);
                const bool same_generation = found != jobs_.end()
                    && found->second.current.get() == item.job.get()
                    && item.job->generation == item.generation;
                if (!same_generation) {
                    discard_pending_progress(*item.job);
                    continue;
                }
                if (!item.transition_running) {
                    if (item.job->phase == JobPhase::stopping) {
                        publish_pending_progress(*item.job);
                    } else {
                        discard_pending_progress(*item.job);
                    }
                    continue;
                }
                if (item.job->phase != JobPhase::running) {
                    discard_pending_progress(*item.job);
                    continue;
                }
                discard_pending_progress(*item.job);
                item.job->phase = JobPhase::stopping;
                item.job->snapshot.stopping = true;
                item.job->snapshot.is_flag_run = false;
                item.job->snapshot.timestamp = item.commit_timestamp;
                item.deliver = true;
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
                delivered_any = true;
#endif
            }
            stop_all_reserved_ = false;
            --active_reservations_;
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
            if (delivered_any) {
                after_stop_hook = after_stop_linearized_hook_;
                after_stop_context = after_stop_linearized_context_;
            }
#endif
            condition_.notify_all();
        }
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
        if (after_stop_hook != nullptr) after_stop_hook(after_stop_context);
#endif
        StopDeliveryGuard delivery{this};
        for (auto& item : state.items) {
            if (item.deliver) {
                static_cast<void>(item.source.request_stop());
            }
        }
    }

    void abort_stop_all_reservation(
        StopAllReservationState& state) noexcept
    {
        std::lock_guard lock{mutex_};
        if (!stop_all_reserved_ || active_reservations_ == 0) {
            std::terminate();
        }
        for (auto& item : state.items) {
            const auto found = jobs_.find(item.job->snapshot.config_id);
            if (found != jobs_.end()
                && found->second.current.get() == item.job.get()
                && item.job->generation == item.generation
                && item.job->phase != JobPhase::completed) {
                publish_pending_progress(*item.job);
            } else {
                discard_pending_progress(*item.job);
            }
        }
        stop_all_reserved_ = false;
        --active_reservations_;
        condition_.notify_all();
    }

    void commit_reservation(const std::shared_ptr<Job>& job) noexcept
    {
        bool stop_before_release = false;
        {
            std::lock_guard lock{mutex_};
            const auto found = jobs_.find(job->snapshot.config_id);
            if (found == jobs_.end()
                || found->second.reserved.get() != job.get()) {
                // A live reservation has exactly one action. Missing identity
                // here indicates an internal ownership violation; silently
                // dropping a protocol-claimed start would be worse.
                std::terminate();
            }
            found->second.current = job;
            found->second.reserved.reset();
            if (active_reservations_ == 0) std::terminate();
            --active_reservations_;
            job->phase = accepting_ ? JobPhase::running : JobPhase::stopping;
            if (!accepting_) {
                job->snapshot.stopping = true;
                job->snapshot.is_flag_run = false;
                job->snapshot.timestamp = next_timestamp(job->snapshot.timestamp);
                stop_before_release = true;
            }
            condition_.notify_all();
        }

        // When shutdown won the owner-mutex race, request stop while the gate
        // still makes backend entry impossible. The committed backend then
        // observes an already-stopped token and still executes exactly once.
        if (stop_before_release) {
            StopDeliveryGuard delivery{this};
            static_cast<void>(job->stop_source.request_stop());
        }
        {
            std::lock_guard gate_lock{job->gate_mutex};
            if (job->gate != StartGate::pending) std::terminate();
            job->gate = StartGate::committed;
        }
        job->gate_condition.notify_one();
    }

    void cancel_reservation(const std::shared_ptr<Job>& job) noexcept
    {
        {
            std::lock_guard lock{mutex_};
            const auto found = jobs_.find(job->snapshot.config_id);
            if (found == jobs_.end()
                || found->second.reserved.get() != job.get()) {
                std::terminate();
            }
        }
        {
            std::lock_guard gate_lock{job->gate_mutex};
            if (job->gate != StartGate::pending) std::terminate();
            job->gate = StartGate::cancelled;
        }
        job->gate_condition.notify_one();
        if (job->worker.joinable()) job->worker.join();
        {
            std::lock_guard lock{mutex_};
            const auto found = jobs_.find(job->snapshot.config_id);
            if (found == jobs_.end()
                || found->second.reserved.get() != job.get()) {
                std::terminate();
            }
            found->second.reserved.reset();
            if (!found->second.current) jobs_.erase(found);
            if (active_reservations_ == 0) std::terminate();
            --active_reservations_;
            condition_.notify_all();
        }
    }

    [[nodiscard]] static bool report(
        const std::weak_ptr<Impl>& weak_owner,
        const std::weak_ptr<Job>& weak_job, RuntimeTaskProgress progress)
    {
        const auto owner = weak_owner.lock();
        const auto job = weak_job.lock();
        if (!owner || !job || !owner->valid_progress(progress)) return false;

        std::lock_guard lock{owner->mutex_};
        if (job->phase == JobPhase::completed) return false;
        const auto found = owner->jobs_.find(job->snapshot.config_id);
        if (found != owner->jobs_.end()
            && found->second.current.get() == job.get()
            && (found->second.stop_reserved || owner->stop_all_reserved_)) {
            // Move-only latest-value staging performs no owner-side allocation,
            // never blocks the backend, and preserves the prepared reply as
            // the next public ordered snapshot.
            job->pending_progress = std::move(progress);
            return true;
        }
        job->snapshot.is_flag_run = job->phase == JobPhase::stopping
            ? false
            : progress.is_flag_run;
        job->snapshot.button = std::move(progress.button);
        job->snapshot.current_task = std::move(progress.current_task);
        job->snapshot.waiting_tasks = std::move(progress.waiting_tasks);
        job->snapshot.timestamp = next_timestamp(job->snapshot.timestamp);
        return true;
    }

    void await_commit_and_run(
        const std::shared_ptr<Job>& job,
        const RuntimeTaskRequest& request) noexcept
    {
        // Preparation is part of the owned worker lifetime. A prepared
        // backend may synchronously reenter shutdown(), including through a
        // stop callback, and must therefore receive the same initiation-only
        // self-shutdown semantics as execute().
        WorkerGuard worker_context{this};
        const bool prepared = !request.prepared_backend
            || request.prepared_backend->prepare(
                job->stop_source.get_token());
        {
            std::unique_lock gate_lock{job->gate_mutex};
            job->preparation_succeeded = prepared;
            job->preparation_complete = true;
            job->gate_condition.notify_all();
            if (!prepared) return;
            job->gate_condition.wait(gate_lock, [&job] {
                return job->gate != StartGate::pending;
            });
            if (job->gate == StartGate::cancelled) {
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
                RuntimeTaskOwnerTestAccess::Hook hook = nullptr;
                void* context = nullptr;
                {
                    std::lock_guard lock{mutex_};
                    hook = after_reservation_cancelled_gate_hook_;
                    context = after_reservation_cancelled_gate_context_;
                }
                if (hook != nullptr) hook(context);
#endif
                return;
            }
        }
        run(job, request);
    }

    void run(
        const std::shared_ptr<Job>& job,
        const RuntimeTaskRequest& request) noexcept
    {
        RuntimeTaskTerminal terminal{false, 1};
        try {
            const RuntimeTaskProgressReporter reporter =
                [weak_owner = weak_from_this(),
                 weak_job = std::weak_ptr<Job>{job}](
                    RuntimeTaskProgress progress) {
                    return report(
                        weak_owner, weak_job, std::move(progress));
                };
            if (request.prepared_backend) {
                terminal = request.prepared_backend->execute(
                    request, job->stop_source.get_token(), reporter);
            } else {
                terminal = backend_(
                    request, job->stop_source.get_token(), reporter);
            }
        } catch (...) {
            terminal = {false, 1};
        }

        std::lock_guard lock{mutex_};
        discard_pending_progress(*job);
        job->phase = JobPhase::completed;
        job->snapshot.running = false;
        job->snapshot.stopping = false;
        job->snapshot.is_flag_run = terminal.is_flag_run;
        job->snapshot.current_task.reset();
        job->snapshot.waiting_tasks.clear();
        job->snapshot.exit_code = terminal.exit_code;
        job->snapshot.timestamp = next_timestamp(job->snapshot.timestamp);
        condition_.notify_all();
    }

    // Allocation-free stop iteration for the noexcept shutdown path. A copied
    // stop_source shares existing state and does not allocate.
    void initiate_shutdown() noexcept
    {
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
        RuntimeTaskOwnerTestAccess::Hook after_closed_hook = nullptr;
        void* after_closed_context = nullptr;
#endif
        {
            std::lock_guard lock{mutex_};
            if (accepting_) {
                accepting_ = false;
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
                after_closed_hook = after_shutdown_closed_hook_;
                after_closed_context = after_shutdown_closed_context_;
#endif
            }
        }
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
        if (after_closed_hook != nullptr) {
            after_closed_hook(after_closed_context);
        }
#endif
        for (;;) {
            std::stop_source source{std::nostopstate};
            bool found_source = false;
            {
                std::lock_guard lock{mutex_};
                for (auto& [config_id, slot] : jobs_) {
                    static_cast<void>(config_id);
                    auto& job = slot.current;
                    if (job && job->phase == JobPhase::running) {
                        discard_pending_progress(*job);
                        job->phase = JobPhase::stopping;
                        job->snapshot.stopping = true;
                        job->snapshot.is_flag_run = false;
                        job->snapshot.timestamp =
                            next_timestamp(job->snapshot.timestamp);
                        source = job->stop_source;
                        found_source = true;
                        break;
                    }
                    if (job && job->phase == JobPhase::stopping
                        && !job->stop_source.stop_requested()) {
                        discard_pending_progress(*job);
                        source = job->stop_source;
                        found_source = true;
                        break;
                    }
                    if (slot.reserved
                        && !slot.reserved->stop_source.stop_requested()) {
                        source = slot.reserved->stop_source;
                        found_source = true;
                        break;
                    }
                }
            }
            if (!found_source) return;
            // Synchronous stop callbacks run with no owner/drain mutex held.
            StopDeliveryGuard delivery{this};
            static_cast<void>(source.request_stop());
        }
    }

    void wait_for_reservations() noexcept
    {
        std::unique_lock lock{mutex_};
        condition_.wait(lock, [this] {
            return active_reservations_ == 0;
        });
    }

    // Allocation-free: shared_ptr copies only increment an existing control
    // block. Admission is already closed, so the set of worker threads cannot
    // grow while this loop drains it.
    void drain_workers() noexcept
    {
        for (;;) {
            std::shared_ptr<Job> candidate;
            {
                std::lock_guard lock{mutex_};
                for (const auto& [config_id, slot] : jobs_) {
                    static_cast<void>(config_id);
                    const auto& job = slot.current;
                    if (!job) continue;
                    if (job->worker.joinable()) {
                        candidate = job;
                        break;
                    }
                }
            }
            if (!candidate) return;
            // called_from_worker() is checked before entering the drain and
            // drain_mutex_ gives this caller exclusive join ownership.
            candidate->worker.join();
        }
    }

    [[nodiscard]] bool has_joinable_worker() const noexcept
    {
        std::lock_guard lock{mutex_};
        return std::any_of(
            jobs_.begin(), jobs_.end(), [](const auto& entry) {
                const auto& slot = entry.second;
                return (slot.current && slot.current->worker.joinable())
                    || (slot.reserved && slot.reserved->worker.joinable());
            });
    }

    RuntimeTaskBackend backend_;
    RuntimeTaskLimits limits_;
    mutable std::mutex mutex_;
    mutable std::condition_variable condition_;
    std::mutex drain_mutex_;
    std::unordered_map<std::string, ConfigSlot> jobs_;
    bool accepting_{true};
    bool stop_all_reserved_{false};
    std::size_t active_reservations_{};
    std::uint64_t next_job_generation_{1};
#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
    RuntimeTaskOwnerTestAccess::Hook after_stop_linearized_hook_{nullptr};
    void* after_stop_linearized_context_{nullptr};
    RuntimeTaskOwnerTestAccess::Hook before_drain_hook_{nullptr};
    void* before_drain_context_{nullptr};
    RuntimeTaskOwnerTestAccess::Hook after_shutdown_closed_hook_{nullptr};
    void* after_shutdown_closed_context_{nullptr};
    RuntimeTaskOwnerTestAccess::Hook
        after_reservation_cancelled_gate_hook_{nullptr};
    void* after_reservation_cancelled_gate_context_{nullptr};
    bool fail_next_thread_start_{false};
#endif
};

RuntimeTaskOwner::RuntimeTaskOwner(
    RuntimeTaskBackend backend, RuntimeTaskLimits limits)
    : impl_(std::make_shared<Impl>(std::move(backend), limits))
{}

RuntimeTaskOwner::~RuntimeTaskOwner() noexcept
{
    if (!impl_) return;
    if (impl_->called_from_worker()) std::terminate();
    impl_->shutdown();
    impl_.reset();
}

RuntimeTaskPrepareStartResult RuntimeTaskOwner::prepare_start(
    RuntimeTaskRequest request)
{
    return impl_->prepare_start(std::move(request), false);
}

RuntimeTaskStartResult RuntimeTaskOwner::start(RuntimeTaskRequest request)
{
    return impl_->start(std::move(request));
}

RuntimeTaskPrepareStopResult RuntimeTaskOwner::prepare_stop(
    const std::string_view config_id)
{
    return impl_->prepare_stop(config_id);
}

RuntimeTaskStopResult RuntimeTaskOwner::request_stop(
    const std::string_view config_id)
{
    return impl_->request_stop(config_id);
}

RuntimeTaskPrepareStopAllResult RuntimeTaskOwner::prepare_stop_all()
{
    return impl_->prepare_stop_all();
}

RuntimeTaskStopAllResult RuntimeTaskOwner::request_stop_all()
{
    return impl_->request_stop_all();
}

std::optional<RuntimeTaskSnapshot> RuntimeTaskOwner::snapshot(
    const std::string_view config_id) const
{
    return impl_->snapshot(config_id);
}

std::vector<RuntimeTaskSnapshot> RuntimeTaskOwner::snapshots() const
{
    return impl_->snapshots();
}

bool RuntimeTaskOwner::wait_for_idle(
    const std::string_view config_id,
    const std::chrono::milliseconds timeout) const
{
    return impl_->wait_for_idle(config_id, timeout);
}

void RuntimeTaskOwner::shutdown() noexcept { impl_->shutdown(); }

#if defined(BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS)
void RuntimeTaskOwnerTestAccess::set_after_stop_linearized_hook(
    RuntimeTaskOwner& owner, const Hook hook, void* const context) noexcept
{
    owner.impl_->set_after_stop_linearized_hook(hook, context);
}

void RuntimeTaskOwnerTestAccess::set_before_drain_hook(
    RuntimeTaskOwner& owner, const Hook hook, void* const context) noexcept
{
    owner.impl_->set_before_drain_hook(hook, context);
}

void RuntimeTaskOwnerTestAccess::set_after_shutdown_closed_hook(
    RuntimeTaskOwner& owner, const Hook hook, void* const context) noexcept
{
    owner.impl_->set_after_shutdown_closed_hook(hook, context);
}

void RuntimeTaskOwnerTestAccess::set_after_reservation_cancelled_gate_hook(
    RuntimeTaskOwner& owner, const Hook hook, void* const context) noexcept
{
    owner.impl_->set_after_reservation_cancelled_gate_hook(hook, context);
}

void RuntimeTaskOwnerTestAccess::fail_next_thread_start(
    RuntimeTaskOwner& owner) noexcept
{
    owner.impl_->fail_next_thread_start();
}

void RuntimeTaskOwnerTestAccess::exhaust_generation_after_next_start(
    RuntimeTaskOwner& owner) noexcept
{
    owner.impl_->exhaust_generation_after_next_start();
}
#endif

}  // namespace baas::service::runtime
