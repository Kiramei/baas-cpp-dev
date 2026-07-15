#include "service/trigger/TriggerCommandCatalog.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

using namespace baas::service::trigger;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct Expected {
    std::string_view input;
    std::string_view canonical;
    TriggerCommandFamily family;
    TriggerCommandResponseMode mode;
    TriggerConfigIdRequirement config_id;
    TriggerInboundBinaryPolicy inbound_binary;
};

void test_complete_python_command_inventory()
{
    using enum TriggerCommandFamily;
    using enum TriggerCommandResponseMode;
    constexpr auto no_config = TriggerConfigIdRequirement::not_required;
    constexpr auto needs_config = TriggerConfigIdRequirement::required;
    constexpr auto no_binary = TriggerInboundBinaryPolicy::forbidden;
    constexpr auto needs_binary = TriggerInboundBinaryPolicy::required;
    constexpr std::array expected{
        Expected{"test_all_sha_stream", "test_all_sha_stream", diagnostics, stream,
                 no_config, no_binary},
        Expected{"update_to_latest_stream", "update_to_latest_stream", update, stream,
                 no_config, no_binary},
        Expected{"start_scheduler", "start_scheduler", scheduler, single, needs_config,
                 no_binary},
        Expected{"stop_scheduler", "stop_scheduler", scheduler, single, needs_config,
                 no_binary},
        Expected{"solve", "solve", task, single, needs_config, no_binary},
        Expected{"start_daily", "start_*", task, single, needs_config, no_binary},
        Expected{"add_config", "add_config*", configuration, single, no_config,
                 no_binary},
        Expected{"remove_config_remote", "remove_config*", configuration, single,
                 no_config, no_binary},
        Expected{"copy_config", "copy_config", configuration, single, no_config,
                 no_binary},
        Expected{"export_config", "export_config", configuration, single, no_config,
                 no_binary},
        Expected{"import_config", "import_config", configuration, single, no_config,
                 needs_binary},
        Expected{"detect_adb", "detect_adb", diagnostics, single, no_config,
                 no_binary},
        Expected{"valid_cdk", "valid_cdk", diagnostics, single, no_config,
                 no_binary},
        Expected{"test_all_sha", "test_all_sha", diagnostics, single, no_config,
                 no_binary},
        Expected{"check_for_update", "check_for_update", update, single, no_config,
                 no_binary},
        Expected{"update_setup_toml", "update_setup_toml", update, single, no_config,
                 no_binary},
        Expected{"update_to_latest", "update_to_latest", update, single, no_config,
                 no_binary},
        Expected{"restart_backend", "restart_backend", backend, single, no_config,
                 no_binary},
        Expected{"stop_all_tasks", "stop_all_tasks", task, single, no_config,
                 no_binary},
        Expected{"control_device", "control_device", device, single, needs_config,
                 no_binary},
        Expected{"status", "status", status, single, no_config, no_binary},
    };

    check(TriggerCommandCatalog::rules().size() == expected.size(),
          "catalog must expose exactly the Python selection-rule inventory");
    for (const auto& item : expected) {
        const auto result = TriggerCommandCatalog::lookup(item.input);
        check(static_cast<bool>(result), "every Python command route must be known");
        if (!result) continue;
        check(result.descriptor != nullptr, "known lookup must include metadata");
        check(result.descriptor->canonical_name == item.canonical,
              "lookup must return the stable canonical selector");
        check(result.descriptor->family == item.family,
              "lookup must return the stable command family");
        check(result.descriptor->response_mode == item.mode,
              "lookup must preserve single versus stream response mode");
        check(result.descriptor->config_id == item.config_id,
              "lookup must preserve config_id requirements");
        check(result.descriptor->inbound_binary == item.inbound_binary,
              "lookup must preserve inbound binary policy");
    }
}

void test_prefix_semantics_and_order()
{
    const auto start_scheduler = TriggerCommandCatalog::lookup("start_scheduler");
    check(start_scheduler && start_scheduler.descriptor->canonical_name == "start_scheduler",
          "exact start_scheduler must win before the start_ family");

    for (const auto input : {"start_", "start_scheduler_extra", "start__private"}) {
        const auto result = TriggerCommandCatalog::lookup(input);
        check(result && result.descriptor->canonical_name == "start_*",
              "start_ must use Python startswith semantics");
    }
    for (const auto input : {"add_config", "add_configuration", "add_config_v2"}) {
        const auto result = TriggerCommandCatalog::lookup(input);
        check(result && result.descriptor->canonical_name == "add_config*",
              "add_config family must use the source's broad prefix rule");
    }
    for (const auto input : {"remove_config", "remove_configuration", "remove_config_v2"}) {
        const auto result = TriggerCommandCatalog::lookup(input);
        check(result && result.descriptor->canonical_name == "remove_config*",
              "remove_config family must use the source's broad prefix rule");
    }
}

void test_unknown_and_hostile_inputs()
{
    constexpr std::array unknown{
        "", "start", "Start_daily", " add_config", "copy_config_extra",
        "test_all_sha_stream_extra", "update_to_latest_stream_extra", "status_extra",
        "remove_confi", "\xF0\x28\x8C\x28"};
    for (const auto input : unknown) {
        const auto result = TriggerCommandCatalog::lookup(input);
        check(!result, "unsupported command must retain a stable unknown classification");
        check(result.classification == TriggerCommandLookupClassification::unknown,
              "unsupported command classification must be unknown");
        check(result.descriptor == nullptr,
              "unknown lookup must not expose unrelated command metadata");
    }

    const std::string exact_with_nul{"status\0extra", 12};
    check(!TriggerCommandCatalog::lookup(exact_with_nul),
          "exact rules must be length-aware for embedded NUL input");
    const std::string prefix_with_nul{"start_\0extra", 12};
    const auto prefix_result = TriggerCommandCatalog::lookup(prefix_with_nul);
    check(prefix_result && prefix_result.descriptor->canonical_name == "start_*",
          "prefix lookup must remain byte-length-aware and mirror startswith");

    std::string oversized_unknown(1U * 1'024U * 1'024U, 'x');
    check(!TriggerCommandCatalog::lookup(oversized_unknown),
          "large unsupported input must classify without truncation");
    std::string oversized_prefix = "add_config";
    oversized_prefix.append(1U * 1'024U * 1'024U, 'x');
    const auto oversized_result = TriggerCommandCatalog::lookup(oversized_prefix);
    check(oversized_result && oversized_result.descriptor->canonical_name == "add_config*",
          "catalog classification must not silently become an admission size gate");
}

void test_table_and_stable_names()
{
    const auto rules = TriggerCommandCatalog::rules();
    check(rules[0].canonical_name == "test_all_sha_stream"
              && rules[1].canonical_name == "update_to_latest_stream"
              && rules[2].canonical_name == "start_scheduler"
              && rules[5].canonical_name == "start_*"
              && rules[6].canonical_name == "add_config*"
              && rules[7].canonical_name == "remove_config*",
          "public rule order must preserve Python dispatch precedence");

    std::size_t prefix_count = 0;
    std::size_t stream_count = 0;
    std::size_t config_id_count = 0;
    std::size_t inbound_binary_count = 0;
    for (const auto& rule : rules) {
        prefix_count += rule.selection == TriggerCommandSelection::prefix ? 1U : 0U;
        stream_count += rule.response_mode == TriggerCommandResponseMode::stream ? 1U : 0U;
        config_id_count += rule.config_id == TriggerConfigIdRequirement::required ? 1U : 0U;
        inbound_binary_count +=
            rule.inbound_binary == TriggerInboundBinaryPolicy::required ? 1U : 0U;
        if (rule.inbound_binary == TriggerInboundBinaryPolicy::required) {
            check(rule.canonical_name == "import_config",
                  "import_config must be the only inbound binary command");
        }
    }
    check(prefix_count == 3, "catalog must contain exactly three Python prefix families");
    check(stream_count == 2, "catalog must contain exactly two stream commands");
    check(config_id_count == 5,
          "catalog must contain exactly five config_id-requiring selectors");
    check(inbound_binary_count == 1,
          "catalog must contain exactly one inbound binary command");

    const auto first = TriggerCommandCatalog::lookup("status");
    const auto second = TriggerCommandCatalog::lookup("status");
    check(first.descriptor == second.descriptor,
          "repeated lookups must return stable static metadata");
    check(trigger_command_family_name(TriggerCommandFamily::configuration)
              == "configuration"
              && trigger_command_selection_name(TriggerCommandSelection::prefix) == "prefix"
              && trigger_command_response_mode_name(TriggerCommandResponseMode::stream)
                  == "stream"
              && trigger_config_id_requirement_name(TriggerConfigIdRequirement::required)
                  == "required"
              && trigger_inbound_binary_policy_name(TriggerInboundBinaryPolicy::forbidden)
                  == "forbidden"
              && trigger_command_lookup_classification_name(
                     TriggerCommandLookupClassification::unknown) == "unknown",
          "metadata names must be stable wire/log vocabulary");

    check(trigger_command_family_name(static_cast<TriggerCommandFamily>(255)) == "unknown"
              && trigger_command_selection_name(
                     static_cast<TriggerCommandSelection>(255)) == "unknown"
              && trigger_command_response_mode_name(
                     static_cast<TriggerCommandResponseMode>(255)) == "unknown"
              && trigger_config_id_requirement_name(
                     static_cast<TriggerConfigIdRequirement>(255)) == "unknown"
              && trigger_inbound_binary_policy_name(
                     static_cast<TriggerInboundBinaryPolicy>(255)) == "unknown"
              && trigger_command_lookup_classification_name(
                     static_cast<TriggerCommandLookupClassification>(255)) == "unknown",
          "out-of-range metadata values must have total stable classification");
}

}  // namespace

int main()
{
    test_complete_python_command_inventory();
    test_prefix_semantics_and_order();
    test_unknown_and_hostile_inputs();
    test_table_and_stable_names();
    if (failures != 0) {
        std::cerr << failures << " trigger command catalog test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "trigger command catalog tests passed\n";
    return EXIT_SUCCESS;
}
