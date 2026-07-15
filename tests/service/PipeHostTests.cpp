#include "service/pipe/PipeHost.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace pipe = baas::service::pipe;
namespace bpip = baas::service::protocol::bpip;
using namespace std::chrono_literals;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value)
{
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

[[nodiscard]] bpip::Bytes frame(
    const bpip::FrameKind kind, const std::span<const std::byte> payload)
{
    return bpip::encode_frame(kind, payload).bytes;
}

[[nodiscard]] bpip::Bytes frame(
    const bpip::FrameKind kind, const std::string_view payload)
{
    return frame(kind, bytes(payload));
}

struct StreamState {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<bpip::Bytes> reads;
    std::deque<std::chrono::milliseconds> read_delays;
    std::vector<bpip::Bytes> writes;
    std::size_t read_offset{};
    bool block_when_empty{};
    bool closed{};
    bool partial_write{};
};

class FakeStream final : public pipe::PipeStream {
public:
    explicit FakeStream(std::shared_ptr<StreamState> state) : state_(std::move(state)) {}

    pipe::PipeIoResult read(
        const std::span<std::byte> output,
        const std::chrono::milliseconds timeout) override
    {
        std::unique_lock lock(state_->mutex);
        const auto ready = state_->changed.wait_for(lock, timeout, [this] {
            return state_->closed || !state_->reads.empty()
                || !state_->block_when_empty;
        });
        if (!ready) return {0, false, false, true};
        if (state_->closed) return {0, true, false, false};
        if (state_->reads.empty()) return {0, true, false, false};
        if (!state_->read_delays.empty()) {
            const auto delay = state_->read_delays.front();
            if (delay >= timeout) {
                state_->changed.wait_for(lock, timeout, [this] {
                    return state_->closed;
                });
                if (state_->closed) return {0, true, false, false};
                return {0, false, false, true};
            }
            state_->changed.wait_for(lock, delay, [this] {
                return state_->closed;
            });
            if (state_->closed) return {0, true, false, false};
            state_->read_delays.pop_front();
        }
        const auto& source = state_->reads.front();
        const auto count = std::min(output.size(), source.size() - state_->read_offset);
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(state_->read_offset),
                    count, output.begin());
        state_->read_offset += count;
        if (state_->read_offset == source.size()) {
            state_->reads.pop_front();
            state_->read_offset = 0;
        }
        return {count, false, false, false};
    }

    pipe::PipeIoResult write_all(
        const std::span<const std::byte> input,
        std::chrono::milliseconds) override
    {
        std::lock_guard lock(state_->mutex);
        if (state_->closed) return {0, false, true, false};
        const auto count = state_->partial_write && !input.empty()
            ? input.size() - 1 : input.size();
        state_->writes.emplace_back(input.begin(), input.begin()
            + static_cast<std::ptrdiff_t>(count));
        return {count, false, state_->partial_write, false};
    }

    void close() noexcept override
    {
        std::lock_guard lock(state_->mutex);
        state_->closed = true;
        state_->changed.notify_all();
    }

private:
    std::shared_ptr<StreamState> state_;
};

class FakeListener final : public pipe::PipeListener {
public:
    void push(std::unique_ptr<pipe::PipeStream> stream)
    {
        std::lock_guard lock(mutex_);
        streams_.push_back(std::move(stream));
        changed_.notify_one();
    }

    std::unique_ptr<pipe::PipeStream> accept() override
    {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [this] { return closed_ || !streams_.empty(); });
        if (closed_) return {};
        auto result = std::move(streams_.front());
        streams_.pop_front();
        return result;
    }

    void close() noexcept override
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::unique_ptr<pipe::PipeStream>> streams_;
    bool closed_{};
};

struct FactoryState {
    std::mutex mutex;
    std::vector<pipe::PipeOpenRequest> requests;
    std::size_t frames{};
    std::size_t closes{};
    bool throw_on_frame{};
    bool emit_batch{};
    std::function<void()> action;
};

class RecordingHandler final : public pipe::PipeChannelHandler {
public:
    explicit RecordingHandler(std::shared_ptr<FactoryState> state)
        : state_(std::move(state))
    {}

    bool on_frame(
        const bpip::Frame&, pipe::PipeConnectionWriter& writer) override
    {
        bool throw_now{};
        bool emit{};
        std::function<void()> action;
        {
            std::lock_guard lock(state_->mutex);
            ++state_->frames;
            throw_now = state_->throw_on_frame;
            emit = state_->emit_batch;
            action = state_->action;
        }
        if (action) action();
        if (throw_now) throw std::runtime_error("handler failure");
        if (emit) {
            const std::array output{
                bpip::Frame{bpip::kind_value(bpip::FrameKind::json),
                    bpip::Bytes{bytes(R"({"type":"reply"})").begin(),
                                bytes(R"({"type":"reply"})").end()}},
                bpip::Frame{bpip::kind_value(bpip::FrameKind::bytes), {}},
            };
            return writer.write_batch(output) == pipe::PipeHostError::none;
        }
        return true;
    }

    void on_close() noexcept override
    {
        std::lock_guard lock(state_->mutex);
        ++state_->closes;
    }

private:
    std::shared_ptr<FactoryState> state_;
};

class RecordingFactory final : public pipe::PipeChannelFactory {
public:
    explicit RecordingFactory(std::shared_ptr<FactoryState> state)
        : state_(std::move(state))
    {}

    std::unique_ptr<pipe::PipeChannelHandler> create(
        const pipe::PipeOpenRequest& request) override
    {
        std::lock_guard lock(state_->mutex);
        state_->requests.push_back(request);
        return std::make_unique<RecordingHandler>(state_);
    }

private:
    std::shared_ptr<FactoryState> state_;
};

template <class Predicate>
bool wait_until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

[[nodiscard]] bpip::Bytes open_and(
    const std::string_view open_json,
    const std::vector<bpip::Bytes>& following = {})
{
    auto result = frame(bpip::FrameKind::json, open_json);
    for (const auto& next : following)
        result.insert(result.end(), next.begin(), next.end());
    return result;
}

void test_bounded_open_codec_and_inventory()
{
    for (const auto channel : {"provider", "sync", "trigger", "remote"}) {
        const auto json = std::string{"{\"type\":\"open\",\"channel\":\""}
            + channel + "\",\"name\":\"client\",\"extra\":[1,true]}";
        const auto decoded = pipe::decode_pipe_open(bytes(json));
        check(decoded && decoded.request->name == "client"
                  && pipe::pipe_channel_name(decoded.request->channel) == channel,
              "every supported channel must decode through one bounded open codec");
    }
    check(pipe::decode_pipe_open(bytes(
              R"({"type":"open","channel":"control","name":"x"})"
          )).error == pipe::PipeHostError::unsupported_channel,
          "control must remain unavailable over Pipe");
    check(pipe::decode_pipe_open(bytes(
              R"({"type":"open","channel":"trigger","name":""})"
          )).error == pipe::PipeHostError::invalid_open_name,
          "empty names must fail before factory selection");
    check(pipe::decode_pipe_open(bytes(
              R"({"type":"open","type":"open","channel":"trigger","name":"x"})"
          )).error == pipe::PipeHostError::duplicate_open_field,
          "duplicate fields must not select ambiguous connection identity");
    check(pipe::decode_pipe_open(bytes("[]")).error
              == pipe::PipeHostError::malformed_open_json,
          "non-object JSON must fail closed");
    pipe::PipeHostLimits limits;
    limits.max_open_json_bytes = 8;
    limits.max_name_bytes = 4;
    check(pipe::decode_pipe_open(bytes("123456789"), limits).error
              == pipe::PipeHostError::open_json_too_large,
          "open payload must be bounded before parsing");
    const auto open_ok = pipe::encode_pipe_open_ok(pipe::PipeChannel::trigger);
    const auto decoded_ok = bpip::Decoder{}.feed(open_ok.bytes);
    check(open_ok && decoded_ok.frames.size() == 1
              && std::string_view{
                  reinterpret_cast<const char*>(decoded_ok.frames[0].payload.data()),
                  decoded_ok.frames[0].payload.size()}
                  == R"({"type":"open_ok","channel":"trigger"})",
          "open_ok must preserve the committed compact v1 shape");
}

void test_fragmented_open_coalesced_business_and_atomic_batch()
{
    auto listener = std::make_unique<FakeListener>();
    auto* listener_view = listener.get();
    const auto stream = std::make_shared<StreamState>();
    const auto wire = open_and(
        R"({"type":"open","channel":"trigger","name":"main"})",
        {frame(bpip::FrameKind::json, R"({"type":"command"})"),
         frame(bpip::FrameKind::close, std::span<const std::byte>{})});
    stream->reads.push_back({wire.begin(), wire.begin() + 3});
    stream->reads.push_back({wire.begin() + 3, wire.begin() + 17});
    stream->reads.push_back({wire.begin() + 17, wire.end()});
    listener_view->push(std::make_unique<FakeStream>(stream));
    const auto factory_state = std::make_shared<FactoryState>();
    factory_state->emit_batch = true;
    pipe::PipeHost host{std::move(listener),
        std::make_shared<RecordingFactory>(factory_state)};
    check(host.start(), "fake host must start exactly once");
    check(wait_until([&] { return host.stats().completed == 1; }),
          "fragmented/coalesced connection must complete deterministically");
    host.stop();
    host.join();

    std::lock_guard factory_lock(factory_state->mutex);
    check(factory_state->requests.size() == 1
              && factory_state->requests[0].channel == pipe::PipeChannel::trigger
              && factory_state->requests[0].name == "main"
              && factory_state->frames == 1 && factory_state->closes == 1,
          "factory and handler must receive one opened logical channel");
    std::lock_guard stream_lock(stream->mutex);
    check(stream->writes.size() == 2,
          "open_ok and handler JSON+BYTES batch must be separate logical writes");
    if (stream->writes.size() == 2) {
        const auto opened = bpip::Decoder{}.feed(stream->writes[0]);
        const auto response = bpip::Decoder{}.feed(stream->writes[1]);
        check(opened.frames.size() == 1
                  && opened.frames[0].kind == bpip::kind_value(bpip::FrameKind::json),
              "open_ok must precede business output");
        check(response.frames.size() == 2
                  && response.frames[0].kind == bpip::kind_value(bpip::FrameKind::json)
                  && response.frames[1].kind == bpip::kind_value(bpip::FrameKind::bytes)
                  && response.frames[1].payload.empty(),
              "JSON and present zero-byte BYTES must share one atomic write buffer");
    }
}

void test_protocol_and_handler_failures_are_terminal()
{
    for (const bool handler_failure : {false, true}) {
        auto listener = std::make_unique<FakeListener>();
        auto* listener_view = listener.get();
        const auto stream = std::make_shared<StreamState>();
        if (handler_failure) {
            stream->reads.push_back(open_and(
                R"({"type":"open","channel":"sync","name":"x"})",
                {frame(bpip::FrameKind::json, "{}") }));
        } else {
            stream->reads.push_back(frame(bpip::FrameKind::bytes, "bad-first"));
        }
        listener_view->push(std::make_unique<FakeStream>(stream));
        const auto factory_state = std::make_shared<FactoryState>();
        factory_state->throw_on_frame = handler_failure;
        pipe::PipeHost host{std::move(listener),
            std::make_shared<RecordingFactory>(factory_state)};
        check(host.start(), "failure host must start with fake listener");
        check(wait_until([&] { return host.stats().completed == 1; }),
              "protocol/handler failure must close deterministically");
        host.stop();
        host.join();
        std::lock_guard lock(stream->mutex);
        check(!stream->writes.empty(), "terminal failure must attempt ERROR+CLOSE");
        const auto terminal = bpip::Decoder{}.feed(stream->writes.back());
        check(terminal.frames.size() == 2
                  && terminal.frames[0].kind == bpip::kind_value(bpip::FrameKind::error)
                  && terminal.frames[1].kind == bpip::kind_value(bpip::FrameKind::close),
              "semantic failures must use one atomic ERROR+CLOSE batch");
    }
}

void test_partial_write_and_connection_limit_close_and_join()
{
    {
        auto listener = std::make_unique<FakeListener>();
        auto* view = listener.get();
        const auto stream = std::make_shared<StreamState>();
        stream->partial_write = true;
        stream->reads.push_back(open_and(
            R"({"type":"open","channel":"provider","name":"x"})"));
        view->push(std::make_unique<FakeStream>(stream));
        const auto factory = std::make_shared<FactoryState>();
        pipe::PipeHost host{std::move(listener),
            std::make_shared<RecordingFactory>(factory)};
        check(host.start(), "partial-write fake must start");
        check(wait_until([&] { return host.stats().completed == 1; }),
              "partial open_ok write must be connection-fatal");
        host.stop();
        host.join();
        check(host.state() == pipe::PipeHostState::stopped,
              "stop and join must reach a terminal stopped state");
    }
    {
        auto listener = std::make_unique<FakeListener>();
        auto* view = listener.get();
        const auto first = std::make_shared<StreamState>();
        first->block_when_empty = true;
        const auto second = std::make_shared<StreamState>();
        second->block_when_empty = true;
        view->push(std::make_unique<FakeStream>(first));
        view->push(std::make_unique<FakeStream>(second));
        pipe::PipeHostLimits limits;
        limits.max_connections = 1;
        const auto factory = std::make_shared<FactoryState>();
        pipe::PipeHost host{std::move(listener),
            std::make_shared<RecordingFactory>(factory), limits};
        check(host.start(), "bounded host must start");
        check(wait_until([&] { return host.stats().rejected == 1; }),
              "connections beyond the fixed cap must be rejected");
        host.stop();
        host.join();
        const auto stats = host.stats();
        check(stats.accepted == 1 && stats.rejected == 1
                  && stats.peak_active <= 1 && stats.active == 0,
              "connection cap and joined accounting must remain exact");
    }
}

void test_open_timeout_and_hard_write_limit()
{
    auto listener = std::make_unique<FakeListener>();
    auto* view = listener.get();
    const auto slow = std::make_shared<StreamState>();
    slow->block_when_empty = true;
    view->push(std::make_unique<FakeStream>(slow));
    pipe::PipeHostLimits limits;
    limits.max_connections = 1;
    limits.open_timeout = 20ms;
    const auto factory = std::make_shared<FactoryState>();
    pipe::PipeHost host{std::move(listener),
        std::make_shared<RecordingFactory>(factory), limits};
    check(host.start(), "slowloris timeout host must start with a fake stream");
    check(wait_until([&] { return host.stats().completed == 1; }),
          "a same-user peer that never sends open must release its worker");
    host.stop();
    host.join();
    {
        std::lock_guard lock(slow->mutex);
        check(!slow->writes.empty(), "open timeout must attempt terminal ERROR+CLOSE");
    }

    bool rejected = false;
    try {
        auto invalid_listener = std::make_unique<FakeListener>();
        pipe::PipeHostLimits invalid;
        invalid.max_atomic_write_bytes = 129U * 1'024U * 1'024U;
        [[maybe_unused]] pipe::PipeHost invalid_host{
            std::move(invalid_listener),
            std::make_shared<RecordingFactory>(std::make_shared<FactoryState>()),
            invalid};
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "atomic write configuration must have a hard memory ceiling");
}

void test_absolute_open_deadline_rejects_drip_feed()
{
    auto listener = std::make_unique<FakeListener>();
    auto* view = listener.get();
    const auto drip = std::make_shared<StreamState>();
    const auto wire = open_and(
        R"({"type":"open","channel":"remote","name":"drip"})");
    const auto first = wire.size() / 3;
    const auto second = first * 2;
    drip->reads.push_back({wire.begin(), wire.begin()
        + static_cast<std::ptrdiff_t>(first)});
    drip->reads.push_back({wire.begin() + static_cast<std::ptrdiff_t>(first),
        wire.begin() + static_cast<std::ptrdiff_t>(second)});
    drip->reads.push_back({wire.begin() + static_cast<std::ptrdiff_t>(second),
        wire.end()});
    drip->read_delays = {20ms, 20ms, 20ms};
    view->push(std::make_unique<FakeStream>(drip));

    pipe::PipeHostLimits limits;
    limits.max_connections = 1;
    limits.open_timeout = 45ms;
    const auto factory = std::make_shared<FactoryState>();
    pipe::PipeHost host{std::move(listener),
        std::make_shared<RecordingFactory>(factory), limits};
    check(host.start(), "drip-feed timeout host must start with a fake stream");
    check(wait_until([&] { return host.stats().completed == 1; }),
          "fragment progress must not reset the absolute open deadline");
    host.stop();
    host.join();

    {
        std::lock_guard lock(factory->mutex);
        check(factory->requests.empty(),
              "an open completed after the absolute deadline must not reach the factory");
    }
    std::lock_guard lock(drip->mutex);
    check(!drip->writes.empty(),
          "absolute open timeout must attempt terminal ERROR+CLOSE");
    if (!drip->writes.empty()) {
        const auto terminal = bpip::Decoder{}.feed(drip->writes.back());
        check(terminal.frames.size() == 2
                  && terminal.frames[0].kind
                      == bpip::kind_value(bpip::FrameKind::error)
                  && std::string_view{
                      reinterpret_cast<const char*>(
                          terminal.frames[0].payload.data()),
                      terminal.frames[0].payload.size()} == "open_timeout",
              "drip-feed rejection must retain the stable open_timeout error");
    }
}

void test_handler_self_join_and_external_join_orders()
{
    for (const bool external_first : {false, true}) {
        auto listener = std::make_unique<FakeListener>();
        auto* view = listener.get();
        const auto stream = std::make_shared<StreamState>();
        stream->reads.push_back(open_and(
            R"({"type":"open","channel":"provider","name":"self-join"})",
            {frame(bpip::FrameKind::json, "{}") }));
        view->push(std::make_unique<FakeStream>(stream));
        const auto factory = std::make_shared<FactoryState>();
        pipe::PipeHost host{std::move(listener),
            std::make_shared<RecordingFactory>(factory)};
        std::mutex order_mutex;
        std::condition_variable order_changed;
        bool entered{};
        bool release{};
        bool self_join_returned{};
        {
            std::lock_guard lock(factory->mutex);
            factory->action = [&] {
                {
                    std::unique_lock order_lock(order_mutex);
                    entered = true;
                    order_changed.notify_all();
                    if (external_first)
                        order_changed.wait(order_lock, [&] { return release; });
                }
                host.stop();
                host.join();
                {
                    std::lock_guard order_lock(order_mutex);
                    self_join_returned = true;
                    order_changed.notify_all();
                }
            };
        }
        check(host.start(), "self-join lifecycle host must start");
        std::thread external;
        if (external_first) {
            {
                std::unique_lock lock(order_mutex);
                order_changed.wait_for(lock, 2s, [&] { return entered; });
            }
            external = std::thread([&] {
                host.stop();
                host.join();
            });
            {
                std::lock_guard lock(order_mutex);
                release = true;
                order_changed.notify_all();
            }
        } else {
            check(wait_until([&] {
                std::lock_guard lock(order_mutex);
                return self_join_returned;
            }), "worker self-join must return without deadlock");
            external = std::thread([&] { host.join(); });
        }
        external.join();
        check(host.state() == pipe::PipeHostState::stopped
                  && host.stats().active == 0,
              "external join must finish every worker after either join order");
    }
}

void test_error_names_are_stable()
{
    using enum pipe::PipeHostError;
    check(pipe::pipe_host_error_name(first_frame_not_json) == "first_frame_not_json"
              && pipe::pipe_host_error_name(duplicate_open_field)
                  == "duplicate_open_field"
              && pipe::pipe_host_error_name(atomic_write_too_large)
                  == "atomic_write_too_large"
              && pipe::pipe_host_error_name(write_failed) == "write_failed",
          "host errors must retain stable non-sensitive names");
}

}  // namespace

int main()
{
    test_bounded_open_codec_and_inventory();
    test_fragmented_open_coalesced_business_and_atomic_batch();
    test_protocol_and_handler_failures_are_terminal();
    test_partial_write_and_connection_limit_close_and_join();
    test_open_timeout_and_hard_write_limit();
    test_absolute_open_deadline_rejects_drip_feed();
    test_handler_self_join_and_external_join_orders();
    test_error_names_are_stable();
    if (failures != 0) {
        std::cerr << failures << " pipe host test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "pipe host tests passed\n";
    return EXIT_SUCCESS;
}
