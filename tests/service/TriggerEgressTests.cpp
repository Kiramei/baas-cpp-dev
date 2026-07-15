#include "service/protocol/TriggerEnvelope.h"
#include "service/protocol/TriggerSession.h"

#include <atomic>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace trigger = baas::service::protocol::trigger;

namespace {

int failures = 0;

template <typename Condition>
void check(const Condition& condition, const std::string_view message)
{
    if (!static_cast<bool>(condition)) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] trigger::CommandAdmission command(
    std::string name,
    const trigger::Timestamp timestamp,
    const trigger::ResponseMode mode = trigger::ResponseMode::single)
{
    return {std::move(name), timestamp, std::nullopt, 2, 0, mode};
}

[[nodiscard]] trigger::OutboundBatch response(
    std::string name,
    const trigger::Timestamp timestamp,
    const bool terminal = true,
    const trigger::ResponseMode mode = trigger::ResponseMode::single,
    std::optional<std::vector<std::byte>> binary = std::nullopt)
{
    trigger::CommandResponse value;
    value.command = std::move(name);
    value.timestamp = timestamp;
    value.response_mode = mode;
    value.terminal = terminal;
    value.data_json = std::string{"{}"};
    value.binary = std::move(binary);
    auto encoded = trigger::encode_command_response(std::move(value));
    if (!encoded) throw std::runtime_error("test response failed to encode");
    return std::move(encoded.batch);
}

void test_lease_retains_queue_budget_and_correlation()
{
    auto limits = trigger::TriggerSessionLimits{};
    limits.max_response_json_bytes = 256;
    limits.max_response_binary_bytes = 64;
    limits.max_queued_batches = 1;
    limits.max_queued_bytes = 512;
    trigger::TriggerSession session{limits};

    const auto admission = session.admit(
        command("stream", 1, trigger::ResponseMode::stream));
    check(admission,
          "stream command must be admitted");
    check(session.publish(*admission.receipt, response(
              "stream", 1, false, trigger::ResponseMode::stream)),
          "progress batch must be queued");
    const auto before = session.stats();
    auto begun = session.begin_send();
    check(begun && begun.lease->batch().timestamp() == 1,
          "begin_send must lease the FIFO head");
    check(session.begin_send().error == trigger::BeginSendError::send_in_progress,
          "only one send lease may exist at a time");

    const auto during = session.stats();
    check(during.queued_batches == before.queued_batches
              && during.queued_bytes == before.queued_bytes
              && during.active_correlations == 1 && during.send_in_progress
              && during.popped_batches == 0,
          "a lease must retain queue charge, ownership, and correlation");
    check(session.publish(*admission.receipt, response(
              "stream", 1, true, trigger::ResponseMode::stream)).error
              == trigger::PublishError::queue_full,
          "leasing must not prematurely relieve queue backpressure");
    check(session.admit(command("status", 1)).error
              == trigger::AdmissionError::duplicate_timestamp,
          "leased terminal identity must remain reserved until confirmation");

    check(session.complete_send(*begun.lease),
          "full batch success must confirm the active lease");
    check(session.publish(*admission.receipt, response(
              "stream", 1, true, trigger::ResponseMode::stream)),
          "confirmation must create queue capacity for the terminal batch");
    auto terminal = session.begin_send();
    check(terminal && session.complete_send(*terminal.lease),
          "terminal batch must complete after confirmation");
    const auto final = session.stats();
    check(final.popped_batches == 2 && final.send_leases_started == 2
              && final.active_correlations == 0 && final.queued_batches == 0
              && !final.send_in_progress,
          "success counters and terminal release must occur exactly on ack");
}

void test_stale_duplicate_ack_and_fail_are_harmless()
{
    trigger::TriggerSession session;
    const auto first_admission = session.admit(command("status", 10));
    check(first_admission && session.publish(
              *first_admission.receipt, response("status", 10)),
          "first response must be ready");
    auto first = session.begin_send();
    check(first && session.complete_send(*first.lease),
          "first lease must complete");
    check(session.complete_send(*first.lease).error
              == trigger::SendTransitionError::no_active_lease,
          "duplicate ack without an active lease must be stable");
    check(session.fail_send(*first.lease).error
              == trigger::SendTransitionError::no_active_lease,
          "duplicate failure without an active lease must not close the session");

    const auto second_admission = session.admit(command("status", 11));
    check(second_admission && session.publish(
              *second_admission.receipt, response("status", 11)),
          "second response must be ready");
    auto second = session.begin_send();
    check(second && second.lease->id() != first.lease->id(),
          "successive leases must have distinct identities");
    check(session.complete_send(*first.lease).error
              == trigger::SendTransitionError::lease_mismatch,
          "stale ack must not consume a newer queue head");
    check(session.fail_send(*first.lease).error
              == trigger::SendTransitionError::lease_mismatch,
          "stale failure must not close a newer send lifecycle");
    check(session.complete_send(*second.lease),
          "current lease must remain completable after stale transitions");
    const auto stats = session.stats();
    check(!stats.closed && stats.popped_batches == 2 && stats.send_failures == 0,
          "invalid transitions must not perturb lifecycle counters");
}

void test_lease_retains_aggregate_byte_backpressure()
{
    auto first_batch = response(
        "stream", 5, false, trigger::ResponseMode::stream,
        std::vector<std::byte>{std::byte{0x01}});
    const auto batch_bytes = first_batch.json().size() + first_batch.binary().size();
    auto limits = trigger::TriggerSessionLimits{};
    limits.max_response_json_bytes = first_batch.json().size();
    limits.max_response_binary_bytes = first_batch.binary().size();
    limits.max_queued_batches = 2;
    limits.max_queued_bytes = batch_bytes + 1;
    trigger::TriggerSession session{limits};

    const auto admission = session.admit(
        command("stream", 5, trigger::ResponseMode::stream));
    check(admission && session.publish(
              *admission.receipt, std::move(first_batch)),
          "byte-backpressure fixture must be ready");
    auto begun = session.begin_send();
    auto second_batch = response(
        "stream", 5, false, trigger::ResponseMode::stream,
        std::vector<std::byte>{std::byte{0x02}});
    check(session.publish(*admission.receipt, std::move(second_batch)).error
              == trigger::PublishError::queued_bytes_exceeded,
          "leased bytes must continue enforcing aggregate backpressure");
    check(session.stats().queue_backpressure == 1,
          "leased-byte rejection must increment backpressure exactly once");
    check(session.complete_send(*begun.lease)
              && session.publish(*admission.receipt, std::move(second_batch)),
          "acknowledgement must relieve the retained byte charge");
    const auto active = session.close();
    check(active.size() == 1 && active[0].timestamp == 5,
          "fixture cleanup must return its still-running stream");
}

void test_send_failure_is_connection_fatal_and_deterministic()
{
    trigger::TriggerSession session;
    const auto stream_admission = session.admit(
        command("stream", 20, trigger::ResponseMode::stream));
    check(stream_admission && session.publish(*stream_admission.receipt, response(
                  "stream", 20, false, trigger::ResponseMode::stream)),
          "active stream progress must be queued");
    const auto status_admission = session.admit(command("status", 21));
    check(status_admission && session.publish(
              *status_admission.receipt, response("status", 21)),
          "completed command output must be queued behind the lease");
    check(session.admit(command("solve", 22)),
          "running command without output must be admitted");
    check(session.request_cancel(22) == trigger::CancelDecision::requested,
          "prior cancellation state must survive failure cleanup");

    auto begun = session.begin_send();
    const auto queued_bytes = session.stats().queued_bytes;
    auto failed = session.fail_send(*begun.lease);
    check(failed && failed.cancel.size() == 2,
          "send failure must return every still-running command exactly once");
    if (failed.cancel.size() == 2) {
        check(failed.cancel[0].timestamp == 20 && !failed.cancel[0].cancel_requested
                  && failed.cancel[1].timestamp == 22
                  && failed.cancel[1].cancel_requested,
              "failure cancellation list must be timestamp ordered and preserve state");
    }
    const auto stats = session.stats();
    check(stats.closed && !stats.send_in_progress && stats.queued_batches == 0
              && stats.active_correlations == 0 && stats.popped_batches == 0
              && stats.send_failures == 1 && stats.dropped_batches == 2
              && stats.dropped_bytes == queued_bytes,
          "failure must atomically close, drop, and account unsendable output");
    check(session.fail_send(*begun.lease).error
              == trigger::SendTransitionError::closed,
          "duplicate failure after closure must be idempotent");
    check(session.complete_send(*begun.lease).error
              == trigger::SendTransitionError::closed,
          "a failed partial batch can never later be acknowledged");
    check(session.begin_send().error == trigger::BeginSendError::closed,
          "no later batch may continue on the failed connection");
    check(session.close().empty(),
          "failure owns the cancellation handoff when it wins closure");
}

void test_concurrent_begin_allows_one_lease()
{
    trigger::TriggerSession session;
    const auto admission = session.admit(command("status", 30));
    check(admission && session.publish(
              *admission.receipt, response("status", 30)),
          "concurrent begin fixture must be ready");

    std::atomic<int> acquired{};
    std::atomic<int> busy{};
    std::mutex lease_mutex;
    std::optional<trigger::SendLease> winner;
    std::vector<std::thread> workers;
    for (int index = 0; index < 16; ++index) {
        workers.emplace_back([&] {
            auto result = session.begin_send();
            if (result) {
                ++acquired;
                std::lock_guard lock(lease_mutex);
                winner = *result.lease;
            } else if (result.error == trigger::BeginSendError::send_in_progress) {
                ++busy;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    check(acquired == 1 && busy == 15 && winner.has_value(),
          "concurrent begin_send calls must linearize to one lease");
    check(session.complete_send(*winner),
          "the single winning lease must remain valid");
}

void test_close_wins_before_ack_or_fail()
{
    trigger::TriggerSession ack_session;
    const auto ack_admission = ack_session.admit(
        command("stream", 35, trigger::ResponseMode::stream));
    check(ack_admission && ack_session.publish(*ack_admission.receipt, response(
                  "stream", 35, false, trigger::ResponseMode::stream)),
          "close-before-ack fixture must be ready");
    auto ack_lease = ack_session.begin_send();
    const auto ack_cancel = ack_session.close();
    check(ack_cancel.size() == 1 && ack_cancel[0].timestamp == 35
              && ack_session.complete_send(*ack_lease.lease).error
                  == trigger::SendTransitionError::closed,
          "a winning close must invalidate a pending acknowledgement");

    trigger::TriggerSession fail_session;
    const auto fail_admission = fail_session.admit(
        command("stream", 36, trigger::ResponseMode::stream));
    check(fail_admission && fail_session.publish(*fail_admission.receipt, response(
                  "stream", 36, false, trigger::ResponseMode::stream)),
          "close-before-fail fixture must be ready");
    auto fail_lease = fail_session.begin_send();
    const auto fail_cancel = fail_session.close();
    const auto late_fail = fail_session.fail_send(*fail_lease.lease);
    check(fail_cancel.size() == 1 && fail_cancel[0].timestamp == 36
              && late_fail.error == trigger::SendTransitionError::closed
              && late_fail.cancel.empty() && fail_session.stats().send_failures == 0,
          "a winning close must own cancellation and reject a late failure");
}

void test_complete_close_race_is_linearizable()
{
    for (int iteration = 0; iteration < 64; ++iteration) {
        trigger::TriggerSession session;
        const auto admission = session.admit(
            command("stream", 40, trigger::ResponseMode::stream));
        check(admission && session.publish(*admission.receipt, response(
                      "stream", 40, false, trigger::ResponseMode::stream)),
              "complete-close race fixture must be ready");
        auto begun = session.begin_send();
        trigger::CompleteSendResult completed{trigger::SendTransitionError::closed};
        std::vector<trigger::ActiveCommand> closed;
        std::thread completer([&] { completed = session.complete_send(*begun.lease); });
        std::thread closer([&] { closed = session.close(); });
        completer.join();
        closer.join();

        const auto stats = session.stats();
        check(closed.size() == 1 && closed[0].timestamp == 40,
              "close must own the active-command cancellation handoff");
        check((completed || completed.error == trigger::SendTransitionError::closed)
                  && stats.closed && stats.popped_batches + stats.dropped_batches == 1
                  && stats.queued_batches == 0 && !stats.send_in_progress,
              "complete-close race must have one linearized queue outcome");
    }
}

void test_fail_close_race_returns_cancellation_once()
{
    for (int iteration = 0; iteration < 64; ++iteration) {
        trigger::TriggerSession session;
        const auto admission = session.admit(
            command("stream", 50, trigger::ResponseMode::stream));
        check(admission && session.publish(*admission.receipt, response(
                      "stream", 50, false, trigger::ResponseMode::stream)),
              "fail-close race fixture must be ready");
        auto begun = session.begin_send();
        trigger::FailSendResult failed{trigger::SendTransitionError::closed, {}};
        std::vector<trigger::ActiveCommand> closed;
        std::thread failing([&] { failed = session.fail_send(*begun.lease); });
        std::thread closing([&] { closed = session.close(); });
        failing.join();
        closing.join();

        const auto handed_off = failed.cancel.size() + closed.size();
        const auto stats = session.stats();
        check(handed_off == 1
                  && (failed || failed.error == trigger::SendTransitionError::closed),
              "fail-close race must return cancellation ownership exactly once");
        check(stats.closed && stats.dropped_batches == 1
                  && stats.popped_batches == 0
                  && (stats.send_failures == 0 || stats.send_failures == 1),
              "fail-close counters must identify which close transition won");
    }
}

}  // namespace

int main()
{
    check(trigger::begin_send_error_name(trigger::BeginSendError::send_in_progress)
              == "send_in_progress",
          "begin-send error names must be stable");
    check(trigger::send_transition_error_name(
              trigger::SendTransitionError::lease_mismatch) == "lease_mismatch",
          "send-transition error names must be stable");
    test_lease_retains_queue_budget_and_correlation();
    test_lease_retains_aggregate_byte_backpressure();
    test_stale_duplicate_ack_and_fail_are_harmless();
    test_send_failure_is_connection_fatal_and_deterministic();
    test_concurrent_begin_allows_one_lease();
    test_close_wins_before_ack_or_fail();
    test_complete_close_race_is_linearizable();
    test_fail_close_race_returns_cancellation_once();

    if (failures != 0) {
        std::cerr << failures << " trigger egress test(s) failed\n";
        return 1;
    }
    std::cout << "trigger egress tests passed\n";
    return 0;
}
