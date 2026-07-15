#include "service/channels/ProviderHandler.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace channels = baas::service::channels;
namespace auth = baas::service::auth;
namespace ws = baas::service::websocket;
using namespace std::chrono_literals;
namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}

[[nodiscard]] auth::SecretBuffer secret(const std::string_view value)
{
    return auth::SecretBuffer{std::as_bytes(std::span{value.data(), value.size()})};
}

class RecordingSink final : public ws::BusinessPlaintextSink {
public:
    ws::BusinessEmitResult emit(ws::BusinessOutboundMessage message) noexcept override
    {
        {
            std::unique_lock lock{mutex_};
            ++calls_;
            if (block_single_) {
                entered_ = true;
                changed_.notify_all();
                changed_.wait(lock, [&] { return released_; });
            }
            if (reject_at_ != 0 && calls_ == reject_at_) {
                return ws::BusinessEmitResult::queue_full;
            }
            messages_.push_back(std::move(message.payload));
        }
        changed_.notify_all();
        return ws::BusinessEmitResult::accepted;
    }

    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage> messages) noexcept override
    {
        std::lock_guard lock{mutex_};
        ++calls_;
        if (reject_at_ != 0 && calls_ == reject_at_) {
            return ws::BusinessEmitResult::queue_full;
        }
        for (auto& message : messages) messages_.push_back(std::move(message.payload));
        return ws::BusinessEmitResult::accepted;
    }

    void reject_at(const std::size_t call)
    {
        std::lock_guard lock{mutex_};
        reject_at_ = call;
    }

    void block_single()
    {
        std::lock_guard lock{mutex_};
        block_single_ = true;
    }

    bool wait_entered()
    {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 2s, [&] { return entered_; });
    }

    void release()
    {
        std::lock_guard lock{mutex_};
        released_ = true;
        changed_.notify_all();
    }

    [[nodiscard]] std::vector<std::string> messages() const
    {
        std::lock_guard lock{mutex_};
        return messages_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::string> messages_;
    std::size_t calls_{};
    std::size_t reject_at_{};
    bool block_single_{};
    bool entered_{};
    bool released_{};
};

class FakeBackend final
    : public channels::ProviderBackend,
      public std::enable_shared_from_this<FakeBackend> {
public:
    class Token final : public channels::ProviderSubscription {
    public:
        Token(std::weak_ptr<FakeBackend> owner, const bool log, const std::size_t id)
            : owner_(std::move(owner)), log_(log), id_(id)
        {}
        ~Token() override
        {
            if (auto owner = owner_.lock()) owner->unsubscribe(log_, id_);
        }
    private:
        std::weak_ptr<FakeBackend> owner_;
        bool log_{};
        std::size_t id_{};
    };

    channels::ProviderBackendResult<channels::ProviderLogsFull> logs_full(
        std::stop_token) override
    {
        std::function<void()> callback;
        {
            std::lock_guard lock{mutex_};
            callback = during_logs_full;
        }
        if (callback) callback();
        return {channels::ProviderLogsFull{scopes_json, entries_json}, error};
    }

    channels::ProviderBackendResult<std::string> status(std::stop_token) override
    {
        return {status_json, error};
    }

    channels::ProviderBackendResult<std::optional<bool>> all_data_initialized(
        std::stop_token) override
    {
        return {std::optional<std::optional<bool>>{initialized}, error};
    }

    channels::ProviderBackendResult<channels::ProviderStaticSnapshot>
        static_snapshot(std::stop_token) override
    {
        return {channels::ProviderStaticSnapshot{timestamp_json, data_json}, error};
    }

    channels::ProviderSubscribeResult subscribe_logs(PushCallback callback) override
    {
        return subscribe(true, std::move(callback));
    }

    channels::ProviderSubscribeResult subscribe_status(PushCallback callback) override
    {
        return subscribe(false, std::move(callback));
    }

    void publish_log(std::string value) { publish(true, std::move(value)); }
    void publish_status(std::string value) { publish(false, std::move(value)); }

    [[nodiscard]] std::size_t subscriptions() const
    {
        std::lock_guard lock{mutex_};
        return logs_.size() + statuses_.size();
    }

    std::string scopes_json{R"(["main","config:a"])"};
    std::string entries_json{R"([{"scope":"main","ratio":1.25,"extra":{"x":1}}])"};
    std::string status_json{R"({"running":true,"progress":0.5,"future":"kept"})"};
    std::optional<bool> initialized{true};
    std::string timestamp_json{"1700000000123.5"};
    std::string data_json{R"({"activities":[{"name":"a","weight":2.75}],"unknown":true})"};
    channels::ProviderBackendError error{channels::ProviderBackendError::none};
    std::function<void()> during_logs_full;

private:
    channels::ProviderSubscribeResult subscribe(const bool log, PushCallback callback)
    {
        std::lock_guard lock{mutex_};
        const auto id = next_++;
        (log ? logs_ : statuses_).emplace(id, std::move(callback));
        return {std::make_unique<Token>(weak_from_this(), log, id),
                channels::ProviderBackendError::none};
    }

    void unsubscribe(const bool log, const std::size_t id)
    {
        std::lock_guard lock{mutex_};
        (log ? logs_ : statuses_).erase(id);
    }

    void publish(const bool log, std::string value)
    {
        std::vector<PushCallback> callbacks;
        {
            std::lock_guard lock{mutex_};
            for (const auto& [id, callback] : (log ? logs_ : statuses_)) {
                static_cast<void>(id);
                callbacks.push_back(callback);
            }
        }
        for (auto& callback : callbacks) callback(value);
    }

    mutable std::mutex mutex_;
    std::size_t next_{1};
    std::unordered_map<std::size_t, PushCallback> logs_;
    std::unordered_map<std::size_t, PushCallback> statuses_;
};

struct Created {
    std::shared_ptr<FakeBackend> backend;
    std::shared_ptr<RecordingSink> sink;
    std::unique_ptr<ws::BusinessChannelHandler> handler;
};

[[nodiscard]] Created create(
    const channels::ProviderHandlerLimits limits = {},
    std::shared_ptr<FakeBackend> backend = std::make_shared<FakeBackend>(),
    std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>())
{
    channels::ProviderHandlerFactory factory{backend, limits};
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::provider;
    auto result = factory.create(context, sink, {});
    check(static_cast<bool>(result), "provider handler creation must succeed");
    return {std::move(backend), std::move(sink), std::move(result.handler)};
}

void test_initial_requests_and_push_parity()
{
    auto created = create();
    auto ready = created.handler->ready({});
    check(ready.status == ws::BusinessHandlerStatus::ok && ready.messages.empty(),
          "ready publishes initial provider messages through the shared sink");
    auto messages = created.sink->messages();
    check(messages.size() == 3, "initialized provider emits three initial messages");
    check(messages[0]
              == R"({"type":"logs_full","scopes":["main","config:a"],"entries":[{"scope":"main","ratio":1.25,"extra":{"x":1}}]})",
          "logs_full preserves scopes, entries, floats, and unknown fields");
    check(messages[1]
              == R"({"type":"status","status":{"running":true,"progress":0.5,"future":"kept"}})",
          "status follows logs_full and preserves unknown fields");
    check(messages[2]
              == R"({"type":"status","status":{"is_all_data_initialized":true}})",
          "initialized status is third");

    created.backend->publish_log(R"({"scope":"main","message":"pushed","n":1.5})");
    created.backend->publish_status(R"({"running":false,"why":{"future":1}})");
    messages = created.sink->messages();
    check(messages.size() == 5
              && messages[3]
                  == R"({"type":"log","entry":{"scope":"main","message":"pushed","n":1.5}})"
              && messages[4]
                  == R"({"type":"status","status":{"running":false,"why":{"future":1}}})",
          "live log/status pushes use exact Python envelopes");

    auto static_result = created.handler->input(
        secret(R"({"type":"static_request","resource_id":"future","ignored":1.25})"),
        false, {});
    check(static_result.status == ws::BusinessHandlerStatus::ok
              && static_result.messages.size() == 1
              && static_result.messages[0].payload
                  == R"({"type":"static_snapshot","timestamp":1700000000123.5,"data":{"activities":[{"name":"a","weight":2.75}],"unknown":true}})",
          "static_request returns raw compatible static snapshot");
    auto status_result = created.handler->input(
        secret(R"({"type":"status_request","resource_id":null})"), false, {});
    check(status_result.status == ws::BusinessHandlerStatus::ok
              && status_result.messages[0].payload == messages[1],
          "status_request returns current status");
    check(created.handler->input(secret(R"({"type":"unknown"})"), false, {}).status
              == ws::BusinessHandlerStatus::protocol_failed,
          "unknown provider request is a protocol failure");
    check(created.handler->input(secret(R"({"type":1})"), false, {}).status
              == ws::BusinessHandlerStatus::protocol_failed,
          "malformed provider request is a protocol failure");
    created.handler->closed(ws::BusinessCloseReason::truncated);
    check(created.backend->subscriptions() == 0, "close unsubscribes both feeds");
}

void test_pending_bounds_and_sink_rejection_fail_closed()
{
    channels::ProviderHandlerLimits limits;
    limits.max_pending_pushes = 1;
    limits.max_pending_push_bytes = limits.max_output_json_bytes;
    auto backend = std::make_shared<FakeBackend>();
    backend->during_logs_full = [backend] {
        backend->publish_log(R"({"message":"one"})");
        backend->publish_status(R"({"state":"two"})");
    };
    auto pending = create(limits, backend);
    check(pending.handler->ready({}).status == ws::BusinessHandlerStatus::internal_error,
          "pre-ready push queue overflow fails closed");
    check(backend->subscriptions() == 0 && pending.sink->messages().empty(),
          "ready overflow releases subscriptions without publishing partial initial state");

    auto rejected = create();
    check(rejected.handler->ready({}).status == ws::BusinessHandlerStatus::ok,
          "sink rejection setup ready succeeds");
    rejected.sink->reject_at(3);
    rejected.backend->publish_log(R"({"message":"rejected"})");
    check(rejected.handler->heartbeat({}).status
              == ws::BusinessHandlerStatus::internal_error,
          "background sink rejection is connection-fatal");

    auto invalid = create();
    check(invalid.handler->ready({}).status == ws::BusinessHandlerStatus::ok,
          "invalid push setup ready succeeds");
    invalid.backend->publish_status(R"({"bad":NaN})");
    check(invalid.handler->heartbeat({}).status
              == ws::BusinessHandlerStatus::internal_error,
          "invalid backend JSON fails closed");

    channels::ProviderHandlerLimits output_limits;
    output_limits.max_output_json_bytes = 64;
    output_limits.max_pending_push_bytes = 64;
    auto bounded = create(output_limits);
    check(bounded.handler->ready({}).status == ws::BusinessHandlerStatus::capacity
              && bounded.sink->messages().empty()
              && bounded.backend->subscriptions() == 0,
          "single provider output bound rejects before partial publication");

    channels::ProviderHandlerLimits input_limits;
    input_limits.max_input_json_bytes = 32;
    auto input_bounded = create(input_limits);
    check(input_bounded.handler->ready({}).status == ws::BusinessHandlerStatus::ok,
          "input bound setup ready succeeds");
    check(input_bounded.handler->input(
              secret(R"({"type":"status_request","padding":"too-long"})"),
              false, {}).status == ws::BusinessHandlerStatus::protocol_failed,
          "provider request input bound is enforced before backend dispatch");
    std::stop_source stopped;
    stopped.request_stop();
    check(input_bounded.handler->heartbeat(stopped.get_token()).status
              == ws::BusinessHandlerStatus::complete,
          "provider heartbeat honors stop");

    auto no_initialized_backend = std::make_shared<FakeBackend>();
    no_initialized_backend->initialized.reset();
    auto no_initialized = create({}, no_initialized_backend);
    check(no_initialized.handler->ready({}).status == ws::BusinessHandlerStatus::ok
              && no_initialized.sink->messages().size() == 2,
          "absent initialized state emits only logs_full and status");
}

void test_close_waits_for_inflight_push()
{
    auto created = create();
    check(created.handler->ready({}).status == ws::BusinessHandlerStatus::ok,
          "close race setup ready succeeds");
    created.sink->block_single();
    std::thread publisher{[&] {
        created.backend->publish_log(R"({"message":"in-flight"})");
    }};
    check(created.sink->wait_entered(), "push enters sink before close");
    std::atomic_bool close_done{false};
    std::thread closer{[&] {
        created.handler->closed(ws::BusinessCloseReason::truncated);
        close_done.store(true);
    }};
    for (int attempt = 0; attempt < 2'000
         && created.backend->subscriptions() != 0; ++attempt) {
        std::this_thread::sleep_for(1ms);
    }
    check(created.backend->subscriptions() == 0 && !close_done.load(),
          "close unsubscribes then waits for admitted callback to leave sink");
    created.sink->release();
    publisher.join();
    closer.join();
    const auto count = created.sink->messages().size();
    created.backend->publish_log(R"({"message":"after-close"})");
    check(close_done.load() && created.backend->subscriptions() == 0
              && created.sink->messages().size() == count,
          "close is race-safe and suppresses later pushes");
}

}  // namespace

int main()
{
    test_initial_requests_and_push_parity();
    test_pending_bounds_and_sink_rejection_fail_closed();
    test_close_waits_for_inflight_push();
    if (failures != 0) {
        std::cerr << failures << " provider handler test(s) failed\n";
        return 1;
    }
    std::cout << "service provider handler tests passed\n";
    return 0;
}
