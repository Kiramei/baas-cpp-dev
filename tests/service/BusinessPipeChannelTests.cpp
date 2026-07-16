#include "service/app/ProductionProviderBackend.h"
#include "service/channels/ProviderHandler.h"
#include "service/channels/RemoteHandler.h"
#include "service/channels/SyncHandler.h"
#include "service/pipe/BusinessPipeChannel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace app = baas::service::app;
namespace auth = baas::service::auth;
namespace channels = baas::service::channels;
namespace pipe = baas::service::pipe;
namespace bpip = baas::service::protocol::bpip;
namespace websocket = baas::service::websocket;
using namespace std::chrono_literals;

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value)
{
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

[[nodiscard]] std::string text(const bpip::Bytes& value)
{
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] bpip::Bytes frame(
    const bpip::FrameKind kind,
    const std::span<const std::byte> payload)
{
    return bpip::encode_frame(kind, payload).bytes;
}

[[nodiscard]] bpip::Bytes frame(
    const bpip::FrameKind kind,
    const std::string_view payload)
{
    return frame(kind, bytes(payload));
}

[[nodiscard]] bpip::Bytes open_and(
    const std::string_view channel,
    const std::string_view name,
    const std::vector<bpip::Bytes>& following = {})
{
    const auto request = std::string{"{\"type\":\"open\",\"channel\":\""}
        + std::string{channel} + "\",\"name\":\"" + std::string{name} + "\"}";
    auto result = frame(bpip::FrameKind::json, request);
    for (const auto& item : following)
        result.insert(result.end(), item.begin(), item.end());
    return result;
}

template <class Predicate>
bool wait_until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

struct StreamState {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<bpip::Bytes> reads;
    std::vector<bpip::Bytes> writes;
    std::size_t read_offset{};
    std::size_t write_calls{};
    std::size_t fail_write_call{};
    std::size_t block_write_call{};
    bool write_entered{};
    bool closed{};
};

class FakeStream final : public pipe::PipeStream {
public:
    explicit FakeStream(std::shared_ptr<StreamState> state)
        : state_(std::move(state))
    {}

    pipe::PipeIoResult read(
        const std::span<std::byte> output,
        std::chrono::milliseconds) override
    {
        std::unique_lock lock{state_->mutex};
        state_->changed.wait(lock, [&] {
            return state_->closed || !state_->reads.empty();
        });
        if (state_->closed) return {0, true, false, false};
        auto& current = state_->reads.front();
        const auto count = std::min(
            output.size(), current.size() - state_->read_offset);
        std::copy_n(current.begin() + static_cast<std::ptrdiff_t>(state_->read_offset),
                    count, output.begin());
        state_->read_offset += count;
        if (state_->read_offset == current.size()) {
            state_->reads.pop_front();
            state_->read_offset = 0;
        }
        return {count, false, false, false};
    }

    pipe::PipeIoResult write_all(
        const std::span<const std::byte> input,
        std::chrono::milliseconds) override
    {
        std::unique_lock lock{state_->mutex};
        ++state_->write_calls;
        const auto call = state_->write_calls;
        state_->writes.emplace_back(input.begin(), input.end());
        if (call == state_->block_write_call) {
            state_->write_entered = true;
            state_->changed.notify_all();
            state_->changed.wait(lock, [&] { return state_->closed; });
        }
        if (state_->closed || call == state_->fail_write_call)
            return {0, false, true, false};
        return {input.size(), false, false, false};
    }

    void close() noexcept override
    {
        std::lock_guard lock{state_->mutex};
        state_->closed = true;
        state_->changed.notify_all();
    }

private:
    std::shared_ptr<StreamState> state_;
};

class FakeListener final : public pipe::PipeListener {
public:
    explicit FakeListener(std::unique_ptr<pipe::PipeStream> stream)
        : stream_(std::move(stream))
    {}

    std::unique_ptr<pipe::PipeStream> accept() override
    {
        std::unique_lock lock{mutex_};
        if (stream_) return std::move(stream_);
        changed_.wait(lock, [&] { return closed_; });
        return nullptr;
    }

    void close() noexcept override
    {
        std::lock_guard lock{mutex_};
        closed_ = true;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::unique_ptr<pipe::PipeStream> stream_;
    bool closed_{};
};

[[nodiscard]] std::vector<bpip::Frame> decoded_write(const bpip::Bytes& wire)
{
    auto decoded = bpip::Decoder{}.feed(wire);
    check(!decoded.error, "captured Pipe write must decode");
    return std::move(decoded.frames);
}

[[nodiscard]] std::shared_ptr<StreamState> run_one(
    std::shared_ptr<pipe::PipeChannelFactory> factory,
    bpip::Bytes input)
{
    auto state = std::make_shared<StreamState>();
    state->reads.push_back(std::move(input));
    pipe::PipeHost host{
        std::make_unique<FakeListener>(std::make_unique<FakeStream>(state)),
        std::move(factory)};
    check(host.start(), "business Pipe host must start");
    check(wait_until([&] { return host.stats().completed == 1; }),
          "business Pipe connection must finish");
    host.stop();
    host.join();
    return state;
}

void test_provider_initial_output_after_open_ok_and_json_requests()
{
    auto backend = std::make_shared<app::ProductionProviderBackend>();
    check(backend->publish_log(
              R"({"scope":"global","time":1,"level":"INFO","message":"ready"})")
              == channels::ProviderBackendError::none,
          "provider fixture log must publish");
    check(backend->publish_status(R"({"active":true})")
              == channels::ProviderBackendError::none,
          "provider fixture status must publish");
    check(backend->set_initialized(true) == channels::ProviderBackendError::none,
          "provider fixture initialization must publish");

    pipe::BusinessPipeChannelFactories dependencies;
    dependencies.provider =
        std::make_shared<channels::ProviderHandlerFactory>(backend);
    auto factory = std::make_shared<pipe::BusinessPipeChannelFactory>(
        std::move(dependencies));
    const auto state = run_one(factory, open_and("provider", "provider", {
        frame(bpip::FrameKind::json, R"({"type":"status_request"})"),
        frame(bpip::FrameKind::close, std::span<const std::byte>{}),
    }));

    std::lock_guard lock{state->mutex};
    check(state->writes.size() == 4,
          "provider must write open_ok, initial pair, initialized, and response");
    if (state->writes.size() != 4) return;
    const auto opened = decoded_write(state->writes[0]);
    const auto initial = decoded_write(state->writes[1]);
    const auto initialized = decoded_write(state->writes[2]);
    const auto response = decoded_write(state->writes[3]);
    check(opened.size() == 1 && text(opened[0].payload).find("open_ok")
              != std::string::npos,
          "provider open_ok must be the first logical write");
    check(initial.size() == 2
              && text(initial[0].payload).find("logs_full") != std::string::npos
              && text(initial[1].payload).find("status") != std::string::npos,
          "provider initial logs/status must share the next atomic write");
    check(initialized.size() == 1
              && text(initialized[0].payload).find("is_all_data_initialized")
                  != std::string::npos,
          "provider initialized projection must follow initial snapshots");
    check(response.size() == 1
              && text(response[0].payload).find("\"active\":true")
                  != std::string::npos,
          "provider JSON request must reuse the production handler");
}

void test_sync_list_pull_and_push_are_json()
{
    auto store = std::make_shared<channels::InMemoryResourceStore>(
        std::vector<channels::InitialResource>{
            {{channels::SyncResource::config, std::string{"alpha"}},
             {"10", R"({"value":1})"}},
        },
        channels::ResourceSnapshot{"11", R"(["alpha"])"},
        [] { return 12.0; });
    pipe::BusinessPipeChannelFactories dependencies;
    dependencies.sync = std::make_shared<channels::SyncHandlerFactory>(store);
    auto factory = std::make_shared<pipe::BusinessPipeChannelFactory>(
        std::move(dependencies));
    const auto state = run_one(factory, open_and("sync", "sync", {
        frame(bpip::FrameKind::json, R"({"type":"list"})"),
        frame(bpip::FrameKind::json,
              R"({"type":"pull","resource":"config","resource_id":"alpha"})"),
        frame(bpip::FrameKind::close, std::span<const std::byte>{}),
    }));

    {
        std::lock_guard lock{state->mutex};
        check(state->writes.size() == 3,
              "sync must write open_ok plus list and pull responses");
        if (state->writes.size() != 3) return;
        const auto listed = decoded_write(state->writes[1]);
        const auto pulled = decoded_write(state->writes[2]);
        check(listed.size() == 1
                  && listed[0].kind == bpip::kind_value(bpip::FrameKind::json)
                  && text(listed[0].payload).find("config_list")
                      != std::string::npos,
              "sync list response must be JSON");
        check(pulled.size() == 1
                  && pulled[0].kind == bpip::kind_value(bpip::FrameKind::json)
                  && text(pulled[0].payload).find("\"value\":1")
                      != std::string::npos,
              "sync pull response must preserve resource JSON");
    }

    auto push_stream = std::make_shared<StreamState>();
    push_stream->reads.push_back(open_and("sync", "sync-push"));
    pipe::PipeHost push_host{
        std::make_unique<FakeListener>(
            std::make_unique<FakeStream>(push_stream)),
        factory};
    check(push_host.start(), "sync push host must start");
    check(wait_until([&] {
        std::lock_guard push_lock{push_stream->mutex};
        return push_stream->writes.size() == 1;
    }), "sync open_ok must complete before backend push");
    check(store->replace_and_publish(
              {channels::SyncResource::config, std::string{"alpha"}},
              {"13", R"({"value":2})"},
              R"([{"op":"replace","path":"/value","value":2}])",
              "filesystem"),
          "sync backend fixture must publish an update");
    check(wait_until([&] {
        std::lock_guard push_lock{push_stream->mutex};
        return push_stream->writes.size() == 2;
    }), "sync subscription must emit one backend push");
    push_host.stop();
    push_host.join();
    {
        std::lock_guard push_lock{push_stream->mutex};
        if (push_stream->writes.size() == 2) {
            const auto pushed = decoded_write(push_stream->writes[1]);
            check(pushed.size() == 1
                      && pushed[0].kind
                          == bpip::kind_value(bpip::FrameKind::json)
                      && text(pushed[0].payload).find("\"direction\":\"push\"")
                          != std::string::npos,
                  "sync backend push must remain a JSON BPIP frame");
        }
    }
}

struct RemoteState {
    std::mutex mutex;
    channels::RemoteSessionCallbacks callbacks;
    std::vector<std::byte> client_bytes;
    channels::RemoteIoStatus startup_status{channels::RemoteIoStatus::closed};
    std::size_t closes{};
};

class FakeRemoteSession final : public channels::RemoteSession {
public:
    explicit FakeRemoteSession(std::shared_ptr<RemoteState> state)
        : state_(std::move(state))
    {}

    channels::RemoteIoStatus send_to_device(
        auth::SecretBuffer payload,
        std::stop_token stop) override
    {
        if (stop.stop_requested()) return channels::RemoteIoStatus::closed;
        std::lock_guard lock{state_->mutex};
        state_->client_bytes.assign(payload.bytes().begin(), payload.bytes().end());
        return channels::RemoteIoStatus::accepted;
    }

    void close() noexcept override
    {
        std::lock_guard lock{state_->mutex};
        ++state_->closes;
    }

private:
    std::shared_ptr<RemoteState> state_;
};

class FakeRemoteBackend final : public channels::RemoteBackend {
public:
    explicit FakeRemoteBackend(std::shared_ptr<RemoteState> state)
        : state_(std::move(state))
    {}

    channels::RemoteOpenResult open(
        std::optional<std::string>,
        channels::RemoteSessionCallbacks callbacks,
        std::stop_token stop) override
    {
        if (stop.stop_requested())
            return {nullptr, channels::RemoteBackendError::internal_error};
        {
            std::lock_guard lock{state_->mutex};
            state_->callbacks = callbacks;
        }
        const std::string startup{"\x00\x01\xfe", 3};
        const auto status = callbacks.device_bytes(startup);
        {
            std::lock_guard lock{state_->mutex};
            state_->startup_status = status;
        }
        return {std::make_unique<FakeRemoteSession>(state_),
                channels::RemoteBackendError::none};
    }

private:
    std::shared_ptr<RemoteState> state_;
};

void test_remote_raw_bytes_and_observed_write_completion()
{
    auto remote = std::make_shared<RemoteState>();
    pipe::BusinessPipeChannelFactories dependencies;
    dependencies.remote = std::make_shared<channels::RemoteHandlerFactory>(
        std::make_shared<FakeRemoteBackend>(remote));
    auto factory = std::make_shared<pipe::BusinessPipeChannelFactory>(
        std::move(dependencies));
    const std::string client{"\x7f\x00", 2};
    const auto state = run_one(factory, open_and("remote", "remote-1", {
        frame(bpip::FrameKind::json,
              R"({"config_id":"alpha","decrypt":false})"),
        frame(bpip::FrameKind::bytes, bytes(client)),
        frame(bpip::FrameKind::close, std::span<const std::byte>{}),
    }));

    {
        std::lock_guard lock{remote->mutex};
        check(remote->startup_status == channels::RemoteIoStatus::accepted,
              "remote callback must observe a fully written Pipe frame");
        check(remote->client_bytes == std::vector<std::byte>{
                  std::byte{0x7f}, std::byte{0x00}},
              "remote client BYTES must reach the production handler byte-exactly");
        check(remote->closes == 1,
              "remote session must close exactly once with the Pipe connection");
    }
    std::lock_guard lock{state->mutex};
    check(state->writes.size() == 2,
          "remote must write open_ok then one startup BYTES frame");
    if (state->writes.size() == 2) {
        const auto output = decoded_write(state->writes[1]);
        check(output.size() == 1
                  && output[0].kind == bpip::kind_value(bpip::FrameKind::bytes)
                  && output[0].payload == std::vector<std::byte>{
                      std::byte{0x00}, std::byte{0x01}, std::byte{0xfe}},
              "remote device payload must remain raw BPIP BYTES");
    }
}

void test_remote_write_failure_is_observed_and_closes_session()
{
    auto remote = std::make_shared<RemoteState>();
    pipe::BusinessPipeChannelFactories dependencies;
    dependencies.remote = std::make_shared<channels::RemoteHandlerFactory>(
        std::make_shared<FakeRemoteBackend>(remote));
    auto factory = std::make_shared<pipe::BusinessPipeChannelFactory>(
        std::move(dependencies));
    auto stream = std::make_shared<StreamState>();
    stream->fail_write_call = 2;
    stream->reads.push_back(open_and("remote", "remote-fail", {
        frame(bpip::FrameKind::json,
              R"({"config_id":"alpha","decrypt":true})"),
    }));
    pipe::PipeHost host{
        std::make_unique<FakeListener>(std::make_unique<FakeStream>(stream)),
        factory};
    check(host.start(), "remote failure host must start");
    check(wait_until([&] { return host.stats().completed == 1; }),
          "remote failed write must interrupt the connection");
    host.stop();
    host.join();
    std::lock_guard lock{remote->mutex};
    check(remote->startup_status != channels::RemoteIoStatus::accepted
              && remote->closes == 1,
          "failed device write receipt must close the partial remote session");
}

class DelegatedHandler final : public pipe::PipeChannelHandler {
public:
    explicit DelegatedHandler(std::shared_ptr<std::atomic_size_t> frames)
        : frames_(std::move(frames))
    {}

    pipe::PipeHandlerResult on_frame(
        const bpip::Frame&,
        pipe::PipeConnectionWriter&,
        std::stop_token) override
    {
        ++*frames_;
        return {};
    }

    void on_close(std::stop_token) noexcept override {}

private:
    std::shared_ptr<std::atomic_size_t> frames_;
};

class DelegatedFactory final : public pipe::PipeChannelFactory {
public:
    explicit DelegatedFactory(std::shared_ptr<std::atomic_size_t> frames)
        : frames_(std::move(frames))
    {}

    std::unique_ptr<pipe::PipeChannelHandler> create(
        const pipe::PipeOpenRequest& request,
        std::stop_token) override
    {
        if (request.channel != pipe::PipeChannel::trigger) return nullptr;
        return std::make_unique<DelegatedHandler>(frames_);
    }

private:
    std::shared_ptr<std::atomic_size_t> frames_;
};

void test_trigger_delegation_and_strict_frame_kinds()
{
    auto trigger_frames = std::make_shared<std::atomic_size_t>();
    pipe::BusinessPipeChannelFactories delegated;
    delegated.trigger = std::make_shared<DelegatedFactory>(trigger_frames);
    auto trigger = std::make_shared<pipe::BusinessPipeChannelFactory>(
        std::move(delegated));
    static_cast<void>(run_one(trigger, open_and("trigger", "trigger", {
        frame(bpip::FrameKind::json, R"({"type":"command"})"),
        frame(bpip::FrameKind::close, std::span<const std::byte>{}),
    })));
    check(trigger_frames->load() == 1,
          "trigger channel must be delegated without business-handler adaptation");

    auto provider_backend = std::make_shared<app::ProductionProviderBackend>();
    pipe::BusinessPipeChannelFactories provider_dependencies;
    provider_dependencies.provider =
        std::make_shared<channels::ProviderHandlerFactory>(provider_backend);
    auto provider = std::make_shared<pipe::BusinessPipeChannelFactory>(
        std::move(provider_dependencies));
    const auto invalid = run_one(provider, open_and("provider", "provider", {
        frame(bpip::FrameKind::bytes, "not-json"),
    }));
    std::lock_guard lock{invalid->mutex};
    check(!invalid->writes.empty(), "invalid provider kind must produce output");
    const auto terminal = decoded_write(invalid->writes.back());
    check(terminal.size() == 2
              && terminal[0].kind == bpip::kind_value(bpip::FrameKind::error)
              && terminal[1].kind == bpip::kind_value(bpip::FrameKind::close),
          "provider BYTES must fail closed with terminal ERROR+CLOSE");

    auto remote_state = std::make_shared<RemoteState>();
    pipe::BusinessPipeChannelFactories remote_dependencies;
    remote_dependencies.remote =
        std::make_shared<channels::RemoteHandlerFactory>(
            std::make_shared<FakeRemoteBackend>(remote_state));
    auto remote = std::make_shared<pipe::BusinessPipeChannelFactory>(
        std::move(remote_dependencies));
    const auto invalid_remote = run_one(remote, open_and("remote", "remote-kind", {
        frame(bpip::FrameKind::json,
              R"({"config_id":"alpha","decrypt":true})"),
        frame(bpip::FrameKind::json, R"({"unexpected":"json"})"),
    }));
    std::lock_guard remote_lock{invalid_remote->mutex};
    const auto remote_terminal = decoded_write(invalid_remote->writes.back());
    check(remote_terminal.size() == 2
              && remote_terminal[0].kind
                  == bpip::kind_value(bpip::FrameKind::error)
              && remote_terminal[1].kind
                  == bpip::kind_value(bpip::FrameKind::close),
          "remote data after config must be BYTES, never a JSON frame");
}

struct ReceiptBarrierState {
    std::mutex mutex;
    std::condition_variable changed;
    std::thread emitter;
    bool completion_entered{};
    bool release_completion{};
    bool handler_closed{};
};

class BlockingReceipt final : public websocket::BusinessBatchCompletion {
public:
    explicit BlockingReceipt(std::shared_ptr<ReceiptBarrierState> state)
        : state_(std::move(state))
    {}

    void complete(websocket::BusinessBatchWriteResult) noexcept override
    {
        std::unique_lock lock{state_->mutex};
        state_->completion_entered = true;
        state_->changed.notify_all();
        state_->changed.wait(lock, [this] { return state_->release_completion; });
    }

private:
    std::shared_ptr<ReceiptBarrierState> state_;
};

class ReceiptBarrierHandler final : public websocket::BusinessChannelHandler {
public:
    ReceiptBarrierHandler(
        std::shared_ptr<websocket::BusinessPlaintextSink> output,
        std::shared_ptr<ReceiptBarrierState> state)
        : output_(std::move(output)), state_(std::move(state))
    {}

    websocket::BusinessHandlerResult ready(std::stop_token) override
    {
        state_->emitter = std::thread{
            [output = output_, state = state_] {
                static_cast<void>(output->emit(
                    {R"({"type":"receipt"})", false},
                    std::make_shared<BlockingReceipt>(state)));
            }};
        return {};
    }

    websocket::BusinessHandlerResult input(
        auth::SecretBuffer, bool, std::stop_token) override
    {
        return {};
    }

    websocket::BusinessHandlerResult heartbeat(std::stop_token) override
    {
        return {};
    }

    void closed(websocket::BusinessCloseReason) noexcept override
    {
        std::lock_guard lock{state_->mutex};
        state_->handler_closed = true;
        state_->changed.notify_all();
    }

private:
    std::shared_ptr<websocket::BusinessPlaintextSink> output_;
    std::shared_ptr<ReceiptBarrierState> state_;
};

class ReceiptBarrierFactory final
    : public websocket::BusinessChannelHandlerFactory {
public:
    explicit ReceiptBarrierFactory(std::shared_ptr<ReceiptBarrierState> state)
        : state_(std::move(state))
    {}

    websocket::BusinessHandlerCreateResult create(
        websocket::BusinessSessionContext,
        std::shared_ptr<websocket::BusinessPlaintextSink> output,
        std::stop_token) override
    {
        return {std::make_unique<ReceiptBarrierHandler>(
                    std::move(output), state_),
                websocket::BusinessHandlerCreateError::none};
    }

private:
    std::shared_ptr<ReceiptBarrierState> state_;
};

void test_close_barrier_waits_for_write_receipt_callback()
{
    auto receipt = std::make_shared<ReceiptBarrierState>();
    pipe::BusinessPipeChannelFactories dependencies;
    dependencies.provider = std::make_shared<ReceiptBarrierFactory>(receipt);
    auto factory = std::make_shared<pipe::BusinessPipeChannelFactory>(
        std::move(dependencies));
    auto stream = std::make_shared<StreamState>();
    stream->reads.push_back(open_and("provider", "receipt-barrier"));
    pipe::PipeHost host{
        std::make_unique<FakeListener>(std::make_unique<FakeStream>(stream)),
        factory};
    check(host.start(), "receipt-barrier host must start");
    check(wait_until([&] {
        std::lock_guard lock{receipt->mutex};
        return receipt->completion_entered;
    }), "observed write receipt must enter its completion callback");

    std::atomic_bool joined{};
    std::thread closer{[&] {
        host.stop();
        host.join();
        joined.store(true, std::memory_order_release);
    }};
    check(wait_until([&] {
        std::lock_guard lock{receipt->mutex};
        return receipt->handler_closed;
    }), "Pipe close must reach the reused handler barrier");
    std::this_thread::sleep_for(20ms);
    check(!joined.load(std::memory_order_acquire),
          "Pipe join must wait until an entered write receipt callback returns");
    {
        std::lock_guard lock{receipt->mutex};
        receipt->release_completion = true;
        receipt->changed.notify_all();
    }
    closer.join();
    if (receipt->emitter.joinable()) receipt->emitter.join();
    check(joined.load(std::memory_order_acquire),
          "Pipe join must finish after the write receipt barrier drains");
}

void test_stop_interrupts_blocked_push_and_waits_for_close_barrier()
{
    auto backend = std::make_shared<app::ProductionProviderBackend>();
    pipe::BusinessPipeChannelFactories dependencies;
    dependencies.provider =
        std::make_shared<channels::ProviderHandlerFactory>(backend);
    auto factory = std::make_shared<pipe::BusinessPipeChannelFactory>(
        std::move(dependencies));
    auto stream = std::make_shared<StreamState>();
    stream->block_write_call = 3;
    stream->reads.push_back(open_and("provider", "provider"));
    pipe::PipeHost host{
        std::make_unique<FakeListener>(std::make_unique<FakeStream>(stream)),
        factory};
    check(host.start(), "blocked-push host must start");
    check(wait_until([&] {
        std::lock_guard lock{stream->mutex};
        return stream->writes.size() >= 2;
    }), "provider initial output must complete before push race");

    std::thread publisher([&] {
        static_cast<void>(backend->publish_log(
            R"({"scope":"global","time":2,"level":"INFO","message":"push"})"));
    });
    check(wait_until([&] {
        std::lock_guard lock{stream->mutex};
        return stream->write_entered;
    }), "provider push must enter the blocking platform write");

    host.stop();
    host.join();
    publisher.join();
    check(host.stats().active == 0,
          "stop must interrupt the write and drain the handler callback barrier");
    const auto writes_after_close = [&] {
        std::lock_guard lock{stream->mutex};
        return stream->writes.size();
    }();
    static_cast<void>(backend->publish_log(
        R"({"scope":"global","time":3,"level":"INFO","message":"late"})"));
    {
        std::lock_guard lock{stream->mutex};
        check(stream->writes.size() == writes_after_close,
              "no provider callback may enter after Pipe handler close returns");
    }
}

}  // namespace

int main()
{
    test_provider_initial_output_after_open_ok_and_json_requests();
    test_sync_list_pull_and_push_are_json();
    test_remote_raw_bytes_and_observed_write_completion();
    test_remote_write_failure_is_observed_and_closes_session();
    test_trigger_delegation_and_strict_frame_kinds();
    test_close_barrier_waits_for_write_receipt_callback();
    test_stop_interrupts_blocked_push_and_waits_for_close_barrier();
    if (failures != 0) {
        std::cerr << failures << " business Pipe channel test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "business Pipe channel tests passed\n";
    return EXIT_SUCCESS;
}
