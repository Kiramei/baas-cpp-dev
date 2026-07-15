#include "service/channels/RemoteHandler.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace auth = baas::service::auth;
namespace channels = baas::service::channels;
namespace ws = baas::service::websocket;
namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}

void require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string{message});
}

auth::SecretBuffer secret(const std::string_view value)
{
    return auth::SecretBuffer{std::as_bytes(std::span{value.data(), value.size()})};
}

class TestSink final : public ws::BusinessPlaintextSink {
public:
    ws::BusinessEmitResult emit(ws::BusinessOutboundMessage message) noexcept override
    {
        return emit(std::move(message), {});
    }

    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage> messages) noexcept override
    {
        if (messages.size() != 1) return ws::BusinessEmitResult::queue_full;
        return emit(std::move(messages.front()), {});
    }

    ws::BusinessEmitResult emit(
        ws::BusinessOutboundMessage message,
        std::shared_ptr<ws::BusinessBatchCompletion> completion) noexcept override
    {
        std::unique_lock lock{mutex};
        output_started = true;
        messages.push_back(std::move(message.payload));
        const auto result = admission;
        if (block_emit) {
            emit_entered = true;
            condition.notify_all();
            condition.wait(lock, [this] { return release_emit; });
        }
        if (result != ws::BusinessEmitResult::accepted) {
            lock.unlock();
            if (completion) completion->complete(ws::BusinessBatchWriteResult::failed);
            return result;
        }
        if (completion) completions.push_back(completion);
        const bool complete = complete_synchronously;
        lock.unlock();
        if (complete && completion)
            completion->complete(synchronous_result);
        return result;
    }

    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage> messages,
        std::shared_ptr<ws::BusinessBatchCompletion> completion) noexcept override
    {
        if (messages.size() != 1) {
            if (completion) completion->complete(ws::BusinessBatchWriteResult::failed);
            return ws::BusinessEmitResult::queue_full;
        }
        return emit(std::move(messages.front()), std::move(completion));
    }

    bool enable_remote_raw_output() noexcept override
    {
        std::lock_guard lock{mutex};
        events.push_back("raw");
        if (!raw_permitted || raw_enabled || output_started) return false;
        raw_enabled = true;
        return true;
    }

    void complete(const std::size_t index, const ws::BusinessBatchWriteResult result)
    {
        std::shared_ptr<ws::BusinessBatchCompletion> completion;
        {
            std::lock_guard lock{mutex};
            completion = completions.at(index);
        }
        completion->complete(result);
    }

    void wait_until_emit_entered()
    {
        std::unique_lock lock{mutex};
        condition.wait(lock, [this] { return emit_entered; });
    }

    void release_blocked_emit()
    {
        std::lock_guard lock{mutex};
        release_emit = true;
        condition.notify_all();
    }

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> messages;
    std::vector<std::shared_ptr<ws::BusinessBatchCompletion>> completions;
    std::vector<std::string> events;
    ws::BusinessEmitResult admission{ws::BusinessEmitResult::accepted};
    bool complete_synchronously{true};
    ws::BusinessBatchWriteResult synchronous_result{
        ws::BusinessBatchWriteResult::written};
    bool raw_permitted{true};
    bool raw_enabled{};
    bool output_started{};
    bool block_emit{};
    bool emit_entered{};
    bool release_emit{};
};

struct BackendState {
    channels::RemoteSessionCallbacks callbacks;
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::string> device_input;
    std::optional<std::string> config_id;
    channels::RemoteIoStatus send_result{channels::RemoteIoStatus::accepted};
    bool throw_send{};
    channels::RemoteBackendError open_error{channels::RemoteBackendError::none};
    std::string synchronous_output;
    std::vector<std::string>* events{};
    std::size_t opens{};
    std::size_t closes{};
    std::size_t active_callbacks{};
    std::size_t active_sends{};
    bool accepting_callbacks{};
    bool block_send{};
    bool send_entered{};
    bool release_send{};

    channels::RemoteIoStatus send(auth::SecretBuffer payload, std::stop_token stop)
    {
        std::unique_lock lock{mutex};
        if (!accepting_callbacks || stop.stop_requested())
            return channels::RemoteIoStatus::closed;
        if (throw_send) throw std::runtime_error("injected send failure");
        ++active_sends;
        device_input.emplace_back(
            reinterpret_cast<const char*>(payload.bytes().data()), payload.size());
        send_entered = true;
        condition.notify_all();
        if (block_send) condition.wait(lock, [this] { return release_send; });
        const auto result = accepting_callbacks
            ? send_result : channels::RemoteIoStatus::closed;
        --active_sends;
        condition.notify_all();
        return result;
    }

    void wait_until_send_entered()
    {
        std::unique_lock lock{mutex};
        condition.wait(lock, [this] { return send_entered; });
    }

    channels::RemoteIoStatus emit(std::string payload)
    {
        std::function<channels::RemoteIoStatus(std::string)> callback;
        {
            std::lock_guard lock{mutex};
            if (!accepting_callbacks) return channels::RemoteIoStatus::closed;
            ++active_callbacks;
            callback = callbacks.device_bytes;
        }
        const auto result = callback(std::move(payload));
        {
            std::lock_guard lock{mutex};
            --active_callbacks;
            condition.notify_all();
        }
        return result;
    }

    void end(const channels::RemoteSessionEnd reason)
    {
        std::function<void(channels::RemoteSessionEnd)> callback;
        {
            std::lock_guard lock{mutex};
            if (!accepting_callbacks) return;
            ++active_callbacks;
            callback = callbacks.ended;
        }
        callback(reason);
        {
            std::lock_guard lock{mutex};
            --active_callbacks;
            condition.notify_all();
        }
    }

    void close()
    {
        std::unique_lock lock{mutex};
        ++closes;
        accepting_callbacks = false;
        release_send = true;
        condition.notify_all();
        condition.wait(lock, [this] {
            return active_callbacks == 0 && active_sends == 0;
        });
        callbacks = {};
    }
};

class TestSession final : public channels::RemoteSession {
public:
    explicit TestSession(std::shared_ptr<BackendState> state)
        : state_(std::move(state))
    {}

    channels::RemoteIoStatus send_to_device(
        auth::SecretBuffer payload, std::stop_token stop) override
    {
        return state_->send(std::move(payload), stop);
    }

    void close() noexcept override
    {
        if (!closed_.exchange(true)) state_->close();
    }

private:
    std::shared_ptr<BackendState> state_;
    std::atomic_bool closed_{};
};

class TestBackend final : public channels::RemoteBackend {
public:
    explicit TestBackend(std::shared_ptr<BackendState> state)
        : state_(std::move(state))
    {}

    channels::RemoteOpenResult open(
        std::optional<std::string> config_id,
        channels::RemoteSessionCallbacks callbacks,
        std::stop_token stop) override
    {
        if (stop.stop_requested())
            return {nullptr, channels::RemoteBackendError::internal_error};
        {
            std::lock_guard lock{state_->mutex};
            ++state_->opens;
            state_->config_id = std::move(config_id);
            state_->callbacks = std::move(callbacks);
            state_->accepting_callbacks = true;
            if (state_->events) state_->events->push_back("open");
        }
        if (!state_->synchronous_output.empty())
            (void)state_->emit(state_->synchronous_output);
        if (state_->open_error != channels::RemoteBackendError::none) {
            state_->close();
            return {nullptr, state_->open_error};
        }
        return {std::make_unique<TestSession>(state_), channels::RemoteBackendError::none};
    }

private:
    std::shared_ptr<BackendState> state_;
};

struct Fixture {
    std::shared_ptr<TestSink> sink{std::make_shared<TestSink>()};
    std::shared_ptr<BackendState> backend_state{std::make_shared<BackendState>()};
    std::shared_ptr<TestBackend> backend{std::make_shared<TestBackend>(backend_state)};
    channels::RemoteHandlerFactory factory{backend};
    std::unique_ptr<ws::BusinessChannelHandler> handler;

    explicit Fixture(const channels::RemoteHandlerLimits limits = {})
        : factory(backend, limits)
    {
        auto created = factory.create(
            {auth::BusinessChannel::remote, "session", "socket", 1, 1}, sink, {});
        require(static_cast<bool>(created), "remote handler create failed");
        handler = std::move(created.handler);
        require(handler->ready({}).status == ws::BusinessHandlerStatus::ok,
                "remote handler ready failed");
    }

    ~Fixture()
    {
        if (handler) handler->closed(ws::BusinessCloseReason::stopped);
    }

    ws::BusinessHandlerResult config(const std::string_view json)
    {
        return handler->input(secret(json), false, {});
    }
};

void test_configuration_and_directional_proxy()
{
    Fixture fixture;
    const auto configured = fixture.config(
        R"({"config_id":"alpha","extra":1.25})");
    check(configured.status == ws::BusinessHandlerStatus::ok,
          "valid configuration must start the backend");
    check(fixture.backend_state->opens == 1
              && fixture.backend_state->config_id == std::optional<std::string>{"alpha"},
          "config_id must reach the backend exactly");
    check(!fixture.sink->raw_enabled,
          "decrypt default true must retain encrypted outbound mode");

    const std::string adb{"\0adb\xFF", 5};
    check(fixture.handler->input(secret(adb), false, {}).status
              == ws::BusinessHandlerStatus::ok,
          "authenticated binary must flow to the device session");
    check(fixture.backend_state->device_input == std::vector<std::string>{adb},
          "WS to ADB bytes must remain exact");
    check(fixture.backend_state->emit("frame") == channels::RemoteIoStatus::accepted,
          "ADB to WS callback must report accepted");
    check(fixture.sink->messages == std::vector<std::string>{"frame"},
          "ADB to WS bytes must remain exact");
}

void test_strict_first_message_and_raw_switch_order()
{
    const std::vector<std::string> invalid{
        "[]", "{}", R"({"config_id":1})", R"({"config_id":null,"decrypt":"no"})",
        R"({"config_id":null,"config_id":null})"};
    for (const auto& json : invalid) {
        Fixture fixture;
        check(fixture.config(json).status == ws::BusinessHandlerStatus::protocol_failed,
              "invalid initial remote configuration must fail protocol");
        check(fixture.backend_state->opens == 0 && !fixture.sink->raw_enabled,
              "invalid configuration must not switch mode or acquire a device");
    }

    Fixture fixture;
    fixture.backend_state->events = &fixture.sink->events;
    check(fixture.config(R"({"config_id":null,"decrypt":false})").status
              == ws::BusinessHandlerStatus::ok,
          "decrypt=false must enable raw remote output");
    check(fixture.sink->raw_enabled
              && fixture.sink->events == std::vector<std::string>({"raw", "open"}),
          "raw switch must occur exactly once before backend callbacks can start");
}

void test_start_failures_and_synchronous_callback()
{
    {
        Fixture fixture;
        fixture.backend_state->open_error = channels::RemoteBackendError::capacity;
        check(fixture.config(R"({"config_id":null})").status
                  == ws::BusinessHandlerStatus::capacity,
              "backend admission capacity must propagate");
        check(fixture.backend_state->closes == 1,
              "failed backend start must release partial resources");
    }
    {
        Fixture fixture;
        fixture.backend_state->synchronous_output = "first-device-frame";
        check(fixture.config(R"({"config_id":"x","decrypt":false})").status
                  == ws::BusinessHandlerStatus::ok,
              "backend may synchronously publish during open");
        check(fixture.sink->raw_enabled
                  && fixture.sink->messages == std::vector<std::string>{"first-device-frame"},
              "synchronous first frame must observe configured raw mode");
    }
}

void test_completion_backpressure_and_failure()
{
    {
        channels::RemoteHandlerLimits limits;
        limits.max_in_flight_frames = 1;
        Fixture fixture{limits};
        fixture.sink->complete_synchronously = false;
        require(fixture.config(R"({"config_id":null})").status
                    == ws::BusinessHandlerStatus::ok,
                "async completion setup failed");
        check(fixture.backend_state->emit("one") == channels::RemoteIoStatus::accepted,
              "first deferred frame must be admitted");
        check(fixture.backend_state->emit("two") == channels::RemoteIoStatus::capacity,
              "in-flight frame bound must apply before silent loss");
        check(fixture.handler->heartbeat({}).status == ws::BusinessHandlerStatus::capacity,
              "backpressure overflow must terminate as capacity");
        fixture.sink->complete(0, ws::BusinessBatchWriteResult::written);
    }
    {
        channels::RemoteHandlerLimits limits;
        limits.max_device_frame_bytes = 3;
        limits.max_in_flight_frames = 2;
        limits.max_in_flight_bytes = 3;
        Fixture fixture{limits};
        fixture.sink->complete_synchronously = false;
        require(fixture.config(R"({"config_id":null})").status
                    == ws::BusinessHandlerStatus::ok,
                "byte budget setup failed");
        require(fixture.backend_state->emit("aa") == channels::RemoteIoStatus::accepted,
                "first byte-budget frame failed");
        check(fixture.backend_state->emit("bb") == channels::RemoteIoStatus::capacity,
              "retained-byte budget must reject before sink admission");
        fixture.sink->complete(0, ws::BusinessBatchWriteResult::written);
    }
    {
        channels::RemoteHandlerLimits limits;
        limits.max_device_frame_bytes = 3;
        limits.max_in_flight_bytes = 3;
        Fixture fixture{limits};
        require(fixture.config(R"({"config_id":null})").status
                    == ws::BusinessHandlerStatus::ok,
                "oversize setup failed");
        check(fixture.backend_state->emit("four") == channels::RemoteIoStatus::capacity
                  && fixture.sink->messages.empty(),
              "oversized device frame must fail before output admission");
    }
    {
        Fixture fixture;
        fixture.sink->synchronous_result = ws::BusinessBatchWriteResult::failed;
        require(fixture.config(R"({"config_id":null})").status
                    == ws::BusinessHandlerStatus::ok,
                "synchronous failure setup failed");
        check(fixture.backend_state->emit("sync-failure")
                  == channels::RemoteIoStatus::internal_error
                  && fixture.handler->heartbeat({}).status
                      == ws::BusinessHandlerStatus::internal_error,
              "synchronous write failure must be returned to the backend");
    }
    {
        Fixture fixture;
        fixture.sink->complete_synchronously = false;
        require(fixture.config(R"({"config_id":null})").status
                    == ws::BusinessHandlerStatus::ok,
                "async failure setup failed");
        require(fixture.backend_state->emit("deferred")
                    == channels::RemoteIoStatus::accepted,
                "deferred frame admission failed");
        fixture.sink->complete(0, ws::BusinessBatchWriteResult::failed);
        check(fixture.handler->heartbeat({}).status
                  == ws::BusinessHandlerStatus::internal_error,
              "asynchronous write failure must terminate the proxy");
    }
    {
        Fixture fixture;
        fixture.sink->admission = ws::BusinessEmitResult::queue_full;
        require(fixture.config(R"({"config_id":null})").status
                    == ws::BusinessHandlerStatus::ok,
                "queue-full setup failed");
        check(fixture.backend_state->emit("rejected") == channels::RemoteIoStatus::capacity
                  && fixture.handler->heartbeat({}).status
                      == ws::BusinessHandlerStatus::capacity,
              "sink rejection must propagate without dropping a frame silently");
    }
}

void test_concurrent_callback_and_close()
{
    Fixture fixture;
    fixture.sink->block_emit = true;
    require(fixture.config(R"({"config_id":null})").status
                == ws::BusinessHandlerStatus::ok,
            "close-race setup failed");
    std::atomic<channels::RemoteIoStatus> emitted{channels::RemoteIoStatus::closed};
    std::thread producer{[&] { emitted = fixture.backend_state->emit("in-progress"); }};
    fixture.sink->wait_until_emit_entered();
    std::atomic_bool close_returned{};
    std::thread closer{[&] {
        fixture.handler->closed(ws::BusinessCloseReason::stopped);
        close_returned = true;
    }};
    std::this_thread::yield();
    check(!close_returned.load(),
          "session close must wait for an entered backend callback");
    fixture.sink->release_blocked_emit();
    producer.join();
    closer.join();
    check(close_returned && emitted == channels::RemoteIoStatus::accepted
              && fixture.backend_state->closes == 1,
          "concurrent callback must settle before exactly-once close returns");
    check(fixture.backend_state->emit("late") == channels::RemoteIoStatus::closed,
          "no callback may enter after session close");
}

void test_blocked_send_is_interrupted_by_close()
{
    Fixture fixture;
    require(fixture.config(R"({"config_id":null})").status
                == ws::BusinessHandlerStatus::ok,
            "blocked-send setup failed");
    fixture.backend_state->block_send = true;
    std::atomic<ws::BusinessHandlerStatus> send_status{ws::BusinessHandlerStatus::ok};
    std::thread sender{[&] {
        send_status = fixture.handler->input(secret("blocked"), false, {}).status;
    }};
    fixture.backend_state->wait_until_send_entered();
    std::thread closer{[&] {
        fixture.handler->closed(ws::BusinessCloseReason::stopped);
    }};
    closer.join();
    sender.join();
    check(fixture.backend_state->closes == 1
              && send_status == ws::BusinessHandlerStatus::complete,
          "concurrent close must interrupt and join a blocked WS-to-ADB send");
}

void test_backend_end_and_factory_guards()
{
    {
        Fixture throwing;
        require(throwing.config(R"({"config_id":null})").status
                    == ws::BusinessHandlerStatus::ok,
                "throwing send setup failed");
        throwing.backend_state->throw_send = true;
        check(throwing.handler->input(secret("payload"), false, {}).status
                  == ws::BusinessHandlerStatus::internal_error
                  && throwing.backend_state->closes == 1,
              "external send exceptions must be contained and close the session");
    }
    Fixture fixture;
    require(fixture.config(R"({"config_id":null})").status
                == ws::BusinessHandlerStatus::ok,
            "backend-end setup failed");
    fixture.backend_state->end(channels::RemoteSessionEnd::device_closed);
    check(fixture.handler->heartbeat({}).status == ws::BusinessHandlerStatus::complete,
          "device close must cleanly finish the business session");

    auto backend = std::make_shared<TestBackend>(std::make_shared<BackendState>());
    channels::RemoteHandlerFactory factory{backend};
    auto sink = std::make_shared<TestSink>();
    check(!factory.create(
               {auth::BusinessChannel::trigger, {}, {}, 0, 0}, sink, {}),
          "remote factory must reject channel confusion");
    bool threw{};
    try {
        channels::RemoteHandlerLimits limits;
        limits.max_in_flight_frames = 0;
        channels::RemoteHandlerFactory invalid{backend, limits};
        (void)invalid;
    }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw, "invalid remote limits must fail construction");
}

}  // namespace

int main()
{
    try {
        test_configuration_and_directional_proxy();
        test_strict_first_message_and_raw_switch_order();
        test_start_failures_and_synchronous_callback();
        test_completion_backpressure_and_failure();
        test_concurrent_callback_and_close();
        test_blocked_send_is_interrupted_by_close();
        test_backend_end_and_factory_guards();
    }
    catch (const std::exception& error) {
        std::cerr << "FAILED with exception: " << error.what() << '\n';
        ++failures;
    }
    if (failures != 0) {
        std::cerr << failures << " remote handler test(s) failed\n";
        return 1;
    }
    std::cout << "service remote handler tests passed\n";
    return 0;
}
