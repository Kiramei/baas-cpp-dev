#include "service/runtime/RuntimeScriptTaskBackend.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace baas::service::runtime {
namespace {

using Clock = std::chrono::steady_clock;

constexpr auto max_task_deadline = std::chrono::hours{24};

RuntimeTaskTerminal failure_terminal(const int exit_code) noexcept
{
    return RuntimeTaskTerminal{false, exit_code};
}

bool bounded_nonempty_text(
    const std::string_view value,
    const std::size_t max_identity_bytes) noexcept
{
    return !value.empty() && value.size() <= max_identity_bytes
        && value.find('\0') == std::string_view::npos;
}

bool lowercase_hex(const std::string_view value) noexcept
{
    for (const char byte : value) {
        if (!((byte >= '0' && byte <= '9')
              || (byte >= 'a' && byte <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool exact_generation(const std::string_view value) noexcept
{
    return value.size() == 64 && lowercase_hex(value);
}

bool exact_commit(const std::string_view value) noexcept
{
    return (value.size() == 40 || value.size() == 64)
        && lowercase_hex(value);
}

bool valid_task_plan(
    const std::vector<std::string>& plan,
    const std::size_t max_identity_bytes) noexcept
{
    return !plan.empty()
        && std::all_of(
            plan.begin(), plan.end(), [max_identity_bytes](const auto& task) {
                return bounded_nonempty_text(task, max_identity_bytes);
            });
}

bool identity_matches(
    const RuntimeScriptTaskIdentity& identity,
    const RuntimeTaskRequest& request,
    const std::vector<std::string>& requested_task_plan,
    const std::size_t max_identity_bytes) noexcept
{
    if (identity.config_id != request.config_id
        || identity.run_mode != request.run_mode
        || identity.requested_task_plan != requested_task_plan
        || identity.canonical_task_plan.size()
            != identity.requested_task_plan.size()) {
        return false;
    }

    return bounded_nonempty_text(identity.config_id, max_identity_bytes)
        && bounded_nonempty_text(
            identity.config_snapshot_id, max_identity_bytes)
        && bounded_nonempty_text(identity.profile, max_identity_bytes)
        && bounded_nonempty_text(identity.device_id, max_identity_bytes)
        && identity.runtime_generation.size() <= max_identity_bytes
        && exact_generation(identity.runtime_generation)
        && identity.scripts_commit.size() <= max_identity_bytes
        && exact_commit(identity.scripts_commit)
        && identity.resources_commit.size() <= max_identity_bytes
        && exact_commit(identity.resources_commit)
        && bounded_nonempty_text(identity.run_mode, max_identity_bytes)
        && valid_task_plan(
            identity.requested_task_plan, max_identity_bytes)
        && valid_task_plan(
            identity.canonical_task_plan, max_identity_bytes);
}

std::vector<std::string> requested_task_plan(
    const RuntimeTaskRequest& request)
{
    std::vector<std::string> result;
    result.reserve(
        request.waiting_tasks.size() + (request.current_task ? 1U : 0U));
    if (request.current_task) result.push_back(*request.current_task);
    result.insert(
        result.end(), request.waiting_tasks.begin(),
        request.waiting_tasks.end());
    return result;
}

RuntimeTaskProgress initial_progress(
    const std::vector<std::string>& task_plan)
{
    RuntimeTaskProgress progress;
    progress.is_flag_run = true;
    progress.current_task = task_plan.front();
    progress.waiting_tasks.assign(task_plan.begin() + 1, task_plan.end());
    return progress;
}

std::optional<RuntimeTaskTerminal> control_terminal(
    const RuntimeScriptTaskExecutionControl& control) noexcept
{
    if (control.deadline_exceeded()) {
        return failure_terminal(runtime_script_task_deadline_exit_code);
    }
    if (control.stop_requested()) {
        return failure_terminal(runtime_script_task_cancelled_exit_code);
    }
    return std::nullopt;
}

RuntimeTaskTerminal controlled_failure(
    const RuntimeScriptTaskExecutionControl& control) noexcept
{
    if (const auto boundary = control_terminal(control)) return *boundary;
    return failure_terminal(runtime_script_task_failure_exit_code);
}

class PreparedRuntimeScriptTaskBackend final
    : public RuntimeTaskPreparedBackend {
public:
    PreparedRuntimeScriptTaskBackend(
        std::shared_ptr<const RuntimeScriptTaskRuntimeFactory> factory,
        RuntimeTaskRequest request,
        RuntimeScriptTaskRepositoryBinding repository,
        const RuntimeScriptTaskBackendOptions options,
        const std::stop_token preparation_stop,
        std::shared_ptr<std::atomic<RuntimeScriptTaskPrepareError>> status)
        : factory_(std::move(factory)), request_(std::move(request)),
          repository_(std::move(repository)), options_(options),
          preparation_stop_(preparation_stop), status_(std::move(status)),
          deadline_(Clock::now() + options.task_deadline)
    {}

    bool prepare(const std::stop_token owner_stop_token) noexcept override
    {
        const std::stop_callback preparation_stop{
            preparation_stop_, [this]() noexcept {
                (void)lifetime_stop_.request_stop();
            }};
        const std::stop_callback owner_stop{
            owner_stop_token, [this]() noexcept {
                (void)lifetime_stop_.request_stop();
            }};
        const RuntimeScriptTaskExecutionControl control{
            lifetime_stop_.get_token(), deadline_};
        try {
            if (control.stop_requested()) return fail(
                RuntimeScriptTaskPrepareError::cancelled);
            if (control.deadline_exceeded()) return fail(
                RuntimeScriptTaskPrepareError::deadline);
            task_plan_ = requested_task_plan(request_);
            runtime_ = factory_->create(request_, task_plan_, control);
            if (control.stop_requested()) return fail(
                RuntimeScriptTaskPrepareError::cancelled);
            if (control.deadline_exceeded()) return fail(
                RuntimeScriptTaskPrepareError::deadline);
            if (!runtime_) return fail(
                RuntimeScriptTaskPrepareError::unavailable);
            identity_ = runtime_->identity();
            if (!identity_matches(
                    identity_, request_, task_plan_, options_.max_identity_bytes)) {
                return fail(RuntimeScriptTaskPrepareError::invalid_identity);
            }
            if (identity_.runtime_generation != repository_.generation
                || identity_.scripts_commit != repository_.scripts_commit
                || identity_.resources_commit != repository_.resources_commit) {
                return fail(RuntimeScriptTaskPrepareError::repository_mismatch);
            }
            status_->store(
                RuntimeScriptTaskPrepareError::none,
                std::memory_order_release);
            return true;
        } catch (const std::bad_alloc&) {
            return fail(RuntimeScriptTaskPrepareError::capacity);
        } catch (...) {
            if (control.stop_requested()) return fail(
                RuntimeScriptTaskPrepareError::cancelled);
            if (control.deadline_exceeded()) return fail(
                RuntimeScriptTaskPrepareError::deadline);
            return fail(RuntimeScriptTaskPrepareError::internal_error);
        }
    }

    RuntimeTaskTerminal execute(
        const RuntimeTaskRequest&,
        const std::stop_token stop_token,
        const RuntimeTaskProgressReporter& report_progress) noexcept override
    {
        const std::stop_callback owner_stop{stop_token, [this]() noexcept {
            (void)lifetime_stop_.request_stop();
        }};
        const RuntimeScriptTaskExecutionControl control{
            lifetime_stop_.get_token(), deadline_};
        try {
            if (const auto boundary = control_terminal(control)) return *boundary;
            const RuntimeTaskProgressReporter controlled_report =
                [&control, &report_progress](RuntimeTaskProgress progress) {
                    if (control_terminal(control)) return false;
                    const auto accepted = report_progress(std::move(progress));
                    return !control_terminal(control) && accepted;
                };
            (void)controlled_report(initial_progress(task_plan_));
            if (const auto boundary = control_terminal(control)) return *boundary;
            auto terminal = runtime_->execute(control, controlled_report);
            if (const auto boundary = control_terminal(control)) return *boundary;
            return terminal;
        } catch (...) {
            return controlled_failure(control);
        }
    }

private:
    bool fail(const RuntimeScriptTaskPrepareError error) noexcept
    {
        status_->store(error, std::memory_order_release);
        runtime_.reset();
        return false;
    }

    std::shared_ptr<const RuntimeScriptTaskRuntimeFactory> factory_;
    RuntimeTaskRequest request_;
    RuntimeScriptTaskRepositoryBinding repository_;
    RuntimeScriptTaskBackendOptions options_;
    std::stop_token preparation_stop_;
    std::shared_ptr<std::atomic<RuntimeScriptTaskPrepareError>> status_;
    RuntimeScriptTaskIdentity identity_;
    std::unique_ptr<RuntimeScriptTaskRuntime> runtime_;
    std::vector<std::string> task_plan_;
    std::stop_source lifetime_stop_;
    Clock::time_point deadline_;
};

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

RuntimeScriptTaskPrepareResult prepare_runtime_script_task_backend(
    std::shared_ptr<const RuntimeScriptTaskRuntimeFactory> factory,
    const RuntimeTaskRequest& request,
    RuntimeScriptTaskRepositoryBinding repository,
    const RuntimeScriptTaskBackendOptions options,
    const std::stop_token stop_token) noexcept
{
    RuntimeScriptTaskPrepareResult result;
    if (!factory || options.task_deadline <= std::chrono::milliseconds::zero()
        || options.task_deadline > max_task_deadline
        || options.max_identity_bytes == 0) {
        result.error = RuntimeScriptTaskPrepareError::invalid_request;
        return result;
    }
    try {
        if (stop_token.stop_requested()) {
            result.error = RuntimeScriptTaskPrepareError::cancelled;
            return result;
        }
        if (!bounded_nonempty_text(request.config_id, options.max_identity_bytes)
            || !bounded_nonempty_text(request.run_mode, options.max_identity_bytes)) {
            result.error = RuntimeScriptTaskPrepareError::invalid_request;
            return result;
        }
        auto task_plan = requested_task_plan(request);
        if (!valid_task_plan(task_plan, options.max_identity_bytes)) {
            result.error = RuntimeScriptTaskPrepareError::invalid_request;
            return result;
        }
        result.preparation_status =
            std::make_shared<std::atomic<RuntimeScriptTaskPrepareError>>(
                RuntimeScriptTaskPrepareError::none);
        result.backend = std::make_shared<PreparedRuntimeScriptTaskBackend>(
            std::move(factory), request, std::move(repository), options,
            stop_token, result.preparation_status);
        result.error = RuntimeScriptTaskPrepareError::none;
        return result;
    } catch (const std::bad_alloc&) {
        result.error = RuntimeScriptTaskPrepareError::capacity;
    } catch (...) {
        if (stop_token.stop_requested()) {
            result.error = RuntimeScriptTaskPrepareError::cancelled;
        } else {
            result.error = RuntimeScriptTaskPrepareError::internal_error;
        }
    }
    return result;
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
        const RuntimeScriptTaskExecutionControl control{
            stop_token, Clock::now() + options.task_deadline};
        try {
            if (const auto boundary = control_terminal(control)) {
                return *boundary;
            }
            if (!bounded_nonempty_text(
                    request.config_id, options.max_identity_bytes)
                || !bounded_nonempty_text(
                    request.run_mode, options.max_identity_bytes)) {
                return controlled_failure(control);
            }
            auto task_plan = requested_task_plan(request);
            if (!valid_task_plan(task_plan, options.max_identity_bytes)) {
                return controlled_failure(control);
            }

            auto runtime = factory->create(
                request, task_plan, control);
            if (!runtime) return controlled_failure(control);
            if (!identity_matches(
                    runtime->identity(), request, task_plan,
                    options.max_identity_bytes)) {
                return controlled_failure(control);
            }
            if (const auto boundary = control_terminal(control)) {
                return *boundary;
            }

            const RuntimeTaskProgressReporter controlled_report =
                [&control, &report_progress](RuntimeTaskProgress progress) {
                    if (control_terminal(control)) return false;
                    const auto accepted = report_progress(std::move(progress));
                    return !control_terminal(control) && accepted;
                };
            // false is either a weak-lease signal or an active control
            // boundary. The runtime observes control after every report.
            (void)controlled_report(initial_progress(task_plan));
            if (const auto boundary = control_terminal(control)) {
                return *boundary;
            }

            auto terminal = runtime->execute(control, controlled_report);
            if (const auto boundary = control_terminal(control)) {
                return *boundary;
            }
            return terminal;
        }
        catch (...) {
            if (const auto boundary = control_terminal(control)) {
                return *boundary;
            }
            return controlled_failure(control);
        }
    };
}

} // namespace baas::service::runtime
