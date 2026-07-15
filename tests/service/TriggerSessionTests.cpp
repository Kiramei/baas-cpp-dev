#include "service/protocol/TriggerSession.h"
#include "service/protocol/TriggerPipeAdapter.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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

[[nodiscard]] trigger::CommandAdmission command(
    std::string name,
    const trigger::Timestamp timestamp,
    const trigger::ResponseMode mode = trigger::ResponseMode::single
)
{
    return {std::move(name), timestamp, std::string{"default"}, 2, 0, mode};
}

[[nodiscard]] trigger::OutboundBatch response(
    std::string name,
    const trigger::Timestamp timestamp,
    const bool terminal = true,
    const trigger::ResponseStatus status = trigger::ResponseStatus::ok,
    std::string json = R"({"type":"command_response","status":"ok"})"
)
{
    return {std::move(name), timestamp, status, terminal, std::move(json), {}};
}

void test_limits_and_stable_error_names()
{
    auto limits = trigger::TriggerSessionLimits{};
    limits.max_in_flight = 0;
    bool rejected = false;
    try {
        [[maybe_unused]] trigger::TriggerSession invalid{limits};
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "zero trigger limits must be rejected");

    limits = {};
    limits.max_queued_bytes = limits.max_response_binary_bytes - 1;
    rejected = false;
    try {
        [[maybe_unused]] trigger::TriggerSession invalid{limits};
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "one response must fit the aggregate queue budget");
    check(trigger::admission_error_name(trigger::AdmissionError::duplicate_timestamp)
              == "duplicate_timestamp",
          "admission error names must be stable");
    check(trigger::publish_error_name(trigger::PublishError::queue_full) == "queue_full",
          "publish error names must be stable");
}

void test_admission_validation_and_correlation_reservation()
{
    auto limits = trigger::TriggerSessionLimits{};
    limits.max_in_flight = 2;
    trigger::TriggerSession session{limits};

    auto invalid = command("Bad-Command", 1);
    check(session.admit(std::move(invalid)).error == trigger::AdmissionError::invalid_command,
          "command names must use the observed lowercase underscore shape");

    invalid = command("status", trigger::maximum_safe_timestamp + 1);
    check(session.admit(std::move(invalid)).error == trigger::AdmissionError::invalid_timestamp,
          "timestamps beyond the JavaScript safe-integer range must be rejected");

    invalid = command("status", 2);
    invalid.config_id = std::string{"\xC0\xAF"};
    check(session.admit(std::move(invalid)).error == trigger::AdmissionError::invalid_config_id,
          "invalid UTF-8 config ids must be rejected before dispatch");

    invalid = command("status", 3);
    invalid.payload_bytes = limits.max_request_payload_bytes + 1;
    check(session.admit(std::move(invalid)).error == trigger::AdmissionError::payload_too_large,
          "oversized decoded payloads must be rejected before dispatch");

    check(static_cast<bool>(session.admit(command("status", 10))),
          "first command must be admitted");
    check(session.admit(command("solve", 10)).error
              == trigger::AdmissionError::duplicate_timestamp,
          "a live timestamp must be unique across commands");
    check(static_cast<bool>(session.admit(command("solve", 11))),
          "second distinct command must be admitted");
    check(session.admit(command("detect_adb", 12)).error
              == trigger::AdmissionError::in_flight_limit,
          "bounded in-flight capacity must reject before dispatch");

    check(static_cast<bool>(session.publish(response("status", 10))),
          "terminal response must enter the outbound queue");
    check(session.admit(command("status", 10)).error
              == trigger::AdmissionError::duplicate_timestamp,
          "terminal correlation must remain reserved until the response is sent");
    const auto sent = session.pop();
    check(sent && sent->timestamp == 10, "pop must return the oldest terminal response");
    check(static_cast<bool>(session.admit(command("status", 10))),
          "timestamp may be reused only after its terminal response is popped");

    const auto stats = session.stats();
    check(stats.accepted == 3 && stats.admission_rejections == 7,
          "admission counters must include every accepted and rejected attempt");
}

void test_streaming_order_and_atomic_binary_batch()
{
    trigger::TriggerSession session;
    check(static_cast<bool>(session.admit(
              command("test_all_sha_stream", 20, trigger::ResponseMode::stream))),
          "stream command must be admitted");

    check(static_cast<bool>(session.publish(response(
              "test_all_sha_stream", 20, false, trigger::ResponseStatus::ok,
              R"({"type":"command_response","command":"test_all_sha_stream","status":"ok","data":{"name":"one"},"timestamp":20})"))),
          "stream progress must be publishable without completing correlation");

    auto binary = response(
        "test_all_sha_stream", 20, false, trigger::ResponseStatus::ok,
        R"({"type":"command_response","command":"test_all_sha_stream","status":"ok","data":{"binary":{"size":4}},"timestamp":20})");
    binary.binary = {std::byte{0x00}, std::byte{0x01}, std::byte{0xFE}, std::byte{0xFF}};
    check(static_cast<bool>(session.publish(std::move(binary))),
          "JSON declaration and binary bytes must be accepted as one batch");

    check(static_cast<bool>(session.publish(response(
              "test_all_sha_stream", 20, true, trigger::ResponseStatus::ok,
              R"({"type":"command_response","command":"test_all_sha_stream","status":"ok","data":{"done":true},"timestamp":20})"))),
          "stream terminal response must complete the correlation");
    check(session.publish(response("test_all_sha_stream", 20)).error
              == trigger::PublishError::terminal_already_queued,
          "no response may follow a queued stream terminal");

    const auto first = session.pop();
    const auto second = session.pop();
    const auto third = session.pop();
    check(first && !first->terminal && first->binary.empty(),
          "first stream progress must preserve FIFO order");
    check(second && !second->terminal && second->binary.size() == 4,
          "binary bytes must remain attached to their declaring JSON response");
    check(third && third->terminal, "terminal stream result must be last");
    check(!session.pop(), "queue must be empty after all batches are sent");
    check(session.stats().active_correlations == 0,
          "terminal pop must release the stream correlation");
}

void test_publish_validation_backpressure_and_retry()
{
    auto limits = trigger::TriggerSessionLimits{};
    limits.max_response_json_bytes = 64;
    limits.max_response_binary_bytes = 64;
    limits.max_queued_batches = 1;
    limits.max_queued_bytes = 80;
    trigger::TriggerSession session{limits};

    check(static_cast<bool>(session.admit(
              command("update_to_latest_stream", 30, trigger::ResponseMode::stream))),
          "bounded stream must be admitted");
    auto progress = response("update_to_latest_stream", 30, false);
    progress.json = R"({"status":"ok"})";
    check(static_cast<bool>(session.publish(progress)),
          "first bounded response must be queued");

    auto terminal = response("update_to_latest_stream", 30);
    terminal.json = R"({"status":"ok","data":{"done":true}})";
    check(session.publish(terminal).error == trigger::PublishError::queue_full,
          "full outbound queue must report retryable backpressure");
    check(session.stats().active_correlations == 1,
          "failed terminal enqueue must not complete the correlation");
    check(static_cast<bool>(session.pop()), "draining one batch must create capacity");
    check(static_cast<bool>(session.publish(std::move(terminal))),
          "terminal publication must succeed when retried after drain");

    trigger::TriggerSession single;
    check(static_cast<bool>(single.admit(command("status", 31))),
          "single response command must be admitted");
    check(single.publish(response("status", 31, false)).error
              == trigger::PublishError::single_response_must_be_terminal,
          "single-response commands must not emit progress frames");
    check(single.publish(response("wrong", 31)).error
              == trigger::PublishError::command_mismatch,
          "response command must exactly echo its admitted command");
    auto invalid_utf8 = response("status", 31);
    invalid_utf8.json = std::string{"\xC0\xAF"};
    check(single.publish(std::move(invalid_utf8)).error
              == trigger::PublishError::invalid_json_utf8,
          "invalid UTF-8 must never reach a JSON transport frame");
}

void test_cancellation_and_disconnect_cleanup()
{
    trigger::TriggerSession session;
    check(static_cast<bool>(session.admit(command("solve", 40))),
          "cancellable task must be admitted");
    check(session.request_cancel(40) == trigger::CancelDecision::requested,
          "first cancellation request must notify the task owner");
    check(session.request_cancel(40) == trigger::CancelDecision::already_requested,
          "cancellation requests must be idempotent");
    check(session.publish(response("solve", 40)).error
              == trigger::PublishError::cancellation_response_required,
          "ordinary success must not race past an accepted cancellation");
    check(static_cast<bool>(session.publish(response(
              "solve", 40, true, trigger::ResponseStatus::cancelled,
              R"({"type":"command_response","command":"solve","status":"error","error":"cancelled","timestamp":40})"))),
          "only an explicit terminal cancellation may complete cancelled work");
    check(session.request_cancel(40) == trigger::CancelDecision::terminal_already_queued,
          "terminal responses close the cancellation window");

    check(static_cast<bool>(session.admit(command("start_scheduler", 41))),
          "second active task must be admitted");
    check(static_cast<bool>(session.admit(command("status", 42))),
          "third active task must be admitted");
    check(session.request_cancel(41) == trigger::CancelDecision::requested,
          "disconnect test must preserve prior cancellation state");
    const auto active = session.close();
    check(active.size() == 2, "disconnect must return every task without a terminal response");
    check(active[0].timestamp == 41 && active[0].cancel_requested
              && active[1].timestamp == 42 && !active[1].cancel_requested,
          "disconnect cancellation list must be deterministic and complete");
    check(session.stats().queued_batches == 0 && session.stats().active_correlations == 0,
          "closed connection must drop unsendable output and all correlations");
    check(session.admit(command("status", 43)).error == trigger::AdmissionError::closed,
          "closed sessions must never reopen admission");
    check(session.publish(response("status", 43)).error == trigger::PublishError::closed,
          "closed sessions must reject late task completions");
    check(session.request_cancel(43) == trigger::CancelDecision::closed,
          "closed sessions must reject new cancellation requests");
}

void test_concurrent_admission_and_publication()
{
    auto limits = trigger::TriggerSessionLimits{};
    limits.max_in_flight = 128;
    limits.max_queued_batches = 128;
    trigger::TriggerSession session{limits};
    std::atomic<int> accepted{};
    std::vector<std::thread> workers;
    for (trigger::Timestamp timestamp = 1; timestamp <= 64; ++timestamp) {
        workers.emplace_back([&session, &accepted, timestamp] {
            if (session.admit(command("status", timestamp))) {
                ++accepted;
                if (!session.publish(response("status", timestamp))) --accepted;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    check(accepted == 64, "concurrent commands must publish without data races");

    std::vector<trigger::Timestamp> observed;
    while (const auto batch = session.pop()) observed.push_back(batch->timestamp);
    std::sort(observed.begin(), observed.end());
    check(observed.size() == 64 && observed.front() == 1 && observed.back() == 64,
          "every concurrent terminal response must be retained exactly once");
    const auto stats = session.stats();
    check(stats.accepted == 64 && stats.published_batches == 64
              && stats.popped_batches == 64 && stats.active_correlations == 0,
          "concurrent lifecycle counters must remain exact");
}

void test_bpip_json_binary_batch_encoding()
{
    auto batch = response("export_config", 50);
    batch.json = R"({"x":1})";
    batch.binary = {std::byte{0x00}, std::byte{0x01}, std::byte{0xFE}, std::byte{0xFF}};
    const auto encoded = trigger::encode_pipe_batch(batch);
    check(static_cast<bool>(encoded), "valid trigger batch must encode to BPIP");
    check(encoded.bytes.size() == 2U * baas::service::protocol::bpip::header_size
              + batch.json.size() + batch.binary.size(),
          "coalesced BPIP batch size must include exactly two headers and payloads");

    baas::service::protocol::bpip::Decoder decoder;
    const auto decoded = decoder.feed(encoded.bytes);
    check(!decoded.error && decoded.frames.size() == 2,
          "one coalesced trigger write must decode as JSON then BYTES");
    if (decoded.frames.size() == 2) {
        check(decoded.frames[0].kind
                  == baas::service::protocol::bpip::kind_value(
                      baas::service::protocol::bpip::FrameKind::json)
                  && decoded.frames[1].kind
                  == baas::service::protocol::bpip::kind_value(
                      baas::service::protocol::bpip::FrameKind::bytes),
              "trigger pipe batch must preserve the protocol frame order");
        check(decoded.frames[1].payload == batch.binary,
              "binary frame payload must remain byte exact");
    }

    auto json_only = batch;
    json_only.binary.clear();
    const auto one_frame = trigger::encode_pipe_batch(json_only);
    decoder.reset();
    const auto one_decoded = decoder.feed(one_frame.bytes);
    check(one_decoded.frames.size() == 1,
          "responses without binary data must encode as one JSON frame");
    check(trigger::encode_pipe_batch(batch, encoded.bytes.size() - 1).error
              == trigger::PipeBatchError::batch_too_large,
          "wire-size budget must reject before allocating a coalesced batch");
}

}  // namespace

int main()
{
    test_limits_and_stable_error_names();
    test_admission_validation_and_correlation_reservation();
    test_streaming_order_and_atomic_binary_batch();
    test_publish_validation_backpressure_and_retry();
    test_cancellation_and_disconnect_cleanup();
    test_concurrent_admission_and_publication();
    test_bpip_json_binary_batch_encoding();

    if (failures != 0) {
        std::cerr << failures << " trigger session test(s) failed\n";
        return 1;
    }
    std::cout << "trigger session tests passed\n";
    return 0;
}
