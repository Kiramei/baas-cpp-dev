#include "script/runtime/LogHost.h"
#include "script/runtime/SynchronousEvaluator.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace runtime = baas::script::runtime;

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <class Function>
void expect_config_error(
    const runtime::LogHostConfigErrorCode code,
    Function&& function,
    const std::string_view message)
{
    try {
        std::forward<Function>(function)();
        check(false, message);
    } catch (const runtime::LogHostConfigError& error) {
        check(error.code() == code, message);
    } catch (...) {
        check(false, message);
    }
}

class RecordingSink final : public runtime::StructuredLogSink {
public:
    explicit RecordingSink(const bool blocked = false) : blocked_(blocked) {}

    void write(const runtime::StructuredLogEvent& event) override
    {
        std::unique_lock lock(mutex_);
        entered_ = true;
        entered_changed_.notify_all();
        release_changed_.wait(lock, [&] { return !blocked_; });
        events_.push_back(event);
    }

    bool wait_until_entered()
    {
        std::unique_lock lock(mutex_);
        return entered_changed_.wait_for(lock, 2s, [&] { return entered_; });
    }

    void release()
    {
        {
            const std::lock_guard lock(mutex_);
            blocked_ = false;
        }
        release_changed_.notify_all();
    }

    [[nodiscard]] std::vector<runtime::StructuredLogEvent> events() const
    {
        const std::lock_guard lock(mutex_);
        return events_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable entered_changed_;
    std::condition_variable release_changed_;
    bool blocked_{};
    bool entered_{};
    std::vector<runtime::StructuredLogEvent> events_;
};

class ThrowingSink final : public runtime::StructuredLogSink {
public:
    void write(const runtime::StructuredLogEvent&) override
    {
        throw std::runtime_error("sink failure must stay behind worker boundary");
    }
};

class OwnerReleaseSink final : public runtime::StructuredLogSink {
public:
    void set_release(std::function<void()> release)
    {
        release_ = std::move(release);
    }

    void write(const runtime::StructuredLogEvent&) override
    {
        {
            std::unique_lock lock(mutex_);
            entered_ = true;
            changed_.notify_all();
            changed_.wait(lock, [&] { return proceed_; });
        }
        release_();
        {
            const std::lock_guard lock(mutex_);
            completed_ = true;
        }
        changed_.notify_all();
    }

    bool wait_until_entered()
    {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, 2s, [&] { return entered_; });
    }

    void proceed()
    {
        {
            const std::lock_guard lock(mutex_);
            proceed_ = true;
        }
        changed_.notify_all();
    }

    bool wait_until_completed()
    {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, 2s, [&] { return completed_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::function<void()> release_;
    bool entered_{};
    bool proceed_{};
    bool completed_{};
};

runtime::HostArguments arguments(
    std::string level,
    std::string message,
    std::optional<runtime::JsonObject> fields = std::nullopt)
{
    runtime::HostArguments result(3);
    result[0] = runtime::HostValue(std::move(level));
    result[1] = runtime::HostValue(std::move(message));
    if (fields)
        result[2] = runtime::HostValue(runtime::JsonValue(std::move(*fields)));
    return result;
}

runtime::HostResult invoke(
    const runtime::SynchronousNativeBinding& binding,
    const runtime::HostArguments& values)
{
    return runtime::invoke_host_callback(
        binding,
        {"baas/log", "emit", "host.log.emit.v1", {1, 0}, 1},
        values,
        {});
}

runtime::SynchronousHostOptions evaluator_options(
    std::shared_ptr<const runtime::SynchronousNativeBindingSet> bindings)
{
    runtime::HostExportDescriptor emit{
        "emit", "host.log.emit.v1", "log.emit"};
    runtime::SynchronousHostOptions options;
    options.metadata = std::make_shared<const runtime::HostModuleRegistry>(
        std::vector<runtime::HostModuleDescriptor>{
            {"baas/log", {1, 0}, {emit}},
        });
    options.bindings = std::move(bindings);
    options.permissions.declared_modules.push_back({"baas/log", 1, 0});
    options.permissions.declared_capabilities.push_back("log.emit");
    options.permissions.policy_capabilities.push_back("log.emit");
    options.permissions.platform_capabilities.push_back("log.emit");
    options.permissions.task_capabilities.push_back("log.emit");
    return options;
}

void test_metadata_redaction_order_and_serialization()
{
    auto sink = std::make_shared<RecordingSink>();
    auto host = std::make_shared<runtime::QueuedLogHost>(
        sink,
        runtime::LogHostIdentity{"task-secret", "session-token", "config"},
        std::vector<std::string>{"secret", "token"});
    const auto binding = runtime::make_queued_log_binding(host);
    auto result = invoke(
        binding,
        arguments(
            "warn", "credential=secret",
            runtime::JsonObject{
                {"secret-key", runtime::JsonValue("token-value")},
                {"nested", runtime::JsonValue(runtime::JsonArray{
                    runtime::JsonValue("safe"), runtime::JsonValue("secret")})},
            }));
    check(result.ok(), "valid structured log event must enqueue");
    host->shutdown();

    const auto events = sink->events();
    check(events.size() == 1, "drain shutdown must deliver the accepted event");
    if (events.empty()) return;
    const auto& event = events.front();
    check(event.level == runtime::StructuredLogLevel::Warning,
          "warn alias must normalize to warning");
    check(event.message == "credential=[REDACTED]",
          "message secrets must be redacted before enqueue");
    check(event.identity.task_id == "task-[REDACTED]" &&
              event.identity.session_id == "session-[REDACTED]" &&
              event.identity.config_name == "config",
          "host identity must be attached and redacted independently of script fields");
    check(event.fields && (*event.fields)[0].first == "[REDACTED]-key" &&
              std::get<std::string>((*event.fields)[0].second.value()) ==
                  "[REDACTED]-value",
          "field keys and string values must be redacted recursively");

    const auto serialized = runtime::serialize_structured_log_event(event, 4'096);
    check(serialized.has_value(), "bounded structured event must serialize");
    check(serialized && *serialized ==
        "{\"level\":\"warning\",\"message\":\"credential=[REDACTED]\","
        "\"task_id\":\"task-[REDACTED]\",\"session_id\":\"session-[REDACTED]\","
        "\"config_name\":\"config\",\"fields\":{\"[REDACTED]-key\":"
        "\"[REDACTED]-value\",\"nested\":[\"safe\",\"[REDACTED]\"]}}",
        "legacy sink serialization must be fixed-key and insertion ordered");
    check(!runtime::serialize_structured_log_event(event, 8),
          "serialization must reject an exact bounded-output overflow");
}

void test_invalid_level_backpressure_and_shutdown()
{
    auto sink = std::make_shared<RecordingSink>(true);
    runtime::QueuedLogHostLimits limits;
    limits.queue_capacity = 1;
    auto host = std::make_shared<runtime::QueuedLogHost>(
        sink, runtime::LogHostIdentity{}, std::vector<std::string>{}, limits);
    const auto binding = runtime::make_queued_log_binding(host);

    const auto invalid = invoke(binding, arguments("verbose", "never"));
    check(!invalid.ok() && invalid.has_error() &&
              invalid.error().code == runtime::HostErrorCode::InvalidArgument &&
              invalid.error().effect_state == runtime::HostEffectState::NotStarted,
          "unknown levels must fail before enqueue");

    check(invoke(binding, arguments("info", "running")).ok(),
          "first event must enter the worker");
    check(sink->wait_until_entered(), "blocking sink must observe the running event");
    check(invoke(binding, arguments("info", "queued")).ok(),
          "one event must fit in the bounded queue");
    const auto full = invoke(binding, arguments("info", "rejected"));
    check(!full.ok() && full.has_error() &&
              full.error().code == runtime::HostErrorCode::Backpressure &&
              full.error().retryable &&
              full.error().effect_state == runtime::HostEffectState::NotStarted,
          "a full queue must return retryable HOST016 without a partial effect");

    sink->release();
    host->shutdown();
    const auto stats = host->stats();
    check(stats.accepted == 2 && stats.delivered == 2 &&
              stats.backpressure_rejections == 1,
          "queue accounting must distinguish accepted, delivered, and rejected events");
    const auto stopped = invoke(binding, arguments("info", "stopped"));
    check(!stopped.ok() && stopped.has_error() &&
              stopped.error().code == runtime::HostErrorCode::Unavailable,
          "shutdown LogHost must reject later submissions as unavailable");
    check(host->stats().unavailable_rejections == 1,
          "shutdown rejection must be observable without reopening the queue");
}

void test_evaluator_to_production_queue_vertical_slice()
{
    auto sink = std::make_shared<RecordingSink>();
    auto host = std::make_shared<runtime::QueuedLogHost>(
        sink,
        runtime::LogHostIdentity{"task-7", "session-3", "primary"},
        std::vector<std::string>{"credential"});
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            runtime::make_queued_log_binding(host)});
    runtime::SynchronousEvaluator evaluator(
        {{"main",
          "import \"baas/log\" as log;\n"
          "log.emit(\"error\", \"credential failed\", {\"attempt\": 2});\n"}},
        evaluator_options(std::move(bindings)));
    static_cast<void>(evaluator.execute("main"));
    host->shutdown();

    const auto events = sink->events();
    check(events.size() == 1 &&
              events[0].level == runtime::StructuredLogLevel::Error &&
              events[0].message == "[REDACTED] failed" &&
              events[0].identity.task_id == "task-7" &&
              events[0].fields && events[0].fields->size() == 1,
          "evaluator must cross the production queued LogHost with host-owned identity");
    check(evaluator.stats().host_calls == 1 && host->stats().delivered == 1,
          "evaluator and production queue must account for the same admitted call");
}

void test_sink_failures_and_configuration_limits()
{
    auto sink = std::make_shared<ThrowingSink>();
    auto host = std::make_shared<runtime::QueuedLogHost>(
        sink, runtime::LogHostIdentity{});
    const auto binding = runtime::make_queued_log_binding(host);
    check(invoke(binding, arguments("error", "accepted before sink failure")).ok(),
          "enqueue success is the Host effect boundary");
    host->shutdown();
    const auto stats = host->stats();
    check(stats.accepted == 1 && stats.delivered == 0 && stats.sink_failures == 1,
          "asynchronous sink exceptions must be contained and counted");

    auto collision_sink = std::make_shared<RecordingSink>();
    auto collision_host = std::make_shared<runtime::QueuedLogHost>(
        collision_sink, runtime::LogHostIdentity{},
        std::vector<std::string>{"secret"});
    const auto collision_binding = runtime::make_queued_log_binding(collision_host);
    const auto collision = invoke(
        collision_binding,
        arguments("info", "collision", runtime::JsonObject{
            {"secret", runtime::JsonValue(1LL)},
            {"[REDACTED]", runtime::JsonValue(2LL)},
        }));
    check(!collision.ok() && collision.has_error() &&
              collision.error().code == runtime::HostErrorCode::InvalidArgument,
          "redaction must reject field-key collisions instead of publishing duplicate JSON keys");
    collision_host->shutdown();
    check(collision_sink->events().empty(),
          "redaction collision must fail before the enqueue effect boundary");

    auto work_limits = runtime::QueuedLogHostLimits{};
    work_limits.max_redaction_work = 2;
    auto work_host = std::make_shared<runtime::QueuedLogHost>(
        std::make_shared<RecordingSink>(), runtime::LogHostIdentity{},
        std::vector<std::string>{"absent"}, work_limits);
    const auto work_binding = runtime::make_queued_log_binding(work_host);
    const auto work_limited = invoke(work_binding, arguments("info", "long scan"));
    check(!work_limited.ok() && work_limited.has_error() &&
              work_limited.error().code == runtime::HostErrorCode::BudgetExceeded,
          "secret scans must consume the configured redaction work budget even without matches");
    work_host->shutdown();

    runtime::StructuredLogEvent invalid_level;
    invalid_level.level = static_cast<runtime::StructuredLogLevel>(255);
    check(!runtime::serialize_structured_log_event(invalid_level, 1'024),
          "serializer must reject an invalid native level discriminator");

    expect_config_error(runtime::LogHostConfigErrorCode::MissingSink,
        [] {
            runtime::QueuedLogHost rejected(
                nullptr, runtime::LogHostIdentity{});
        }, "a null production sink must be rejected");
    expect_config_error(runtime::LogHostConfigErrorCode::InvalidLimits,
        [] {
            auto limits = runtime::QueuedLogHostLimits{};
            limits.queue_capacity = 0;
            runtime::QueuedLogHost rejected(
                std::make_shared<RecordingSink>(), runtime::LogHostIdentity{}, {}, limits);
        }, "zero queue capacity must be rejected");
    expect_config_error(runtime::LogHostConfigErrorCode::InvalidSecret,
        [] {
            runtime::QueuedLogHost rejected(
                std::make_shared<RecordingSink>(), runtime::LogHostIdentity{}, {""});
        }, "empty redaction secrets must be rejected");
    expect_config_error(runtime::LogHostConfigErrorCode::InvalidIdentity,
        [] {
            runtime::QueuedLogHost rejected(
                std::make_shared<RecordingSink>(),
                runtime::LogHostIdentity{std::string("\xC0\xAF", 2), {}, {}});
        }, "invalid UTF-8 host identity must be rejected");
}

void test_last_binding_release_from_worker_retains_task_state()
{
    auto sink = std::make_shared<OwnerReleaseSink>();
    auto host = std::make_shared<runtime::QueuedLogHost>(
        sink, runtime::LogHostIdentity{});
    std::optional<runtime::SynchronousNativeBinding> binding(
        runtime::make_queued_log_binding(host));
    sink->set_release([&] { binding.reset(); });

    check(invoke(*binding, arguments("info", "worker-owned teardown")).ok(),
          "worker teardown fixture event must enqueue");
    check(sink->wait_until_entered(),
          "worker teardown fixture must reach the sink before owner release");
    host.reset();
    sink->proceed();
    check(sink->wait_until_completed(),
          "destroying the last binding on its worker must not self-join or deadlock");

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (sink.use_count() != 1 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    check(!binding && sink.use_count() == 1,
          "worker task must retain diagnostics state and release all task ownership safely");
}

}  // namespace

int main()
{
    test_metadata_redaction_order_and_serialization();
    test_invalid_level_backpressure_and_shutdown();
    test_evaluator_to_production_queue_vertical_slice();
    test_sink_failures_and_configuration_limits();
    test_last_binding_release_from_worker_retains_task_state();
    if (failures != 0) {
        std::cerr << failures << " LogHost test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "queued production LogHost tests passed\n";
    return EXIT_SUCCESS;
}
