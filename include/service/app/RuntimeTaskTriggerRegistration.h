#pragma once

#include "service/trigger/TriggerDispatch.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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
    // Exact Python-compatible data object for command_response.data. The
    // registration validates and bounds it before publication.
    std::string data_json;
    RuntimeTaskControlError error{RuntimeTaskControlError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == RuntimeTaskControlError::none;
    }
};

// Service-owned lifecycle boundary for scheduler and one-shot task jobs.
//
// Every method performs only the bounded ownership transfer/admission needed
// to start or stop work and returns immediately; it must never wait for a
// long-running BAAS job to finish. Implementations own accepted work
// independently of the Trigger connection. In particular, no Trigger stop
// token is passed across this boundary, so disconnecting a client cannot
// become job cancellation.
//
// Implementations may be invoked concurrently and must be thread-safe. A
// start_task implementation receives the original command/task spelling and
// owns legacy start_* alias normalization. Outcomes such as already-running
// are successful data JSON, matching Python ServiceRuntime.
class RuntimeTaskControl {
public:
    virtual ~RuntimeTaskControl() = default;

    [[nodiscard]] virtual RuntimeTaskControlResult start_scheduler(
        std::string_view config_id) = 0;
    [[nodiscard]] virtual RuntimeTaskControlResult stop_scheduler(
        std::string_view config_id) = 0;
    [[nodiscard]] virtual RuntimeTaskControlResult start_task(
        std::string_view config_id, std::string_view requested_task) = 0;
    [[nodiscard]] virtual RuntimeTaskControlResult stop_all_tasks() = 0;
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
