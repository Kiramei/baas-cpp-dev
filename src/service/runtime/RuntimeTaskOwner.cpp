#include "service/runtime/RuntimeTaskOwner.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace baas::service::runtime {
namespace {

enum class JobPhase : std::uint8_t { completed, running, stopping };

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

}  // namespace

std::string_view runtime_task_start_decision_name(
    const RuntimeTaskStartDecision decision) noexcept
{
    switch (decision) {
        case RuntimeTaskStartDecision::started: return "started";
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
    }
    return "unknown-config";
}

class RuntimeTaskOwner::Impl final {
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

    ~Impl() noexcept { shutdown(); }

    [[nodiscard]] RuntimeTaskStartResult start(RuntimeTaskRequest request)
    {
        if (!valid_request(request)) {
            return {RuntimeTaskStartDecision::invalid_request, std::nullopt};
        }

        std::unique_lock lock{mutex_};
        if (!accepting_) {
            return {RuntimeTaskStartDecision::owner_stopped, std::nullopt};
        }

        auto found = jobs_.find(request.config_id);
        if (found == jobs_.end()) {
            if (jobs_.size() >= limits_.max_configs) {
                return {
                    RuntimeTaskStartDecision::capacity_exceeded, std::nullopt};
            }
            auto job = std::make_unique<Job>();
            job->snapshot.config_id = request.config_id;
            touch(*job);
            found = jobs_.emplace(request.config_id, std::move(job)).first;
        }
        Job& job = *found->second;
        if (job.phase == JobPhase::running) {
            return {
                RuntimeTaskStartDecision::already_running, job.snapshot};
        }
        if (job.phase == JobPhase::stopping) {
            return {RuntimeTaskStartDecision::stopping, job.snapshot};
        }

        // Completed workers publish their final state after releasing mutex_.
        // Joining here cannot wait on a worker that still needs this lock and
        // prevents the prior thread object from escaping service ownership.
        if (job.worker.joinable()) job.worker.join();

        ++job.generation;
        const auto generation = job.generation;
        job.stop_source = std::stop_source{};
        job.manual_stop_requested = false;
        job.phase = JobPhase::running;
        job.snapshot.running = true;
        job.snapshot.stopping = false;
        job.snapshot.is_flag_run = true;
        job.snapshot.exit_code.reset();
        job.snapshot.run_mode = request.run_mode;
        job.snapshot.current_task = request.current_task;
        job.snapshot.waiting_tasks = request.waiting_tasks;
        touch(job);

        try {
            Job* const stable_job = &job;
            job.worker = std::thread{
                [this, stable_job, generation, request = std::move(request)] {
                    run(*stable_job, generation, request);
                }};
        } catch (...) {
            job.phase = JobPhase::completed;
            job.snapshot.running = false;
            job.snapshot.stopping = false;
            job.snapshot.is_flag_run = false;
            job.snapshot.current_task.reset();
            job.snapshot.waiting_tasks.clear();
            job.snapshot.exit_code = 1;
            touch(job);
            condition_.notify_all();
            return {
                RuntimeTaskStartDecision::thread_start_failed, job.snapshot};
        }
        return {RuntimeTaskStartDecision::started, job.snapshot};
    }

    [[nodiscard]] RuntimeTaskStopResult request_stop(
        const std::string_view config_id)
    {
        if (!bounded_text(config_id, limits_.max_config_id_bytes)) {
            return {RuntimeTaskStopDecision::unknown_config, std::nullopt};
        }
        std::unique_lock lock{mutex_};
        const auto found = jobs_.find(std::string{config_id});
        if (found == jobs_.end()) {
            return {RuntimeTaskStopDecision::unknown_config, std::nullopt};
        }
        Job& job = *found->second;
        if (job.phase == JobPhase::completed) {
            return {RuntimeTaskStopDecision::already_stopped, job.snapshot};
        }
        if (job.phase == JobPhase::stopping) {
            return {RuntimeTaskStopDecision::already_stopping, job.snapshot};
        }
        auto stop_source = mark_stopping_locked(job);
        const auto result = RuntimeTaskStopResult{
            RuntimeTaskStopDecision::stop_requested, job.snapshot};
        lock.unlock();
        static_cast<void>(stop_source.request_stop());
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
        return found->second->snapshot;
    }

    [[nodiscard]] std::vector<RuntimeTaskSnapshot> snapshots() const
    {
        std::lock_guard lock{mutex_};
        std::vector<RuntimeTaskSnapshot> result;
        result.reserve(jobs_.size());
        for (const auto& [config_id, job] : jobs_) {
            static_cast<void>(config_id);
            result.push_back(job->snapshot);
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
            return found == jobs_.end()
                || found->second->phase == JobPhase::completed;
        });
    }

    void shutdown() noexcept
    {
        std::lock_guard shutdown_lock{shutdown_mutex_};
        std::vector<Job*> jobs;
        std::vector<std::stop_source> stop_sources;
        {
            std::lock_guard lock{mutex_};
            accepting_ = false;
            jobs.reserve(jobs_.size());
            stop_sources.reserve(jobs_.size());
            for (auto& [config_id, job] : jobs_) {
                static_cast<void>(config_id);
                if (job->phase == JobPhase::running) {
                    stop_sources.push_back(mark_stopping_locked(*job));
                } else if (job->phase == JobPhase::stopping) {
                    stop_sources.push_back(job->stop_source);
                }
                jobs.push_back(job.get());
            }
        }
        // std::stop_callback executes synchronously in request_stop(). Never
        // invoke user backend callbacks while holding the owner state mutex.
        for (auto& source : stop_sources) {
            static_cast<void>(source.request_stop());
        }
        for (Job* const job : jobs) {
            if (job->worker.joinable()) job->worker.join();
        }
    }

private:
    struct Job {
        RuntimeTaskSnapshot snapshot;
        JobPhase phase{JobPhase::completed};
        std::stop_source stop_source;
        std::thread worker;
        std::uint64_t generation{};
        bool manual_stop_requested{false};
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

    void touch(Job& job) noexcept
    {
        const auto now = unix_timestamp_ms();
        if (now > job.snapshot.timestamp) {
            job.snapshot.timestamp = now;
        } else if (job.snapshot.timestamp < json_safe_integer_max) {
            ++job.snapshot.timestamp;
        }
    }

    [[nodiscard]] std::stop_source mark_stopping_locked(Job& job) noexcept
    {
        job.phase = JobPhase::stopping;
        job.manual_stop_requested = true;
        job.snapshot.stopping = true;
        job.snapshot.is_flag_run = false;
        touch(job);
        return job.stop_source;
    }

    [[nodiscard]] bool report(
        Job& job, const std::uint64_t generation,
        RuntimeTaskProgress progress)
    {
        if (!valid_progress(progress)) return false;
        std::lock_guard lock{mutex_};
        if (job.generation != generation || job.phase == JobPhase::completed) {
            return false;
        }
        job.snapshot.is_flag_run = job.phase == JobPhase::stopping
            ? false
            : progress.is_flag_run;
        job.snapshot.button = std::move(progress.button);
        job.snapshot.current_task = std::move(progress.current_task);
        job.snapshot.waiting_tasks = std::move(progress.waiting_tasks);
        touch(job);
        return true;
    }

    void run(
        Job& job, const std::uint64_t generation,
        const RuntimeTaskRequest& request) noexcept
    {
        bool succeeded = false;
        try {
            const RuntimeTaskProgressReporter reporter =
                [this, &job, generation](RuntimeTaskProgress progress) {
                    return report(job, generation, std::move(progress));
                };
            succeeded = backend_(
                request, job.stop_source.get_token(), reporter);
        } catch (...) {
            succeeded = false;
        }

        std::lock_guard lock{mutex_};
        if (job.generation != generation) return;
        const bool manually_stopped = job.manual_stop_requested;
        job.phase = JobPhase::completed;
        job.snapshot.running = false;
        job.snapshot.stopping = false;
        job.snapshot.is_flag_run = false;
        job.snapshot.current_task.reset();
        job.snapshot.waiting_tasks.clear();
        if (!manually_stopped && !succeeded) {
            job.snapshot.exit_code = 1;
        } else {
            job.snapshot.exit_code.reset();
        }
        touch(job);
        condition_.notify_all();
    }

    RuntimeTaskBackend backend_;
    RuntimeTaskLimits limits_;
    mutable std::mutex mutex_;
    mutable std::condition_variable condition_;
    std::mutex shutdown_mutex_;
    std::unordered_map<std::string, std::unique_ptr<Job>> jobs_;
    bool accepting_{true};
};

RuntimeTaskOwner::RuntimeTaskOwner(
    RuntimeTaskBackend backend, RuntimeTaskLimits limits)
    : impl_(std::make_unique<Impl>(std::move(backend), limits))
{}

RuntimeTaskOwner::~RuntimeTaskOwner() noexcept = default;

RuntimeTaskStartResult RuntimeTaskOwner::start(RuntimeTaskRequest request)
{
    return impl_->start(std::move(request));
}

RuntimeTaskStopResult RuntimeTaskOwner::request_stop(
    const std::string_view config_id)
{
    return impl_->request_stop(config_id);
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

}  // namespace baas::service::runtime
