#pragma once

#include "service/trigger/TriggerDispatch.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace baas::service::app {

inline constexpr std::size_t runtime_task_trigger_hard_max_json_bytes =
    1U * 1'024U * 1'024U;
inline constexpr std::size_t runtime_task_trigger_hard_max_json_depth = 128;
inline constexpr std::size_t runtime_task_trigger_hard_max_json_nodes = 65'536;

struct RuntimeTaskTriggerLimits {
    std::size_t max_payload_bytes{64U * 1'024U};
    std::size_t max_payload_depth{16};
    std::size_t max_payload_nodes{1'024};
    std::size_t max_task_bytes{4U * 1'024U};
    std::size_t max_result_json_bytes{64U * 1'024U};
    std::size_t max_result_json_depth{32};
    std::size_t max_result_json_nodes{4'096};
};

enum class RuntimeTaskControlError : std::uint8_t {
    none,
    invalid_config_id,
    invalid_task,
    conflict,
    capacity,
    unavailable,
    internal_error,
};

[[nodiscard]] std::string_view runtime_task_control_error_name(
    RuntimeTaskControlError error) noexcept;

struct RuntimeTaskControlResult {
    RuntimeTaskControlError error{RuntimeTaskControlError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == RuntimeTaskControlError::none;
    }
};

// A reversible reservation prepared by RuntimeTaskControl. Destruction before
// a successful commit must abort/release the reservation and must not start or
// stop real work. commit() performs the one-way ownership transfer and must
// return immediately without waiting for the long-running job.
class RuntimeTaskPreparedOperation {
public:
    virtual ~RuntimeTaskPreparedOperation() = default;
    [[nodiscard]] virtual RuntimeTaskControlResult commit() = 0;
};

struct RuntimeTaskPrepareResult {
    // Exact Python-compatible data object for command_response.data. It is
    // computed without starting/stopping real work, then bounded before claim.
    std::string data_json;
    std::unique_ptr<RuntimeTaskPreparedOperation> operation;
    RuntimeTaskControlError error{RuntimeTaskControlError::none};

    RuntimeTaskPrepareResult() = default;
    RuntimeTaskPrepareResult(
        std::string data,
        std::unique_ptr<RuntimeTaskPreparedOperation> prepared,
        RuntimeTaskControlError prepare_error = RuntimeTaskControlError::none)
        : data_json(std::move(data)), operation(std::move(prepared)),
          error(prepare_error)
    {}
    RuntimeTaskPrepareResult(RuntimeTaskPrepareResult&&) noexcept = default;
    RuntimeTaskPrepareResult& operator=(
        RuntimeTaskPrepareResult&&) noexcept = default;
    RuntimeTaskPrepareResult(const RuntimeTaskPrepareResult&) = delete;
    RuntimeTaskPrepareResult& operator=(const RuntimeTaskPrepareResult&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == RuntimeTaskControlError::none
            && static_cast<bool>(operation);
    }
};

// Service-owned lifecycle boundary for scheduler and one-shot task jobs.
//
// Every prepare method creates only a reversible reservation. It must not
// start/stop a real job. The registration validates data, atomically claims an
// irrevocable terminal, then invokes RuntimeTaskPreparedOperation::commit() to
// perform the ownership transfer. Implementations own committed work
// independently of the Trigger connection; no Trigger stop token crosses this
// boundary.
//
// Implementations may be invoked concurrently and must be thread-safe. A
// start_task implementation receives the original command/task spelling and
// owns legacy start_* alias normalization. Outcomes such as already-running
// are successful data JSON, matching Python ServiceRuntime.
class RuntimeTaskControl {
public:
    virtual ~RuntimeTaskControl() = default;

    [[nodiscard]] virtual RuntimeTaskPrepareResult prepare_start_scheduler(
        std::string_view config_id) = 0;
    [[nodiscard]] virtual RuntimeTaskPrepareResult prepare_stop_scheduler(
        std::string_view config_id) = 0;
    [[nodiscard]] virtual RuntimeTaskPrepareResult prepare_start_task(
        std::string_view config_id, std::string_view requested_task) = 0;
    [[nodiscard]] virtual RuntimeTaskPrepareResult prepare_stop_all_tasks() = 0;
};

enum class RuntimeTaskTriggerRegistrationError : std::uint8_t {
    none,
    missing_control,
    invalid_limits,
    resource_exhausted,
};

[[nodiscard]] std::string_view runtime_task_trigger_registration_error_name(
    RuntimeTaskTriggerRegistrationError error) noexcept;

struct RuntimeTaskTriggerRegistrationResult {
    std::vector<trigger::TriggerHandlerRegistration> registrations;
    RuntimeTaskTriggerRegistrationError error{
        RuntimeTaskTriggerRegistrationError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == RuntimeTaskTriggerRegistrationError::none
            && registrations.size() == 5;
    }
};

// Returns exactly the five catalog registrations start_scheduler,
// stop_scheduler, solve, start_*, and stop_all_tasks. It does not install a
// placeholder owner and does not alter application composition.
[[nodiscard]] RuntimeTaskTriggerRegistrationResult
make_runtime_task_trigger_registrations(
    std::shared_ptr<RuntimeTaskControl> control,
    RuntimeTaskTriggerLimits limits = {}) noexcept;

}  // namespace baas::service::app
