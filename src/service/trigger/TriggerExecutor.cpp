#include "service/trigger/TriggerExecutor.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace baas::service::trigger {
namespace {

thread_local const void* current_trigger_executor = nullptr;
thread_local bool invoking_trigger_stop_callbacks = false;

struct TriggerOwnerScanFrame final {
    const void* state{};
    TriggerOwnerScanFrame* prior{};
};

thread_local TriggerOwnerScanFrame* current_trigger_owner_scan = nullptr;

[[nodiscard]] bool trigger_owner_scan_contains(const void* state) noexcept
{
    for (auto* frame = current_trigger_owner_scan;
         frame != nullptr; frame = frame->prior) {
        if (frame->state == state) return true;
    }
    return false;
}

struct TriggerOwnerScanGuard final {
    TriggerOwnerScanFrame frame;

    explicit TriggerOwnerScanGuard(const void* state) noexcept
        : frame{state, current_trigger_owner_scan}
    {
        current_trigger_owner_scan = &frame;
    }

    ~TriggerOwnerScanGuard()
    {
        current_trigger_owner_scan = frame.prior;
    }
};

[[nodiscard]] bool request_stop_outside_locks(
    std::stop_source& source) noexcept
{
    const bool prior = invoking_trigger_stop_callbacks;
    invoking_trigger_stop_callbacks = true;
    const bool requested = source.request_stop();
    invoking_trigger_stop_callbacks = prior;
    return requested;
}

enum class SlotState : std::uint8_t {
    free,
    reserved,
    queued,
    running,
    completed,
};

[[nodiscard]] bool retryable(const TriggerResponseResult& result) noexcept
{
    return result.disposition
        == TriggerResponseDisposition::retryable_backpressure;
}

}  // namespace

struct TriggerConnectionOwner::State final {
    State(
        std::shared_ptr<TriggerExecutor::Impl> executor_owner,
        std::shared_ptr<trigger_protocol::TriggerSession> trigger_session,
        const std::size_t task_limit) noexcept
        : executor(std::move(executor_owner)), session(std::move(trigger_session)),
          max_tasks(task_limit)
    {}

    ~State()
    {
        session->release_execution_owner();
    }

    std::shared_ptr<TriggerExecutor::Impl> executor;
    std::shared_ptr<trigger_protocol::TriggerSession> session;
    const std::size_t max_tasks{};
    std::mutex operation_mutex;
    std::atomic<bool> accepting{true};
    std::atomic<bool> close_required{false};
};

struct TriggerExecutor::Impl final {
    struct Slot final {
        SlotState state{SlotState::free};
        std::shared_ptr<TriggerConnectionOwner::State> connection;
        const TriggerHandler* handler{};
        trigger_protocol::Timestamp timestamp{};
        std::stop_source stop_source;
        std::optional<AdmittedTriggerRequest> request;
        std::optional<PendingTriggerResponse> pending;
        std::size_t input_bytes{};
        std::size_t pending_bytes{};
    };

    Impl(
        std::shared_ptr<const TriggerDispatcher> owned_dispatcher,
        const TriggerExecutorLimits configured_limits)
        : dispatcher(std::move(owned_dispatcher)),
          limits(configured_limits),
          slots(limits.max_tasks),
          queue(limits.max_queued_tasks),
          connections(limits.max_connections),
          shutdown_stop_sources(limits.max_tasks)
    {
        if (!dispatcher) {
            throw std::invalid_argument("trigger executor requires a dispatcher");
        }
        if (limits.worker_threads == 0 || limits.max_tasks == 0
            || limits.max_queued_tasks == 0
            || limits.max_tasks_per_connection == 0
            || limits.max_connections == 0
            || limits.max_input_bytes == 0
            || limits.max_input_bytes_per_connection == 0
            || limits.max_pending_response_bytes == 0
            || limits.max_pending_response_bytes_per_connection == 0
            || limits.max_queued_tasks > limits.max_tasks) {
            throw std::invalid_argument("trigger executor limits are invalid");
        }
        workers.reserve(limits.worker_threads);
    }

    void start_workers(const std::shared_ptr<Impl>& self)
    {
        workers_remaining = limits.worker_threads;
        try {
            for (std::size_t index = 0; index < limits.worker_threads; ++index) {
                workers.emplace_back(std::make_unique<std::jthread>(
                    [self] { self->run_worker(); }));
            }
        } catch (...) {
            {
                std::lock_guard lock(mutex);
                stopping = true;
                workers_remaining = workers.size();
            }
            available.notify_all();
            for (auto& worker : workers) {
                dispose_worker_handle(worker, true);
            }
            throw;
        }
    }

    ~Impl() = default;

    [[nodiscard]] std::pair<std::optional<std::size_t>, TriggerSubmitError>
    reserve(
        const std::shared_ptr<TriggerConnectionOwner::State>& connection,
        const TriggerHandler& handler,
        const trigger_protocol::Timestamp timestamp,
        const std::size_t input_charge,
        std::stop_source stop_source) noexcept
    {
        std::lock_guard lock(mutex);
        if (stopping) return {std::nullopt, TriggerSubmitError::executor_stopped};
        if (!connection->accepting.load(std::memory_order_acquire)) {
            return {std::nullopt, TriggerSubmitError::connection_stopped};
        }
        if (active_tasks >= limits.max_tasks) {
            ++rejected;
            return {std::nullopt, TriggerSubmitError::global_task_limit};
        }
        if (connection_active_locked(*connection) >= connection->max_tasks) {
            ++rejected;
            return {std::nullopt, TriggerSubmitError::connection_task_limit};
        }
        if (queued + reserved >= limits.max_queued_tasks) {
            ++rejected;
            return {std::nullopt, TriggerSubmitError::queue_full};
        }
        if (input_charge > limits.max_input_bytes - retained_input_bytes) {
            ++rejected;
            return {std::nullopt, TriggerSubmitError::global_input_limit};
        }
        if (input_charge > limits.max_input_bytes_per_connection
            || input_charge > connection_input_bytes_locked(*connection,
                limits.max_input_bytes_per_connection)) {
            ++rejected;
            return {std::nullopt, TriggerSubmitError::connection_input_limit};
        }

        const auto found = std::find_if(slots.begin(), slots.end(), [](const Slot& slot) {
            return slot.state == SlotState::free;
        });
        if (found == slots.end()) {
            ++rejected;
            return {std::nullopt, TriggerSubmitError::global_task_limit};
        }
        const auto index = static_cast<std::size_t>(found - slots.begin());
        found->state = SlotState::reserved;
        found->connection = connection;
        found->handler = &handler;
        found->timestamp = timestamp;
        found->stop_source = std::move(stop_source);
        found->input_bytes = input_charge;
        ++reserved;
        ++active_tasks;
        retained_input_bytes += input_charge;
        return {index, TriggerSubmitError::none};
    }

    void release_reservation(const std::size_t index) noexcept
    {
        std::lock_guard lock(mutex);
        if (index < slots.size() && slots[index].state == SlotState::reserved) {
            release_slot_locked(index);
            ++rejected;
        }
    }

    [[nodiscard]] bool commit(
        const std::size_t index,
        AdmittedTriggerRequest request) noexcept
    {
        {
            std::lock_guard lock(mutex);
            auto& slot = slots[index];
            if (stopping || slot.state != SlotState::reserved
                || !slot.connection
                || !slot.connection->accepting.load(std::memory_order_acquire)) {
                return false;
            }
            slot.request.emplace(std::move(request));
            slot.state = SlotState::queued;
            --reserved;
            ++queued;
            queue[(queue_head + queue_count) % queue.size()] = index;
            ++queue_count;
            ++accepted;
        }
        available.notify_one();
        return true;
    }

    void run_worker() noexcept
    {
        struct ExitGuard final {
            Impl& executor;
            ~ExitGuard()
            {
                {
                    std::lock_guard lock(executor.mutex);
                    if (executor.workers_remaining != 0)
                        --executor.workers_remaining;
                }
                executor.workers_finished.notify_all();
                current_trigger_executor = nullptr;
            }
        } exit_guard{*this};
        current_trigger_executor = this;
        for (;;) {
            std::size_t index{};
            std::stop_token stop_token;
            {
                std::unique_lock lock(mutex);
                available.wait(lock, [this] { return stopping || queue_count != 0; });
                if (stopping && queue_count != 0) {
                    index = queue[queue_head];
                    queue_head = (queue_head + 1) % queue.size();
                    --queue_count;
                    --queued;
                    release_slot_locked(index);
                    lock.unlock();
                    state_changed.notify_all();
                    continue;
                }
                if (queue_count == 0) {
                    if (stopping) {
                        return;
                    }
                    continue;
                }
                index = queue[queue_head];
                queue_head = (queue_head + 1) % queue.size();
                --queue_count;
                auto& slot = slots[index];
                slot.state = SlotState::running;
                --queued;
                ++running;
                stop_token = slot.stop_source.get_token();
            }

            TriggerDispatchResult result;
            try {
                auto& slot = slots[index];
                result = TriggerExecutor::execute(
                    *dispatcher, *slot.handler, *slot.request, stop_token);
            } catch (...) {
                result = {
                    TriggerDispatchError::internal_failure,
                    TriggerDispatchDisposition::close_session,
                    trigger_protocol::AdmissionError::none,
                    TriggerResponseResult{
                        TriggerResponseError::internal_failure,
                        TriggerResponseDisposition::close_session,
                    },
                    std::nullopt,
                };
            }
#if defined(BAAS_SERVICE_TRIGGER_EXECUTOR_TEST_HOOKS)
            TriggerExecutorTestAccess::AfterDispatchHook hook{};
            void* hook_context{};
            {
                std::lock_guard lock(mutex);
                hook = after_dispatch_hook;
                hook_context = after_dispatch_hook_context;
            }
            if (hook) hook(hook_context);
#endif
            std::shared_ptr<TriggerConnectionOwner::State> connection;
            bool must_close =
                result.disposition == TriggerDispatchDisposition::close_session;
            {
                std::lock_guard lock(mutex);
                auto& slot = slots[index];
                connection = slot.connection;
                ++handlers_finished;
                --running;
                release_input_locked(slot);
                const bool connection_open = connection
                    && connection->accepting.load(std::memory_order_acquire)
                    && !connection->close_required.load(std::memory_order_acquire);
                if (result.disposition == TriggerDispatchDisposition::retry_response
                    && result.pending_response && slot.stop_source.stop_requested()
                    && connection_open) {
                    if (!result.pending_response->replace_with_cancelled())
                        must_close = true;
                }
                const bool completion_forbidden = stopping || !connection_open;
                if (!completion_forbidden && !must_close
                    && result.disposition == TriggerDispatchDisposition::retry_response
                    && result.pending_response) {
                    const auto pending_charge = result.pending_response->bytes();
                    if (pending_charge > limits.max_pending_response_bytes
                            - pending_response_bytes
                        || pending_charge
                            > remaining_connection_pending_bytes_locked(
                                *connection,
                                limits.max_pending_response_bytes_per_connection)) {
                        must_close = true;
                    }
                }
                if (must_close && connection) {
                    connection->accepting.store(false, std::memory_order_release);
                    connection->close_required.store(true, std::memory_order_release);
                }
                if (!completion_forbidden
                    && result.disposition == TriggerDispatchDisposition::retry_response
                    && result.pending_response && !must_close) {
                    slot.pending_bytes = result.pending_response->bytes();
                    pending_response_bytes += slot.pending_bytes;
                    slot.pending.emplace(std::move(*result.pending_response));
                    slot.request.reset();
                    slot.state = SlotState::completed;
                    ++completed;
                } else {
                    release_slot_locked(index);
                }
            }
            state_changed.notify_all();
            if (must_close && connection) force_close(connection);
        }
    }

    void force_close(
        const std::shared_ptr<TriggerConnectionOwner::State>& connection,
        const bool request_stops = true) noexcept
    {
        std::vector<trigger_protocol::ActiveCommand> active;
        {
            std::lock_guard operation_lock(connection->operation_mutex);
            connection->accepting.store(false, std::memory_order_release);
            connection->close_required.store(true, std::memory_order_release);
            try {
                active = connection->session->close();
            } catch (...) {
                // No platform or container failure may cross the worker
                // boundary; the slot scan below still requests stop for every
                // owned task.
            }
        }
        cancel_handoff(connection, active, true, request_stops);
    }

    void cancel_handoff(
        const std::shared_ptr<TriggerConnectionOwner::State>& connection,
        const std::vector<trigger_protocol::ActiveCommand>& active,
        const bool all,
        const bool request_stops = true) noexcept
    {
        if (trigger_owner_scan_contains(connection.get())) return;
        TriggerOwnerScanGuard scan_guard{connection.get()};
        for (std::size_t index = 0; index < slots.size(); ++index) {
            std::optional<std::stop_source> stop_source;
            {
                std::lock_guard lock(mutex);
                auto& slot = slots[index];
                if (slot.state == SlotState::free
                    || slot.connection != connection)
                    continue;
                const bool selected = all || std::any_of(
                    active.begin(), active.end(), [&slot](const auto& command) {
                        return command.timestamp == slot.timestamp;
                    });
                if (!selected) continue;
                if (request_stops) stop_source.emplace(slot.stop_source);
                if (slot.state == SlotState::completed) {
                    --completed;
                    release_slot_locked(index);
                }
            }
            if (stop_source)
                static_cast<void>(request_stop_outside_locks(*stop_source));
        }
        state_changed.notify_all();
    }

    struct StopTarget final {
        bool found{};
        std::size_t slot_index{std::numeric_limits<std::size_t>::max()};
        std::optional<std::stop_source> source;
    };

    [[nodiscard]] StopTarget find_stop_target(
        const std::shared_ptr<TriggerConnectionOwner::State>& connection,
        const trigger_protocol::Timestamp timestamp) noexcept
    {
        std::lock_guard lock(mutex);
        for (std::size_t index = 0; index < slots.size(); ++index) {
            auto& slot = slots[index];
            if (slot.state != SlotState::free
                && slot.connection == connection
                && slot.timestamp == timestamp) {
                return {true, index, slot.stop_source};
            }
        }
        return {};
    }

    [[nodiscard]] bool replace_completed_with_cancelled(
        const std::shared_ptr<TriggerConnectionOwner::State>& connection,
        const trigger_protocol::Timestamp timestamp,
        const StopTarget& target)
    {
        std::lock_guard lock(mutex);
        if (!target.found || !target.source
            || target.slot_index >= slots.size()) return false;
        auto& slot = slots[target.slot_index];
        if (slot.state == SlotState::completed
            && slot.connection == connection
            && slot.timestamp == timestamp
            && slot.stop_source.get_token()
                == target.source->get_token()) {
            if (!slot.pending || !slot.pending->replace_with_cancelled())
                return true;
            pending_response_bytes -= slot.pending_bytes;
            slot.pending_bytes = slot.pending->bytes();
            pending_response_bytes += slot.pending_bytes;
        }
        return false;
    }

    [[nodiscard]] TriggerRetryResult retry_pending(
        const std::shared_ptr<TriggerConnectionOwner::State>& connection)
    {
        TriggerRetryResult summary;
        for (std::size_t index = 0; index < slots.size(); ++index) {
            std::optional<PendingTriggerResponse> pending;
            {
                std::lock_guard lock(mutex);
                auto& slot = slots[index];
                if (slot.state != SlotState::completed
                    || slot.connection != connection || !slot.pending) {
                    continue;
                }
                pending.emplace(std::move(*slot.pending));
                slot.pending.reset();
            }

            ++summary.attempted;
            TriggerResponseResult retried;
            try {
                retried = pending->retry();
            } catch (...) {
                retried = {
                    TriggerResponseError::internal_failure,
                    TriggerResponseDisposition::close_session,
                };
            }

            {
                std::lock_guard lock(mutex);
                ++pending_retries;
                auto& slot = slots[index];
                if (retried) {
                    ++summary.published;
                    --completed;
                    release_slot_locked(index);
                } else if (retryable(retried)) {
                    slot.pending.emplace(std::move(*pending));
                    ++summary.still_pending;
                } else {
                    --completed;
                    release_slot_locked(index);
                    summary.close_required = true;
                    connection->close_required.store(true, std::memory_order_release);
                }
            }
            state_changed.notify_all();
        }
        return summary;
    }

    [[nodiscard]] TriggerExecutorStats snapshot() const noexcept
    {
        std::lock_guard lock(mutex);
        return {
            reserved, queued, running, completed, active_tasks,
            accepted, rejected, handlers_finished, pending_retries,
            retained_input_bytes, pending_response_bytes, stopping,
        };
    }

    [[nodiscard]] TriggerConnectionStats connection_snapshot(
        const TriggerConnectionOwner::State& connection) const noexcept
    {
        std::lock_guard lock(mutex);
        TriggerConnectionStats result;
        for (const auto& slot : slots) {
            if (slot.state == SlotState::free
                || slot.connection.get() != &connection) continue;
            ++result.active_tasks;
            if (slot.state == SlotState::queued) ++result.queued;
            if (slot.state == SlotState::running) ++result.running;
            if (slot.state == SlotState::completed) ++result.completed;
            result.retained_input_bytes += slot.input_bytes;
            result.pending_response_bytes += slot.pending_bytes;
        }
        result.accepting = connection.accepting.load(std::memory_order_acquire);
        result.close_required =
            connection.close_required.load(std::memory_order_acquire);
        return result;
    }

    void wait_connection_idle(
        const std::shared_ptr<TriggerConnectionOwner::State>& connection) noexcept
    {
        std::unique_lock lock(mutex);
        state_changed.wait(lock, [this, &connection] {
            return connection_active_locked(*connection) == 0;
        });
    }

    void shutdown() noexcept
    {
        const bool called_from_worker = current_trigger_executor != nullptr
            || invoking_trigger_stop_callbacks;
        bool initiate = false;
        std::vector<std::unique_ptr<std::jthread>> owned_workers;
        {
            std::lock_guard shutdown_lock(shutdown_mutex);
            if (!shutdown_started) {
                shutdown_started = true;
                initiate = true;
                {
                    std::lock_guard lock(mutex);
                    stopping = true;
                    for (auto& slot : slots) {
                        if (slot.state != SlotState::free) {
                            shutdown_stop_sources[shutdown_stop_source_count++]
                                .emplace(slot.stop_source);
                        }
                    }
                }
            }
            if (called_from_worker) {
                for (auto& worker : workers) {
                    dispose_worker_handle(worker, false);
                }
            } else if (!workers.empty()) {
                owned_workers = std::move(workers);
            }
        }

        if (initiate) {
            // Registry storage is fixed at construction. Snapshot one strong
            // reference at a time so noexcept shutdown never allocates.
            for (std::size_t index = 0; index < connections.size(); ++index) {
                std::shared_ptr<TriggerConnectionOwner::State> connection;
                {
                    std::lock_guard lock(mutex);
                    connection = connections[index].lock();
                }
                if (connection) force_close(connection, false);
            }
            {
                std::lock_guard shutdown_lock(shutdown_mutex);
                registry_close_complete = true;
            }
            shutdown_finished.notify_all();
            for (std::size_t index = 0;
                 index < shutdown_stop_source_count; ++index) {
                if (shutdown_stop_sources[index]) {
                    static_cast<void>(request_stop_outside_locks(
                        *shutdown_stop_sources[index]));
                    shutdown_stop_sources[index].reset();
                }
            }
            available.notify_all();
        }

        if (called_from_worker) return;
        {
            std::unique_lock shutdown_lock(shutdown_mutex);
            shutdown_finished.wait(shutdown_lock, [this] {
                return registry_close_complete;
            });
        }
        for (auto& worker : owned_workers) {
            dispose_worker_handle(worker, true);
        }
        std::unique_lock lock(mutex);
        workers_finished.wait(lock, [this] {
            return workers_remaining == 0;
        });
    }

    static void dispose_worker_handle(
        std::unique_ptr<std::jthread>& worker,
        const bool may_join) noexcept
    {
        if (!worker || !worker->joinable()) return;
        if (may_join) {
            try {
                worker->join();
                return;
            } catch (const std::system_error&) {
            }
        }
        try {
            worker->detach();
        } catch (const std::system_error&) {
            // Destroying a still-joinable jthread terminates the process. If
            // both OS transitions fail, intentionally leak only the handle;
            // workers_remaining remains the authoritative completion gate.
            static_cast<void>(worker.release());
        }
    }

    [[nodiscard]] std::size_t connection_active_locked(
        const TriggerConnectionOwner::State& connection) const noexcept
    {
        return static_cast<std::size_t>(std::count_if(
            slots.begin(), slots.end(), [&connection](const Slot& slot) {
                return slot.state != SlotState::free
                    && slot.connection.get() == &connection;
            }));
    }

    [[nodiscard]] std::size_t connection_input_bytes_locked(
        const TriggerConnectionOwner::State& connection,
        const std::size_t limit) const noexcept
    {
        std::size_t used{};
        for (const auto& slot : slots) {
            if (slot.state != SlotState::free
                && slot.connection.get() == &connection) {
                used += slot.input_bytes;
            }
        }
        return used > limit ? 0 : limit - used;
    }

    [[nodiscard]] std::size_t remaining_connection_pending_bytes_locked(
        const TriggerConnectionOwner::State& connection,
        const std::size_t limit) const noexcept
    {
        std::size_t used{};
        for (const auto& slot : slots) {
            if (slot.state != SlotState::free
                && slot.connection.get() == &connection) {
                used += slot.pending_bytes;
            }
        }
        return used > limit ? 0 : limit - used;
    }

    void release_input_locked(Slot& slot) noexcept
    {
        if (slot.input_bytes <= retained_input_bytes)
            retained_input_bytes -= slot.input_bytes;
        slot.input_bytes = 0;
    }

    void release_slot_locked(const std::size_t index) noexcept
    {
        auto& slot = slots[index];
        if (slot.state == SlotState::reserved) --reserved;
        release_input_locked(slot);
        if (slot.pending_bytes <= pending_response_bytes)
            pending_response_bytes -= slot.pending_bytes;
        slot.pending_bytes = 0;
        slot.pending.reset();
        slot.request.reset();
        slot.handler = nullptr;
        slot.timestamp = 0;
        slot.connection.reset();
        slot.state = SlotState::free;
        if (active_tasks != 0) --active_tasks;
    }

    std::shared_ptr<const TriggerDispatcher> dispatcher;
    TriggerExecutorLimits limits;
    mutable std::mutex mutex;
    std::mutex shutdown_mutex;
    std::condition_variable available;
    std::condition_variable state_changed;
    std::condition_variable workers_finished;
    std::condition_variable shutdown_finished;
    std::vector<Slot> slots;
    std::vector<std::size_t> queue;
    std::size_t queue_head{};
    std::size_t queue_count{};
    std::vector<std::weak_ptr<TriggerConnectionOwner::State>> connections;
    std::vector<std::optional<std::stop_source>> shutdown_stop_sources;
    std::vector<std::unique_ptr<std::jthread>> workers;
    std::size_t reserved{};
    std::size_t queued{};
    std::size_t running{};
    std::size_t completed{};
    std::size_t active_tasks{};
    std::size_t accepted{};
    std::size_t rejected{};
    std::size_t handlers_finished{};
    std::size_t pending_retries{};
    std::size_t retained_input_bytes{};
    std::size_t pending_response_bytes{};
    std::size_t workers_remaining{};
    std::size_t shutdown_stop_source_count{};
    bool stopping{};
    bool shutdown_started{};
    bool registry_close_complete{};
#if defined(BAAS_SERVICE_TRIGGER_EXECUTOR_TEST_HOOKS)
    TriggerExecutorTestAccess::AfterDispatchHook after_dispatch_hook{};
    void* after_dispatch_hook_context{};
#endif
};

std::string_view trigger_submit_error_name(const TriggerSubmitError error) noexcept
{
    using enum TriggerSubmitError;
    switch (error) {
        case none: return "none";
        case unregistered_command: return "unregistered_command";
        case executor_stopped: return "executor_stopped";
        case connection_stopped: return "connection_stopped";
        case global_task_limit: return "global_task_limit";
        case connection_task_limit: return "connection_task_limit";
        case queue_full: return "queue_full";
        case global_input_limit: return "global_input_limit";
        case connection_input_limit: return "connection_input_limit";
        case admission_rejected: return "admission_rejected";
        case transaction_failed: return "transaction_failed";
    }
    return "unknown";
}

const TriggerHandler* TriggerExecutor::resolve_handler(
    const TriggerDispatcher& dispatcher,
    const TriggerCommandDescriptor& descriptor) noexcept
{
    return dispatcher.find_handler(descriptor);
}

AdmittedTriggerRequest TriggerExecutor::make_request(
    trigger_protocol::TriggerIngressItem item,
    trigger_protocol::TriggerSession& session,
    trigger_protocol::AdmissionReceipt receipt) noexcept
{
    return {std::move(item), session, std::move(receipt)};
}

TriggerDispatchResult TriggerExecutor::execute(
    const TriggerDispatcher& dispatcher,
    const TriggerHandler& handler,
    const AdmittedTriggerRequest& request,
    const std::stop_token stop_token)
{
    return dispatcher.execute_admitted(handler, request, stop_token);
}

TriggerExecutor::TriggerExecutor(
    std::shared_ptr<const TriggerDispatcher> dispatcher,
    const TriggerExecutorLimits limits)
    : impl_(std::make_shared<Impl>(std::move(dispatcher), limits))
{
    impl_->start_workers(impl_);
}

TriggerExecutor::~TriggerExecutor() noexcept
{
    shutdown();
}

#if defined(BAAS_SERVICE_TRIGGER_EXECUTOR_TEST_HOOKS)
void TriggerExecutorTestAccess::set_after_dispatch_hook(
    TriggerExecutor& executor, const AfterDispatchHook hook,
    void* const context) noexcept
{
    std::lock_guard lock(executor.impl_->mutex);
    executor.impl_->after_dispatch_hook = hook;
    executor.impl_->after_dispatch_hook_context = context;
}
#endif

TriggerConnectionOwner TriggerExecutor::connect(
    std::shared_ptr<trigger_protocol::TriggerSession> session,
    std::size_t max_tasks)
{
    if (!session)
        throw std::invalid_argument("trigger connection requires a session");
    if (max_tasks == 0) max_tasks = impl_->limits.max_tasks_per_connection;
    if (max_tasks == 0 || max_tasks > impl_->limits.max_tasks) {
        throw std::invalid_argument("connection trigger task limit is invalid");
    }
    std::shared_ptr<TriggerConnectionOwner::State> state;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->stopping)
            throw std::runtime_error("trigger executor is stopped");
        if (!session->try_claim_execution_owner())
            throw std::runtime_error("trigger session already has an execution owner");
        const auto free_connection = std::find_if(
            impl_->connections.begin(), impl_->connections.end(),
            [](const auto& connection) { return connection.expired(); });
        if (free_connection == impl_->connections.end()) {
            session->release_execution_owner();
            throw std::runtime_error("trigger executor connection limit reached");
        }
        try {
            state = std::make_shared<TriggerConnectionOwner::State>(
                impl_, session, max_tasks);
        } catch (...) {
            session->release_execution_owner();
            throw;
        }
        *free_connection = state;
    }
    return TriggerConnectionOwner{std::move(state)};
}

TriggerExecutorStats TriggerExecutor::stats() const noexcept
{
    return impl_->snapshot();
}

void TriggerExecutor::shutdown() noexcept
{
    if (impl_) impl_->shutdown();
}

TriggerConnectionOwner::TriggerConnectionOwner(
    std::shared_ptr<State> state) noexcept
    : state_(std::move(state))
{}

TriggerConnectionOwner::~TriggerConnectionOwner() noexcept
{
    shutdown();
}

TriggerConnectionOwner::TriggerConnectionOwner(
    TriggerConnectionOwner&& other) noexcept
    : state_(std::move(other.state_))
{}

TriggerConnectionOwner& TriggerConnectionOwner::operator=(
    TriggerConnectionOwner&& other) noexcept
{
    if (this != &other) {
        shutdown();
        state_ = std::move(other.state_);
    }
    return *this;
}

TriggerSubmitResult TriggerConnectionOwner::submit(
    trigger_protocol::TriggerIngressItem item)
{
    const auto timestamp = item.envelope().timestamp;
    const auto binary_bytes = item.binary() ? item.binary()->size() : 0;
    if (binary_bytes > std::numeric_limits<std::size_t>::max()
            - item.envelope().payload_json.size()) {
        return {TriggerSubmitError::global_input_limit, {}, timestamp};
    }
    const auto input_charge = item.envelope().payload_json.size() + binary_bytes;
    if (!state_) return {TriggerSubmitError::connection_stopped, {}, timestamp};
    auto& executor = *state_->executor;
    const auto* handler = TriggerExecutor::resolve_handler(
        *executor.dispatcher, item.descriptor());
    if (handler == nullptr) {
        return {TriggerSubmitError::unregistered_command, {}, timestamp};
    }

    std::unique_lock operation_lock(state_->operation_mutex);
    if (!state_->accepting.load(std::memory_order_acquire)) {
        return {TriggerSubmitError::connection_stopped, {}, timestamp};
    }

    std::optional<std::size_t> reserved_slot;
    TriggerSubmitError reserve_error{TriggerSubmitError::none};
    try {
        std::stop_source stop_source;
        auto reservation = executor.reserve(
            state_, *handler, timestamp, input_charge, std::move(stop_source));
        reserved_slot = reservation.first;
        reserve_error = reservation.second;
    } catch (...) {
        return {TriggerSubmitError::transaction_failed, {}, timestamp};
    }
    if (!reserved_slot) return {reserve_error, {}, timestamp};

    trigger_protocol::AdmissionResult admission;
    try {
        admission = state_->session->admit(item.admission());
    } catch (...) {
        executor.release_reservation(*reserved_slot);
        return {TriggerSubmitError::transaction_failed, {}, timestamp};
    }
    if (!admission || !admission.receipt) {
        executor.release_reservation(*reserved_slot);
        return {
            TriggerSubmitError::admission_rejected,
            admission.error,
            timestamp,
        };
    }

    const auto receipt = *admission.receipt;
    auto request = TriggerExecutor::make_request(
        std::move(item), *state_->session, std::move(*admission.receipt));
    if (!executor.commit(*reserved_slot, std::move(request))) {
        bool rolled_back = false;
        try {
            rolled_back = static_cast<bool>(state_->session->rollback(receipt));
        } catch (...) {
            rolled_back = false;
        }
        executor.release_reservation(*reserved_slot);
        if (!rolled_back) {
            state_->accepting.store(false, std::memory_order_release);
            state_->close_required.store(true, std::memory_order_release);
            std::vector<trigger_protocol::ActiveCommand> active;
            try {
                active = state_->session->close();
            } catch (...) {
                // The fixed-capacity task table remains the authoritative
                // cancellation owner even if a platform mutex reports failure.
            }
            operation_lock.unlock();
            executor.cancel_handoff(state_, active, true);
        }
        return {TriggerSubmitError::transaction_failed, {}, timestamp};
    }
    return {TriggerSubmitError::none, {}, timestamp};
}

TriggerCancelResult TriggerConnectionOwner::request_cancel(
    const trigger_protocol::Timestamp timestamp)
{
    if (!state_) return {};
    TriggerCancelResult result;
    bool must_close = false;
    std::vector<trigger_protocol::ActiveCommand> active;
    TriggerExecutor::Impl::StopTarget stop_target;
    {
        std::lock_guard operation_lock(state_->operation_mutex);
        result.session_decision = state_->session->request_cancel(timestamp);
        if (result.session_decision == trigger_protocol::CancelDecision::requested
            || result.session_decision
                == trigger_protocol::CancelDecision::already_requested) {
            stop_target = state_->executor->find_stop_target(state_, timestamp);
            result.task_found = stop_target.found;
        }
    }
    if (stop_target.source)
        result.stop_requested = request_stop_outside_locks(*stop_target.source);
    {
        std::lock_guard operation_lock(state_->operation_mutex);
        if (result.task_found
            && (result.session_decision == trigger_protocol::CancelDecision::requested
                || result.session_decision
                    == trigger_protocol::CancelDecision::already_requested)) {
            must_close = state_->executor->replace_completed_with_cancelled(
                state_, timestamp, stop_target);
        }
        if (must_close) {
            state_->accepting.store(false, std::memory_order_release);
            active = state_->session->close();
        }
    }
    if (must_close) state_->executor->cancel_handoff(state_, active, true);
    return result;
}

TriggerRetryResult TriggerConnectionOwner::retry_pending()
{
    if (!state_) return {};
    std::vector<trigger_protocol::ActiveCommand> active;
    TriggerRetryResult result;
    {
        std::lock_guard operation_lock(state_->operation_mutex);
        result = state_->executor->retry_pending(state_);
        if (result.close_required) {
            state_->accepting.store(false, std::memory_order_release);
            active = state_->session->close();
        }
    }
    if (result.close_required)
        state_->executor->cancel_handoff(state_, active, true);
    return result;
}

TriggerCompleteSendResult TriggerConnectionOwner::complete_send(
    const trigger_protocol::SendLease& lease)
{
    if (!state_) {
        return {{trigger_protocol::SendTransitionError::closed}, {}};
    }
    TriggerCompleteSendResult result;
    std::vector<trigger_protocol::ActiveCommand> active;
    {
        std::lock_guard operation_lock(state_->operation_mutex);
        result.send = state_->session->complete_send(lease);
        if (result.send) result.retry = state_->executor->retry_pending(state_);
        if (result.retry.close_required) {
            state_->accepting.store(false, std::memory_order_release);
            active = state_->session->close();
        }
    }
    if (result.retry.close_required)
        state_->executor->cancel_handoff(state_, active, true);
    return result;
}

TriggerFailSendResult TriggerConnectionOwner::fail_send(
    const trigger_protocol::SendLease& lease)
{
    if (!state_) return {trigger_protocol::SendTransitionError::closed, 0};
    trigger_protocol::FailSendResult failed;
    {
        std::lock_guard operation_lock(state_->operation_mutex);
        failed = state_->session->fail_send(lease);
        if (failed)
            state_->accepting.store(false, std::memory_order_release);
    }
    if (failed) state_->executor->cancel_handoff(state_, failed.cancel, true);
    return {failed.error, failed.cancel.size()};
}

TriggerCloseResult TriggerConnectionOwner::close()
{
    if (!state_) return {};
    std::vector<trigger_protocol::ActiveCommand> active;
    {
        std::lock_guard operation_lock(state_->operation_mutex);
        state_->accepting.store(false, std::memory_order_release);
        active = state_->session->close();
    }
    state_->executor->cancel_handoff(state_, active, true);
    return {active.size()};
}

TriggerConnectionStats TriggerConnectionOwner::stats() const noexcept
{
    if (!state_) return {};
    return state_->executor->connection_snapshot(*state_);
}

void TriggerConnectionOwner::shutdown() noexcept
{
    if (!state_) return;
    if (trigger_owner_scan_contains(state_.get())) return;
    try {
        static_cast<void>(close());
    } catch (...) {
        state_->accepting.store(false, std::memory_order_release);
        state_->executor->cancel_handoff(state_, {}, true);
    }
    // Never block any executor worker on task drain: same-pool queued work and
    // cross-pool shutdown cycles would otherwise deadlock. Active slots retain
    // State; an external non-worker shutdown remains the required drain point.
    if (current_trigger_executor != nullptr
        || invoking_trigger_stop_callbacks) return;
    state_->executor->wait_connection_idle(state_);
}

}  // namespace baas::service::trigger
