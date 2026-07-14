#include "script/runtime/BoundedExecutor.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using baas::script::runtime::BoundedExecutor;
using baas::script::runtime::ExecutorShutdown;
using baas::script::runtime::ShutdownMode;
using baas::script::runtime::SubmitTimeout;
using baas::script::runtime::TaskCancelled;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <class Exception, class Function>
void check_throws(Function&& function, const std::string_view message)
{
    try {
        std::forward<Function>(function)();
        check(false, message);
    } catch (const Exception&) {
    } catch (...) {
        check(false, message);
    }
}

void test_configuration_results_and_exceptions()
{
    check_throws<std::invalid_argument>([] { BoundedExecutor executor(0, 1); },
                                        "zero workers must be rejected");
    check_throws<std::invalid_argument>([] { BoundedExecutor executor(1, 0); },
                                        "zero queue capacity must be rejected");

    BoundedExecutor executor(2, 4);
    check(executor.WorkerCount() == 2, "worker count should be observable");
    check(executor.QueueCapacity() == 4, "queue capacity should be observable");

    auto value = executor.TrySubmit([owned = std::make_unique<int>(41)] {
        return *owned + 1;
    });
    check(value.has_value(), "move-only no-argument callable should be accepted");
    check(value->Future().get() == 42, "task result should reach its future");

    auto failure = executor.TrySubmit([]() -> int {
        throw std::logic_error("task failure");
    });
    check(failure.has_value(), "throwing callable should be accepted");
    check_throws<std::logic_error>([&] { (void)failure->Future().get(); },
                                   "task exception should reach its future");
}

void test_full_queue_backpressure_and_deadline()
{
    BoundedExecutor executor(1, 1);
    std::promise<void> started_promise;
    auto started = started_promise.get_future();
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();

    auto running = executor.TrySubmit([&started_promise, release] {
        started_promise.set_value();
        release.wait();
        return 1;
    });
    check(running.has_value(), "first task should be accepted");
    check(started.wait_for(2s) == std::future_status::ready,
          "worker should start the gate task");

    auto queued = executor.TrySubmit([] { return 2; });
    check(queued.has_value(), "one task should fit in the bounded queue");
    auto full = executor.TrySubmit([] { return 3; });
    check(!full.has_value(), "TrySubmit should explicitly report a full queue");

    const auto before = std::chrono::steady_clock::now();
    check_throws<SubmitTimeout>([&] {
        (void)executor.SubmitUntil(std::chrono::steady_clock::now() + 40ms,
                                   [] { return 4; });
    }, "deadline submission should time out while capacity stays unavailable");
    check(std::chrono::steady_clock::now() - before >= 20ms,
          "deadline submission should apply real backpressure");

    release_promise.set_value();
    check(running->Future().get() == 1, "running gate task should complete");
    check(queued->Future().get() == 2, "queued task should complete");
}

void test_queued_cancellation_releases_capacity()
{
    BoundedExecutor executor(1, 1);
    std::promise<void> started_promise;
    auto started = started_promise.get_future();
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();

    auto running = executor.TrySubmit([&started_promise, release] {
        started_promise.set_value();
        release.wait();
    });
    check(started.wait_for(2s) == std::future_status::ready,
          "gate task should be running before cancellation test");

    auto queued = executor.TrySubmit([] { return 7; });
    check(queued.has_value(), "queued cancellation target should be accepted");
    check(queued->Cancel(), "queued task cancellation should win");
    check(!queued->Cancel(), "queued task cancellation should be idempotent");
    check_throws<TaskCancelled>([&] { (void)queued->Future().get(); },
                                "queued cancellation should use TaskCancelled");

    auto replacement = executor.TrySubmit([] { return 8; });
    check(replacement.has_value(), "queued cancellation should immediately free capacity");
    release_promise.set_value();
    running->Future().get();
    check(replacement->Future().get() == 8, "replacement task should run");
}

void test_running_cooperative_stop()
{
    BoundedExecutor executor(1, 1);
    std::promise<void> started_promise;
    auto started = started_promise.get_future();

    auto running = executor.TrySubmit([&started_promise](const std::stop_token stop) {
        started_promise.set_value();
        while (!stop.stop_requested()) {
            std::this_thread::yield();
        }
        return 19;
    });
    check(started.wait_for(2s) == std::future_status::ready,
          "stop-aware task should start");
    check(running->Cancel(), "running cancellation should request cooperative stop");
    check(!running->Cancel(), "cooperative stop request should be idempotent");
    check(running->Future().get() == 19,
          "running task should retain control of its cooperative result");
}

void test_shutdown_drain()
{
    BoundedExecutor executor(2, 8);
    std::vector<baas::script::runtime::TaskHandle<int>> handles;
    for (int value = 0; value < 8; ++value) {
        auto handle = executor.TrySubmit([value] { return value * value; });
        check(handle.has_value(), "drain task should fit in configured queue");
        if (handle) {
            handles.push_back(std::move(*handle));
        }
    }

    executor.Shutdown(ShutdownMode::Drain);
    for (std::size_t index = 0; index < handles.size(); ++index) {
        check(handles[index].Future().get() == static_cast<int>(index * index),
              "Drain should finish every accepted task");
    }
    check_throws<ExecutorShutdown>([&] { (void)executor.TrySubmit([] {}); },
                                   "submission after Drain should be rejected");
}

void test_shutdown_cancel_pending()
{
    BoundedExecutor executor(1, 2);
    std::promise<void> started_promise;
    auto started = started_promise.get_future();
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();

    auto running = executor.TrySubmit([&started_promise, release] {
        started_promise.set_value();
        release.wait();
        return 1;
    });
    check(started.wait_for(2s) == std::future_status::ready,
          "CancelPending gate task should start");
    auto queued_one = executor.TrySubmit([] { return 2; });
    auto queued_two = executor.TrySubmit([] { return 3; });
    check(queued_one.has_value() && queued_two.has_value(),
          "CancelPending targets should be queued");

    auto shutdown = std::async(std::launch::async, [&] {
        executor.Shutdown(ShutdownMode::CancelPending);
    });
    check(queued_one->Future().wait_for(2s) == std::future_status::ready &&
              queued_two->Future().wait_for(2s) == std::future_status::ready,
          "CancelPending should fulfil queued futures before running tasks exit");
    check_throws<TaskCancelled>([&] { (void)queued_one->Future().get(); },
                                "first pending task should be cancelled");
    check_throws<TaskCancelled>([&] { (void)queued_two->Future().get(); },
                                "second pending task should be cancelled");

    release_promise.set_value();
    check(running->Future().get() == 1, "CancelPending should not abort a running task");
    check(shutdown.wait_for(2s) == std::future_status::ready,
          "CancelPending shutdown should join after running tasks finish");
    shutdown.get();
    check_throws<ExecutorShutdown>([&] {
        (void)executor.SubmitUntil(std::chrono::steady_clock::now() + 1s, [] {});
    }, "deadline submission after shutdown should be rejected, not timed out");
}

void test_shutdown_from_worker_does_not_self_join()
{
    auto executor = std::make_unique<BoundedExecutor>(1, 1);
    auto* const executor_ptr = executor.get();
    auto handle = executor->TrySubmit([executor_ptr] {
        executor_ptr->Shutdown(ShutdownMode::Drain);
        return 23;
    });
    check(handle.has_value(), "worker shutdown task should be accepted");
    check(handle->Future().wait_for(2s) == std::future_status::ready,
          "a worker initiating shutdown must not self-join");
    check(handle->Future().get() == 23,
          "worker should finish normally after initiating shutdown");
    executor.reset();
}

void test_concurrent_worker_shutdown_does_not_cross_join()
{
    auto executor = std::make_unique<BoundedExecutor>(2, 2);
    auto* const executor_ptr = executor.get();
    std::atomic<int> ready{0};

    auto make_task = [&] {
        return executor->TrySubmit([executor_ptr, &ready] {
            ready.fetch_add(1, std::memory_order_acq_rel);
            while (ready.load(std::memory_order_acquire) != 2) {
                std::this_thread::yield();
            }
            executor_ptr->Shutdown(ShutdownMode::Drain);
            return 1;
        });
    };
    auto first = make_task();
    auto second = make_task();
    check(first.has_value() && second.has_value(),
          "concurrent worker shutdown tasks should be accepted");
    check(first->Future().wait_for(2s) == std::future_status::ready &&
              second->Future().wait_for(2s) == std::future_status::ready,
          "workers initiating shutdown concurrently must not cross-join");
    check(first->Future().get() + second->Future().get() == 2,
          "both concurrent worker shutdown tasks should return");
    executor.reset();
}

void test_destruction_from_worker_does_not_self_join()
{
    auto* executor = new BoundedExecutor(1, 1);
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();
    auto handle = executor->TrySubmit([executor, release] {
        release.wait();
        delete executor;
        return 29;
    });
    check(handle.has_value(), "worker destruction task should be accepted");
    release_promise.set_value();
    check(handle->Future().wait_for(2s) == std::future_status::ready,
          "destruction on a worker must not self-join");
    check(handle->Future().get() == 29,
          "worker should finish after destroying its executor facade");
}

void test_submit_shutdown_race()
{
    BoundedExecutor executor(4, 16);
    std::atomic<int> executed{0};
    std::atomic<int> rejected{0};
    std::mutex handles_mutex;
    std::vector<baas::script::runtime::TaskHandle<void>> handles;
    std::vector<std::jthread> submitters;

    for (int producer = 0; producer < 8; ++producer) {
        submitters.emplace_back([&] {
            for (;;) {
                try {
                    auto handle = executor.SubmitUntil(
                        std::chrono::steady_clock::now() + 20ms,
                        [&executed] { executed.fetch_add(1, std::memory_order_relaxed); }
                    );
                    std::lock_guard lock(handles_mutex);
                    handles.push_back(std::move(handle));
                } catch (const SubmitTimeout&) {
                    continue;
                } catch (const ExecutorShutdown&) {
                    rejected.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    std::this_thread::sleep_for(10ms);
    executor.Shutdown(ShutdownMode::Drain);
    submitters.clear();

    for (auto& handle : handles) {
        handle.Future().get();
    }
    check(rejected.load(std::memory_order_relaxed) == 8,
          "every racing submitter should observe stable shutdown rejection");
    check(executed.load(std::memory_order_relaxed) == static_cast<int>(handles.size()),
          "every accepted racing submission should execute exactly once");
}

void test_stress()
{
    constexpr int task_count = 5000;
    BoundedExecutor executor(4, 32);
    std::atomic<int> executions{0};
    std::vector<baas::script::runtime::TaskHandle<int>> handles;
    handles.reserve(task_count);

    for (int value = 0; value < task_count; ++value) {
        handles.push_back(executor.SubmitUntil(
            std::chrono::steady_clock::now() + 5s,
            [value, &executions] {
                executions.fetch_add(1, std::memory_order_relaxed);
                return value;
            }
        ));
    }
    executor.Shutdown(ShutdownMode::Drain);

    long long sum = 0;
    for (auto& handle : handles) {
        sum += handle.Future().get();
    }
    check(executions.load(std::memory_order_relaxed) == task_count,
          "stress run should execute every accepted task exactly once");
    check(sum == static_cast<long long>(task_count - 1) * task_count / 2,
          "stress futures should preserve every result");
}

}  // namespace

int main()
{
    test_configuration_results_and_exceptions();
    test_full_queue_backpressure_and_deadline();
    test_queued_cancellation_releases_capacity();
    test_running_cooperative_stop();
    test_shutdown_drain();
    test_shutdown_cancel_pending();
    test_shutdown_from_worker_does_not_self_join();
    test_concurrent_worker_shutdown_does_not_cross_join();
    test_destruction_from_worker_does_not_self_join();
    test_submit_shutdown_race();
    test_stress();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All bounded executor tests passed\n";
    return EXIT_SUCCESS;
}
