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

struct RuntimeTaskRequest {
    std::string config_id;
    std::string run_mode;
    std::optional<std::string> current_task;
    std::vector<std::string> waiting_tasks;
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

// The backend is injected by the composition root that owns the real BAAS
// runtime. Different configs invoke it concurrently, so the implementation and
// everything it captures must be thread-safe. Returning false or throwing
// marks a natural completion as failed.
// A cooperative manual stop is observable through stop_token and is reported
// as a normal completion after the backend returns. Production backends must
// provide provable stop-safe points and return after cancellation: C++ cannot
// safely force-terminate an arbitrary worker thread, so shutdown() drains by
// joining cooperative backends.
using RuntimeTaskBackend = std::function<bool(
    const RuntimeTaskRequest& request, std::stop_token stop_token,
    const RuntimeTaskProgressReporter& report_progress)>;

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

enum class RuntimeTaskStartDecision : std::uint8_t {
    started,
    already_running,
    stopping,
    owner_stopped,
    capacity_exceeded,
    invalid_request,
    thread_start_failed,
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

enum class RuntimeTaskStopDecision : std::uint8_t {
    stop_requested,
    already_stopping,
    already_stopped,
    unknown_config,
};

[[nodiscard]] std::string_view runtime_task_stop_decision_name(
    RuntimeTaskStopDecision decision) noexcept;

struct RuntimeTaskStopResult {
    RuntimeTaskStopDecision decision{RuntimeTaskStopDecision::unknown_config};
    std::optional<RuntimeTaskSnapshot> snapshot;
};

// Service-lifetime owner for long-running BAAS jobs. Jobs are keyed by config,
// so a config has at most one live worker while different configs may execute
// concurrently. Trigger/WebSocket request lifetimes are deliberately absent
// from this API. All public operations are thread-safe.
class RuntimeTaskOwner final {
public:
    explicit RuntimeTaskOwner(
        RuntimeTaskBackend backend, RuntimeTaskLimits limits = {});
    ~RuntimeTaskOwner() noexcept;

    RuntimeTaskOwner(const RuntimeTaskOwner&) = delete;
    RuntimeTaskOwner& operator=(const RuntimeTaskOwner&) = delete;
    RuntimeTaskOwner(RuntimeTaskOwner&&) = delete;
    RuntimeTaskOwner& operator=(RuntimeTaskOwner&&) = delete;

    [[nodiscard]] RuntimeTaskStartResult start(RuntimeTaskRequest request);
    [[nodiscard]] RuntimeTaskStopResult request_stop(std::string_view config_id);
    [[nodiscard]] std::optional<RuntimeTaskSnapshot> snapshot(
        std::string_view config_id) const;
    [[nodiscard]] std::vector<RuntimeTaskSnapshot> snapshots() const;

    // Waits for the keyed worker to finish. An unknown config is already idle.
    [[nodiscard]] bool wait_for_idle(
        std::string_view config_id, std::chrono::milliseconds timeout) const;

    // Stops admission, requests cooperative stop for every worker, and joins
    // every owned thread. Repeated calls are safe.
    void shutdown() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace baas::service::runtime
