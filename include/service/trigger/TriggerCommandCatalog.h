#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace baas::service::trigger {

enum class TriggerCommandFamily : std::uint8_t {
    scheduler,
    task,
    configuration,
    diagnostics,
    update,
    backend,
    device,
    status,
};

enum class TriggerCommandSelection : std::uint8_t { exact, prefix };
enum class TriggerCommandResponseMode : std::uint8_t { single, stream };
enum class TriggerConfigIdRequirement : std::uint8_t { not_required, required };
enum class TriggerInboundBinaryPolicy : std::uint8_t { forbidden, required };

struct TriggerCommandDescriptor {
    // Exact command name, or a canonical prefix selector ending in '*'.
    std::string_view canonical_name;
    TriggerCommandFamily family{TriggerCommandFamily::task};
    TriggerCommandSelection selection{TriggerCommandSelection::exact};
    TriggerCommandResponseMode response_mode{TriggerCommandResponseMode::single};
    TriggerConfigIdRequirement config_id{TriggerConfigIdRequirement::not_required};
    TriggerInboundBinaryPolicy inbound_binary{TriggerInboundBinaryPolicy::forbidden};
};

enum class TriggerCommandLookupClassification : std::uint8_t { known, unknown };

struct TriggerCommandLookupResult {
    TriggerCommandLookupClassification classification{
        TriggerCommandLookupClassification::unknown};
    const TriggerCommandDescriptor* descriptor{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return classification == TriggerCommandLookupClassification::known;
    }
};

// Allocation-free command selection metadata shared by trigger transports.
// Payload validation, execution, networking, and command-name admission remain
// outside this catalog.
class TriggerCommandCatalog final {
public:
    TriggerCommandCatalog() = delete;

    [[nodiscard]] static std::span<const TriggerCommandDescriptor> rules() noexcept;
    [[nodiscard]] static TriggerCommandLookupResult lookup(
        std::string_view command) noexcept;
};

[[nodiscard]] std::string_view trigger_command_family_name(
    TriggerCommandFamily family) noexcept;
[[nodiscard]] std::string_view trigger_command_selection_name(
    TriggerCommandSelection selection) noexcept;
[[nodiscard]] std::string_view trigger_command_response_mode_name(
    TriggerCommandResponseMode mode) noexcept;
[[nodiscard]] std::string_view trigger_config_id_requirement_name(
    TriggerConfigIdRequirement requirement) noexcept;
[[nodiscard]] std::string_view trigger_inbound_binary_policy_name(
    TriggerInboundBinaryPolicy policy) noexcept;
[[nodiscard]] std::string_view trigger_command_lookup_classification_name(
    TriggerCommandLookupClassification classification) noexcept;

}  // namespace baas::service::trigger
