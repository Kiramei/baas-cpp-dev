#pragma once

#include "service/trigger/TriggerDispatch.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace baas::service::app {

inline constexpr std::size_t status_trigger_hard_max_json_bytes =
    4U * 1'024U * 1'024U;
inline constexpr std::size_t status_trigger_hard_max_json_depth = 256;
inline constexpr std::size_t status_trigger_hard_max_json_nodes = 262'144;

struct StatusTriggerLimits {
    // Leaves headroom inside the default 1 MiB trigger response envelope.
    std::size_t max_json_bytes{512U * 1'024U};
    std::size_t max_json_depth{64};
    std::size_t max_json_nodes{32'768};
};

enum class StatusSourceError : std::uint8_t {
    none,
    cancelled,
    capacity,
    unavailable,
};

[[nodiscard]] std::string_view status_source_error_name(
    StatusSourceError error) noexcept;

struct StatusSourceResult {
    std::string data_json;
    StatusSourceError error{StatusSourceError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == StatusSourceError::none;
    }
};

// The production runtime bridge implements this interface with the equivalent
// of Python ServiceRuntime.current_status(): one stable, deep-copied JSON object
// snapshot. Implementations may be called concurrently by TriggerExecutor,
// must be thread-safe, must observe stop around blocking work, and must bound
// retained work before returning at the configured registration limit.
class StatusSource {
public:
    virtual ~StatusSource() = default;
    [[nodiscard]] virtual StatusSourceResult current_status(
        std::stop_token stop) = 0;
};

using StatusSourceCallback =
    std::function<StatusSourceResult(std::stop_token)>;

enum class StatusTriggerRegistrationError : std::uint8_t {
    none,
    missing_source,
    empty_callback,
    invalid_limits,
    resource_exhausted,
};

[[nodiscard]] std::string_view status_trigger_registration_error_name(
    StatusTriggerRegistrationError error) noexcept;

struct StatusTriggerRegistrationResult {
    std::optional<trigger::TriggerHandlerRegistration> registration;
    StatusTriggerRegistrationError error{StatusTriggerRegistrationError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == StatusTriggerRegistrationError::none
            && registration.has_value();
    }
};

// Each overload returns exactly one canonical "status" registration. No other
// trigger command is registered or assigned a placeholder implementation.
[[nodiscard]] StatusTriggerRegistrationResult make_status_trigger_registration(
    std::shared_ptr<StatusSource> source,
    StatusTriggerLimits limits = {}) noexcept;

[[nodiscard]] StatusTriggerRegistrationResult make_status_trigger_registration(
    StatusSourceCallback callback,
    StatusTriggerLimits limits = {}) noexcept;

}  // namespace baas::service::app
