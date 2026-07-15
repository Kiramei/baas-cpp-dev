#include "service/trigger/TriggerDispatch.h"

#include <atomic>
#include <concepts>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace dispatch = baas::service::trigger;
namespace protocol = baas::service::protocol::trigger;

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

[[nodiscard]] std::optional<protocol::TriggerIngressItem> ingress_item(
    const std::string_view command,
    const protocol::Timestamp timestamp,
    const std::optional<std::string_view> config_id = std::nullopt,
    const std::string_view payload = "{}")
{
    std::string json = "{\"type\":\"command\",\"command\":\"";
    json.append(command);
    json.append("\",\"timestamp\":");
    json.append(std::to_string(timestamp));
    if (config_id) {
        json.append(",\"config_id\":\"");
        json.append(*config_id);
        json.push_back('"');
    }
    json.append(",\"payload\":");
    json.append(payload);
    json.push_back('}');

    protocol::TriggerIngress ingress;
    if (!ingress.receive_json_frame(json)) return std::nullopt;
    return ingress.take_ready();
}

[[nodiscard]] protocol::OutboundBatch response(
    std::string command,
    const protocol::Timestamp timestamp,
    const protocol::ResponseMode mode = protocol::ResponseMode::single,
    const bool terminal = true)
{
    protocol::CommandResponse value;
    value.command = std::move(command);
    value.timestamp = timestamp;
    value.response_mode = mode;
    value.terminal = terminal;
    value.data_json = std::string{"{}"};
    auto encoded = protocol::encode_command_response(std::move(value));
    if (!encoded) throw std::runtime_error("test response failed to encode");
    return std::move(encoded.batch);
}

[[nodiscard]] protocol::AdmissionResult admit(
    protocol::TriggerSession& session,
    std::string command,
    const protocol::Timestamp timestamp,
    const protocol::ResponseMode mode = protocol::ResponseMode::single)
{
    protocol::CommandAdmission admission;
    admission.command = std::move(command);
    admission.timestamp = timestamp;
    admission.payload_bytes = 2;
    admission.response_mode = mode;
    return session.admit(std::move(admission));
}

[[nodiscard]] std::optional<protocol::SendLease> confirm_one(
    protocol::TriggerSession& session)
{
    auto begun = session.begin_send();
    if (!begun || !session.complete_send(*begun.lease)) return std::nullopt;
    return std::move(begun.lease);
}

[[nodiscard]] dispatch::TriggerDispatcher dispatcher_with(
    std::vector<dispatch::TriggerHandlerRegistration> registrations,
    dispatch::TriggerDispatchLimits limits = {})
{
    auto built = dispatch::TriggerDispatcher::create(
        std::move(registrations), limits);
    if (!built) throw std::runtime_error("test dispatcher failed to build");
    return std::move(*built.dispatcher);
}

void test_registry_validation_and_unregistered_precedes_admission()
{
    auto invalid_limits = dispatch::TriggerDispatchLimits{};
    invalid_limits.max_exception_error_bytes = 1;
    check(dispatch::TriggerDispatcher::create({}, invalid_limits).error
              == dispatch::TriggerRegistryError::invalid_limits,
          "invalid dispatcher limits must be stable");
    check(dispatch::TriggerDispatcher::create({{"not_a_command", [](
              const dispatch::AdmittedTriggerRequest&,
              dispatch::TriggerResponseSink&) {}}}).error
              == dispatch::TriggerRegistryError::unknown_descriptor,
          "unknown descriptor registration must be rejected");
    check(dispatch::TriggerDispatcher::create({{"status", {}}}).error
              == dispatch::TriggerRegistryError::empty_handler,
          "empty handlers must be rejected");
    check(dispatch::TriggerDispatcher::create({
              {"status", [](const dispatch::AdmittedTriggerRequest&,
                            dispatch::TriggerResponseSink&) {}},
              {"status", [](const dispatch::AdmittedTriggerRequest&,
                            dispatch::TriggerResponseSink&) {}},
          }).error == dispatch::TriggerRegistryError::duplicate_registration,
          "duplicate descriptor registration must be rejected");

    auto dispatcher = dispatcher_with({
        {"solve", [](const dispatch::AdmittedTriggerRequest&,
                     dispatch::TriggerResponseSink& sink) {
            (void)sink.success();
        }},
    });
    protocol::TriggerSession session;
    auto item = ingress_item("status", 1);
    auto result = dispatcher.submit(std::move(*item), session);
    check(result.error == dispatch::TriggerDispatchError::unregistered_command
              && result.disposition
                  == dispatch::TriggerDispatchDisposition::rejected_before_admission,
          "unregistered descriptor must fail before admission");
    check(session.stats().accepted == 0
              && session.stats().active_correlations == 0,
          "unregistered dispatch must never reserve a timestamp");
}

void test_prefix_identity_and_staged_terminal_commit()
{
    std::string observed_command;
    std::string observed_descriptor;
    bool side_effect_after_success = false;
    dispatch::TriggerResponseResult staged;
    auto dispatcher = dispatcher_with({
        {"start_*", [&](const dispatch::AdmittedTriggerRequest& request,
                        dispatch::TriggerResponseSink& sink) {
            observed_command = request.command();
            observed_descriptor = request.descriptor().canonical_name;
            staged = sink.success(R"({"started":true})");
            // The terminal is only staged here; the handler can finish its
            // side effects before submit commits success to the session.
            side_effect_after_success = true;
        }},
    });

    protocol::TriggerSession session;
    auto item = ingress_item("start_custom_task", 10, "default");
    auto result = dispatcher.submit(std::move(*item), session);
    check(result && staged
              && staged.disposition == dispatch::TriggerResponseDisposition::staged,
          "normal handler return must commit its staged terminal once");
    check(side_effect_after_success && observed_command == "start_custom_task"
              && observed_descriptor == "start_*",
          "prefix handlers must receive actual command plus canonical descriptor");
    auto sent = confirm_one(session);
    check(sent && sent->batch().command() == "start_custom_task"
              && sent->batch().json().find("start_custom_task") != std::string::npos,
          "response identity must preserve the actual prefix command");
}

void test_single_progress_and_unique_stream_terminal()
{
    dispatch::TriggerResponseResult progress_rejected;
    auto single = dispatcher_with({
        {"status", [&](const dispatch::AdmittedTriggerRequest&,
                       dispatch::TriggerResponseSink& sink) {
            progress_rejected = sink.progress(R"({"bad":true})");
            (void)sink.success();
        }},
    });
    protocol::TriggerSession single_session;
    auto single_item = ingress_item("status", 20);
    check(single.submit(std::move(*single_item), single_session),
          "single handler may recover from forbidden progress");
    check(progress_rejected.error
              == dispatch::TriggerResponseError::progress_for_single,
          "single response mode must reject progress at the sealed sink");

    dispatch::TriggerResponseResult second_terminal;
    auto stream = dispatcher_with({
        {"test_all_sha_stream", [&](const dispatch::AdmittedTriggerRequest& request,
                                    dispatch::TriggerResponseSink& sink) {
            check(request.response_mode() == protocol::ResponseMode::stream,
                  "stream mode must come from the catalog admission");
            (void)sink.progress(
                R"({"name":"part"})",
                std::vector<std::byte>{std::byte{0x01}, std::byte{0xFE}});
            (void)sink.success(R"({"count":1})");
            second_terminal = sink.error("late");
        }},
    });
    protocol::TriggerSession stream_session;
    auto stream_item = ingress_item("test_all_sha_stream", 21);
    check(stream.submit(std::move(*stream_item), stream_session),
          "stream progress and terminal must dispatch");
    check(second_terminal.error
              == dispatch::TriggerResponseError::terminal_already_published,
          "a second staged terminal must be rejected deterministically");
    auto progress = confirm_one(stream_session);
    auto terminal = confirm_one(stream_session);
    check(progress && !progress->batch().terminal()
              && progress->batch().has_binary()
              && progress->batch().binary().size() == 2,
          "progress binary must remain one atomic outbound batch");
    check(terminal && terminal->batch().terminal()
              && terminal->batch().json().find(R"("done":true)")
                  != std::string::npos,
          "stream terminal must retain codec-owned done binding");
}

class HostileException final : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override
    {
        return "bad\xFFmessage-that-is-deliberately-long";
    }
};

void test_success_then_throw_is_replaced_by_bounded_error()
{
    auto limits = dispatch::TriggerDispatchLimits{};
    limits.max_exception_error_bytes = 24;
    auto dispatcher = dispatcher_with({
        {"status", [](const dispatch::AdmittedTriggerRequest&,
                      dispatch::TriggerResponseSink& sink) {
            (void)sink.success(R"({"must_not_escape":true})");
            throw HostileException{};
        }},
    }, limits);
    protocol::TriggerSession session;
    auto item = ingress_item("status", 30);
    const auto result = dispatcher.submit(std::move(*item), session);
    check(result.error == dispatch::TriggerDispatchError::handler_exception
              && result.disposition == dispatch::TriggerDispatchDisposition::completed,
          "handler exception must be contained by a committed terminal error");
    auto sent = confirm_one(session);
    check(sent && sent->batch().status() == protocol::ResponseStatus::error
              && sent->batch().json().find("must_not_escape") == std::string::npos
              && sent->batch().json().find("handler exception: bad?m")
                  != std::string::npos,
          "staged success must be discarded and hostile exception text sanitized");
    check(sent && sent->batch().json().size()
              < limits.response_envelope.max_output_json_bytes,
          "bounded exception terminal must remain within output limits");
}

void test_binary_terminal_backpressure_has_no_copy_retry_contract()
{
    using PublishSignature = protocol::PublishResult (protocol::TriggerSession::*)(
        const protocol::AdmissionReceipt&, protocol::OutboundBatch&&);
    static_assert(std::same_as<decltype(&protocol::TriggerSession::publish),
                               PublishSignature>);

    auto limits = protocol::TriggerSessionLimits{};
    limits.max_queued_batches = 1;
    protocol::TriggerSession session{limits};
    auto dispatcher = dispatcher_with({
        {"test_all_sha_stream", [](const dispatch::AdmittedTriggerRequest&,
                                   dispatch::TriggerResponseSink& sink) {
            (void)sink.progress(R"({"phase":1})");
            (void)sink.success(
                R"({"phase":2})",
                std::vector<std::byte>(2U * 1'024U * 1'024U, std::byte{0xA5}));
        }},
    });
    auto item = ingress_item("test_all_sha_stream", 40);
    auto result = dispatcher.submit(std::move(*item), session);
    check(result.disposition == dispatch::TriggerDispatchDisposition::retry_response
              && result.pending_response && result.pending_response->pending()
              && result.response.publish_error == protocol::PublishError::queue_full,
          "terminal binary must remain owned for exact backpressure retry");
    check(session.stats().active_correlations == 1
              && session.stats().popped_batches == 0,
          "failed terminal commit must not release correlation");
    check(confirm_one(session), "draining progress must create terminal capacity");
    check(result.pending_response->retry(),
          "pending terminal must retry by move after capacity is available");
    auto terminal = confirm_one(session);
    check(terminal && terminal->batch().binary().size() == 2U * 1'024U * 1'024U
              && terminal->batch().binary().front() == std::byte{0xA5},
          "binary retry must retain byte-exact atomic payload");
}

void test_ignored_progress_backpressure_cannot_orphan_correlation()
{
    for (const bool should_throw : {false, true}) {
        auto limits = protocol::TriggerSessionLimits{};
        limits.max_queued_batches = 1;
        protocol::TriggerSession session{limits};
        auto blocker = admit(session, "status", 50);
        auto blocker_batch = response("status", 50);
        check(blocker && session.publish(
                  *blocker.receipt, std::move(blocker_batch)),
              "backpressure blocker must fill the queue");

        dispatch::TriggerResponseResult ignored_progress;
        auto dispatcher = dispatcher_with({
            {"test_all_sha_stream", [&](const dispatch::AdmittedTriggerRequest&,
                                        dispatch::TriggerResponseSink& sink) {
                ignored_progress = sink.progress(R"({"lost":false})");
                if (should_throw) throw std::runtime_error("after progress");
            }},
        });
        auto item = ingress_item("test_all_sha_stream", should_throw ? 52 : 51);
        auto result = dispatcher.submit(std::move(*item), session);
        check(ignored_progress.publish_error == protocol::PublishError::queue_full
                  && result.disposition
                      == dispatch::TriggerDispatchDisposition::retry_response
                  && result.pending_response,
              "ignored progress backpressure must yield a retryable terminal, not progress");
        check(confirm_one(session), "blocker must drain before terminal retry");
        check(result.pending_response->retry(),
              "auto error terminal must remain retryable after ignored progress");
        check(confirm_one(session),
              "retried terminal must complete the formerly blocked correlation");
        check(session.stats().active_correlations == 0,
              "return/throw after rejected progress must not orphan correlation");
    }
}

void test_cancelled_and_closed_publish_dispositions()
{
    protocol::TriggerSession cancelled_session;
    auto dispatcher = dispatcher_with({
        {"status", [&](const dispatch::AdmittedTriggerRequest& request,
                       dispatch::TriggerResponseSink& sink) {
            check(cancelled_session.request_cancel(request.timestamp())
                      == protocol::CancelDecision::requested,
                  "test must race cancellation before terminal commit");
            (void)sink.success();
        }},
    });
    auto item = ingress_item("status", 60);
    auto result = dispatcher.submit(std::move(*item), cancelled_session);
    check(result.disposition == dispatch::TriggerDispatchDisposition::close_session
              && result.response.publish_error
                  == protocol::PublishError::cancellation_response_required,
          "cancellation-wins publish failure must explicitly require close/recovery");
    const auto active = cancelled_session.close();
    check(active.size() == 1 && active[0].timestamp == 60,
          "caller can deterministically close and recover the stuck correlation");

    protocol::TriggerSession closed_session;
    [[maybe_unused]] const auto closed = closed_session.close();
    auto closed_item = ingress_item("status", 61);
    auto rejected = dispatcher.submit(std::move(*closed_item), closed_session);
    check(rejected.error == dispatch::TriggerDispatchError::admission_rejected
              && rejected.admission_error == protocol::AdmissionError::closed,
          "closed admission error must pass through without handler invocation");
}

void test_receipt_owner_generation_and_rollback()
{
    check(!protocol::AdmissionResult{},
          "default admission result without receipt must not report success");
    protocol::TriggerSession first_session;
    auto first = admit(first_session, "status", 70);
    auto first_batch = response("status", 70);
    check(first && first_session.publish(*first.receipt, std::move(first_batch)),
          "first receipt must publish its own response");
    check(confirm_one(first_session), "first terminal must be acknowledged");
    auto second = admit(first_session, "status", 70);
    auto stale_batch = response("status", 70);
    check(first_session.publish(*first.receipt, std::move(stale_batch)).error
              == protocol::PublishError::invalid_admission_receipt,
          "old generation must not inject into reused timestamp");
    auto current_batch = response("status", 70);
    check(first_session.publish(*second.receipt, std::move(current_batch)),
          "current generation receipt must remain valid");
    const auto before_rollback = first_session.stats();
    check(first_session.rollback(*second.receipt).error
              == protocol::RollbackError::response_already_queued,
          "rollback must never erase a correlation with visible queued output");
    const auto after_rollback = first_session.stats();
    check(after_rollback.queued_batches == before_rollback.queued_batches
              && after_rollback.active_correlations
                  == before_rollback.active_correlations,
          "rejected rollback must preserve queue and active correlation state");

    protocol::TriggerSession other_session;
    auto other = admit(other_session, "status", 70);
    auto cross_owner = response("status", 70);
    check(other_session.publish(*second.receipt, std::move(cross_owner)).error
              == protocol::PublishError::invalid_admission_receipt,
          "receipt must not cross session owners");
    check(other_session.rollback(*other.receipt),
          "unused receipt must support deterministic rollback");
    check(other_session.rollback(*other.receipt).error
              == protocol::RollbackError::invalid_admission_receipt,
          "duplicate rollback must be stable");

    alignas(protocol::TriggerSession)
        std::byte storage[sizeof(protocol::TriggerSession)];
    auto* original = std::construct_at(
        reinterpret_cast<protocol::TriggerSession*>(storage));
    auto old_owner = admit(*original, "status", 71);
    std::destroy_at(original);
    auto* replacement = std::construct_at(
        reinterpret_cast<protocol::TriggerSession*>(storage));
    auto new_owner = admit(*replacement, "status", 71);
    auto reused_address_batch = response("status", 71);
    check(replacement->publish(
              *old_owner.receipt, std::move(reused_address_batch)).error
              == protocol::PublishError::invalid_admission_receipt,
          "same-address session reconstruction must not revive an old receipt");
    check(replacement->rollback(*new_owner.receipt),
          "replacement session current receipt must remain valid");
    std::destroy_at(replacement);
}

void test_dispatcher_concurrent_sessions()
{
    std::atomic<int> invoked{};
    std::atomic<int> completed{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const dispatch::AdmittedTriggerRequest&,
                       dispatch::TriggerResponseSink& sink) {
            ++invoked;
            (void)sink.success();
        }},
    });
    std::vector<std::thread> workers;
    for (protocol::Timestamp timestamp = 100; timestamp < 132; ++timestamp) {
        workers.emplace_back([&, timestamp] {
            protocol::TriggerSession session;
            auto item = ingress_item("status", timestamp);
            auto result = dispatcher.submit(std::move(*item), session);
            if (result && confirm_one(session)) ++completed;
        });
    }
    for (auto& worker : workers) worker.join();
    check(invoked == 32 && completed == 32,
          "immutable dispatcher must support concurrent independent sessions");
}

}  // namespace

int main()
{
    check(dispatch::trigger_registry_error_name(
              dispatch::TriggerRegistryError::duplicate_registration)
              == "duplicate_registration",
          "registry error names must be stable");
    check(protocol::publish_error_name(
              protocol::PublishError::invalid_admission_receipt)
              == "invalid_admission_receipt",
          "receipt publish error name must be stable");
    check(protocol::rollback_error_name(
              protocol::RollbackError::response_already_queued)
              == "response_already_queued",
          "rollback error names must be stable");
    test_registry_validation_and_unregistered_precedes_admission();
    test_prefix_identity_and_staged_terminal_commit();
    test_single_progress_and_unique_stream_terminal();
    test_success_then_throw_is_replaced_by_bounded_error();
    test_binary_terminal_backpressure_has_no_copy_retry_contract();
    test_ignored_progress_backpressure_cannot_orphan_correlation();
    test_cancelled_and_closed_publish_dispositions();
    test_receipt_owner_generation_and_rollback();
    test_dispatcher_concurrent_sessions();

    if (failures != 0) {
        std::cerr << failures << " trigger dispatch test(s) failed\n";
        return 1;
    }
    std::cout << "trigger dispatch tests passed\n";
    return 0;
}
