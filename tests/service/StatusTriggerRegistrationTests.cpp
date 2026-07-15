#include "service/app/StatusTriggerRegistration.h"
#include "service/trigger/TriggerExecutor.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
namespace app = baas::service::app;
namespace trigger = baas::service::trigger;
namespace protocol = baas::service::protocol::trigger;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
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
    const std::string_view command, const protocol::Timestamp timestamp)
{
    std::string json = R"({"type":"command","command":")";
    json.append(command);
    json.append(R"(","timestamp":)");
    json.append(std::to_string(timestamp));
    json.append(R"(,"payload":{}})");
    protocol::TriggerIngress ingress;
    if (!ingress.receive_json_frame(json)) return std::nullopt;
    return ingress.take_ready();
}

[[nodiscard]] std::shared_ptr<protocol::TriggerSession> borrowed_session(
    protocol::TriggerSession& session)
{
    return {&session, [](protocol::TriggerSession*) noexcept {}};
}

[[nodiscard]] std::shared_ptr<const trigger::TriggerDispatcher> dispatcher_with(
    trigger::TriggerHandlerRegistration registration,
    const trigger::TriggerDispatchLimits limits = {})
{
    std::vector<trigger::TriggerHandlerRegistration> registrations;
    registrations.emplace_back(std::move(registration));
    auto built = trigger::TriggerDispatcher::create(
        std::move(registrations), limits);
    if (!built) throw std::runtime_error("status dispatcher build failed");
    return std::make_shared<const trigger::TriggerDispatcher>(
        std::move(*built.dispatcher));
}

[[nodiscard]] trigger::TriggerHandlerRegistration registration(
    app::StatusSourceCallback callback,
    const app::StatusTriggerLimits limits = {})
{
    auto made = app::make_status_trigger_registration(
        std::move(callback), limits);
    if (!made) throw std::runtime_error("status registration build failed");
    return std::move(*made.registration);
}

struct ObservedBatch {
    protocol::ResponseStatus status{protocol::ResponseStatus::error};
    std::string json;
    bool terminal{};
};

[[nodiscard]] ObservedBatch execute_one(
    app::StatusSourceCallback callback,
    const app::StatusTriggerLimits status_limits = {},
    const trigger::TriggerDispatchLimits dispatch_limits = {})
{
    auto dispatcher = dispatcher_with(
        registration(std::move(callback), status_limits), dispatch_limits);
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    protocol::TriggerSession session;
    auto connection = executor.connect(borrowed_session(session));
    auto item = ingress_item("status", 7);
    if (!item || !connection.submit(std::move(*item))) {
        throw std::runtime_error("status submit failed");
    }
    if (!wait_until([&] { return session.stats().queued_batches == 1; })) {
        throw std::runtime_error("status response timeout");
    }
    auto begun = session.begin_send();
    if (!begun) throw std::runtime_error("status response lease failed");
    ObservedBatch observed{
        begun.lease->batch().status(),
        begun.lease->batch().json(),
        begun.lease->batch().terminal(),
    };
    if (!connection.complete_send(*begun.lease)) {
        throw std::runtime_error("status response completion failed");
    }
    return observed;
}

[[nodiscard]] protocol::OutboundBatch blocker_batch()
{
    protocol::CommandResponse response;
    response.command = "status";
    response.timestamp = 1;
    response.data_json = "{}";
    auto encoded = protocol::encode_command_response(std::move(response));
    if (!encoded) throw std::runtime_error("blocker encode failed");
    return std::move(encoded.batch);
}

class StaticStatusSource final : public app::StatusSource {
public:
    explicit StaticStatusSource(std::string value) : value_(std::move(value)) {}

    [[nodiscard]] app::StatusSourceResult current_status(
        std::stop_token) override
    {
        ++calls;
        return {value_, app::StatusSourceError::none};
    }

    std::atomic<unsigned int> calls{};

private:
    std::string value_;
};

void test_factory_is_exact_and_fail_closed()
{
    auto missing = app::make_status_trigger_registration(
        std::shared_ptr<app::StatusSource>{});
    check(!missing
              && missing.error == app::StatusTriggerRegistrationError::missing_source,
          "factory must reject a missing status source");
    auto empty = app::make_status_trigger_registration(app::StatusSourceCallback{});
    check(!empty
              && empty.error == app::StatusTriggerRegistrationError::empty_callback,
          "factory must reject an empty callback");

    app::StatusTriggerLimits invalid;
    invalid.max_json_bytes = 1;
    check(app::make_status_trigger_registration(
              [](std::stop_token) { return app::StatusSourceResult{"{}"}; }, invalid)
              .error == app::StatusTriggerRegistrationError::invalid_limits,
          "factory must reject a byte limit that cannot contain an object");
    invalid = {};
    invalid.max_json_depth = app::status_trigger_hard_max_json_depth + 1;
    check(app::make_status_trigger_registration(
              [](std::stop_token) { return app::StatusSourceResult{"{}"}; }, invalid)
              .error == app::StatusTriggerRegistrationError::invalid_limits,
          "factory must enforce the hard recursive depth ceiling");

    auto source = std::make_shared<StaticStatusSource>("{}");
    auto made = app::make_status_trigger_registration(source);
    check(made && made.registration->descriptor_name == "status",
          "factory must return exactly the canonical status descriptor");
    auto dispatcher = dispatcher_with(std::move(*made.registration));
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    protocol::TriggerSession session;
    auto connection = executor.connect(borrowed_session(session));
    auto unsupported = ingress_item("copy_config", 1);
    check(unsupported
              && connection.submit(std::move(*unsupported)).error
                  == trigger::TriggerSubmitError::unregistered_command,
          "the status slice must not register another catalog command");
    check(source->calls.load() == 0,
          "an unregistered command must not reach the status source");
}

void test_exact_python_status_data_envelope()
{
    const std::string data =
        R"({"default_config":{"config_id":"default_config","running":false,"is_flag_run":false,"button":null,"current_task":null,"waiting_tasks":[],"exit_code":null,"run_mode":null,"timestamp":1700000000123},"1700000000999":{"running":true,"button":1.25}})";
    auto observed = execute_one([&](std::stop_token stop) {
        check(!stop.stop_requested(), "normal status source call must not be cancelled");
        return app::StatusSourceResult{data};
    });
    const std::string expected =
        R"({"type":"command_response","command":"status","status":"ok","data":)"
        + data + R"(,"timestamp":7})";
    check(observed.status == protocol::ResponseStatus::ok && observed.terminal
              && observed.json == expected,
          "status success must preserve current_status as the exact data object");

    auto empty = execute_one([](std::stop_token) {
        return app::StatusSourceResult{"{}"};
    });
    check(empty.json
              == R"({"type":"command_response","command":"status","status":"ok","data":{},"timestamp":7})",
          "an uninitialized Python runtime maps to an empty status object");
}

void test_invalid_capacity_exception_and_source_errors()
{
    for (const std::string value : {
             "[]", "null", R"({"broken":})", R"({"x":1,"x":2})"}) {
        const auto observed = execute_one([value](std::stop_token) {
            return app::StatusSourceResult{value};
        });
        check(observed.status == protocol::ResponseStatus::error
                  && observed.json.find("status_invalid_json") != std::string::npos,
              "non-object, malformed, or duplicate status JSON must fail closed");
    }

    app::StatusTriggerLimits bytes;
    bytes.max_json_bytes = 32;
    auto oversized = execute_one([](std::stop_token) {
        return app::StatusSourceResult{
            R"({"value":"012345678901234567890123456789"})"};
    }, bytes);
    check(oversized.json.find("status_json_capacity") != std::string::npos,
          "oversized status JSON must report capacity");

    app::StatusTriggerLimits depth;
    depth.max_json_depth = 2;
    auto too_deep = execute_one([](std::stop_token) {
        return app::StatusSourceResult{R"({"a":{"b":{}}})"};
    }, depth);
    check(too_deep.json.find("status_json_capacity") != std::string::npos,
          "status JSON depth must be bounded before publication");

    app::StatusTriggerLimits nodes;
    nodes.max_json_nodes = 2;
    auto too_many_nodes = execute_one([](std::stop_token) {
        return app::StatusSourceResult{R"({"a":1,"b":2})"};
    }, nodes);
    check(too_many_nodes.json.find("status_json_capacity") != std::string::npos,
          "status JSON nodes must be bounded before publication");

    auto throwing = execute_one([](std::stop_token) -> app::StatusSourceResult {
        throw std::runtime_error("secret runtime detail");
    });
    check(throwing.json.find("status_source_exception") != std::string::npos
              && throwing.json.find("secret runtime detail") == std::string::npos,
          "source exceptions must become stable non-sensitive errors");

    for (const auto [error, message] : std::array{
             std::pair{app::StatusSourceError::capacity,
                       std::string_view{"status_source_capacity"}},
             std::pair{app::StatusSourceError::unavailable,
                       std::string_view{"status_source_unavailable"}},
         }) {
        auto observed = execute_one([error](std::stop_token) {
            return app::StatusSourceResult{{}, error};
        });
        check(observed.status == protocol::ResponseStatus::error
                  && observed.json.find(message) != std::string::npos,
              "source failure categories must retain stable wire errors");
    }
}

void test_cancellation_wins_before_terminal_publication()
{
    std::atomic<bool> entered{};
    std::atomic<bool> saw_stop{};
    auto dispatcher = dispatcher_with(registration([&](std::stop_token stop) {
        entered.store(true, std::memory_order_release);
        while (!stop.stop_requested()) std::this_thread::yield();
        saw_stop.store(true, std::memory_order_release);
        return app::StatusSourceResult{R"({"late":true})"};
    }));
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    protocol::TriggerSession session;
    auto connection = executor.connect(borrowed_session(session));
    auto item = ingress_item("status", 9);
    check(item && connection.submit(std::move(*item)),
          "running cancellation fixture must submit");
    check(wait_until([&] { return entered.load(std::memory_order_acquire); }),
          "status source must begin before cancellation");
    const auto cancelled = connection.request_cancel(9);
    check(cancelled.task_found && cancelled.stop_requested,
          "running status cancellation must reach the source stop token");
    check(wait_until([&] { return session.stats().queued_batches == 1; }),
          "cancelled status must publish one correlated terminal");
    auto begun = session.begin_send();
    check(begun && saw_stop.load(std::memory_order_acquire)
              && begun.lease->batch().status() == protocol::ResponseStatus::cancelled
              && begun.lease->batch().json().find("late") == std::string::npos,
          "cancellation must replace a late source snapshot");
    if (begun) static_cast<void>(connection.complete_send(*begun.lease));

    auto source_cancelled = execute_one([](std::stop_token) {
        return app::StatusSourceResult{{}, app::StatusSourceError::cancelled};
    });
    check(source_cancelled.status == protocol::ResponseStatus::cancelled,
          "a cooperative source cancellation must publish cancelled terminal state");
}

void test_concurrent_thread_safe_source_calls()
{
    std::atomic<unsigned int> calls{};
    std::atomic<unsigned int> active{};
    std::atomic<unsigned int> peak{};
    auto made = app::make_status_trigger_registration(
        [&](std::stop_token stop) {
            if (stop.stop_requested()) {
                return app::StatusSourceResult{{}, app::StatusSourceError::cancelled};
            }
            const auto now = active.fetch_add(1) + 1;
            auto previous = peak.load();
            while (previous < now && !peak.compare_exchange_weak(previous, now)) {}
            calls.fetch_add(1);
            std::this_thread::sleep_for(3ms);
            active.fetch_sub(1);
            return app::StatusSourceResult{"{}"};
        });
    auto dispatcher = dispatcher_with(std::move(*made.registration));
    trigger::TriggerExecutor executor(dispatcher, {4, 32, 32, 32});
    protocol::TriggerSession session;
    auto connection = executor.connect(borrowed_session(session), 32);
    for (protocol::Timestamp timestamp = 100; timestamp < 120; ++timestamp) {
        auto item = ingress_item("status", timestamp);
        check(item && connection.submit(std::move(*item)),
              "concurrent status request must submit within bounds");
    }
    check(wait_until([&] { return calls.load() == 20
                                  && executor.stats().active_tasks == 0; }),
          "all concurrent status source calls must finish");
    check(peak.load() > 1,
          "TriggerExecutor must be able to invoke the documented thread-safe source concurrently");
    for (int index = 0; index < 20; ++index) {
        auto begun = session.begin_send();
        check(static_cast<bool>(begun), "every concurrent status call must have one terminal");
        if (begun) static_cast<void>(connection.complete_send(*begun.lease));
    }
    check(session.stats().active_correlations == 0,
          "concurrent status terminals must release every correlation");
}

void test_backpressure_retries_without_source_rerun()
{
    protocol::TriggerSessionLimits session_limits;
    session_limits.max_queued_batches = 1;
    protocol::TriggerSession session(session_limits);
    protocol::CommandAdmission admission;
    admission.command = "status";
    admission.timestamp = 1;
    admission.payload_bytes = 2;
    auto blocker = session.admit(std::move(admission));
    check(blocker && session.publish(*blocker.receipt, blocker_batch()),
          "backpressure blocker must fill the response queue");

    std::atomic<unsigned int> calls{};
    auto dispatcher = dispatcher_with(registration([&](std::stop_token) {
        calls.fetch_add(1);
        return app::StatusSourceResult{R"({"default_config":{"running":false}})"};
    }));
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    auto connection = executor.connect(borrowed_session(session));
    auto item = ingress_item("status", 2);
    check(item && connection.submit(std::move(*item)),
          "backpressured status request must submit");
    check(wait_until([&] { return connection.stats().completed == 1; }),
          "completed status terminal must be retained under backpressure");
    check(calls.load() == 1 && executor.stats().active_tasks == 1,
          "pending terminal must retain ownership without rerunning the source");

    auto first = session.begin_send();
    check(static_cast<bool>(first), "blocker send lease must begin");
    if (first) {
        const auto completed = connection.complete_send(*first.lease);
        check(completed && completed.retry.attempted == 1
                  && completed.retry.published == 1,
              "capacity release must retry the retained status terminal");
    }
    check(calls.load() == 1 && connection.stats().active_tasks == 0,
          "status source must run exactly once across response retry");
    auto status = session.begin_send();
    check(status && status.lease->batch().json().find("default_config")
                          != std::string::npos,
          "retried terminal must preserve the original status snapshot");
    if (status) static_cast<void>(connection.complete_send(*status.lease));
}

void test_success_encode_rejection_recovers_to_error_terminal()
{
    trigger::TriggerDispatchLimits dispatch;
    dispatch.response_envelope.max_output_json_bytes = 128;
    std::string padding(80, 'x');
    auto observed = execute_one([padding](std::stop_token) {
        return app::StatusSourceResult{
            std::string{R"({"padding":")"} + padding + R"("})"};
    }, {}, dispatch);
    check(observed.status == protocol::ResponseStatus::error
              && observed.json.find("status_response_rejected") != std::string::npos,
          "a rejected success encoding must recover to one bounded error terminal");
}

void test_stable_error_names()
{
    constexpr std::array source_errors{
        app::StatusSourceError::none,
        app::StatusSourceError::cancelled,
        app::StatusSourceError::capacity,
        app::StatusSourceError::unavailable,
    };
    for (const auto error : source_errors) {
        check(app::status_source_error_name(error) != "unknown",
              "every source error must have a stable name");
    }
    constexpr std::array registration_errors{
        app::StatusTriggerRegistrationError::none,
        app::StatusTriggerRegistrationError::missing_source,
        app::StatusTriggerRegistrationError::empty_callback,
        app::StatusTriggerRegistrationError::invalid_limits,
        app::StatusTriggerRegistrationError::resource_exhausted,
    };
    for (const auto error : registration_errors) {
        check(app::status_trigger_registration_error_name(error) != "unknown",
              "every registration error must have a stable name");
    }
}

}  // namespace

int main()
{
    test_factory_is_exact_and_fail_closed();
    test_exact_python_status_data_envelope();
    test_invalid_capacity_exception_and_source_errors();
    test_cancellation_wins_before_terminal_publication();
    test_concurrent_thread_safe_source_calls();
    test_backpressure_retries_without_source_rerun();
    test_success_encode_rejection_recovers_to_error_terminal();
    test_stable_error_names();
    if (failures != 0) {
        std::cerr << failures << " status trigger test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Status trigger registration tests passed\n";
    return EXIT_SUCCESS;
}
