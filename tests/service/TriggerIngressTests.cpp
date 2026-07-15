#include "service/protocol/TriggerIngress.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace trigger = baas::service::protocol::trigger;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void check(const trigger::TriggerIngressResult& result, const std::string_view message)
{
    check(static_cast<bool>(result), message);
}

void check_command_rejection(
    const trigger::TriggerIngressResult& result,
    const trigger::TriggerIngressError error,
    const std::string_view command_name,
    const trigger::Timestamp timestamp,
    const std::string_view message
)
{
    check(!result && result.error == error
              && result.disposition()
                  == trigger::TriggerIngressDisposition::command_rejection
              && result.command_rejection
              && result.command_rejection->command == command_name
              && result.command_rejection->timestamp == timestamp
              && result.command_rejection->code == error
              && !result.command_rejection->code_name().empty()
              && !result.command_rejection->safe_message().empty(),
          message);
}

[[nodiscard]] std::string command(
    const std::uint64_t timestamp,
    const std::string_view name = "status",
    const std::string_view payload = "{}",
    const std::string_view config_id = "null"
)
{
    return std::string{"{\"type\":\"command\",\"command\":\""} + std::string{name}
        + "\",\"timestamp\":" + std::to_string(timestamp)
        + ",\"config_id\":" + std::string{config_id}
        + ",\"payload\":" + std::string{payload} + "}";
}

[[nodiscard]] std::string binary_command(const std::uint64_t timestamp)
{
    return command(
        timestamp,
        "import_config",
        R"({"binary":true,"name":"owned"})"
    );
}

void test_json_only_ready_item_is_owned_and_single_outstanding()
{
    trigger::TriggerIngress ingress;
    auto frame = command(1, "test_all_sha_stream", R"({"name":"alpha"})");
    const auto accepted = ingress.receive_json_frame(frame);
    check(accepted && accepted.outcome == trigger::TriggerIngressOutcome::ready,
          "JSON-only command must complete immediately");
    check(ingress.state() == trigger::TriggerIngressState::ready,
          "completed command must reserve the sole ready slot");
    frame.assign(frame.size(), 'x');

    auto item = ingress.take_ready();
    check(item && !item->has_binary(),
          "JSON-only ready item must distinguish absent binary input");
    if (item) {
        check(item->envelope().command == "test_all_sha_stream"
                  && item->envelope().timestamp == 1
                  && item->envelope().payload_json == R"({"name":"alpha"})",
              "ready item must own decoded envelope data after source mutation");
        check(item->build_admission()
                  && item->admission().command == "test_all_sha_stream"
                  && item->admission().timestamp == 1
                  && item->admission().payload_bytes
                      == item->envelope().payload_json.size()
                  && item->admission().binary_bytes == 0
                  && item->admission().response_mode == trigger::ResponseMode::stream,
              "catalog stream policy must flow into the owned admission");
    }
    check(ingress.state() == trigger::TriggerIngressState::accepting_json,
          "taking the item must release the one-outstanding gate");
    check(ingress.receive_json_frame(command(2)),
          "next command must be accepted after take_ready");

    trigger::TriggerIngress overwritten;
    check(overwritten.receive_json_frame(command(3)),
          "item_pending fatality test must create one ready item");
    const auto blocked = overwritten.receive_json_frame(command(4));
    check(!blocked && blocked.error == trigger::TriggerIngressError::item_pending
              && blocked.disposition() == trigger::TriggerIngressDisposition::fatal
              && !blocked.command_rejection
              && overwritten.state() == trigger::TriggerIngressState::closed
              && !overwritten.take_ready(),
          "consumed frame while an item is pending must close instead of losing a command");
}

void test_declared_binary_is_adjacent_owned_and_zero_length_distinct()
{
    trigger::TriggerIngress ingress;
    const auto waiting = ingress.receive_json_frame(binary_command(10));
    check(waiting && waiting.outcome == trigger::TriggerIngressOutcome::awaiting_binary
              && ingress.state() == trigger::TriggerIngressState::awaiting_binary,
          "only declared import_config input must await the adjacent binary frame");

    const auto empty = ingress.receive_binary_frame(std::span<const std::byte>{});
    check(empty && empty.outcome == trigger::TriggerIngressOutcome::ready,
          "a present zero-length binary frame must complete the command");
    auto empty_item = ingress.take_ready();
    check(empty_item && empty_item->has_binary()
              && empty_item->binary()->empty()
              && empty_item->admission().binary_bytes == 0,
          "present empty binary must remain distinct from absent binary");

    check(ingress.receive_json_frame(binary_command(11)),
          "next binary command declaration must be accepted");
    std::vector<std::byte> source{
        std::byte{0x01}, std::byte{0x7F}, std::byte{0xA5},
    };
    check(ingress.receive_binary_frame(source),
          "declared non-empty binary frame must complete");
    source.assign(source.size(), std::byte{0x00});
    auto item = ingress.take_ready();
    check(item && item->has_binary() && item->binary()->size() == 3
              && (*item->binary())[0] == std::byte{0x01}
              && (*item->binary())[2] == std::byte{0xA5}
              && item->admission().binary_bytes == 3,
          "ready item must own byte-exact input independently of transport storage");
}

void test_binary_marker_gate_and_strict_frame_order()
{
    const std::array stray{std::byte{0x01}};
    trigger::TriggerIngress stray_ingress;
    auto result = stray_ingress.receive_binary_frame(stray);
    check(!result
              && result.error == trigger::TriggerIngressError::binary_without_declaration
              && result.disposition() == trigger::TriggerIngressDisposition::fatal
              && !result.command_rejection
              && stray_ingress.state() == trigger::TriggerIngressState::closed,
          "binary input without a declaration must close without correlation");

    trigger::TriggerIngress ordering_ingress;
    check(ordering_ingress.receive_json_frame(binary_command(20)),
          "binary declaration must enter awaiting state");
    result = ordering_ingress.receive_json_frame(command(21));
    check(!result
              && result.error
                  == trigger::TriggerIngressError::json_while_awaiting_binary
              && result.disposition() == trigger::TriggerIngressDisposition::fatal
              && !result.command_rejection
              && ordering_ingress.state() == trigger::TriggerIngressState::closed,
          "JSON instead of promised binary must close without forged correlation");
    result = ordering_ingress.receive_binary_frame(stray);
    check(!result
              && result.error == trigger::TriggerIngressError::closed
              && result.disposition() == trigger::TriggerIngressDisposition::closed,
          "late binary must see permanent closure after an ordering failure");

    trigger::TriggerIngress policy_ingress;
    result = policy_ingress.receive_json_frame(command(
        22, "import_config", R"({"binary":false})"));
    check_command_rejection(
        result, trigger::TriggerIngressError::binary_marker_required,
        "import_config", 22,
          "import_config must declare its required binary at ingress");
    check(policy_ingress.state() == trigger::TriggerIngressState::accepting_json,
          "binary marker policy rejection must keep correlation input available");

    result = policy_ingress.receive_json_frame(command(
        23, "status", R"({"binary":true})"));
    check_command_rejection(
        result, trigger::TriggerIngressError::binary_marker_forbidden,
        "status", 23,
          "catalog-forbidden binary marker must fail before frame state changes");
    check(policy_ingress.receive_json_frame(command(24)),
          "command-level marker rejections must allow the next command");
}

void test_catalog_policy_and_direct_session_admission()
{
    trigger::TriggerIngress ingress;
    auto result = ingress.receive_json_frame(command(24, "not_a_command"));
    check_command_rejection(
        result, trigger::TriggerIngressError::unknown_command,
        "not_a_command", 24,
          "unknown commands must have a stable ingress rejection");
    check(ingress.state() == trigger::TriggerIngressState::accepting_json,
          "unknown command rejection must preserve the serial ingress");

    result = ingress.receive_json_frame(command(25, "start_scheduler"));
    check_command_rejection(
        result, trigger::TriggerIngressError::config_id_required,
        "start_scheduler", 25,
          "catalog-required config id must reject absence");
    result = ingress.receive_json_frame(command(
        26, "start_scheduler", "{}", R"("")"));
    check_command_rejection(
        result, trigger::TriggerIngressError::config_id_required,
        "start_scheduler", 26,
          "catalog-required config id must reject an empty string");

    check(ingress.receive_json_frame(command(261, "status", "{}", R"("")")),
          "commands without a config requirement must preserve an empty config id");
    auto optional_config_item = ingress.take_ready();
    trigger::TriggerSession optional_config_session;
    check(optional_config_item
              && optional_config_item->envelope().config_id
              && optional_config_item->envelope().config_id->empty()
              && optional_config_item->admit_to(optional_config_session).outcome
                  == trigger::TriggerIngressOutcome::admitted,
          "status with present-empty config id must become ready and admit safely");

    check(ingress.receive_json_frame(command(
              27, "start_custom", "{}", R"("cfg")")),
          "known prefix command with config id must become ready");
    auto item = ingress.take_ready();
    check(item && item->descriptor().canonical_name == "start_*"
              && item->admission().response_mode == trigger::ResponseMode::single,
          "ready item must retain the exact catalog decision and response mode");
    trigger::TriggerSession session;
    check(item && item->admit_to(session).outcome
              == trigger::TriggerIngressOutcome::admitted,
          "catalog-derived ready item must admit directly to TriggerSession");
    const auto duplicate = item
        ? item->admit_to(session) : trigger::TriggerIngressResult{};
    check_command_rejection(
        duplicate, trigger::TriggerIngressError::admission_rejected,
        "start_custom", 27,
        "session admission rejection must preserve safe correlation identity");
    check(duplicate.admission_error == trigger::AdmissionError::duplicate_timestamp,
          "session admission rejection must retain its stable admission subtype");

    trigger::TriggerIngress closed_ingress;
    check(closed_ingress.receive_json_frame(command(28)),
          "closed-session policy test must produce one ready item");
    auto closed_item = closed_ingress.take_ready();
    trigger::TriggerSession closed_session;
    static_cast<void>(closed_session.close());
    const auto closed_admission = closed_item
        ? closed_item->admit_to(closed_session) : trigger::TriggerIngressResult{};
    check(!closed_admission
              && closed_admission.error == trigger::TriggerIngressError::closed
              && closed_admission.admission_error == trigger::AdmissionError::closed
              && closed_admission.disposition()
                  == trigger::TriggerIngressDisposition::closed
              && !closed_admission.command_rejection,
          "closed session admission must not claim a sendable command rejection");
}

void test_frame_and_aggregate_limits_are_connection_fatal()
{
    const auto plain = command(30);
    const auto declared = binary_command(31);

    trigger::TriggerIngressLimits json_limits;
    json_limits.max_json_frame_bytes = plain.size() - 1;
    trigger::TriggerIngress json_bounded{json_limits};
    auto result = json_bounded.receive_json_frame(plain);
    check(!result && result.error == trigger::TriggerIngressError::json_too_large
              && result.disposition() == trigger::TriggerIngressDisposition::fatal
              && !result.command_rejection
              && json_bounded.state() == trigger::TriggerIngressState::closed,
          "oversized JSON frame must close without correlation");

    trigger::TriggerIngressLimits binary_limits;
    binary_limits.max_binary_frame_bytes = 2;
    trigger::TriggerIngress binary_bounded{binary_limits};
    check(binary_bounded.receive_json_frame(declared),
          "bounded ingress must accept the declaration itself");
    const std::array too_large{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
    };
    result = binary_bounded.receive_binary_frame(too_large);
    check(!result && result.error == trigger::TriggerIngressError::binary_too_large
              && result.disposition() == trigger::TriggerIngressDisposition::fatal
              && !result.command_rejection
              && binary_bounded.state() == trigger::TriggerIngressState::closed,
          "oversized promised binary must close and clear the partial command");

    trigger::TriggerIngressLimits aggregate_json_limits;
    aggregate_json_limits.max_aggregate_bytes = plain.size() - 1;
    trigger::TriggerIngress aggregate_json{aggregate_json_limits};
    result = aggregate_json.receive_json_frame(plain);
    check(!result && result.error == trigger::TriggerIngressError::aggregate_too_large
              && result.disposition() == trigger::TriggerIngressDisposition::fatal
              && aggregate_json.state() == trigger::TriggerIngressState::closed,
          "aggregate JSON gate must be connection-fatal");

    trigger::TriggerIngressLimits aggregate_binary_limits;
    aggregate_binary_limits.max_binary_frame_bytes = 8;
    aggregate_binary_limits.max_aggregate_bytes = declared.size() + 1;
    trigger::TriggerIngress aggregate_binary{aggregate_binary_limits};
    check(aggregate_binary.receive_json_frame(declared),
          "aggregate test declaration must enter awaiting state");
    const std::array two_bytes{std::byte{0x01}, std::byte{0x02}};
    result = aggregate_binary.receive_binary_frame(two_bytes);
    check(!result && result.error == trigger::TriggerIngressError::aggregate_too_large
              && result.disposition() == trigger::TriggerIngressDisposition::fatal
              && !result.command_rejection
              && aggregate_binary.state() == trigger::TriggerIngressState::closed,
          "aggregate binary overflow must close and clear the pending envelope");
}

void test_envelope_failures_are_connection_fatal()
{
    trigger::TriggerIngress ingress;
    auto result = ingress.receive_json_frame(
        R"({"type":"command","command":"status","command":"solve","timestamp":40})"
    );
    check(!result && result.error == trigger::TriggerIngressError::envelope_rejected
              && result.envelope_error == trigger::EnvelopeError::duplicate_key
              && result.disposition() == trigger::TriggerIngressDisposition::fatal
              && !result.command_rejection
              && ingress.state() == trigger::TriggerIngressState::closed,
          "codec failure must preserve detail but never forge correlation");

    bool rejected = false;
    try {
        trigger::TriggerIngressLimits invalid;
        invalid.max_json_frame_bytes = 0;
        [[maybe_unused]] trigger::TriggerIngress value{invalid};
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "zero ingress limits must fail construction");

    rejected = false;
    try {
        trigger::TriggerIngressLimits invalid;
        invalid.envelope.max_depth = 257;
        [[maybe_unused]] trigger::TriggerIngress value{invalid};
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "invalid envelope limits must fail ingress construction");
}

void test_reset_and_close_are_explicit_and_terminal()
{
    trigger::TriggerIngress reset_pending;
    check(reset_pending.receive_json_frame(binary_command(50)),
          "reset test must enter awaiting state");
    reset_pending.reset();
    check(reset_pending.state() == trigger::TriggerIngressState::accepting_json,
          "reset must discard partial binary state");
    const auto late = reset_pending.receive_binary_frame(
        std::span<const std::byte>{});
    check(late.error == trigger::TriggerIngressError::binary_without_declaration
              && late.disposition() == trigger::TriggerIngressDisposition::fatal
              && reset_pending.state() == trigger::TriggerIngressState::closed,
          "late binary after reset must close and cannot attach to another command");

    trigger::TriggerIngress reset_ready;
    check(reset_ready.receive_json_frame(command(51)),
          "ready reset test must complete a command");
    reset_ready.reset();
    check(!reset_ready.take_ready()
              && reset_ready.state() == trigger::TriggerIngressState::accepting_json
              && reset_ready.receive_json_frame(command(52)),
          "reset must explicitly discard an untaken ready item");

    trigger::TriggerIngress ingress;
    check(ingress.receive_json_frame(binary_command(53)),
          "close test must enter awaiting state");
    ingress.close();
    ingress.close();
    check(ingress.state() == trigger::TriggerIngressState::closed
              && !ingress.take_ready(),
          "close must be idempotent and discard all outstanding input");
    auto result = ingress.receive_json_frame(command(54));
    check(!result && result.error == trigger::TriggerIngressError::closed
              && result.disposition() == trigger::TriggerIngressDisposition::closed
              && !result.command_rejection,
          "closed ingress must reject JSON forever");
    result = ingress.receive_binary_frame(std::span<const std::byte>{});
    check(!result && result.error == trigger::TriggerIngressError::closed
              && result.disposition() == trigger::TriggerIngressDisposition::closed,
          "closed ingress must reject binary forever");
    ingress.reset();
    check(ingress.state() == trigger::TriggerIngressState::closed,
          "reset must never reopen a closed ingress");
}

void test_error_disposition_matrix_and_names_are_total()
{
    using TriggerIngressDisposition = trigger::TriggerIngressDisposition;
    struct Expected {
        trigger::TriggerIngressError error;
        trigger::TriggerIngressDisposition disposition;
        std::string_view name;
    };
    using enum trigger::TriggerIngressError;
    constexpr std::array expected{
        Expected{none, TriggerIngressDisposition::none, "none"},
        Expected{trigger::TriggerIngressError::closed, TriggerIngressDisposition::closed,
                 "closed"},
        Expected{item_pending, TriggerIngressDisposition::fatal, "item_pending"},
        Expected{json_while_awaiting_binary, TriggerIngressDisposition::fatal,
                 "json_while_awaiting_binary"},
        Expected{binary_without_declaration, TriggerIngressDisposition::fatal,
                 "binary_without_declaration"},
        Expected{json_too_large, TriggerIngressDisposition::fatal, "json_too_large"},
        Expected{binary_too_large, TriggerIngressDisposition::fatal,
                 "binary_too_large"},
        Expected{aggregate_too_large, TriggerIngressDisposition::fatal,
                 "aggregate_too_large"},
        Expected{envelope_rejected, TriggerIngressDisposition::fatal,
                 "envelope_rejected"},
        Expected{unknown_command, TriggerIngressDisposition::command_rejection,
                 "unknown_command"},
        Expected{config_id_required, TriggerIngressDisposition::command_rejection,
                 "config_id_required"},
        Expected{binary_marker_required, TriggerIngressDisposition::command_rejection,
                 "binary_marker_required"},
        Expected{binary_marker_forbidden, TriggerIngressDisposition::command_rejection,
                 "binary_marker_forbidden"},
        Expected{admission_rejected, TriggerIngressDisposition::command_rejection,
                 "admission_rejected"},
    };
    for (const auto& item : expected) {
        check(trigger::trigger_ingress_error_name(item.error) == item.name
                  && trigger::trigger_ingress_disposition(item.error)
                      == item.disposition,
              "every ingress error must have one stable executable disposition");
    }
    check(trigger::trigger_ingress_disposition_name(
              TriggerIngressDisposition::none) == "none"
              && trigger::trigger_ingress_disposition_name(
                     TriggerIngressDisposition::fatal) == "fatal"
              && trigger::trigger_ingress_disposition_name(
                     TriggerIngressDisposition::recoverable)
                  == "recoverable"
              && trigger::trigger_ingress_disposition_name(
                     TriggerIngressDisposition::command_rejection)
                  == "command_rejection"
              && trigger::trigger_ingress_disposition_name(
                     TriggerIngressDisposition::closed) == "closed",
          "all disposition vocabulary must remain stable");
    check(trigger::trigger_ingress_error_name(
              static_cast<trigger::TriggerIngressError>(255)) == "unknown"
              && trigger::trigger_ingress_disposition(
                     static_cast<trigger::TriggerIngressError>(255))
                  == TriggerIngressDisposition::fatal
              && trigger::trigger_ingress_disposition_name(
                     static_cast<trigger::TriggerIngressDisposition>(255)) == "unknown",
          "unknown future error values must fail closed with total names");
    check(trigger::trigger_command_rejection_safe_message(unknown_command)
              == "Unsupported command"
              && trigger::trigger_command_rejection_safe_message(config_id_required)
                  == "config_id is required"
              && trigger::trigger_command_rejection_safe_message(
                     binary_marker_required)
                  == "binary archive payload is required for import_config"
              && trigger::trigger_command_rejection_safe_message(
                     binary_marker_forbidden)
                  == "binary payload is not allowed for this command"
              && trigger::trigger_command_rejection_safe_message(admission_rejected)
                  == "command admission rejected"
              && trigger::trigger_command_rejection_safe_message(json_too_large)
                  == "command rejected",
          "command rejection messages must be bounded static text");
}

}  // namespace

int main()
{
    test_json_only_ready_item_is_owned_and_single_outstanding();
    test_declared_binary_is_adjacent_owned_and_zero_length_distinct();
    test_binary_marker_gate_and_strict_frame_order();
    test_catalog_policy_and_direct_session_admission();
    test_frame_and_aggregate_limits_are_connection_fatal();
    test_envelope_failures_are_connection_fatal();
    test_reset_and_close_are_explicit_and_terminal();
    test_error_disposition_matrix_and_names_are_total();
    if (failures != 0) {
        std::cerr << failures << " trigger ingress test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "trigger ingress tests passed\n";
    return EXIT_SUCCESS;
}
