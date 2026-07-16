#pragma once

#include "service/adapters/FileResourceStore.h"
#include "service/trigger/TriggerDispatch.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace baas::service::app {

struct ConfigurationTriggerLimits {
    std::size_t max_payload_bytes{64U * 1'024U};
    std::size_t max_payload_depth{16};
    std::size_t max_payload_nodes{1'024};
    std::size_t max_id_bytes{256};
    std::size_t max_name_bytes{1'024};
    std::size_t max_server_bytes{256};
};

enum class ConfigurationTriggerRegistrationError : std::uint8_t {
    none,
    missing_store,
    invalid_limits,
    resource_exhausted,
};

[[nodiscard]] std::string_view configuration_trigger_registration_error_name(
    ConfigurationTriggerRegistrationError error) noexcept;

struct ConfigurationTriggerRegistrationResult {
    std::vector<trigger::TriggerHandlerRegistration> registrations;
    ConfigurationTriggerRegistrationError error{
        ConfigurationTriggerRegistrationError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ConfigurationTriggerRegistrationError::none
            && registrations.size() == 5;
    }
};

// Registers Python's add_config* and remove_config* prefix families plus the
// exact copy_config, export_config, and import_config commands. TOML updater
// commands remain outside this configuration-profile slice.
[[nodiscard]] ConfigurationTriggerRegistrationResult
make_configuration_trigger_registrations(
    std::shared_ptr<adapters::FileResourceStore> store,
    ConfigurationTriggerLimits limits = {}) noexcept;

}  // namespace baas::service::app
