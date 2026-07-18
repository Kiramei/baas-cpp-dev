#pragma once

#include "service/runtime/RuntimeTaskOwner.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace baas::service::runtime {

// Exact immutable identities retained by one request-plan runtime. Generation
// is the repository's 64-character lowercase SHA-256 identity; repository
// commits use the accepted exact 40/64-character lowercase hexadecimal form.
// The production factory resolves config_id, pins one matching repository
// generation, and resolves every requested task in order before returning.
struct RuntimeScriptTaskIdentity {
    std::string config_id;
    std::string config_snapshot_id;
    std::string profile;
    std::string device_id;
    std::string runtime_generation;
    std::string scripts_commit;
    std::string resources_commit;
    std::string run_mode;
    std::vector<std::string> requested_task_plan;
    std::vector<std::string> canonical_task_plan;
};

class RuntimeScriptTaskExecutionControl final {
public:
    RuntimeScriptTaskExecutionControl(
        std::stop_token stop_token,
        std::chrono::steady_clock::time_point deadline) noexcept;

    [[nodiscard]] std::stop_token stop_token() const noexcept;
    [[nodiscard]] std::chrono::steady_clock::time_point deadline()
        const noexcept;
    [[nodiscard]] bool stop_requested() const noexcept;
    [[nodiscard]] bool deadline_exceeded() const noexcept;

private:
    std::stop_token stop_token_;
    std::chrono::steady_clock::time_point deadline_;
};

// A factory must return a fresh runtime for every create() call. The runtime
// owns the complete ordered requested/canonical plans, their execution plans
// and procedure activations, the config snapshot, native adapters, and every
// Host lifetime used by execute(). It executes the entire plan and may publish
// intermediate progress through the reporter, which it must not retain. It
// must not discover or reread mutable repository/config state after creation.
class RuntimeScriptTaskRuntime {
public:
    virtual ~RuntimeScriptTaskRuntime() = default;

    RuntimeScriptTaskRuntime(const RuntimeScriptTaskRuntime&) = delete;
    RuntimeScriptTaskRuntime& operator=(const RuntimeScriptTaskRuntime&) =
        delete;

    [[nodiscard]] virtual const RuntimeScriptTaskIdentity& identity()
        const noexcept = 0;
    [[nodiscard]] virtual RuntimeTaskTerminal execute(
        const RuntimeScriptTaskExecutionControl& control,
        const RuntimeTaskProgressReporter& report_progress) = 0;

protected:
    RuntimeScriptTaskRuntime() = default;
};

// create() may be called concurrently for different configs. Implementations
// must therefore be thread-safe; task-local mutable state belongs in the
// returned unique runtime, never in shared factory scratch storage.
class RuntimeScriptTaskRuntimeFactory {
public:
    virtual ~RuntimeScriptTaskRuntimeFactory() = default;

    RuntimeScriptTaskRuntimeFactory(
        const RuntimeScriptTaskRuntimeFactory&) = delete;
    RuntimeScriptTaskRuntimeFactory& operator=(
        const RuntimeScriptTaskRuntimeFactory&) = delete;

    [[nodiscard]] virtual std::unique_ptr<RuntimeScriptTaskRuntime> create(
        const RuntimeTaskRequest& request,
        std::span<const std::string> requested_task_plan,
        const RuntimeScriptTaskExecutionControl& control) const = 0;

protected:
    RuntimeScriptTaskRuntimeFactory() = default;
};

struct RuntimeScriptTaskBackendOptions {
    std::chrono::milliseconds task_deadline{std::chrono::minutes{30}};
    std::size_t max_identity_bytes{1'024};
};

inline constexpr int runtime_script_task_failure_exit_code = 1;
inline constexpr int runtime_script_task_deadline_exit_code = 124;
inline constexpr int runtime_script_task_cancelled_exit_code = 130;

// Explicit opt-in composition seam. Constructing this backend does not install
// it in the service or replace the Tauri/Python default runtime path.
[[nodiscard]] RuntimeTaskBackend make_runtime_script_task_backend(
    std::shared_ptr<const RuntimeScriptTaskRuntimeFactory> factory,
    RuntimeScriptTaskBackendOptions options = {});

} // namespace baas::service::runtime
