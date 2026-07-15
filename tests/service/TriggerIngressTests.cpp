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

[[nodiscard]] std::string command(
    const std::uint64_t timestamp,
    const std::string_view name = "status",
    const std::string_view payload = "{}"
)
{
    return std::string{"{\"type\":\"command\",\"command\":\""} + std::string{name}
        + "\",\"timestamp\":" + std::to_string(timestamp)
        + ",\"config_id\":null,\"payload\":" + std::string{payload} + "}";
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
    auto frame = command(1, "status", R"({"name":"alpha"})");
    const auto accepted = ingress.receive_json_frame(
        frame, trigger::ResponseMode::stream
    );
    check(accepted && accepted.outcome == trigger::TriggerIngressOutcome::ready,
          "JSON-only command must complete immediately");
    check(ingress.state() == trigger::TriggerIngressState::ready,
          "completed command must reserve the sole ready slot");

    const auto blocked = ingress.receive_json_frame(command(2));
    check(!blocked && blocked.error == trigger::TriggerIngressError::item_pending,
          "a second command must not overwrite an untaken ready item");
    frame.assign(frame.size(), 'x');

    auto item = ingress.take_ready();
    check(item && !item->has_binary(),
          "JSON-only ready item must distinguish absent binary input");
    if (item) {
        check(item->envelope().command == "status"
                  && item->envelope().timestamp == 1
                  && item->envelope().payload_json == R"({"name":"alpha"})",
              "ready item must own decoded envelope data after source mutation");
        check(item->build_admission()
                  && item->admission().command == "status"
                  && item->admission().timestamp == 1
                  && item->admission().payload_bytes
                      == item->envelope().payload_json.size()
                  && item->admission().binary_bytes == 0
                  && item->admission().response_mode == trigger::ResponseMode::stream,
              "ready item must own the matching BuildAdmissionResult and admission");
    }
    check(ingress.state() == trigger::TriggerIngressState::accepting_json,
          "taking the item must release the one-outstanding gate");
    check(ingress.receive_json_frame(command(2)),
          "next command must be accepted after the ready item is taken");
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
    trigger::TriggerIngress ingress;
    const std::array stray{std::byte{0x01}};
    auto result = ingress.receive_binary_frame(stray);
    check(!result
              && result.error == trigger::TriggerIngressError::binary_without_declaration,
          "binary input without a declaration must be rejected");

    check(ingress.receive_json_frame(binary_command(20)),
          "binary declaration must enter awaiting state");
    result = ingress.receive_json_frame(command(21));
    check(!result
              && result.error
                  == trigger::TriggerIngressError::json_while_awaiting_binary
              && ingress.state() == trigger::TriggerIngressState::accepting_json,
          "JSON instead of promised binary must reject and clear partial state");
    result = ingress.receive_binary_frame(stray);
    check(!result
              && result.error == trigger::TriggerIngressError::binary_without_declaration,
          "late binary must not attach after an ordering failure");
    check(ingress.receive_json_frame(command(21)),
          "clean JSON must recover immediately after ordering failure");
    result = ingress.receive_binary_frame(stray);
    check(!result && result.error == trigger::TriggerIngressError::item_pending,
          "frames must not overwrite a completed untaken command");
    static_cast<void>(ingress.take_ready());

    check(ingress.receive_json_frame(command(
              22, "import_config", R"({"binary":false})"
          )),
          "false binary marker must complete as JSON-only");
    auto item = ingress.take_ready();
    check(item && !item->has_binary(),
          "false binary marker must not consume a binary frame");

    check(ingress.receive_json_frame(command(
              23, "status", R"({"binary":true})"
          )),
          "binary marker on another command must remain JSON-only");
    item = ingress.take_ready();
    check(item && !item->has_binary(),
          "only import_config may declare inbound binary");
}

void test_frame_and_aggregate_limits_clear_partial_state()
{
    const auto plain = command(30);
    const auto declared = binary_command(31);

    trigger::TriggerIngressLimits json_limits;
    json_limits.max_json_frame_bytes = plain.size() - 1;
    trigger::TriggerIngress json_bounded{json_limits};
    auto result = json_bounded.receive_json_frame(plain);
    check(!result && result.error == trigger::TriggerIngressError::json_too_large
              && json_bounded.state() == trigger::TriggerIngressState::accepting_json,
          "oversized JSON frame must reject without partial state");

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
              && binary_bounded.state()
                  == trigger::TriggerIngressState::accepting_json,
          "oversized promised binary must consume and clear the partial command");
    check(binary_bounded.receive_json_frame(command(32)),
          "binary limit failure must permit a clean next command");

    trigger::TriggerIngressLimits aggregate_json_limits;
    aggregate_json_limits.max_aggregate_bytes = plain.size() - 1;
    trigger::TriggerIngress aggregate_json{aggregate_json_limits};
    result = aggregate_json.receive_json_frame(plain);
    check(!result && result.error == trigger::TriggerIngressError::aggregate_too_large,
          "aggregate gate must independently bound JSON-only commands");

    trigger::TriggerIngressLimits aggregate_binary_limits;
    aggregate_binary_limits.max_binary_frame_bytes = 8;
    aggregate_binary_limits.max_aggregate_bytes = declared.size() + 1;
    trigger::TriggerIngress aggregate_binary{aggregate_binary_limits};
    check(aggregate_binary.receive_json_frame(declared),
          "aggregate test declaration must enter awaiting state");
    const std::array two_bytes{std::byte{0x01}, std::byte{0x02}};
    result = aggregate_binary.receive_binary_frame(two_bytes);
    check(!result && result.error == trigger::TriggerIngressError::aggregate_too_large
              && aggregate_binary.state()
                  == trigger::TriggerIngressState::accepting_json,
          "aggregate binary overflow must clear the pending envelope");
    check(aggregate_binary.receive_json_frame(command(33)),
          "aggregate failure must recover for the next complete command");
}

void test_envelope_failures_modes_and_limit_validation_recover()
{
    trigger::TriggerIngress ingress;
    auto result = ingress.receive_json_frame(
        R"({"type":"command","command":"status","command":"solve","timestamp":40})"
    );
    check(!result && result.error == trigger::TriggerIngressError::envelope_rejected
              && result.envelope_error == trigger::EnvelopeError::duplicate_key,
          "ingress must preserve bounded codec error classification");
    check(ingress.state() == trigger::TriggerIngressState::accepting_json
              && ingress.receive_json_frame(command(40)),
          "malicious JSON must not poison later valid input");
    static_cast<void>(ingress.take_ready());

    result = ingress.receive_json_frame(
        command(41), static_cast<trigger::ResponseMode>(99)
    );
    check(!result && result.error == trigger::TriggerIngressError::invalid_response_mode
              && ingress.state() == trigger::TriggerIngressState::accepting_json,
          "invalid caller-supplied response mode must fail before partial state");

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
    trigger::TriggerIngress ingress;
    check(ingress.receive_json_frame(binary_command(50)),
          "reset test must enter awaiting state");
    ingress.reset();
    check(ingress.state() == trigger::TriggerIngressState::accepting_json,
          "reset must discard partial binary state");
    check(ingress.receive_binary_frame(std::span<const std::byte>{}).error
              == trigger::TriggerIngressError::binary_without_declaration,
          "reset must prevent a late binary frame from attaching");

    check(ingress.receive_json_frame(command(51)),
          "ready reset test must complete a command");
    ingress.reset();
    check(!ingress.take_ready()
              && ingress.state() == trigger::TriggerIngressState::accepting_json,
          "reset must explicitly discard an untaken ready item");

    check(ingress.receive_json_frame(binary_command(52)),
          "close test must enter awaiting state");
    ingress.close();
    ingress.close();
    check(ingress.state() == trigger::TriggerIngressState::closed
              && !ingress.take_ready(),
          "close must be idempotent and discard all outstanding input");
    auto result = ingress.receive_json_frame(command(53));
    check(!result && result.error == trigger::TriggerIngressError::closed,
          "closed ingress must reject JSON forever");
    result = ingress.receive_binary_frame(std::span<const std::byte>{});
    check(!result && result.error == trigger::TriggerIngressError::closed,
          "closed ingress must reject binary forever");
    ingress.reset();
    check(ingress.state() == trigger::TriggerIngressState::closed,
          "reset must never reopen a closed ingress");
}

void test_error_names_are_stable()
{
    using enum trigger::TriggerIngressError;
    check(trigger::trigger_ingress_error_name(none) == "none"
              && trigger::trigger_ingress_error_name(closed) == "closed"
              && trigger::trigger_ingress_error_name(item_pending) == "item_pending"
              && trigger::trigger_ingress_error_name(json_while_awaiting_binary)
                  == "json_while_awaiting_binary"
              && trigger::trigger_ingress_error_name(binary_without_declaration)
                  == "binary_without_declaration"
              && trigger::trigger_ingress_error_name(invalid_response_mode)
                  == "invalid_response_mode"
              && trigger::trigger_ingress_error_name(json_too_large)
                  == "json_too_large"
              && trigger::trigger_ingress_error_name(binary_too_large)
                  == "binary_too_large"
              && trigger::trigger_ingress_error_name(aggregate_too_large)
                  == "aggregate_too_large"
              && trigger::trigger_ingress_error_name(envelope_rejected)
                  == "envelope_rejected"
              && trigger::trigger_ingress_error_name(admission_rejected)
                  == "admission_rejected",
          "ingress errors must have stable machine-readable names");
}

}  // namespace

int main()
{
    test_json_only_ready_item_is_owned_and_single_outstanding();
    test_declared_binary_is_adjacent_owned_and_zero_length_distinct();
    test_binary_marker_gate_and_strict_frame_order();
    test_frame_and_aggregate_limits_clear_partial_state();
    test_envelope_failures_modes_and_limit_validation_recover();
    test_reset_and_close_are_explicit_and_terminal();
    test_error_names_are_stable();
    if (failures != 0) {
        std::cerr << failures << " trigger ingress test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "trigger ingress tests passed\n";
    return EXIT_SUCCESS;
}
