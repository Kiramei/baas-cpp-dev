#include "service/trigger/TriggerCommandCatalog.h"

#include <array>

namespace baas::service::trigger {
namespace {

using enum TriggerCommandFamily;
using enum TriggerCommandResponseMode;

constexpr auto exact = TriggerCommandSelection::exact;
constexpr auto prefix = TriggerCommandSelection::prefix;
constexpr auto no_config = TriggerConfigIdRequirement::not_required;
constexpr auto needs_config = TriggerConfigIdRequirement::required;
constexpr auto no_binary = TriggerInboundBinaryPolicy::forbidden;
constexpr auto needs_binary = TriggerInboundBinaryPolicy::required;

// Order mirrors service/channels/trigger.py followed by
// service/api/commands.py. Prefix rules intentionally use Python startswith
// semantics, including the bare prefix itself.
constexpr std::array<TriggerCommandDescriptor, 21> command_rules{{
    {"test_all_sha_stream", diagnostics, exact, stream, no_config, no_binary},
    {"update_to_latest_stream", update, exact, stream, no_config, no_binary},
    {"start_scheduler", scheduler, exact, single, needs_config, no_binary},
    {"stop_scheduler", scheduler, exact, single, needs_config, no_binary},
    {"solve", task, exact, single, needs_config, no_binary},
    {"start_*", task, prefix, single, needs_config, no_binary},
    {"add_config*", configuration, prefix, single, no_config, no_binary},
    {"remove_config*", configuration, prefix, single, no_config, no_binary},
    {"copy_config", configuration, exact, single, no_config, no_binary},
    {"export_config", configuration, exact, single, no_config, no_binary},
    {"import_config", configuration, exact, single, no_config, needs_binary},
    {"detect_adb", diagnostics, exact, single, no_config, no_binary},
    {"valid_cdk", diagnostics, exact, single, no_config, no_binary},
    {"test_all_sha", diagnostics, exact, single, no_config, no_binary},
    {"check_for_update", update, exact, single, no_config, no_binary},
    {"update_setup_toml", update, exact, single, no_config, no_binary},
    {"update_to_latest", update, exact, single, no_config, no_binary},
    {"restart_backend", backend, exact, single, no_config, no_binary},
    {"stop_all_tasks", task, exact, single, no_config, no_binary},
    {"control_device", device, exact, single, needs_config, no_binary},
    {"status", status, exact, single, no_config, no_binary},
}};

[[nodiscard]] constexpr bool matches(
    const TriggerCommandDescriptor& rule, const std::string_view command) noexcept
{
    if (rule.selection == TriggerCommandSelection::exact)
        return command == rule.canonical_name;
    if (rule.canonical_name.empty() || rule.canonical_name.back() != '*')
        return false;
    const auto prefix_size = rule.canonical_name.size() - 1;
    return command.size() >= prefix_size
        && command.substr(0, prefix_size)
            == rule.canonical_name.substr(0, prefix_size);
}

}  // namespace

std::span<const TriggerCommandDescriptor> TriggerCommandCatalog::rules() noexcept
{
    return command_rules;
}

TriggerCommandLookupResult TriggerCommandCatalog::lookup(
    const std::string_view command) noexcept
{
    for (const auto& rule : command_rules) {
        if (matches(rule, command))
            return {TriggerCommandLookupClassification::known, &rule};
    }
    return {};
}

std::string_view trigger_command_family_name(
    const TriggerCommandFamily family) noexcept
{
    using enum TriggerCommandFamily;
    switch (family) {
        case scheduler: return "scheduler";
        case task: return "task";
        case configuration: return "configuration";
        case diagnostics: return "diagnostics";
        case update: return "update";
        case backend: return "backend";
        case device: return "device";
        case status: return "status";
    }
    return "unknown";
}

std::string_view trigger_command_selection_name(
    const TriggerCommandSelection selection) noexcept
{
    using enum TriggerCommandSelection;
    switch (selection) {
        case exact: return "exact";
        case prefix: return "prefix";
    }
    return "unknown";
}

std::string_view trigger_command_response_mode_name(
    const TriggerCommandResponseMode mode) noexcept
{
    using enum TriggerCommandResponseMode;
    switch (mode) {
        case single: return "single";
        case stream: return "stream";
    }
    return "unknown";
}

std::string_view trigger_config_id_requirement_name(
    const TriggerConfigIdRequirement requirement) noexcept
{
    using enum TriggerConfigIdRequirement;
    switch (requirement) {
        case not_required: return "not_required";
        case required: return "required";
    }
    return "unknown";
}

std::string_view trigger_inbound_binary_policy_name(
    const TriggerInboundBinaryPolicy policy) noexcept
{
    using enum TriggerInboundBinaryPolicy;
    switch (policy) {
        case forbidden: return "forbidden";
        case required: return "required";
    }
    return "unknown";
}

std::string_view trigger_command_lookup_classification_name(
    const TriggerCommandLookupClassification classification) noexcept
{
    using enum TriggerCommandLookupClassification;
    switch (classification) {
        case known: return "known";
        case unknown: return "unknown";
    }
    return "unknown";
}

}  // namespace baas::service::trigger
