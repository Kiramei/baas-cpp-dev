#include "service/trigger/TriggerExecutor.h"

#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace trigger = baas::service::trigger;
namespace protocol = baas::service::protocol::trigger;
using namespace std::chrono_literals;

[[nodiscard]] std::size_t
trigger_executor_production_header_size() noexcept;

namespace {

int failures = 0;
thread_local int callback_shutdown_test_depth = 0;

class HostileException final : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override
    {
        return "bad\xFFmessage-that-is-deliberately-long";
    }
};

template <typename Condition>
void check(const Condition& condition, const std::string_view message)
{
    if (!static_cast<bool>(condition)) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Predicate>
bool wait_until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

[[nodiscard]] std::optional<protocol::TriggerIngressItem> ingress_item(
    const std::string_view command, const protocol::Timestamp timestamp,
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

[[nodiscard]] std::shared_ptr<const trigger::TriggerDispatcher> dispatcher_with(
    std::vector<trigger::TriggerHandlerRegistration> registrations,
    const trigger::TriggerDispatchLimits limits = {})
{
    auto built = trigger::TriggerDispatcher::create(
        std::move(registrations), limits);
    if (!built) throw std::runtime_error("dispatcher build failed");
    return std::make_shared<const trigger::TriggerDispatcher>(
        std::move(*built.dispatcher));
}

[[nodiscard]] std::shared_ptr<protocol::TriggerSession> borrowed_session(
    protocol::TriggerSession& session)
{
    return {&session, [](protocol::TriggerSession*) noexcept {}};
}

[[nodiscard]] protocol::AdmissionResult admit(
    protocol::TriggerSession& session, std::string command,
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

[[nodiscard]] protocol::OutboundBatch terminal(
    std::string command, const protocol::Timestamp timestamp)
{
    protocol::CommandResponse response;
    response.command = std::move(command);
    response.timestamp = timestamp;
    response.data_json = std::string{"{}"};
    auto encoded = protocol::encode_command_response(std::move(response));
    if (!encoded) throw std::runtime_error("response encode failed");
    return std::move(encoded.batch);
}

[[nodiscard]] bool confirm_one(protocol::TriggerSession& session)
{
    auto begun = session.begin_send();
    return begun && session.complete_send(*begun.lease);
}

class ExecutorReadyObserver final : public protocol::OutputReadyObserver {
public:
    void output_ready() noexcept override
    {
        if (connection != nullptr) {
            const auto snapshot = connection->stats();
            observed_active.store(snapshot.active_tasks);
        }
        calls.fetch_add(1);
        changed.notify_all();
    }

    [[nodiscard]] bool wait_for_call(const std::size_t expected)
    {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 500ms, [&] {
            return calls.load() >= expected;
        });
    }

    trigger::TriggerConnectionOwner* connection{};
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic_size_t calls{};
    std::atomic_size_t observed_active{};
};

void test_reserve_before_admit_and_queue_saturation()
{
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool release = false;
    std::atomic<int> started{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest& request,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            ++started;
            if (request.timestamp() == 1) {
                std::unique_lock lock(gate_mutex);
                gate_cv.wait(lock, [&] { return release; });
            }
            static_cast<void>(sink.success());
        }},
    });
    trigger::TriggerExecutor executor(
        dispatcher, {1, 3, 1, 3});
    protocol::TriggerSession session;
    auto connection = executor.connect(borrowed_session(session), 3);

    auto first = ingress_item("status", 1);
    check(connection.submit(std::move(*first)), "first task must reserve and submit");
    check(wait_until([&] { return started.load() == 1; }),
          "first task must enter the worker");
    auto second = ingress_item("status", 2);
    check(connection.submit(std::move(*second)), "one queued task must fit");
    const auto before = session.stats();
    auto saturated = ingress_item("status", 3);
    const auto rejected = connection.submit(std::move(*saturated));
    check(rejected.error == trigger::TriggerSubmitError::queue_full,
          "queue saturation must reject before admission");
    check(session.stats().accepted == before.accepted
              && session.stats().active_correlations == before.active_correlations,
          "capacity rejection must not admit or leak correlation state");
    auto unregistered = ingress_item("copy_config", 4);
    const auto before_unregistered = session.stats().accepted;
    check(connection.submit(std::move(*unregistered)).error
              == trigger::TriggerSubmitError::unregistered_command,
          "unregistered descriptor must reject through the owner");
    check(session.stats().accepted == before_unregistered,
          "unregistered owner submission must precede admission");

    {
        std::lock_guard lock(gate_mutex);
        release = true;
    }
    gate_cv.notify_all();
    check(wait_until([&] { return executor.stats().active_tasks == 0; }),
          "accepted work must finish after gate release");
    check(confirm_one(session) && confirm_one(session),
          "both accepted terminals must be drainable");

    {
        std::lock_guard lock(gate_mutex);
        release = false;
    }
    started = 0;
    trigger::TriggerExecutor limited_executor(dispatcher, {1, 2, 2, 2});
    protocol::TriggerSession first_limited_session;
    protocol::TriggerSession second_limited_session;
    protocol::TriggerSession third_limited_session;
    auto first_limited = limited_executor.connect(
        borrowed_session(first_limited_session), 1);
    auto second_limited = limited_executor.connect(
        borrowed_session(second_limited_session), 2);
    auto third_limited = limited_executor.connect(
        borrowed_session(third_limited_session), 2);
    auto limited_running = ingress_item("status", 1);
    check(first_limited.submit(std::move(*limited_running)),
          "per-connection limit fixture must start");
    check(wait_until([&] { return started.load() == 1; }),
          "per-connection fixture worker must block");
    auto same_connection = ingress_item("status", 2);
    check(first_limited.submit(std::move(*same_connection)).error
              == trigger::TriggerSubmitError::connection_task_limit,
          "per-connection charge must reject before admission");
    check(first_limited_session.stats().accepted == 1,
          "per-connection rejection must not mutate session admission");
    auto global_second = ingress_item("status", 2);
    check(second_limited.submit(std::move(*global_second)),
          "second connection must consume remaining global slot");
    auto global_third = ingress_item("status", 3);
    check(third_limited.submit(std::move(*global_third)).error
              == trigger::TriggerSubmitError::global_task_limit,
          "global task charge must reject before admission");
    check(third_limited_session.stats().accepted == 0,
          "global rejection must not admit the third connection");
    {
        std::lock_guard lock(gate_mutex);
        release = true;
    }
    gate_cv.notify_all();
    check(wait_until([&] { return limited_executor.stats().active_tasks == 0; }),
          "limited tasks must retire");
    check(confirm_one(first_limited_session) && confirm_one(second_limited_session),
          "limited accepted tasks must drain");
}

void test_queued_cancel_skips_business_handler()
{
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool release = false;
    std::atomic<int> first_started{};
    std::atomic<int> second_side_effect{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest& request,
                       trigger::TriggerResponseSink& sink, std::stop_token token) {
            if (request.timestamp() == 10) {
                ++first_started;
                std::unique_lock lock(gate_mutex);
                gate_cv.wait(lock, [&] { return release || token.stop_requested(); });
            } else {
                ++second_side_effect;
            }
            static_cast<void>(sink.success());
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {1, 2, 2, 2});
    protocol::TriggerSession session;
    auto connection = executor.connect(borrowed_session(session));
    auto first = ingress_item("status", 10);
    auto second = ingress_item("status", 11);
    check(connection.submit(std::move(*first)), "running task must submit");
    check(wait_until([&] { return first_started.load() == 1; }),
          "first handler must block the worker");
    check(connection.submit(std::move(*second)), "second task must queue");
    const auto cancelled = connection.request_cancel(11);
    check(cancelled.session_decision == protocol::CancelDecision::requested
              && cancelled.task_found && cancelled.stop_requested,
          "cancel must reach session and queued owner under one operation lock");
    {
        std::lock_guard lock(gate_mutex);
        release = true;
    }
    gate_cv.notify_all();
    check(wait_until([&] { return executor.stats().active_tasks == 0; }),
          "cancelled queued task must complete without business execution");
    check(second_side_effect.load() == 0,
          "queued cancellation must skip the business handler entirely");
    check(confirm_one(session), "first terminal must drain");
    auto cancelled_send = session.begin_send();
    check(cancelled_send
              && cancelled_send.lease->batch().status()
                  == protocol::ResponseStatus::cancelled,
          "bridge must generate a cancelled terminal for skipped queued work");
    if (cancelled_send) static_cast<void>(session.complete_send(*cancelled_send.lease));
}

void test_running_cancel_exception_and_terminal_after_return()
{
    std::atomic<bool> entered{};
    std::atomic<bool> stop_seen{};
    auto cancelling_dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token token) {
            entered = true;
            while (!token.stop_requested()) std::this_thread::yield();
            stop_seen = true;
            static_cast<void>(sink.cancelled());
        }},
    });
    trigger::TriggerExecutor executor(cancelling_dispatcher, {1, 2, 2, 2});
    protocol::TriggerSession session;
    auto connection = executor.connect(borrowed_session(session));
    auto item = ingress_item("status", 20);
    check(connection.submit(std::move(*item)), "running cancel fixture must submit");
    check(wait_until([&] { return entered.load(); }), "handler must begin");
    static_cast<void>(connection.request_cancel(20));
    check(wait_until([&] { return stop_seen.load(); }),
          "running handler must observe read-only stop token");
    check(wait_until([&] { return executor.stats().active_tasks == 0; }),
          "cancelled running task must retire");
    check(confirm_one(session), "cancelled terminal must drain");

    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool release = false;
    std::atomic<bool> staged{};
    auto staged_dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            static_cast<void>(sink.success(R"({"premature":true})"));
            staged = true;
            std::unique_lock lock(gate_mutex);
            gate_cv.wait(lock, [&] { return release; });
        }},
    });
    trigger::TriggerExecutor staged_executor(staged_dispatcher, {1, 1, 1, 1});
    protocol::TriggerSession staged_session;
    auto staged_connection = staged_executor.connect(borrowed_session(staged_session));
    auto staged_item = ingress_item("status", 21);
    check(staged_connection.submit(std::move(*staged_item)),
          "staged task must submit");
    check(wait_until([&] { return staged.load(); }), "handler must stage terminal");
    check(staged_session.stats().queued_batches == 0,
          "terminal must remain invisible until the handler truly returns");
    {
        std::lock_guard lock(gate_mutex);
        release = true;
    }
    gate_cv.notify_all();
    check(wait_until([&] { return staged_session.stats().queued_batches == 1; }),
          "terminal must commit after handler return");
    check(confirm_one(staged_session), "staged terminal must drain");

    auto dispatch_limits = trigger::TriggerDispatchLimits{};
    dispatch_limits.max_exception_error_bytes = 24;
    auto throwing_dispatcher = dispatcher_with({
        {"status", [](const trigger::AdmittedTriggerRequest&,
                      trigger::TriggerResponseSink& sink, std::stop_token) {
            static_cast<void>(sink.success(R"({"must_not_escape":true})"));
            throw HostileException{};
        }},
    }, dispatch_limits);
    trigger::TriggerExecutor throwing_executor(throwing_dispatcher, {1, 1, 1, 1});
    protocol::TriggerSession throwing_session;
    auto throwing_connection = throwing_executor.connect(
        borrowed_session(throwing_session));
    auto throwing_item = ingress_item("status", 22);
    check(throwing_connection.submit(std::move(*throwing_item)),
          "throwing handler must submit");
    check(wait_until([&] { return throwing_session.stats().queued_batches == 1; }),
          "exception boundary must produce a terminal");
    auto failed = throwing_session.begin_send();
    check(failed && failed.lease->batch().status() == protocol::ResponseStatus::error,
          "success staged before exception must be replaced by error");
    check(failed
              && failed.lease->batch().json().find("must_not_escape")
                  == std::string::npos
              && failed.lease->batch().json().find("handler exception: bad?m")
                  != std::string::npos,
          "hostile exception text must be bounded and sanitized");
    if (failed) static_cast<void>(throwing_session.complete_send(*failed.lease));
}

void test_prefix_identity_single_and_stream_rules()
{
    std::string actual_command;
    std::string descriptor_name;
    trigger::TriggerResponseResult staged;
    auto prefix_dispatcher = dispatcher_with({
        {"start_*", [&](const trigger::AdmittedTriggerRequest& request,
                        trigger::TriggerResponseSink& sink, std::stop_token) {
            actual_command = request.command();
            descriptor_name = request.descriptor().canonical_name;
            staged = sink.success(R"({"started":true})");
        }},
    });
    trigger::TriggerExecutor prefix_executor(prefix_dispatcher, {1, 1, 1, 1});
    protocol::TriggerSession prefix_session;
    auto prefix_connection = prefix_executor.connect(borrowed_session(prefix_session));
    auto prefix_item = ingress_item("start_custom_task", 25, "default");
    check(prefix_connection.submit(std::move(*prefix_item)),
          "prefix command must submit through its canonical registration");
    check(wait_until([&] { return prefix_session.stats().queued_batches == 1; }),
          "prefix terminal must publish");
    check(staged.disposition == trigger::TriggerResponseDisposition::staged
              && actual_command == "start_custom_task"
              && descriptor_name == "start_*",
          "handler must see actual command and canonical prefix descriptor");
    auto prefix_lease = prefix_session.begin_send();
    check(prefix_lease
              && prefix_lease.lease->batch().command() == "start_custom_task",
          "response must preserve actual prefix identity");
    if (prefix_lease)
        static_cast<void>(prefix_connection.complete_send(*prefix_lease.lease));

    trigger::TriggerResponseResult progress_rejected;
    auto single_dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            progress_rejected = sink.progress(R"({"bad":true})");
            static_cast<void>(sink.success());
        }},
    });
    trigger::TriggerExecutor single_executor(single_dispatcher, {1, 1, 1, 1});
    protocol::TriggerSession single_session;
    auto single_connection = single_executor.connect(borrowed_session(single_session));
    auto single_item = ingress_item("status", 26);
    check(single_connection.submit(std::move(*single_item)),
          "single handler must submit");
    check(wait_until([&] { return single_session.stats().queued_batches == 1; }),
          "single terminal must publish");
    check(progress_rejected.error == trigger::TriggerResponseError::progress_for_single,
          "single mode must reject progress without preventing recovery");
    check(confirm_one(single_session), "single terminal must drain");

    trigger::TriggerResponseResult second_terminal;
    auto stream_dispatcher = dispatcher_with({
        {"test_all_sha_stream", [&](const trigger::AdmittedTriggerRequest& request,
                                    trigger::TriggerResponseSink& sink,
                                    std::stop_token) {
            check(request.response_mode() == protocol::ResponseMode::stream,
                  "stream mode must come from catalog admission");
            static_cast<void>(sink.progress(
                R"({"part":1})",
                std::vector<std::byte>{std::byte{0x01}, std::byte{0xFE}}));
            static_cast<void>(sink.success(R"({"count":1})"));
            second_terminal = sink.error("late");
        }},
    });
    trigger::TriggerExecutor stream_executor(stream_dispatcher, {1, 1, 1, 1});
    protocol::TriggerSession stream_session;
    auto stream_connection = stream_executor.connect(borrowed_session(stream_session));
    auto stream_item = ingress_item("test_all_sha_stream", 27);
    check(stream_connection.submit(std::move(*stream_item)),
          "stream handler must submit");
    check(wait_until([&] { return stream_session.stats().queued_batches == 2; }),
          "progress and terminal must both publish");
    check(second_terminal.error
              == trigger::TriggerResponseError::terminal_already_published,
          "second terminal must be rejected deterministically");
    auto progress = stream_session.begin_send();
    check(progress && !progress.lease->batch().terminal()
              && progress.lease->batch().has_binary()
              && progress.lease->batch().binary().size() == 2,
          "stream progress binary must remain atomic");
    if (progress)
        static_cast<void>(stream_connection.complete_send(*progress.lease));
    auto stream_terminal = stream_session.begin_send();
    check(stream_terminal && stream_terminal.lease->batch().terminal()
              && stream_terminal.lease->batch().json().find(R"("done":true)")
                  != std::string::npos,
          "stream terminal must retain codec-owned done binding");
    if (stream_terminal)
        static_cast<void>(stream_connection.complete_send(*stream_terminal.lease));
}

void test_pending_terminal_is_owned_and_retried_without_rerun()
{
    protocol::TriggerSessionLimits session_limits;
    session_limits.max_queued_batches = 1;
    protocol::TriggerSession session(session_limits);
    protocol::CommandAdmission blocker_admission;
    blocker_admission.command = "status";
    blocker_admission.timestamp = 30;
    blocker_admission.payload_bytes = 2;
    auto blocker = session.admit(std::move(blocker_admission));
    auto blocker_batch = terminal("status", 30);
    check(blocker && session.publish(*blocker.receipt, std::move(blocker_batch)),
          "test blocker must fill egress capacity");

    std::atomic<int> invoked{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            ++invoked;
            std::vector<std::byte> binary(2U * 1'024U * 1'024U, std::byte{0x5A});
            static_cast<void>(sink.success(std::nullopt, std::move(binary)));
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    auto connection = executor.connect(borrowed_session(session));
    auto item = ingress_item("status", 31);
    check(connection.submit(std::move(*item)), "backpressured task must submit");
    check(wait_until([&] { return connection.stats().completed == 1; }),
          "owner must retain completed pending terminal");
    check(invoked.load() == 1 && executor.stats().active_tasks == 1,
          "pending response must retain capacity without rerunning handler");

    auto lease = session.begin_send();
    check(lease, "blocker lease must begin");
    if (lease) {
        const auto completed = connection.complete_send(*lease.lease);
        check(completed && completed.retry.attempted == 1
                  && completed.retry.published == 1,
              "egress capacity release must retry the retained terminal");
    }
    check(invoked.load() == 1 && connection.stats().active_tasks == 0,
          "pending retry must never invoke the handler again");
    auto terminal_lease = session.begin_send();
    check(terminal_lease
              && terminal_lease.lease->batch().binary().size()
                  == 2U * 1'024U * 1'024U
              && terminal_lease.lease->batch().binary().front()
                  == std::byte{0x5A},
          "pending retry must preserve byte-exact binary output");
    if (terminal_lease)
        static_cast<void>(connection.complete_send(*terminal_lease.lease));
}

void test_ignored_progress_backpressure_cannot_orphan_correlation()
{
    for (const bool should_throw : {false, true}) {
        protocol::TriggerSessionLimits limits;
        limits.max_queued_batches = 1;
        protocol::TriggerSession session(limits);
        auto blocker = admit(session, "status", 35);
        auto blocker_batch = terminal("status", 35);
        check(blocker && session.publish(*blocker.receipt, std::move(blocker_batch)),
              "progress backpressure blocker must fill the queue");

        trigger::TriggerResponseResult ignored_progress;
        auto dispatcher = dispatcher_with({
            {"test_all_sha_stream", [&](const trigger::AdmittedTriggerRequest&,
                                        trigger::TriggerResponseSink& sink,
                                        std::stop_token) {
                ignored_progress = sink.progress(R"({"lost":false})");
                if (should_throw) throw std::runtime_error("after progress");
            }},
        });
        trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
        auto connection = executor.connect(borrowed_session(session));
        auto item = ingress_item("test_all_sha_stream", should_throw ? 37 : 36);
        check(connection.submit(std::move(*item)),
              "ignored-progress fixture must submit");
        check(wait_until([&] { return connection.stats().completed == 1; }),
              "return or throw after rejected progress must retain terminal");
        check(ignored_progress.publish_error == protocol::PublishError::queue_full,
              "progress backpressure must remain explicit to handler");
        auto blocker_lease = session.begin_send();
        check(blocker_lease, "progress blocker must begin send");
        if (blocker_lease) {
            const auto completed = connection.complete_send(*blocker_lease.lease);
            check(completed.retry.published == 1,
                  "auto terminal must retry after capacity release");
        }
        check(confirm_one(session), "auto terminal must drain");
        check(session.stats().active_correlations == 0,
              "ignored progress return/throw must not orphan correlation");
    }
}

void test_receipt_owner_generation_and_rollback_visibility()
{
    check(!protocol::AdmissionResult{},
          "default admission without receipt must not report success");
    protocol::TriggerSession first_session;
    auto first = admit(first_session, "status", 50);
    auto first_batch = terminal("status", 50);
    check(first && first_session.publish(*first.receipt, std::move(first_batch)),
          "current receipt must publish");
    check(confirm_one(first_session), "first generation terminal must drain");
    auto second = admit(first_session, "status", 50);
    auto stale = terminal("status", 50);
    check(first_session.publish(*first.receipt, std::move(stale)).error
              == protocol::PublishError::invalid_admission_receipt,
          "old generation must not inject into reused timestamp");
    auto current = terminal("status", 50);
    check(first_session.publish(*second.receipt, std::move(current)),
          "new generation receipt must publish");
    const auto before = first_session.stats();
    check(first_session.rollback(*second.receipt).error
              == protocol::RollbackError::response_already_queued,
          "rollback must reject visible response state");
    const auto after = first_session.stats();
    check(after.queued_batches == before.queued_batches
              && after.active_correlations == before.active_correlations,
          "rejected rollback must preserve queue and correlation");

    protocol::TriggerSession other_session;
    auto other = admit(other_session, "status", 50);
    auto cross_owner = terminal("status", 50);
    check(other_session.publish(*second.receipt, std::move(cross_owner)).error
              == protocol::PublishError::invalid_admission_receipt,
          "receipt must not cross session owners");
    check(other_session.rollback(*other.receipt),
          "unused receipt must roll back exactly once");
    check(other_session.rollback(*other.receipt).error
              == protocol::RollbackError::invalid_admission_receipt,
          "duplicate rollback must be stable");

    alignas(protocol::TriggerSession)
        std::byte storage[sizeof(protocol::TriggerSession)];
    auto* original = std::construct_at(
        reinterpret_cast<protocol::TriggerSession*>(storage));
    auto old_owner = admit(*original, "status", 51);
    std::destroy_at(original);
    auto* replacement = std::construct_at(
        reinterpret_cast<protocol::TriggerSession*>(storage));
    auto new_owner = admit(*replacement, "status", 51);
    auto reused_address = terminal("status", 51);
    check(replacement->publish(*old_owner.receipt, std::move(reused_address)).error
              == protocol::PublishError::invalid_admission_receipt,
          "same-address reconstruction must not revive an old receipt");
    check(replacement->rollback(*new_owner.receipt),
          "replacement session receipt must remain current");
    std::destroy_at(replacement);
}

void test_shutdown_and_executor_before_owner_destruction()
{
    std::atomic<bool> entered{};
    std::atomic<bool> stopped{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token token) {
            entered = true;
            while (!token.stop_requested()) std::this_thread::yield();
            stopped = true;
            static_cast<void>(sink.cancelled());
        }},
    });
    protocol::TriggerSession session;
    std::optional<trigger::TriggerConnectionOwner> owner;
    {
        auto executor = std::make_unique<trigger::TriggerExecutor>(
            dispatcher, trigger::TriggerExecutorLimits{1, 2, 2, 2});
        owner.emplace(executor->connect(borrowed_session(session)));
        auto item = ingress_item("status", 40);
        check(owner->submit(std::move(*item)), "shutdown fixture must submit");
        check(wait_until([&] { return entered.load(); }), "shutdown handler must start");
        executor.reset();
    }
    check(stopped.load(), "executor shutdown must request stop and join worker");
    check(session.stats().closed, "executor shutdown must close active connection");
    owner.reset();
    check(true, "owner destruction after executor must not dereference freed state");
}

void test_worker_initiated_shutdown_has_no_self_join()
{
    std::atomic<trigger::TriggerExecutor*> executor_pointer{};
    std::atomic<bool> shutdown_returned{};
    std::atomic<bool> external_returned{};
    std::mutex finish_mutex;
    std::condition_variable finish_cv;
    bool allow_handler_return = false;
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            executor_pointer.load(std::memory_order_acquire)->shutdown();
            shutdown_returned = true;
            {
                std::unique_lock lock(finish_mutex);
                finish_cv.wait(lock, [&] { return allow_handler_return; });
            }
            static_cast<void>(sink.cancelled());
        }},
    });
    protocol::TriggerSession session;
    auto executor = std::make_unique<trigger::TriggerExecutor>(
        dispatcher, trigger::TriggerExecutorLimits{1, 1, 1, 1});
    executor_pointer.store(executor.get(), std::memory_order_release);
    auto owner = executor->connect(borrowed_session(session));
    auto item = ingress_item("status", 41);
    check(owner.submit(std::move(*item)), "self-shutdown task must submit");
    check(wait_until([&] { return shutdown_returned.load(); }),
          "shutdown called by a worker must return without joining itself");
    std::thread external_shutdown([&] {
        executor->shutdown();
        external_returned = true;
    });
    std::this_thread::sleep_for(20ms);
    check(!external_returned.load(),
          "later external shutdown must wait for detached worker exit");
    {
        std::lock_guard lock(finish_mutex);
        allow_handler_return = true;
    }
    finish_cv.notify_all();
    external_shutdown.join();
    check(external_returned.load(),
          "external shutdown must return after detached worker exits");
    check(wait_until([&] { return owner.stats().active_tasks == 0; }),
          "detached calling worker must still retire its owned slot");
    executor.reset();
}

void test_pending_cancel_and_fatal_submission_linearization()
{
    protocol::TriggerSessionLimits limits;
    limits.max_queued_batches = 1;
    protocol::TriggerSession session(limits);
    auto blocker = admit(session, "status", 60);
    auto blocker_batch = terminal("status", 60);
    check(blocker && session.publish(*blocker.receipt, std::move(blocker_batch)),
          "pending-cancel blocker must publish");
    std::atomic<int> invoked{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            ++invoked;
            static_cast<void>(sink.success());
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    auto owner = executor.connect(borrowed_session(session));
    auto item = ingress_item("status", 61);
    check(owner.submit(std::move(*item)), "pending-cancel task must submit");
    check(wait_until([&] { return owner.stats().completed == 1; }),
          "success terminal must become owner-held pending state");
    const auto cancelled = owner.request_cancel(61);
    check(cancelled.session_decision == protocol::CancelDecision::requested,
          "cancellation must win before pending terminal is queued");
    check(owner.stats().completed == 1 && invoked.load() == 1,
          "pending terminal replacement must not rerun business handler");
    auto blocker_lease = session.begin_send();
    check(blocker_lease, "pending-cancel blocker lease must begin");
    if (blocker_lease)
        static_cast<void>(owner.complete_send(*blocker_lease.lease));
    auto cancelled_lease = session.begin_send();
    check(cancelled_lease
              && cancelled_lease.lease->batch().status()
                  == protocol::ResponseStatus::cancelled,
          "pending success must be replaced by cancelled terminal");
    if (cancelled_lease)
        static_cast<void>(owner.complete_send(*cancelled_lease.lease));

    protocol::TriggerSession fatal_session;
    std::atomic<bool> handler_entered{};
    auto fatal_dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            handler_entered = true;
            static_cast<void>(fatal_session.close());
            static_cast<void>(sink.success());
        }},
    });
    trigger::TriggerExecutor fatal_executor(fatal_dispatcher, {1, 2, 2, 2});
    auto fatal_owner = fatal_executor.connect(borrowed_session(fatal_session));
    auto fatal_item = ingress_item("status", 62);
    check(fatal_owner.submit(std::move(*fatal_item)), "fatal task must submit");
    check(wait_until([&] { return handler_entered.load(); }),
          "fatal handler must execute");
    check(wait_until([&] { return fatal_owner.stats().close_required; }),
          "fatal result must linearize close-required in owner state");
    const auto accepted_before = fatal_session.stats().accepted;
    auto after_fatal = ingress_item("status", 63);
    check(fatal_owner.submit(std::move(*after_fatal)).error
              == trigger::TriggerSubmitError::connection_stopped,
          "no submit may reserve after fatal close linearization");
    check(fatal_session.stats().accepted == accepted_before,
          "post-fatal rejection must occur before admission");
}

void test_fail_send_handoff_stops_running_owner()
{
    protocol::TriggerSession session;
    auto blocker = admit(session, "status", 70);
    auto blocker_batch = terminal("status", 70);
    check(blocker && session.publish(*blocker.receipt, std::move(blocker_batch)),
          "fail-send blocker must publish");
    std::atomic<bool> entered{};
    std::atomic<bool> stopped{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token token) {
            entered = true;
            while (!token.stop_requested()) std::this_thread::yield();
            stopped = true;
            static_cast<void>(sink.cancelled());
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    auto owner = executor.connect(borrowed_session(session));
    auto item = ingress_item("status", 71);
    check(owner.submit(std::move(*item)), "fail-send running task must submit");
    check(wait_until([&] { return entered.load(); }),
          "fail-send handler must be running");
    auto lease = session.begin_send();
    check(lease, "fail-send lease must begin");
    if (lease) {
        const auto failed = owner.fail_send(*lease.lease);
        check(failed && failed.cancellations_consumed == 1,
              "successful fail-send must consume active task handoff");
    }
    check(wait_until([&] { return stopped.load(); }),
          "fail-send handoff must request stop on running owner");
    check(wait_until([&] { return owner.stats().active_tasks == 0; }),
          "fail-send cancellation must retire the running task");
}

void test_input_and_pending_byte_budgets()
{
    auto simple_dispatcher = dispatcher_with({
        {"status", [](const trigger::AdmittedTriggerRequest&,
                      trigger::TriggerResponseSink& sink, std::stop_token) {
            static_cast<void>(sink.success());
        }},
    });
    trigger::TriggerExecutorLimits input_limits;
    input_limits.worker_threads = 1;
    input_limits.max_tasks = 2;
    input_limits.max_queued_tasks = 2;
    input_limits.max_tasks_per_connection = 2;
    input_limits.max_input_bytes = 32;
    input_limits.max_input_bytes_per_connection = 32;
    trigger::TriggerExecutor input_executor(simple_dispatcher, input_limits);
    protocol::TriggerSession input_session;
    auto input_owner = input_executor.connect(borrowed_session(input_session));
    std::string oversized_payload =
        "{\"x\":\"" + std::string(64, 'x') + "\"}";
    auto oversized = ingress_item("status", 80, std::nullopt, oversized_payload);
    check(oversized.has_value(), "oversized executor input must pass ingress limits");
    const auto oversized_result = input_owner.submit(std::move(*oversized));
    check(oversized_result.error
              == trigger::TriggerSubmitError::global_input_limit,
          "global input byte budget must reject before admission");
    const auto input_session_stats = input_session.stats();
    const auto input_executor_stats = input_executor.stats();
    check(input_session_stats.accepted == 0
              && input_executor_stats.retained_input_bytes == 0,
          "input budget rejection must not retain bytes or correlation");

    auto connection_input_limits = input_limits;
    connection_input_limits.max_input_bytes = 256;
    connection_input_limits.max_input_bytes_per_connection = 32;
    trigger::TriggerExecutor connection_input_executor(
        simple_dispatcher, connection_input_limits);
    protocol::TriggerSession connection_input_session;
    auto connection_input_owner = connection_input_executor.connect(
        borrowed_session(connection_input_session));
    auto connection_oversized = ingress_item(
        "status", 801, std::nullopt, oversized_payload);
    const auto connection_oversized_result = connection_input_owner.submit(
        std::move(*connection_oversized));
    check(connection_oversized_result.error
              == trigger::TriggerSubmitError::connection_input_limit
              && connection_input_session.stats().accepted == 0,
          "per-connection input budget must reject independently before admission");

    protocol::CommandResponse primary_response;
    primary_response.command = "status";
    primary_response.timestamp = 802;
    auto encoded_primary = protocol::encode_command_response(
        std::move(primary_response));
    protocol::CommandResponse fallback_response;
    fallback_response.command = "status";
    fallback_response.timestamp = 802;
    fallback_response.status = protocol::ResponseStatus::cancelled;
    fallback_response.error = "cancelled";
    auto encoded_fallback = protocol::encode_command_response(
        std::move(fallback_response));
    const auto primary_bytes = encoded_primary.batch.json().size();
    const auto fallback_bytes = encoded_fallback.batch.json().size();
    check(encoded_primary && encoded_fallback && fallback_bytes > primary_bytes,
          "cancelled fallback fixture must be larger than primary success");
    protocol::TriggerSessionLimits tight_session_limits;
    tight_session_limits.max_queued_batches = 1;
    auto tight_session = std::make_shared<protocol::TriggerSession>(
        tight_session_limits);
    auto tight_blocker = admit(*tight_session, "status", 803);
    auto tight_blocker_batch = terminal("status", 803);
    check(tight_blocker && tight_session->publish(
              *tight_blocker.receipt, std::move(tight_blocker_batch)),
          "per-connection pending fixture must fill outbound capacity");
    trigger::TriggerExecutorLimits tight_pending_limits;
    tight_pending_limits.worker_threads = 1;
    tight_pending_limits.max_tasks = 1;
    tight_pending_limits.max_queued_tasks = 1;
    tight_pending_limits.max_tasks_per_connection = 1;
    tight_pending_limits.max_pending_response_bytes =
        primary_bytes + fallback_bytes + 1;
    tight_pending_limits.max_pending_response_bytes_per_connection =
        fallback_bytes;
    trigger::TriggerExecutor tight_pending_executor(
        simple_dispatcher, tight_pending_limits);
    auto tight_owner = tight_pending_executor.connect(tight_session);
    auto tight_item = ingress_item("status", 802);
    check(tight_owner.submit(std::move(*tight_item)),
          "tight per-connection pending fixture must submit");
    check(wait_until([&] {
        return tight_pending_executor.stats().handlers_finished == 1;
    }), "tight per-connection pending handler must finish");
    check(tight_owner.stats().close_required
              && tight_pending_executor.stats().completed == 0
              && tight_pending_executor.stats().pending_response_bytes == 0,
          "per-connection pending budget must charge larger cancelled fallback");

    protocol::TriggerSessionLimits session_limits;
    session_limits.max_queued_batches = 1;
    protocol::TriggerSession first_session(session_limits);
    protocol::TriggerSession second_session(session_limits);
    auto first_blocker = admit(first_session, "status", 81);
    auto second_blocker = admit(second_session, "status", 82);
    auto first_blocker_batch = terminal("status", 81);
    auto second_blocker_batch = terminal("status", 82);
    check(first_blocker && first_session.publish(
              *first_blocker.receipt, std::move(first_blocker_batch)),
          "first pending-budget blocker must publish");
    check(second_blocker && second_session.publish(
              *second_blocker.receipt, std::move(second_blocker_batch)),
          "second pending-budget blocker must publish");

    std::atomic<int> invoked{};
    auto binary_dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            ++invoked;
            std::vector<std::byte> output(
                2U * 1'024U * 1'024U, std::byte{0x44});
            static_cast<void>(sink.success(std::nullopt, std::move(output)));
        }},
    });
    trigger::TriggerExecutorLimits pending_limits;
    pending_limits.worker_threads = 2;
    pending_limits.max_tasks = 2;
    pending_limits.max_queued_tasks = 2;
    pending_limits.max_tasks_per_connection = 1;
    pending_limits.max_pending_response_bytes = 3U * 1'024U * 1'024U;
    pending_limits.max_pending_response_bytes_per_connection =
        3U * 1'024U * 1'024U;
    trigger::TriggerExecutor pending_executor(binary_dispatcher, pending_limits);
    auto first_owner = pending_executor.connect(borrowed_session(first_session));
    auto second_owner = pending_executor.connect(borrowed_session(second_session));
    std::string large_input =
        "{\"x\":\"" + std::string(512U * 1'024U, 'p') + "\"}";
    auto first_item = ingress_item("status", 83, std::nullopt, large_input);
    auto second_item = ingress_item("status", 84, std::nullopt, large_input);
    check(first_owner.submit(std::move(*first_item))
              && second_owner.submit(std::move(*second_item)),
          "two large pending-budget fixtures must submit");
    check(wait_until([&] { return pending_executor.stats().handlers_finished == 2; }),
          "both large-output handlers must finish exactly once");
    const auto bounded = pending_executor.stats();
    check(invoked.load() == 2 && bounded.completed == 1
              && bounded.active_tasks == 1,
          "second large pending terminal must be rejected by byte budget");
    check(bounded.retained_input_bytes == 0
              && bounded.pending_response_bytes
                  <= pending_limits.max_pending_response_bytes,
          "completed pending state must release large input and retain bounded output");
    check(first_owner.stats().close_required != second_owner.stats().close_required,
          "exactly one connection must close when global pending bytes saturate");

    auto* surviving_owner = first_owner.stats().completed == 1
        ? &first_owner : &second_owner;
    auto* surviving_session = first_owner.stats().completed == 1
        ? &first_session : &second_session;
    auto blocker_lease = surviving_session->begin_send();
    check(blocker_lease, "surviving pending blocker must begin");
    if (blocker_lease)
        static_cast<void>(surviving_owner->complete_send(*blocker_lease.lease));
    check(wait_until([&] {
        return pending_executor.stats().pending_response_bytes == 0;
    }), "pending bytes must be released exactly after retry publication");
    check(invoked.load() == 2, "pending byte retry must not rerun either handler");
}

void test_owner_self_shutdown_and_concurrent_lifecycle()
{
    std::atomic<trigger::TriggerConnectionOwner*> owner_pointer{};
    std::atomic<bool> self_returned{};
    std::atomic<bool> external_returned{};
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool release = false;
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            owner_pointer.load(std::memory_order_acquire)->shutdown();
            self_returned = true;
            std::unique_lock lock(gate_mutex);
            gate_cv.wait(lock, [&] { return release; });
            static_cast<void>(sink.cancelled());
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    protocol::TriggerSession session;
    auto owner = executor.connect(borrowed_session(session));
    owner_pointer.store(&owner, std::memory_order_release);
    auto item = ingress_item("status", 90);
    check(owner.submit(std::move(*item)), "owner self-shutdown task must submit");
    check(wait_until([&] { return self_returned.load(); }),
          "owner shutdown from its own handler must not wait on itself");
    std::thread external([&] {
        owner.shutdown();
        external_returned = true;
    });
    std::this_thread::sleep_for(20ms);
    check(!external_returned.load(),
          "later external owner shutdown must wait for running slot");
    std::thread observer([&] {
        for (int index = 0; index < 100; ++index) {
            static_cast<void>(owner.stats());
            static_cast<void>(owner.request_cancel(90));
        }
    });
    {
        std::lock_guard lock(gate_mutex);
        release = true;
    }
    gate_cv.notify_all();
    observer.join();
    external.join();
    owner.shutdown();
    check(external_returned.load() && owner.stats().active_tasks == 0,
          "double shutdown with concurrent stats/cancel must converge safely");
}

void test_connection_registry_shutdown_and_connect_race()
{
    auto dispatcher = dispatcher_with({
        {"status", [](const trigger::AdmittedTriggerRequest&,
                      trigger::TriggerResponseSink& sink, std::stop_token) {
            static_cast<void>(sink.success());
        }},
    });
    {
        trigger::TriggerExecutor executor(dispatcher, {1, 2, 2, 2});
        protocol::TriggerSession idle_session;
        auto idle_owner = executor.connect(borrowed_session(idle_session));
        bool duplicate_rejected = false;
        try {
            auto duplicate = executor.connect(borrowed_session(idle_session));
            static_cast<void>(duplicate);
        } catch (const std::runtime_error&) {
            duplicate_rejected = true;
        }
        check(duplicate_rejected,
              "one session must not acquire two independent connection owners");
        trigger::TriggerExecutor second_executor(dispatcher, {1, 2, 2, 2});
        bool cross_executor_rejected = false;
        try {
            auto duplicate = second_executor.connect(borrowed_session(idle_session));
            static_cast<void>(duplicate);
        } catch (const std::runtime_error&) {
            cross_executor_rejected = true;
        }
        check(cross_executor_rejected,
              "session claim must reject a second executor pool");
        executor.shutdown();
        check(idle_session.stats().closed && !idle_owner.stats().accepting,
              "shutdown must close zero-task registered connections");
    }
    {
        trigger::TriggerExecutor executor(dispatcher, {1, 2, 2, 2});
        protocol::TriggerSession queued_output_session;
        auto owner = executor.connect(borrowed_session(queued_output_session));
        auto item = ingress_item("status", 91);
        check(owner.submit(std::move(*item)), "unsent-terminal fixture must submit");
        check(wait_until([&] {
            return queued_output_session.stats().queued_batches == 1
                && owner.stats().active_tasks == 0;
        }), "terminal must be queued after execution slot release");
        executor.shutdown();
        check(queued_output_session.stats().closed
                  && queued_output_session.stats().queued_batches == 0,
              "registry shutdown must close unsent terminal connections");
    }
    {
        trigger::TriggerExecutor executor(dispatcher, {1, 2, 2, 2});
        protocol::TriggerSession raced_session;
        std::optional<trigger::TriggerConnectionOwner> raced_owner;
        std::atomic<bool> start{};
        std::thread connector([&] {
            while (!start.load()) std::this_thread::yield();
            try {
                raced_owner.emplace(executor.connect(borrowed_session(raced_session)));
            } catch (const std::runtime_error&) {
            }
        });
        std::thread stopper([&] {
            while (!start.load()) std::this_thread::yield();
            executor.shutdown();
        });
        start = true;
        connector.join();
        stopper.join();
        check(!raced_owner || raced_session.stats().closed,
              "connect/shutdown race must reject or return a closed owner");
    }
    {
        auto closed_session = std::make_shared<protocol::TriggerSession>();
        trigger::TriggerExecutor first_executor(dispatcher, {1, 1, 1, 1});
        {
            auto owner = first_executor.connect(closed_session);
            static_cast<void>(owner.close());
        }
        first_executor.shutdown();
        trigger::TriggerExecutor second_executor(dispatcher, {1, 1, 1, 1});
        bool closed_rejected = false;
        try {
            auto owner = second_executor.connect(closed_session);
            static_cast<void>(owner);
        } catch (const std::runtime_error&) {
            closed_rejected = true;
        }
        check(closed_rejected, "closed sessions must never be reclaimed by an executor");
    }
}

void test_external_first_and_cross_owner_worker_shutdown()
{
    std::atomic<trigger::TriggerExecutor*> executor_pointer{};
    std::atomic<bool> entered{};
    std::atomic<bool> allow_self{};
    std::atomic<bool> self_returned{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            entered = true;
            while (!allow_self.load()) std::this_thread::yield();
            executor_pointer.load()->shutdown();
            self_returned = true;
            static_cast<void>(sink.cancelled());
        }},
    });
    auto session = std::make_shared<protocol::TriggerSession>();
    auto executor = std::make_unique<trigger::TriggerExecutor>(
        dispatcher, trigger::TriggerExecutorLimits{1, 2, 2, 2});
    executor_pointer = executor.get();
    auto owner = executor->connect(session);
    auto item = ingress_item("status", 92);
    check(owner.submit(std::move(*item)), "external-first fixture must submit");
    check(wait_until([&] { return entered.load(); }), "external-first handler must enter");
    std::atomic<bool> external_returned{};
    std::thread external([&] {
        executor->shutdown();
        external_returned = true;
    });
    std::this_thread::sleep_for(20ms);
    allow_self = true;
    external.join();
    check(self_returned.load() && external_returned.load(),
          "external-first and worker shutdown must not deadlock");

    std::atomic<trigger::TriggerConnectionOwner*> other_owner{};
    std::atomic<bool> first_started{};
    std::atomic<bool> invoke_cross_shutdown{};
    std::atomic<int> queued_side_effect{};
    auto cross_dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest& request,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            if (request.timestamp() == 93) {
                first_started = true;
                while (!invoke_cross_shutdown.load()) std::this_thread::yield();
                other_owner.load()->shutdown();
            } else {
                ++queued_side_effect;
            }
            static_cast<void>(sink.success());
        }},
    });
    trigger::TriggerExecutor cross_executor(cross_dispatcher, {1, 2, 2, 1});
    auto first_session = std::make_shared<protocol::TriggerSession>();
    auto second_session = std::make_shared<protocol::TriggerSession>();
    auto first_owner = cross_executor.connect(first_session);
    auto second_owner = cross_executor.connect(second_session);
    other_owner = &second_owner;
    auto first_item = ingress_item("status", 93);
    auto second_item = ingress_item("status", 94);
    check(first_owner.submit(std::move(*first_item)), "cross-owner first task must submit");
    check(wait_until([&] { return first_started.load(); }),
          "cross-owner first handler must run");
    check(second_owner.submit(std::move(*second_item)),
          "cross-owner second task must queue");
    invoke_cross_shutdown = true;
    check(wait_until([&] { return cross_executor.stats().active_tasks == 0; }),
          "worker shutdown of queued owner must not block the only worker");
    check(queued_side_effect.load() == 0,
          "queued owner shutdown must suppress business side effects");
    second_owner.shutdown();
}

void test_two_executor_workers_cross_shutdown_without_join_cycle()
{
    std::atomic<trigger::TriggerExecutor*> first_executor{};
    std::atomic<trigger::TriggerExecutor*> second_executor{};
    std::atomic<int> returned{};
    std::barrier rendezvous{2};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest& request,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            rendezvous.arrive_and_wait();
            auto* other = request.timestamp() == 200
                ? second_executor.load(std::memory_order_acquire)
                : first_executor.load(std::memory_order_acquire);
            other->shutdown();
            ++returned;
            static_cast<void>(sink.cancelled());
        }},
    });
    trigger::TriggerExecutor first(dispatcher, {1, 1, 1, 1});
    trigger::TriggerExecutor second(dispatcher, {1, 1, 1, 1});
    first_executor = &first;
    second_executor = &second;
    auto first_session = std::make_shared<protocol::TriggerSession>();
    auto second_session = std::make_shared<protocol::TriggerSession>();
    auto first_owner = first.connect(first_session);
    auto second_owner = second.connect(second_session);
    auto first_item = ingress_item("status", 200);
    auto second_item = ingress_item("status", 201);
    check(first_owner.submit(std::move(*first_item))
              && second_owner.submit(std::move(*second_item)),
          "cross-executor shutdown fixtures must submit");
    check(wait_until([&] { return returned.load() == 2; }),
          "executor workers must never join each other during cross shutdown");
    first.shutdown();
    second.shutdown();
    check(first.stats().active_tasks == 0 && second.stats().active_tasks == 0,
          "external shutdown must drain both cross-stopped executors");
}

void test_concurrent_external_shutdown_waits_for_registry_close()
{
    constexpr std::size_t idle_count = 2'048;
    auto dispatcher = dispatcher_with({
        {"status", [](const trigger::AdmittedTriggerRequest&,
                      trigger::TriggerResponseSink& sink, std::stop_token) {
            static_cast<void>(sink.success());
        }},
    });
    trigger::TriggerExecutorLimits limits{1, 1, 1, 1};
    limits.max_connections = idle_count + 1;
    trigger::TriggerExecutor executor(dispatcher, limits);
    std::vector<std::shared_ptr<protocol::TriggerSession>> sessions;
    std::vector<trigger::TriggerConnectionOwner> owners;
    sessions.reserve(idle_count + 1);
    owners.reserve(idle_count + 1);
    for (std::size_t index = 0; index < idle_count; ++index) {
        sessions.push_back(std::make_shared<protocol::TriggerSession>());
        owners.push_back(executor.connect(sessions.back()));
    }
    auto unsent_session = std::make_shared<protocol::TriggerSession>();
    owners.push_back(executor.connect(unsent_session));
    sessions.push_back(unsent_session);
    auto unsent = admit(*unsent_session, "status", 202);
    auto unsent_batch = terminal("status", 202);
    check(unsent && unsent_session->publish(
              *unsent.receipt, std::move(unsent_batch)),
          "registry gate fixture must queue an unsent terminal");

    std::barrier start{3};
    std::atomic<int> observed_closed{};
    auto stop = [&] {
        start.arrive_and_wait();
        executor.shutdown();
        if (sessions.front()->stats().closed
            && unsent_session->stats().closed
            && unsent_session->stats().queued_batches == 0) {
            ++observed_closed;
        }
    };
    std::thread first(stop);
    std::thread second(stop);
    start.arrive_and_wait();
    first.join();
    second.join();
    check(observed_closed.load() == 2,
          "every external shutdown caller must wait for registry force-close");
}

void test_shutdown_race_cannot_publish_completed_after_registry_scan()
{
    constexpr std::size_t task_count = 32;
    protocol::TriggerSessionLimits session_limits;
    session_limits.max_in_flight = task_count + 1;
    session_limits.max_queued_batches = 1;
    auto session = std::make_shared<protocol::TriggerSession>(session_limits);
    auto blocker = admit(*session, "status", 203);
    auto blocker_batch = terminal("status", 203);
    check(blocker && session->publish(*blocker.receipt, std::move(blocker_batch)),
          "close/backpressure fixture must fill outbound capacity");
    std::atomic<std::size_t> staged{};
    std::atomic<bool> release{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            static_cast<void>(sink.success());
            ++staged;
            while (!release.load(std::memory_order_acquire))
                std::this_thread::yield();
        }},
    });
    trigger::TriggerExecutorLimits limits{
        task_count, task_count, task_count, task_count};
    trigger::TriggerExecutor executor(dispatcher, limits);
    auto owner = executor.connect(session);
    for (std::size_t index = 0; index < task_count; ++index) {
        auto item = ingress_item("status", 204 + index);
        check(owner.submit(std::move(*item)),
              "close/backpressure raced task must submit");
    }
    check(wait_until([&] { return staged.load() == task_count; }),
          "all raced handlers must stage terminals before release");
    std::barrier race{2};
    std::thread stopper([&] {
        race.arrive_and_wait();
        executor.shutdown();
    });
    race.arrive_and_wait();
    release.store(true, std::memory_order_release);
    stopper.join();
    const auto stats = executor.stats();
    check(stats.completed == 0 && stats.active_tasks == 0
              && stats.pending_response_bytes == 0,
          "stopping executor must discard late pending results after registry scan");
    check(session->stats().closed,
          "shutdown race must leave the backpressured session closed");
}

void test_running_to_completed_cancel_window_uses_cancelled_fallback()
{
    constexpr std::size_t task_count = 32;
    protocol::TriggerSessionLimits session_limits;
    session_limits.max_in_flight = task_count + 1;
    session_limits.max_queued_batches = 1;
    auto session = std::make_shared<protocol::TriggerSession>(session_limits);
    auto blocker = admit(*session, "status", 300);
    auto blocker_batch = terminal("status", 300);
    check(blocker && session->publish(*blocker.receipt, std::move(blocker_batch)),
          "cancel-window fixture must fill outbound capacity");
    std::atomic<std::size_t> staged{};
    std::atomic<bool> release{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            static_cast<void>(sink.success());
            ++staged;
            while (!release.load(std::memory_order_acquire))
                std::this_thread::yield();
        }},
    });
    trigger::TriggerExecutorLimits limits{
        task_count, task_count, task_count, task_count};
    trigger::TriggerExecutor executor(dispatcher, limits);
    auto owner = executor.connect(session);
    for (std::size_t index = 0; index < task_count; ++index) {
        auto item = ingress_item("status", 301 + index);
        check(owner.submit(std::move(*item)),
              "cancel-window raced task must submit");
    }
    check(wait_until([&] { return staged.load() == task_count; }),
          "all cancel-window handlers must stage success before racing");
    std::barrier race{2};
    std::thread canceller([&] {
        race.arrive_and_wait();
        for (std::size_t index = 0; index < task_count; ++index)
            static_cast<void>(owner.request_cancel(301 + index));
    });
    race.arrive_and_wait();
    release.store(true, std::memory_order_release);
    canceller.join();
    check(wait_until([&] { return owner.stats().completed == task_count; }),
          "every cancelled backpressured terminal must remain retry-owned");

    auto lease = session->begin_send();
    check(lease, "cancel-window blocker must begin send");
    if (lease) static_cast<void>(owner.complete_send(*lease.lease));
    std::size_t cancelled_count{};
    while (cancelled_count < task_count) {
        auto terminal_lease = session->begin_send();
        if (!terminal_lease) break;
        if (terminal_lease.lease->batch().status()
            == protocol::ResponseStatus::cancelled) {
            ++cancelled_count;
        }
        static_cast<void>(owner.complete_send(*terminal_lease.lease));
    }
    check(cancelled_count == task_count && owner.stats().active_tasks == 0,
          "cancel requests must replace every late success without rerunning handlers");
}

void test_irrevocable_terminal_resists_cancel_and_shutdown_rewrite()
{
    protocol::TriggerSessionLimits limits;
    limits.max_queued_batches = 1;
    auto session = std::make_shared<protocol::TriggerSession>(limits);
    auto blocker = admit(*session, "status", 350);
    auto blocker_batch = terminal("status", 350);
    check(blocker && session->publish(*blocker.receipt, std::move(blocker_batch)),
          "irrevocable fixture must fill outbound capacity");
    std::atomic<bool> claimed{};
    std::atomic<bool> release{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            const auto prepared = sink.irrevocable_success();
            claimed = sink.irrevocable_terminal_claimed();
            check(prepared && claimed,
                  "handler must prepare and claim an irrevocable terminal");
            while (!release.load(std::memory_order_acquire))
                std::this_thread::yield();
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    auto owner = executor.connect(session);
    auto item = ingress_item("status", 351);
    check(owner.submit(std::move(*item)),
          "irrevocable cancellation fixture must submit");
    check(wait_until([&] { return claimed.load(); }),
          "handler must close cancellation before its side-effect commit");
    const auto cancelled = owner.request_cancel(351);
    check(cancelled.session_decision
              == protocol::CancelDecision::terminal_already_queued
              && !cancelled.stop_requested,
          "cancel after irrevocable claim must not request stop or rewrite success");
    release = true;
    check(wait_until([&] { return owner.stats().completed == 1; }),
          "irrevocable success must retain backpressure ownership");
    auto lease = session->begin_send();
    if (lease) static_cast<void>(owner.complete_send(*lease.lease));
    auto success = session->begin_send();
    check(success && success.lease->batch().status() == protocol::ResponseStatus::ok,
          "retried irrevocable terminal must remain successful");
    if (success) static_cast<void>(owner.complete_send(*success.lease));

    auto shutdown_session = std::make_shared<protocol::TriggerSession>();
    std::atomic<bool> shutdown_claimed{};
    std::atomic<bool> shutdown_release{};
    auto shutdown_dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            static_cast<void>(sink.irrevocable_success());
            shutdown_claimed = sink.irrevocable_terminal_claimed();
            while (!shutdown_release.load(std::memory_order_acquire))
                std::this_thread::yield();
        }},
    });
    trigger::TriggerExecutor shutdown_executor(
        shutdown_dispatcher, {1, 1, 1, 1});
    auto shutdown_owner = shutdown_executor.connect(shutdown_session);
    auto shutdown_item = ingress_item("status", 352);
    check(shutdown_owner.submit(std::move(*shutdown_item)),
          "irrevocable shutdown fixture must submit");
    check(wait_until([&] { return shutdown_claimed.load(); }),
          "shutdown fixture must claim before shutdown starts");
    std::thread stopping([&] { shutdown_executor.shutdown(); });
    shutdown_release = true;
    stopping.join();
    check(shutdown_executor.stats().active_tasks == 0
              && shutdown_session->stats().closed,
          "shutdown after claim must drain without fabricating cancelled output");
}

void test_stop_callback_reenters_stats_cancel_and_owner_shutdown()
{
    std::atomic<trigger::TriggerExecutor*> executor_pointer{};
    std::atomic<trigger::TriggerConnectionOwner*> owner_pointer{};
    std::atomic<bool> callback_registered{};
    std::atomic<bool> callback_returned{};
    std::atomic<bool> outer_cancel_returned{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token token) {
            std::stop_callback callback(token, [&] {
                static_cast<void>(executor_pointer.load()->stats());
                static_cast<void>(owner_pointer.load()->stats());
                static_cast<void>(owner_pointer.load()->request_cancel(400));
                owner_pointer.load()->shutdown();
                callback_returned = true;
            });
            callback_registered = true;
            while (!callback_returned.load(std::memory_order_acquire))
                std::this_thread::yield();
            static_cast<void>(sink.cancelled());
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    auto session = std::make_shared<protocol::TriggerSession>();
    auto owner = executor.connect(session);
    executor_pointer = &executor;
    owner_pointer = &owner;
    auto item = ingress_item("status", 400);
    check(owner.submit(std::move(*item)),
          "reentrant stop-callback fixture must submit");
    check(wait_until([&] { return callback_registered.load(); }),
          "handler must register stop callback before cancellation");
    std::thread cancel([&] {
        static_cast<void>(owner.request_cancel(400));
        outer_cancel_returned = true;
    });
    check(wait_until([&] {
        return callback_returned.load() && outer_cancel_returned.load();
    }), "stop callback may reenter stats, cancel, and owner shutdown");
    cancel.join();
    check(wait_until([&] { return owner.stats().active_tasks == 0; }),
          "reentrant owner shutdown callback must not wait on its handler slot");
    check(session->stats().closed,
          "owner shutdown from stop callback must still close the session");
}

void test_shutdown_stop_callback_reenters_executor_shutdown()
{
    std::atomic<trigger::TriggerExecutor*> executor_pointer{};
    std::atomic<trigger::TriggerConnectionOwner*> owner_pointer{};
    std::atomic<bool> callback_registered{};
    std::atomic<bool> callback_returned{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token token) {
            std::stop_callback callback(token, [&] {
                static_cast<void>(executor_pointer.load()->stats());
                executor_pointer.load()->shutdown();
                owner_pointer.load()->shutdown();
                callback_returned = true;
            });
            callback_registered = true;
            while (!callback_returned.load(std::memory_order_acquire))
                std::this_thread::yield();
            static_cast<void>(sink.cancelled());
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    auto session = std::make_shared<protocol::TriggerSession>();
    auto owner = executor.connect(session);
    executor_pointer = &executor;
    owner_pointer = &owner;
    auto item = ingress_item("status", 401);
    check(owner.submit(std::move(*item)),
          "shutdown stop-callback fixture must submit");
    check(wait_until([&] { return callback_registered.load(); }),
          "shutdown fixture must register callback before stop");
    std::atomic<bool> shutdown_returned{};
    std::thread shutdown([&] {
        executor.shutdown();
        shutdown_returned = true;
    });
    check(wait_until([&] {
        return callback_returned.load() && shutdown_returned.load();
    }), "registry gate must precede callbacks that reenter executor shutdown");
    shutdown.join();
    check(executor.stats().active_tasks == 0 && session->stats().closed,
          "external shutdown must drain after reentrant stop callback returns");
}

void test_owner_shutdown_callback_scan_has_bounded_reentry_depth()
{
    constexpr std::size_t task_count = 8;
    struct ShutdownCallback final {
        trigger::TriggerConnectionOwner* owner{};
        std::atomic<int>* max_depth{};
        std::atomic<int>* invoked{};

        void operator()() const noexcept
        {
            const auto current = ++callback_shutdown_test_depth;
            auto observed = max_depth->load();
            while (current > observed
                   && !max_depth->compare_exchange_weak(observed, current)) {
            }
            owner->shutdown();
            --callback_shutdown_test_depth;
            ++*invoked;
        }
    };
    using RegisteredCallback = std::stop_callback<ShutdownCallback>;
    std::vector<std::unique_ptr<RegisteredCallback>> callbacks(task_count);
    std::atomic<trigger::TriggerConnectionOwner*> owner_pointer{};
    std::atomic<std::size_t> registered{};
    std::atomic<int> max_depth{};
    std::atomic<int> invoked{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest& request,
                       trigger::TriggerResponseSink& sink, std::stop_token token) {
            const auto index = static_cast<std::size_t>(request.timestamp() - 600);
            callbacks[index] = std::make_unique<RegisteredCallback>(
                token, ShutdownCallback{
                    owner_pointer.load(), &max_depth, &invoked});
            ++registered;
            while (!token.stop_requested()) std::this_thread::yield();
            static_cast<void>(sink.cancelled());
        }},
    });
    trigger::TriggerExecutor executor(
        dispatcher, {task_count, task_count, task_count, task_count});
    auto session = std::make_shared<protocol::TriggerSession>();
    auto owner = executor.connect(session);
    owner_pointer = &owner;
    for (std::size_t index = 0; index < task_count; ++index) {
        auto item = ingress_item("status", 600 + index);
        check(owner.submit(std::move(*item)),
              "bounded callback-depth fixture must submit");
    }
    check(wait_until([&] { return registered.load() == task_count; }),
          "every callback-depth handler must register before shutdown");
    owner.shutdown();
    check(invoked.load() == static_cast<int>(task_count),
          "owner shutdown must invoke every registered task callback");
    check(max_depth.load() == 1,
          "nested owner shutdown callbacks must not recursively rescan all slots");
    check(owner.stats().active_tasks == 0,
          "owner shutdown callback scan must drain every task slot");
    callbacks.clear();
}

void test_owner_close_callback_scan_has_bounded_reentry_depth()
{
    constexpr std::size_t task_count = 8;
    struct CloseCallback final {
        trigger::TriggerConnectionOwner* owner{};
        std::atomic<int>* max_depth{};
        std::atomic<int>* invoked{};

        void operator()() const noexcept
        {
            const auto current = ++callback_shutdown_test_depth;
            auto observed = max_depth->load();
            while (current > observed
                   && !max_depth->compare_exchange_weak(observed, current)) {
            }
            static_cast<void>(owner->close());
            --callback_shutdown_test_depth;
            ++*invoked;
        }
    };
    using RegisteredCallback = std::stop_callback<CloseCallback>;
    std::vector<std::unique_ptr<RegisteredCallback>> callbacks(task_count);
    std::atomic<trigger::TriggerConnectionOwner*> owner_pointer{};
    std::atomic<std::size_t> registered{};
    std::atomic<int> max_depth{};
    std::atomic<int> invoked{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest& request,
                       trigger::TriggerResponseSink& sink, std::stop_token token) {
            const auto index = static_cast<std::size_t>(request.timestamp() - 700);
            callbacks[index] = std::make_unique<RegisteredCallback>(
                token, CloseCallback{
                    owner_pointer.load(), &max_depth, &invoked});
            ++registered;
            while (!token.stop_requested()) std::this_thread::yield();
            static_cast<void>(sink.cancelled());
        }},
    });
    trigger::TriggerExecutor executor(
        dispatcher, {task_count, task_count, task_count, task_count});
    auto session = std::make_shared<protocol::TriggerSession>();
    auto owner = executor.connect(session);
    owner_pointer = &owner;
    for (std::size_t index = 0; index < task_count; ++index) {
        auto item = ingress_item("status", 700 + index);
        check(owner.submit(std::move(*item)),
              "direct-close callback fixture must submit");
    }
    check(wait_until([&] { return registered.load() == task_count; }),
          "every direct-close callback must register before close");
    static_cast<void>(owner.close());
    check(wait_until([&] {
        return invoked.load() == static_cast<int>(task_count);
    }),
          "direct close must invoke every registered task callback once");
    check(max_depth.load() == 1,
          "callback direct-close must not recursively rescan the same owner");
    check(wait_until([&] { return owner.stats().active_tasks == 0; }),
          "direct-close callback scan must retire every task");
    callbacks.clear();
}

void test_cross_owner_callback_scan_cycle_is_bounded()
{
    struct CycleCallback final {
        trigger::TriggerConnectionOwner* next{};
        std::atomic<int>* max_depth{};
        std::atomic<int>* invoked{};

        void operator()() const noexcept
        {
            const auto current = ++callback_shutdown_test_depth;
            auto observed = max_depth->load();
            while (current > observed
                   && !max_depth->compare_exchange_weak(observed, current)) {
            }
            static_cast<void>(next->close());
            --callback_shutdown_test_depth;
            ++*invoked;
        }
    };
    std::optional<std::stop_callback<CycleCallback>> first_callback;
    std::optional<std::stop_callback<CycleCallback>> second_callback;
    std::atomic<trigger::TriggerConnectionOwner*> first_owner_pointer{};
    std::atomic<trigger::TriggerConnectionOwner*> second_owner_pointer{};
    std::atomic<int> registered{};
    std::atomic<int> max_depth{};
    std::atomic<int> first_invoked{};
    std::atomic<int> second_invoked{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest& request,
                       trigger::TriggerResponseSink& sink, std::stop_token token) {
            if (request.timestamp() == 800) {
                first_callback.emplace(token, CycleCallback{
                    second_owner_pointer.load(), &max_depth, &first_invoked});
            } else {
                second_callback.emplace(token, CycleCallback{
                    first_owner_pointer.load(), &max_depth, &second_invoked});
            }
            ++registered;
            while (!token.stop_requested()) std::this_thread::yield();
            static_cast<void>(sink.cancelled());
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {2, 2, 2, 1});
    auto first_session = std::make_shared<protocol::TriggerSession>();
    auto second_session = std::make_shared<protocol::TriggerSession>();
    auto first_owner = executor.connect(first_session);
    auto second_owner = executor.connect(second_session);
    first_owner_pointer = &first_owner;
    second_owner_pointer = &second_owner;
    auto first_item = ingress_item("status", 800);
    auto second_item = ingress_item("status", 801);
    check(first_owner.submit(std::move(*first_item))
              && second_owner.submit(std::move(*second_item)),
          "cross-owner callback cycle fixtures must submit");
    check(wait_until([&] { return registered.load() == 2; }),
          "both cross-owner callbacks must register before close");
    static_cast<void>(first_owner.close());
    check(first_invoked.load() == 1 && second_invoked.load() == 1
              && max_depth.load() == 2,
          "A-to-B-to-A callback cycle must terminate at the lower active frame");
    check(wait_until([&] {
        return first_owner.stats().active_tasks == 0
            && second_owner.stats().active_tasks == 0;
    }), "cross-owner callback cycle must retire both task slots");
    check(first_session->stats().closed && second_session->stats().closed,
          "cross-owner callback cycle must close both sessions");
    first_callback.reset();
    second_callback.reset();
}

void test_cancel_capability_does_not_cross_timestamp_reuse()
{
    struct BlockingCallback final {
        std::atomic<bool>* entered{};
        std::atomic<bool>* release{};
        std::atomic<bool>* returned{};

        void operator()() const noexcept
        {
            entered->store(true, std::memory_order_release);
            while (!release->load(std::memory_order_acquire))
                std::this_thread::yield();
            returned->store(true, std::memory_order_release);
        }
    };

    protocol::TriggerSessionLimits session_limits;
    session_limits.max_in_flight = 3;
    session_limits.max_queued_batches = 1;
    auto session = std::make_shared<protocol::TriggerSession>(session_limits);
    std::atomic<int> invocation{};
    std::atomic<bool> callback_registered{};
    std::atomic<bool> callback_entered{};
    std::atomic<bool> release_callback{};
    std::atomic<bool> callback_returned{};
    std::optional<std::stop_callback<BlockingCallback>> old_callback;
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token token) {
            if (++invocation == 1) {
                old_callback.emplace(token, BlockingCallback{
                    &callback_entered, &release_callback, &callback_returned});
                callback_registered = true;
                while (!token.stop_requested()) std::this_thread::yield();
                static_cast<void>(sink.cancelled());
            } else {
                static_cast<void>(sink.success());
            }
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    auto owner = executor.connect(session);
    auto old_item = ingress_item("status", 500);
    check(owner.submit(std::move(*old_item)),
          "old timestamp-generation fixture must submit");
    check(wait_until([&] { return callback_registered.load(); }),
          "old task must register its blocking stop callback");
    std::atomic<bool> cancel_returned{};
    std::thread cancel([&] {
        static_cast<void>(owner.request_cancel(500));
        cancel_returned = true;
    });
    check(wait_until([&] { return callback_entered.load(); }),
          "old stop callback must block cancellation after stop is committed");
    check(wait_until([&] {
        return session->stats().queued_batches == 1
            && owner.stats().active_tasks == 0;
    }), "old cancelled terminal must retire before callback returns");
    auto old_lease = session->begin_send();
    check(old_lease
              && old_lease.lease->batch().status()
                  == protocol::ResponseStatus::cancelled,
          "old generation must queue its cancelled terminal");
    if (old_lease) static_cast<void>(owner.complete_send(*old_lease.lease));

    auto blocker = admit(*session, "status", 501);
    auto blocker_batch = terminal("status", 501);
    check(blocker && session->publish(*blocker.receipt, std::move(blocker_batch)),
          "timestamp-reuse fixture must refill outbound capacity");
    auto new_item = ingress_item("status", 500);
    check(owner.submit(std::move(*new_item)),
          "same timestamp must admit a new generation after completed send");
    check(wait_until([&] { return owner.stats().completed == 1; }),
          "new generation success must be pending before old callback returns");
    release_callback = true;
    cancel.join();
    check(callback_returned.load() && cancel_returned.load(),
          "old cancellation must resume after new generation is pending");

    auto blocker_lease = session->begin_send();
    check(blocker_lease, "timestamp-reuse blocker must begin send");
    if (blocker_lease)
        static_cast<void>(owner.complete_send(*blocker_lease.lease));
    auto new_lease = session->begin_send();
    check(new_lease && new_lease.lease->batch().timestamp() == 500
              && new_lease.lease->batch().status()
                  == protocol::ResponseStatus::ok,
          "old cancel capability must not replace reused timestamp success");
    if (new_lease) static_cast<void>(owner.complete_send(*new_lease.lease));
    old_callback.reset();
}

void test_terminal_already_queued_never_requests_task_stop()
{
    struct CountingCallback final {
        std::atomic<int>* invoked{};
        void operator()() const noexcept { ++*invoked; }
    };
    struct DispatchBarrier final {
        std::atomic<bool> entered{};
        std::atomic<bool> release{};
    } barrier;
    const auto hook = +[](void* context) noexcept {
        auto& value = *static_cast<DispatchBarrier*>(context);
        value.entered.store(true, std::memory_order_release);
        while (!value.release.load(std::memory_order_acquire))
            std::this_thread::yield();
    };

    std::atomic<int> callback_invoked{};
    std::optional<std::stop_callback<CountingCallback>> callback;
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token token) {
            callback.emplace(token, CountingCallback{&callback_invoked});
            static_cast<void>(sink.success());
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    trigger::TriggerExecutorTestAccess::set_after_dispatch_hook(
        executor, hook, &barrier);
    auto session = std::make_shared<protocol::TriggerSession>();
    auto owner = executor.connect(session);
    auto item = ingress_item("status", 502);
    check(owner.submit(std::move(*item)),
          "terminal-queued cancellation fixture must submit");
    check(wait_until([&] { return barrier.entered.load(); }),
          "test hook must pause after terminal publication before slot retirement");
    const auto cancelled = owner.request_cancel(502);
    check(cancelled.session_decision
              == protocol::CancelDecision::terminal_already_queued
              && !cancelled.task_found && !cancelled.stop_requested,
          "terminal_already_queued must never acquire or request task stop");
    check(callback_invoked.load() == 0,
          "terminal-already-queued cancellation must not invoke stop callbacks");
    barrier.release = true;
    check(wait_until([&] { return owner.stats().active_tasks == 0; }),
          "paused terminal task must retire after barrier release");
    trigger::TriggerExecutorTestAccess::set_after_dispatch_hook(
        executor, nullptr, nullptr);
    auto lease = session->begin_send();
    check(lease && lease.lease->batch().status() == protocol::ResponseStatus::ok,
          "terminal-already-queued result must remain successful");
    if (lease) static_cast<void>(owner.complete_send(*lease.lease));
    callback.reset();
}

void test_whole_service_teardown_from_worker_keeps_shared_session_alive()
{
    std::optional<trigger::TriggerConnectionOwner> owner;
    std::unique_ptr<trigger::TriggerExecutor> executor;
    std::atomic<bool> objects_destroyed{};
    std::atomic<bool> handler_finished{};
    std::atomic<bool> submit_returned{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            while (!submit_returned.load(std::memory_order_acquire))
                std::this_thread::yield();
            owner.reset();
            executor.reset();
            objects_destroyed = true;
            static_cast<void>(sink.cancelled());
            handler_finished = true;
        }},
    });
    auto session = std::make_shared<protocol::TriggerSession>();
    executor = std::make_unique<trigger::TriggerExecutor>(
        dispatcher, trigger::TriggerExecutorLimits{1, 1, 1, 1});
    owner.emplace(executor->connect(session));
    auto item = ingress_item("status", 95);
    const auto submitted = owner->submit(std::move(*item));
    submit_returned.store(true, std::memory_order_release);
    check(submitted, "whole-service teardown task must submit");
    check(wait_until([&] { return objects_destroyed.load(); }),
          "worker must safely destroy owner and executor facades");
    check(wait_until([&] { return handler_finished.load(); }),
          "worker-held Impl and shared session must survive through callback exit");
    check(session->stats().closed,
          "worker teardown must close the shared session before final release");
}

void test_concurrent_connections_respect_global_bound()
{
    std::atomic<int> invoked{};
    auto dispatcher = dispatcher_with({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            ++invoked;
            static_cast<void>(sink.success());
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {4, 16, 16, 2});
    std::vector<std::unique_ptr<protocol::TriggerSession>> sessions;
    std::vector<trigger::TriggerConnectionOwner> connections;
    for (int index = 0; index < 8; ++index) {
        sessions.push_back(std::make_unique<protocol::TriggerSession>());
        connections.push_back(executor.connect(borrowed_session(*sessions.back())));
    }
    std::vector<std::thread> submitters;
    for (std::size_t index = 0; index < connections.size(); ++index) {
        submitters.emplace_back([&, index] {
            auto item = ingress_item("status", 100 + index);
            check(connections[index].submit(std::move(*item)),
                  "independent concurrent submit must fit global capacity");
        });
    }
    for (auto& submitter : submitters) submitter.join();
    check(wait_until([&] { return executor.stats().active_tasks == 0; }),
          "concurrent tasks must all retire");
    check(invoked.load() == 8, "every accepted concurrent handler must execute once");
    for (const auto& session_item : sessions)
        check(confirm_one(*session_item), "each connection terminal must drain");
}

void test_executor_output_ready_observer_is_immediate_and_cancellable()
{
    auto dispatcher = dispatcher_with({
        {"status", [](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& sink, std::stop_token) {
            static_cast<void>(sink.success());
        }},
    });
    trigger::TriggerExecutor executor(dispatcher, {1, 4, 4, 4});
    protocol::TriggerSession session;
    auto connection = executor.connect(borrowed_session(session));
    auto observer = std::make_shared<ExecutorReadyObserver>();
    observer->connection = &connection;
    auto subscription = connection.observe_output_ready(observer);
    check(subscription, "connection owner must expose the session ready observer seam");

    auto first = ingress_item("status", 901);
    check(connection.submit(std::move(*first)), "ready observer task must submit");
    check(observer->wait_for_call(1) && session.stats().queued_batches == 1,
          "worker publication must wake egress immediately without a heartbeat poll");
    auto lease = session.begin_send();
    check(lease && connection.complete_send(*lease.lease),
          "ready observer response must drain through connection send ownership");

    subscription.reset();
    auto second = ingress_item("status", 902);
    check(connection.submit(std::move(*second)), "post-unsubscribe task must submit");
    check(wait_until([&] { return session.stats().queued_batches == 1; })
              && observer->calls.load() == 1,
          "unsubscribed executor observer must receive no later edge");
    lease = session.begin_send();
    check(lease && connection.complete_send(*lease.lease),
          "post-unsubscribe response must remain drainable");

    auto closing_subscription = connection.observe_output_ready(observer);
    static_cast<void>(connection.close());
    check(!closing_subscription,
          "connection close must cancel the delegated observer generation");
}

}  // namespace

int main()
{
    check(trigger_executor_production_header_size()
              == sizeof(trigger::TriggerExecutor),
          "test-hook and production header views must agree in one link image");
    check(trigger::trigger_submit_error_name(
              trigger::TriggerSubmitError::global_task_limit)
              == "global_task_limit",
          "submit error names must be stable");
    test_reserve_before_admit_and_queue_saturation();
    test_queued_cancel_skips_business_handler();
    test_running_cancel_exception_and_terminal_after_return();
    test_prefix_identity_single_and_stream_rules();
    test_pending_terminal_is_owned_and_retried_without_rerun();
    test_ignored_progress_backpressure_cannot_orphan_correlation();
    test_receipt_owner_generation_and_rollback_visibility();
    test_shutdown_and_executor_before_owner_destruction();
    test_worker_initiated_shutdown_has_no_self_join();
    test_pending_cancel_and_fatal_submission_linearization();
    test_fail_send_handoff_stops_running_owner();
    test_input_and_pending_byte_budgets();
    test_owner_self_shutdown_and_concurrent_lifecycle();
    test_connection_registry_shutdown_and_connect_race();
    test_external_first_and_cross_owner_worker_shutdown();
    test_two_executor_workers_cross_shutdown_without_join_cycle();
    test_concurrent_external_shutdown_waits_for_registry_close();
    test_shutdown_race_cannot_publish_completed_after_registry_scan();
    test_running_to_completed_cancel_window_uses_cancelled_fallback();
    test_irrevocable_terminal_resists_cancel_and_shutdown_rewrite();
    test_stop_callback_reenters_stats_cancel_and_owner_shutdown();
    test_shutdown_stop_callback_reenters_executor_shutdown();
    test_owner_shutdown_callback_scan_has_bounded_reentry_depth();
    test_owner_close_callback_scan_has_bounded_reentry_depth();
    test_cross_owner_callback_scan_cycle_is_bounded();
    test_cancel_capability_does_not_cross_timestamp_reuse();
    test_terminal_already_queued_never_requests_task_stop();
    test_whole_service_teardown_from_worker_keeps_shared_session_alive();
    test_concurrent_connections_respect_global_bound();
    test_executor_output_ready_observer_is_immediate_and_cancellable();

    if (failures != 0) {
        std::cerr << failures << " trigger executor test(s) failed\n";
        return 1;
    }
    std::cout << "trigger executor tests passed\n";
    return 0;
}
