#include "service/protocol/PipeFraming.h"
#include "service/protocol/TriggerEnvelope.h"
#include "service/protocol/TriggerPipeAdapter.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace trigger = baas::service::protocol::trigger;
namespace bpip = baas::service::protocol::bpip;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void check_decode_error(
    const std::string_view json,
    const trigger::EnvelopeError expected,
    const std::string_view message)
{
    const auto result = trigger::decode_command_envelope(json);
    check(result.error == expected, message);
}

trigger::CommandResponse success(
    std::string command = "status",
    const trigger::Timestamp timestamp = 1,
    std::optional<std::string> data = std::string{"{}"})
{
    trigger::CommandResponse response;
    response.command = std::move(command);
    response.timestamp = timestamp;
    response.data_json = std::move(data);
    return response;
}

void test_command_decode_and_compatibility()
{
    const auto decoded = trigger::decode_command_envelope(
        R"({"type":"command","command":"import_config","timestamp":1700000000123,"config_id":"\u914d\u7f6e","payload":{"binary":true,"nested":{"name":"A\nB"}},"future":42})");
    check(static_cast<bool>(decoded), "valid v1 command must decode");
    check(decoded.envelope.command == "import_config"
              && decoded.envelope.timestamp == 1'700'000'000'123ULL,
          "command identity and JS-safe timestamp must be preserved");
    check(decoded.envelope.config_id == std::string{"配置"},
          "JSON escapes must decode to UTF-8 config ids");
    check(decoded.envelope.payload_json
              == R"({"binary":true,"nested":{"name":"A\nB"}})",
          "payload must be compact validated JSON with semantic escapes preserved");
    check(decoded.envelope.expects_binary,
          "only payload.binary true must declare the next inbound binary frame");
    check(trigger::make_admission(decoded.envelope, std::nullopt).error
              == trigger::EnvelopeError::binary_presence_mismatch,
          "declared binary input must not dispatch without its following frame");
    const auto empty_binary_admission =
        trigger::make_admission(decoded.envelope, std::size_t{0});
    check(empty_binary_admission && empty_binary_admission.admission.binary_bytes == 0,
          "a received zero-byte frame must remain distinct from a missing frame");

    const auto defaults = trigger::decode_command_envelope(
        R"({"type":"command","command":"status","timestamp":0,"unknown":true})");
    check(defaults && defaults.envelope.payload_json == "{}"
              && !defaults.envelope.config_id && !defaults.envelope.expects_binary,
          "Pydantic-compatible missing optional fields and unknown fields must survive");
    check(trigger::make_admission(defaults.envelope, std::size_t{1}).error
              == trigger::EnvelopeError::binary_presence_mismatch,
          "undeclared binary input must not shift the global frame stream");

    const auto false_binary = trigger::decode_command_envelope(
        R"({"type":"command","command":"import_config","timestamp":2,"payload":{"binary":false}})");
    check(false_binary && !false_binary.envelope.expects_binary,
          "false binary marker must not consume a following frame");

    const auto unrelated_binary = trigger::decode_command_envelope(
        R"({"type":"command","command":"status","timestamp":3,"payload":{"binary":true}})");
    check(unrelated_binary && !unrelated_binary.envelope.expects_binary,
          "only import_config may consume the following global binary frame");
}

void test_schema_and_json_rejections()
{
    check(trigger::envelope_error_name(trigger::EnvelopeError::binary_presence_mismatch)
              == "binary_presence_mismatch",
          "envelope error names must remain stable for transport adapters");
    check_decode_error("[]", trigger::EnvelopeError::root_not_object,
                       "command root must be an object");
    check_decode_error(R"({"command":"status","timestamp":1})",
                       trigger::EnvelopeError::missing_type,
                       "type is required");
    check_decode_error(R"({"type":"other","command":"status","timestamp":1})",
                       trigger::EnvelopeError::invalid_type,
                       "type must be the command literal");
    check_decode_error(R"({"type":"command","timestamp":1})",
                       trigger::EnvelopeError::missing_command,
                       "command is required");
    check_decode_error(R"({"type":"command","command":"Bad-Name","timestamp":1})",
                       trigger::EnvelopeError::invalid_command,
                       "command inventory shape must be bounded lowercase ASCII");
    check_decode_error(R"({"type":"command","command":"status"})",
                       trigger::EnvelopeError::missing_timestamp,
                       "timestamp is required");
    for (const auto timestamp : {"-1", "1.0", "1e3", "9007199254740992"}) {
        const auto json = std::string{R"({"type":"command","command":"status","timestamp":)"}
            + timestamp + '}';
        check_decode_error(json, trigger::EnvelopeError::invalid_timestamp,
                           "timestamp must be an unsigned JS-safe integer token");
    }
    check_decode_error(
        R"({"type":"command","command":"status","timestamp":1,"config_id":""})",
        trigger::EnvelopeError::invalid_config_id,
        "empty config ids must fail before admission");
    check_decode_error(
        R"({"type":"command","command":"status","timestamp":1,"payload":[]})",
        trigger::EnvelopeError::invalid_payload,
        "command payload must be an object");
    check_decode_error(
        R"({"type":"command","command":"status","timestamp":1,"payload":{"x":1,"x":2}})",
        trigger::EnvelopeError::duplicate_key,
        "duplicate keys must be rejected recursively");
    check_decode_error(
        R"({"type":"command","command":"status","timestamp":1,})",
        trigger::EnvelopeError::invalid_json,
        "trailing commas must not be accepted");

    std::string invalid_utf8 =
        R"({"type":"command","command":"status","timestamp":1,"payload":{"x":")";
    invalid_utf8.append("\xC0\xAF", 2);
    invalid_utf8 += R"("}})";
    check_decode_error(invalid_utf8, trigger::EnvelopeError::invalid_utf8,
                       "non-scalar UTF-8 must fail before parsing");
}

void test_parser_budgets()
{
    auto limits = trigger::TriggerEnvelopeLimits{};
    limits.max_input_json_bytes = 8;
    check(trigger::decode_command_envelope("123456789", limits).error
              == trigger::EnvelopeError::input_too_large,
          "input bytes must be rejected before allocation-heavy parsing");

    limits = {};
    limits.max_depth = 2;
    check(trigger::decode_command_envelope(R"({"a":{"b":1}})", limits).error
              == trigger::EnvelopeError::depth_limit,
          "nested input must respect the configured depth budget");

    limits = {};
    limits.max_nodes = 2;
    check(trigger::decode_command_envelope(R"({"a":1,"b":2})", limits).error
              == trigger::EnvelopeError::node_limit,
          "every JSON value must consume the node budget");

    limits = {};
    limits.max_string_bytes = 1;
    check(trigger::decode_command_envelope(R"({"x":"y"})", limits).error
              == trigger::EnvelopeError::string_limit,
          "keys and decoded strings must share one string-byte budget");

    limits = {};
    limits.max_work = 4;
    check(trigger::decode_command_envelope("     {}", limits).error
              == trigger::EnvelopeError::work_limit,
          "whitespace must consume bounded parser work");

    limits = {};
    limits.max_depth = 257;
    check(trigger::decode_command_envelope("{}", limits).error
              == trigger::EnvelopeError::invalid_limits,
          "recursive depth configuration must retain a hard stack ceiling");
}

void test_deterministic_response_encoding()
{
    auto response = success("status", 7, R"({"z":1,"text":"A\nB"})");
    const auto encoded = trigger::encode_command_response(std::move(response));
    check(encoded && encoded.batch.json()
              == R"({"type":"command_response","command":"status","status":"ok","data":{"z":1,"text":"A\nB"},"timestamp":7})",
          "success response outer fields and compact JSON must be deterministic");
    check(!encoded.batch.has_binary() && encoded.batch.binary().empty(),
          "JSON-only response must not promise a binary frame");

    trigger::CommandResponse error;
    error.command = "solve";
    error.timestamp = 8;
    error.status = trigger::ResponseStatus::error;
    error.error = "bad \"task\"";
    const auto encoded_error = trigger::encode_command_response(std::move(error));
    check(encoded_error && encoded_error.batch.json()
              == R"({"type":"command_response","command":"solve","status":"error","error":"bad \"task\"","timestamp":8})",
          "error response must use the legacy wire status and JSON-safe message");

    auto cancelled = success("solve", 9, std::nullopt);
    cancelled.status = trigger::ResponseStatus::cancelled;
    cancelled.error = "cancelled";
    const auto encoded_cancelled = trigger::encode_command_response(std::move(cancelled));
    check(encoded_cancelled
              && encoded_cancelled.batch.status() == trigger::ResponseStatus::cancelled
              && encoded_cancelled.batch.json().find(R"("status":"error")")
                  != std::string::npos,
          "internal cancellation must retain legacy Tauri error wire semantics");
}

void test_stream_terminal_wire_binding()
{
    auto progress = success("test_all_sha_stream", 30, R"({"name":"one"})");
    progress.response_mode = trigger::ResponseMode::stream;
    progress.terminal = false;
    const auto encoded_progress = trigger::encode_command_response(std::move(progress));
    check(encoded_progress && !encoded_progress.batch.terminal()
              && encoded_progress.batch.response_mode() == trigger::ResponseMode::stream
              && encoded_progress.batch.json().find("done") == std::string::npos,
          "stream progress must remain nonterminal on both server and Tauri wire state");

    auto terminal = success("test_all_sha_stream", 31, R"({"updated":2})");
    terminal.response_mode = trigger::ResponseMode::stream;
    const auto encoded_terminal = trigger::encode_command_response(std::move(terminal));
    check(encoded_terminal && encoded_terminal.batch.terminal()
              && encoded_terminal.batch.json().find(R"("done":true)")
                  != std::string::npos,
          "successful stream terminal must inject data.done=true for Tauri cleanup");

    auto forged_progress = success("test_all_sha_stream", 32, R"({"done":true})");
    forged_progress.response_mode = trigger::ResponseMode::stream;
    forged_progress.terminal = false;
    check(trigger::encode_command_response(std::move(forged_progress)).error
              == trigger::EnvelopeError::invalid_terminal,
          "dispatchers must not forge the codec-owned stream terminal marker");

    auto non_object_terminal = success("test_all_sha_stream", 33, R"([1,2])");
    non_object_terminal.response_mode = trigger::ResponseMode::stream;
    check(trigger::encode_command_response(std::move(non_object_terminal)).error
              == trigger::EnvelopeError::invalid_terminal,
          "successful stream terminal data must be an object that can carry done");

    auto invalid_single = success("status", 34);
    invalid_single.terminal = false;
    check(trigger::encode_command_response(std::move(invalid_single)).error
              == trigger::EnvelopeError::invalid_terminal,
          "single response codec must reject nonterminal output before session state");
}

void test_binary_metadata_and_zero_length_frame()
{
    auto response = success("export_config", 10, R"({"name":"demo"})");
    response.binary = std::vector<std::byte>{
        std::byte{0x00}, std::byte{0x01}, std::byte{0xFE}, std::byte{0xFF}};
    const auto encoded = trigger::encode_command_response(std::move(response));
    check(encoded && encoded.batch.has_binary() && encoded.batch.json()
              == R"({"type":"command_response","command":"export_config","status":"ok","data":{"name":"demo","binary":{"size":4}},"timestamp":10})",
          "codec must exclusively inject the exact binary byte count");

    const auto wire = trigger::encode_pipe_batch(encoded.batch);
    bpip::Decoder decoder;
    const auto frames = decoder.feed(wire.bytes);
    check(frames.frames.size() == 2 && frames.frames[1].payload.size() == 4,
          "encoded response must remain one ordered JSON-plus-BYTES pipe batch");

    auto empty = success("export_config", 11, R"({})");
    empty.binary = std::vector<std::byte>{};
    const auto empty_encoded = trigger::encode_command_response(std::move(empty));
    check(empty_encoded && empty_encoded.batch.has_binary()
              && empty_encoded.batch.json().find(R"("binary":{"size":0})")
                  != std::string::npos,
          "present empty binary must declare size zero");
    decoder.reset();
    const auto empty_frames = decoder.feed(
        trigger::encode_pipe_batch(empty_encoded.batch).bytes);
    check(empty_frames.frames.size() == 2 && empty_frames.frames[1].payload.empty(),
          "present empty binary must still emit the promised BYTES frame");

    auto reserved = success("status", 12, R"({"binary":{"size":9}})");
    check(trigger::encode_command_response(std::move(reserved)).error
              == trigger::EnvelopeError::reserved_binary_field,
          "callers cannot forge a data.binary promise without attached bytes");
    auto non_object = success("export_config", 13, R"([1,2])");
    non_object.binary = std::vector<std::byte>{std::byte{0x01}};
    check(trigger::encode_command_response(std::move(non_object)).error
              == trigger::EnvelopeError::invalid_data,
          "binary metadata requires object data like the Python handler");
}

void test_response_rejections_and_session_integration()
{
    auto invalid_data = success("status", 20, R"({"x":1,"x":2})");
    check(trigger::encode_command_response(std::move(invalid_data)).error
              == trigger::EnvelopeError::duplicate_key,
          "response data must use the same duplicate-free parser");

    trigger::CommandResponse missing_error;
    missing_error.command = "solve";
    missing_error.timestamp = 21;
    missing_error.status = trigger::ResponseStatus::error;
    check(trigger::encode_command_response(std::move(missing_error)).error
              == trigger::EnvelopeError::invalid_error,
          "error status must carry a non-empty bounded message");

    auto contradictory_success = success("status", 21);
    contradictory_success.error = "must not be hidden";
    check(trigger::encode_command_response(std::move(contradictory_success)).error
              == trigger::EnvelopeError::invalid_error,
          "success status must reject an error that would otherwise be silently omitted");

    auto limits = trigger::TriggerEnvelopeLimits{};
    limits.max_output_json_bytes = 16;
    check(trigger::encode_command_response(success("status", 22), limits).error
              == trigger::EnvelopeError::output_too_large,
          "response serialization must stop at its independent byte budget");

    limits = {};
    limits.max_input_json_bytes = 8;
    limits.max_output_json_bytes = 256;
    check(static_cast<bool>(trigger::encode_command_response(
              success("status", 22,
                      R"({"response_data_is_longer_than_input":true})"),
              limits)),
          "response data must use the output JSON budget, not the request budget");

    limits = {};
    limits.max_binary_bytes = 1;
    auto large_binary = success("export_config", 23);
    large_binary.binary =
        std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}};
    check(trigger::encode_command_response(std::move(large_binary), limits).error
              == trigger::EnvelopeError::binary_too_large,
          "binary output must fail before envelope construction when oversized");

    const auto decoded = trigger::decode_command_envelope(
        R"({"type":"command","command":"status","timestamp":24,"payload":{}})");
    trigger::TriggerSession session;
    const auto admission = trigger::make_admission(decoded.envelope, std::nullopt);
    check(decoded && admission
              && session.admit(admission.admission),
          "decoded metadata must directly form a bounded session admission");
    const auto reply = trigger::encode_command_response(success("status", 24));
    check(reply && session.publish(reply.batch),
          "only codec-produced response metadata must enter the session");
    auto begun = session.begin_send();
    const bool completed = begun && session.complete_send(*begun.lease);
    check(completed && begun.lease->batch().command() == decoded.envelope.command
              && begun.lease->batch().timestamp() == decoded.envelope.timestamp,
          "codec and session must retain exact correlation metadata end to end");
}

}  // namespace

int main()
{
    test_command_decode_and_compatibility();
    test_schema_and_json_rejections();
    test_parser_budgets();
    test_deterministic_response_encoding();
    test_stream_terminal_wire_binding();
    test_binary_metadata_and_zero_length_frame();
    test_response_rejections_and_session_integration();

    if (failures != 0) {
        std::cerr << failures << " trigger envelope test(s) failed\n";
        return 1;
    }
    std::cout << "trigger envelope tests passed\n";
    return 0;
}
