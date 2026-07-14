#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace baas::script::runtime {

class TaskCancelled final : public std::runtime_error {
public:
    TaskCancelled();
};

class ExecutorShutdown final : public std::runtime_error {
public:
    ExecutorShutdown();
};

class SubmitTimeout final : public std::runtime_error {
public:
    SubmitTimeout();
};

enum class ShutdownMode {
    // Stop accepting submissions, then execute every task already accepted.
    Drain,
    // Stop accepting submissions, cancel queued tasks, and let running tasks
    // finish. Running tasks are stopped only when their handle requests it.
    CancelPending,
};

namespace detail {

enum class TaskPhase {
    Queued,
    Running,
    Finished,
    Cancelled,
};

class TaskBase {
public:
    virtual ~TaskBase() = default;

    bool RequestCancel() noexcept;
    bool CancelQueued() noexcept;

    virtual void Execute() noexcept = 0;

protected:
    bool BeginExecution() noexcept;
    void FinishExecution() noexcept;
    std::stop_token StopToken() const noexcept;
    virtual void SetCancelledException() noexcept = 0;

private:
    std::atomic<TaskPhase> phase_{TaskPhase::Queued};
    std::stop_source stop_source_;
};

template <class Callable, bool AcceptsStopToken = std::invocable<Callable&, std::stop_token>>
struct TaskResult;

template <class Callable>
struct TaskResult<Callable, true> {
    using type = std::invoke_result_t<Callable&, std::stop_token>;
};

template <class Callable>
struct TaskResult<Callable, false> {
    static_assert(std::invocable<Callable&>,
                  "executor tasks must be callable with std::stop_token or with no arguments");
    using type = std::invoke_result_t<Callable&>;
};

template <class Callable>
using TaskResultT = typename TaskResult<Callable>::type;

template <class Callable>
class Task final : public TaskBase {
public:
    using Result = TaskResultT<Callable>;

    explicit Task(Callable callable)
        : callable_(std::move(callable))
    {
    }

    std::future<Result> GetFuture()
    {
        return promise_.get_future();
    }

    void Execute() noexcept override
    {
        if (!BeginExecution()) {
            return;
        }

        try {
            if constexpr (std::is_void_v<Result>) {
                Invoke();
                promise_.set_value();
            } else {
                promise_.set_value(Invoke());
            }
        } catch (...) {
            try {
                promise_.set_exception(std::current_exception());
            } catch (...) {
                // The task owns its promise and fulfils it exactly once. This
                // fallback only protects a noexcept worker boundary.
            }
        }
        FinishExecution();
    }

protected:
    void SetCancelledException() noexcept override
    {
        try {
            promise_.set_exception(std::make_exception_ptr(TaskCancelled{}));
        } catch (...) {
            // Cancellation races are settled by TaskBase before this call.
        }
    }

private:
    decltype(auto) Invoke()
    {
        if constexpr (std::invocable<Callable&, std::stop_token>) {
            return std::invoke(callable_, StopToken());
        } else {
            return std::invoke(callable_);
        }
    }

    Callable callable_;
    std::promise<Result> promise_;
};

}  // namespace detail

template <class Result>
class TaskHandle final {
public:
    TaskHandle(const TaskHandle&) = delete;
    TaskHandle& operator=(const TaskHandle&) = delete;
    TaskHandle(TaskHandle&&) noexcept = default;
    TaskHandle& operator=(TaskHandle&&) noexcept = default;

    std::future<Result>& Future() noexcept
    {
        return future_;
    }

    std::future<Result> TakeFuture() && noexcept
    {
        return std::move(future_);
    }

    bool Cancel() noexcept
    {
        if (!cancel_) {
            return false;
        }
        return cancel_();
    }

private:
    friend class BoundedExecutor;

    TaskHandle(std::future<Result> future, std::function<bool()> cancel)
        : future_(std::move(future)), cancel_(std::move(cancel))
    {
    }

    std::future<Result> future_;
    std::function<bool()> cancel_;
};

class BoundedExecutor final {
public:
    // worker_count and queue_capacity describe separate bounds: running tasks
    // do not consume queue capacity. Both values must be greater than zero.
    BoundedExecutor(std::size_t worker_count, std::size_t queue_capacity);
    ~BoundedExecutor();

    BoundedExecutor(const BoundedExecutor&) = delete;
    BoundedExecutor& operator=(const BoundedExecutor&) = delete;
    BoundedExecutor(BoundedExecutor&&) = delete;
    BoundedExecutor& operator=(BoundedExecutor&&) = delete;

    template <class Callable>
    auto TrySubmit(Callable&& callable)
        -> std::optional<TaskHandle<detail::TaskResultT<std::decay_t<Callable>>>>
    {
        // A disengaged optional means only "queue full". Once shutdown begins,
        // every submission throws ExecutorShutdown instead.
        using StoredCallable = std::decay_t<Callable>;
        using Result = detail::TaskResultT<StoredCallable>;
        auto task = std::make_shared<detail::Task<StoredCallable>>(
            StoredCallable(std::forward<Callable>(callable))
        );
        auto future = task->GetFuture();

        switch (TryEnqueue(task)) {
        case EnqueueResult::Accepted:
            return TaskHandle<Result>{std::move(future), CancellationFor(task)};
        case EnqueueResult::Full:
            return std::nullopt;
        case EnqueueResult::Shutdown:
            throw ExecutorShutdown{};
        case EnqueueResult::Timeout:
            break;
        }
        std::terminate();
    }

    template <class Clock, class Duration, class Callable>
    auto SubmitUntil(const std::chrono::time_point<Clock, Duration>& deadline,
                     Callable&& callable)
        -> TaskHandle<detail::TaskResultT<std::decay_t<Callable>>>
    {
        // The wait is always implemented with steady_clock. Deadlines from
        // other clocks are converted once at entry, avoiding wall-clock jumps
        // while blocked on queue capacity.
        using StoredCallable = std::decay_t<Callable>;
        using Result = detail::TaskResultT<StoredCallable>;

        const auto source_now = Clock::now();
        const auto steady_now = std::chrono::steady_clock::now();
        const auto remaining = deadline - source_now;
        const auto steady_deadline = remaining <= Duration::zero()
            ? steady_now
            : steady_now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(remaining);

        auto task = std::make_shared<detail::Task<StoredCallable>>(
            StoredCallable(std::forward<Callable>(callable))
        );
        auto future = task->GetFuture();

        switch (EnqueueUntil(task, steady_deadline)) {
        case EnqueueResult::Accepted:
            return TaskHandle<Result>{std::move(future), CancellationFor(task)};
        case EnqueueResult::Shutdown:
            throw ExecutorShutdown{};
        case EnqueueResult::Timeout:
            throw SubmitTimeout{};
        case EnqueueResult::Full:
            break;
        }
        std::terminate();
    }

    // Shutdown is idempotent. The first caller chooses the mode; all callers
    // return only after workers have exited (except a calling worker itself,
    // which is detached and exits immediately after its current task).
    void Shutdown(ShutdownMode mode = ShutdownMode::Drain) noexcept;

    std::size_t WorkerCount() const noexcept;
    std::size_t QueueCapacity() const noexcept;

private:
    struct State;

    enum class EnqueueResult {
        Accepted,
        Full,
        Timeout,
        Shutdown,
    };

    EnqueueResult TryEnqueue(const std::shared_ptr<detail::TaskBase>& task);
    EnqueueResult EnqueueUntil(const std::shared_ptr<detail::TaskBase>& task,
                               std::chrono::steady_clock::time_point deadline);
    std::function<bool()> CancellationFor(
        const std::shared_ptr<detail::TaskBase>& task) const;

    std::shared_ptr<State> state_;
    std::size_t worker_count_;
    std::size_t queue_capacity_;
    class WorkerSet;
    std::unique_ptr<WorkerSet> workers_;
};

}  // namespace baas::script::runtime
