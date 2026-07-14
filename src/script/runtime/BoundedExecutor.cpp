#include "script/runtime/BoundedExecutor.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace baas::script::runtime {

namespace {
thread_local const void* current_executor_state = nullptr;
}

TaskCancelled::TaskCancelled()
    : std::runtime_error("task cancelled")
{
}

ExecutorShutdown::ExecutorShutdown()
    : std::runtime_error("executor is shut down")
{
}

SubmitTimeout::SubmitTimeout()
    : std::runtime_error("executor submit deadline expired")
{
}

namespace detail {

bool TaskBase::RequestCancel() noexcept
{
    TaskPhase expected = TaskPhase::Queued;
    if (phase_.compare_exchange_strong(expected, TaskPhase::Cancelled,
                                       std::memory_order_acq_rel)) {
        SetCancelledException();
        return true;
    }
    if (expected == TaskPhase::Running) {
        return stop_source_.request_stop();
    }
    return false;
}

bool TaskBase::CancelQueued() noexcept
{
    TaskPhase expected = TaskPhase::Queued;
    if (!phase_.compare_exchange_strong(expected, TaskPhase::Cancelled,
                                        std::memory_order_acq_rel)) {
        return false;
    }
    SetCancelledException();
    return true;
}

bool TaskBase::BeginExecution() noexcept
{
    TaskPhase expected = TaskPhase::Queued;
    return phase_.compare_exchange_strong(expected, TaskPhase::Running,
                                          std::memory_order_acq_rel);
}

void TaskBase::FinishExecution() noexcept
{
    phase_.store(TaskPhase::Finished, std::memory_order_release);
}

std::stop_token TaskBase::StopToken() const noexcept
{
    return stop_source_.get_token();
}

}  // namespace detail

struct BoundedExecutor::State final {
    State(const std::size_t capacity, const std::size_t worker_count)
        : capacity(capacity), workers_remaining(worker_count)
    {
    }

    void RunWorker() noexcept
    {
        struct ExitGuard final {
            State& state;

            ~ExitGuard()
            {
                {
                    std::lock_guard lock(state.mutex);
                    --state.workers_remaining;
                }
                state.workers_finished.notify_all();
                current_executor_state = nullptr;
            }
        } exit_guard{*this};
        current_executor_state = this;

        for (;;) {
            std::shared_ptr<detail::TaskBase> task;
            {
                std::unique_lock lock(mutex);
                task_available.wait(lock, [this] {
                    return stopping || !queue.empty();
                });
                if (queue.empty()) {
                    if (stopping) {
                        return;
                    }
                    continue;
                }
                task = std::move(queue.front());
                queue.pop_front();
            }
            space_available.notify_one();
            task->Execute();
        }
    }

    bool CalledFromWorker() const noexcept
    {
        return current_executor_state == this;
    }

    void WaitForWorkers() noexcept
    {
        std::unique_lock lock(mutex);
        workers_finished.wait(lock, [this] { return workers_remaining == 0; });
    }

    bool Cancel(const std::shared_ptr<detail::TaskBase>& task) noexcept
    {
        bool removed = false;
        {
            std::lock_guard lock(mutex);
            const auto found = std::find(queue.begin(), queue.end(), task);
            if (found != queue.end()) {
                queue.erase(found);
                removed = true;
            }
        }

        const bool cancelled = removed ? task->CancelQueued() : task->RequestCancel();
        if (removed) {
            space_available.notify_one();
        }
        return cancelled;
    }

    void BeginShutdown(const ShutdownMode mode) noexcept
    {
        std::deque<std::shared_ptr<detail::TaskBase>> pending;
        {
            std::lock_guard lock(mutex);
            if (stopping) {
                return;
            }
            accepting = false;
            stopping = true;
            if (mode == ShutdownMode::CancelPending) {
                pending.swap(queue);
            }
        }

        for (const auto& task : pending) {
            task->CancelQueued();
        }
        task_available.notify_all();
        space_available.notify_all();
    }

    const std::size_t capacity;
    std::mutex mutex;
    std::condition_variable task_available;
    std::condition_variable space_available;
    std::condition_variable workers_finished;
    std::deque<std::shared_ptr<detail::TaskBase>> queue;
    bool accepting{true};
    bool stopping{false};
    std::size_t workers_remaining;
};

class BoundedExecutor::WorkerSet final {
public:
    explicit WorkerSet(const std::size_t count)
    {
        threads.reserve(count);
    }

    void JoinAll(const bool called_from_worker, State& state) noexcept
    {
        std::vector<std::jthread> owned_threads;
        {
            std::lock_guard lock(threads_mutex);
            if (called_from_worker) {
                for (auto& thread : threads) {
                    if (thread.joinable()) {
                        thread.detach();
                    }
                }
                threads.clear();
                return;
            }
            owned_threads = std::move(threads);
        }

        for (auto& thread : owned_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        state.WaitForWorkers();
    }

    std::mutex threads_mutex;
    std::vector<std::jthread> threads;
};

BoundedExecutor::BoundedExecutor(const std::size_t worker_count,
                                 const std::size_t queue_capacity)
    : worker_count_(worker_count),
      queue_capacity_(queue_capacity)
{
    if (worker_count == 0) {
        throw std::invalid_argument("worker_count must be greater than zero");
    }
    if (queue_capacity == 0) {
        throw std::invalid_argument("queue_capacity must be greater than zero");
    }

    state_ = std::make_shared<State>(queue_capacity, worker_count);
    workers_ = std::make_unique<WorkerSet>(worker_count);
    try {
        for (std::size_t index = 0; index < worker_count; ++index) {
            workers_->threads.emplace_back([state = state_] {
                state->RunWorker();
            });
        }
    } catch (...) {
        state_->BeginShutdown(ShutdownMode::CancelPending);
        for (auto& thread : workers_->threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        workers_->threads.clear();
        throw;
    }
}

BoundedExecutor::~BoundedExecutor()
{
    Shutdown(ShutdownMode::CancelPending);
}

BoundedExecutor::EnqueueResult BoundedExecutor::TryEnqueue(
    const std::shared_ptr<detail::TaskBase>& task)
{
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->accepting) {
            return EnqueueResult::Shutdown;
        }
        if (state_->queue.size() >= state_->capacity) {
            return EnqueueResult::Full;
        }
        state_->queue.push_back(task);
    }
    state_->task_available.notify_one();
    return EnqueueResult::Accepted;
}

BoundedExecutor::EnqueueResult BoundedExecutor::EnqueueUntil(
    const std::shared_ptr<detail::TaskBase>& task,
    const std::chrono::steady_clock::time_point deadline)
{
    {
        std::unique_lock lock(state_->mutex);
        const bool ready = state_->space_available.wait_until(lock, deadline, [this] {
            return !state_->accepting || state_->queue.size() < state_->capacity;
        });
        if (!state_->accepting) {
            return EnqueueResult::Shutdown;
        }
        if (!ready) {
            return EnqueueResult::Timeout;
        }
        state_->queue.push_back(task);
    }
    state_->task_available.notify_one();
    return EnqueueResult::Accepted;
}

std::function<bool()> BoundedExecutor::CancellationFor(
    const std::shared_ptr<detail::TaskBase>& task) const
{
    const std::weak_ptr<State> weak_state = state_;
    const std::weak_ptr<detail::TaskBase> weak_task = task;
    return [weak_state, weak_task]() noexcept {
        const auto locked_task = weak_task.lock();
        if (!locked_task) {
            return false;
        }
        if (const auto state = weak_state.lock()) {
            return state->Cancel(locked_task);
        }
        return locked_task->RequestCancel();
    };
}

void BoundedExecutor::Shutdown(const ShutdownMode mode) noexcept
{
    if (!workers_) {
        return;
    }
    state_->BeginShutdown(mode);
    workers_->JoinAll(state_->CalledFromWorker(), *state_);
}

std::size_t BoundedExecutor::WorkerCount() const noexcept
{
    return worker_count_;
}

std::size_t BoundedExecutor::QueueCapacity() const noexcept
{
    return queue_capacity_;
}

}  // namespace baas::script::runtime
