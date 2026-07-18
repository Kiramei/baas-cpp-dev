#include "service/runtime/RuntimeScriptTaskBackend.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace baas::service::runtime {
namespace {

using Clock = std::chrono::steady_clock;

constexpr auto max_task_deadline = std::chrono::hours{24};

RuntimeTaskTerminal failure_terminal(const int exit_code) noexcept
{
    return RuntimeTaskTerminal{false, exit_code};
}

bool bounded_nonempty(
    const std::string& value, const std::size_t max_identity_bytes) noexcept
{
    return !value.empty() && value.size() <= max_identity_bytes;
}

bool identity_matches(
    const RuntimeScriptTaskIdentity& identity,
    const RuntimeTaskRequest& request, const std::string_view selected_task,
    const std::size_t max_identity_bytes) noexcept
{
    if (identity.config_id != request.config_id
        || identity.run_mode != request.run_mode
        || identity.requested_task != selected_task
        || identity.runtime_generation == 0) {
        return false;
    }

    return bounded_nonempty(identity.config_id, max_identity_bytes)
        && bounded_nonempty(identity.config_snapshot_id, max_identity_bytes)
        && bounded_nonempty(identity.profile, max_identity_bytes)
        && bounded_nonempty(identity.device_id, max_identity_bytes)
        && bounded_nonempty(identity.scripts_commit, max_identity_bytes)
        && bounded_nonempty(identity.resources_commit, max_identity_bytes)
        && bounded_nonempty(identity.run_mode, max_identity_bytes)
        && bounded_nonempty(identity.requested_task, max_identity_bytes)
        && bounded_nonempty(identity.canonical_task, max_identity_bytes);
}

struct TaskSelection {
    std::string_view selected_task;
    bool selected_from_waiting{};
};

TaskSelection select_task(const RuntimeTaskRequest& request) noexcept
{
    if (request.current_task && !request.current_task->empty()) {
        return TaskSelection{*request.current_task, false};
    }
    if (!request.waiting_tasks.empty()
        && !request.waiting_tasks.front().empty()) {
        return TaskSelection{request.waiting_tasks.front(), true};
    }
    return {};
}

RuntimeTaskProgress initial_progress(
    const RuntimeTaskRequest& request, const TaskSelection selection)
{
    RuntimeTaskProgress progress;
    progress.is_flag_run = true;
    progress.current_task = std::string{selection.selected_task};
    progress.waiting_tasks = request.waiting_tasks;
    if (selection.selected_from_waiting) {
        progress.waiting_tasks.erase(progress.waiting_tasks.begin());
    }
    return progress;
}

} // namespace

RuntimeScriptTaskExecutionControl::RuntimeScriptTaskExecutionControl(
    std::stop_token stop_token, const Clock::time_point deadline) noexcept
    : stop_token_(stop_token), deadline_(deadline)
{
}

std::stop_token RuntimeScriptTaskExecutionControl::stop_token() const noexcept
{
    return stop_token_;
}

Clock::time_point RuntimeScriptTaskExecutionControl::deadline() const noexcept
{
    return deadline_;
}

bool RuntimeScriptTaskExecutionControl::stop_requested() const noexcept
{
    return stop_token_.stop_requested();
}

bool RuntimeScriptTaskExecutionControl::deadline_exceeded() const noexcept
{
    return Clock::now() >= deadline_;
}

RuntimeTaskBackend make_runtime_script_task_backend(
    std::shared_ptr<const RuntimeScriptTaskRuntimeFactory> factory,
    const RuntimeScriptTaskBackendOptions options)
{
    if (!factory) {
        throw std::invalid_argument{
            "runtime script task backend requires a factory"};
    }
    if (options.task_deadline <= std::chrono::milliseconds::zero()
        || options.task_deadline > max_task_deadline
        || options.max_identity_bytes == 0) {
        throw std::invalid_argument{
            "runtime script task backend options are invalid"};
    }

    return [factory = std::move(factory), options](
               const RuntimeTaskRequest& request,
               const std::stop_token stop_token,
               const RuntimeTaskProgressReporter& report_progress) noexcept {
        try {
            const auto selection = select_task(request);
            if (request.config_id.empty() || request.run_mode.empty()
                || selection.selected_task.empty()) {
                return failure_terminal(runtime_script_task_failure_exit_code);
            }

            const auto now = Clock::now();
            const RuntimeScriptTaskExecutionControl control{
                stop_token, now + options.task_deadline};
            if (control.stop_requested()) {
                return failure_terminal(
                    runtime_script_task_cancelled_exit_code);
            }

            auto runtime = factory->create(
                request, selection.selected_task, control);
            if (!runtime
                || !identity_matches(
                    runtime->identity(), request, selection.selected_task,
                    options.max_identity_bytes)) {
                return failure_terminal(runtime_script_task_failure_exit_code);
            }
            if (control.stop_requested()) {
                return failure_terminal(
                    runtime_script_task_cancelled_exit_code);
            }
            if (control.deadline_exceeded()) {
                return failure_terminal(
                    runtime_script_task_deadline_exit_code);
            }

            // false is a weak-lease/stale-report signal, not execution failure.
            // RuntimeTaskOwner already owns cancellation through stop_token.
            (void)report_progress(initial_progress(request, selection));

            auto terminal = runtime->execute(control);
            if (control.stop_requested()) {
                return failure_terminal(
                    runtime_script_task_cancelled_exit_code);
            }
            if (control.deadline_exceeded()) {
                return failure_terminal(
                    runtime_script_task_deadline_exit_code);
            }
            if (terminal.is_flag_run) {
                return failure_terminal(runtime_script_task_failure_exit_code);
            }
            return terminal;
        }
        catch (...) {
            return failure_terminal(runtime_script_task_failure_exit_code);
        }
    };
}

} // namespace baas::service::runtime
