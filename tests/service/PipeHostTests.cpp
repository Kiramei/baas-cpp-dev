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
#include <stop_token>
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

struct ReadBarrier {
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t arrived{};
    std::size_t target{};
};

struct WriteRaceGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool io_entered{};
    bool retry_attempted{};
};

struct StreamState {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<bpip::Bytes> reads;
    std::deque<std::chrono::milliseconds> read_delays;
    std::vector<bpip::Bytes> writes;
    std::size_t read_offset{};
    std::size_t read_calls{};
    std::size_t barrier_read_call{};
    std::shared_ptr<ReadBarrier> read_barrier;
    std::shared_ptr<WriteRaceGate> write_race_gate;
    bool block_when_empty{};
    bool closed{};
    bool partial_write{};
    std::size_t partial_write_on_call{};
    std::size_t throw_after_partial_on_call{};
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
        ++state_->read_calls;
        if (state_->read_barrier
            && state_->read_calls == state_->barrier_read_call) {
            const auto barrier = state_->read_barrier;
            lock.unlock();
            std::unique_lock barrier_lock(barrier->mutex);
            ++barrier->arrived;
            barrier->changed.notify_all();
            barrier->changed.wait_for(barrier_lock, 2s, [&] {
                return barrier->arrived >= barrier->target;
            });
            barrier_lock.unlock();
            lock.lock();
            if (state_->closed) return {0, true, false, false};
        }
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
        const auto partial = state_->partial_write
            || (state_->partial_write_on_call != 0
                && state_->writes.size() + 1 == state_->partial_write_on_call)
            || (state_->throw_after_partial_on_call != 0
                && state_->writes.size() + 1 == state_->throw_after_partial_on_call);
        const auto count = partial && !input.empty()
            ? input.size() - 1 : input.size();
        state_->writes.emplace_back(input.begin(), input.begin()
            + static_cast<std::ptrdiff_t>(count));
        if (state_->throw_after_partial_on_call != 0
            && state_->writes.size() == state_->throw_after_partial_on_call) {
            if (state_->write_race_gate) {
                const auto gate = state_->write_race_gate;
                std::unique_lock gate_lock(gate->mutex);
                gate->io_entered = true;
                gate->changed.notify_all();
                gate->changed.wait_for(gate_lock, 2s, [&] {
                    return gate->retry_attempted;
                });
            }
            throw std::runtime_error("write threw after partial visibility");
        }
        return {count, false, partial, false};
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
    std::function<void(std::stop_token)> action;
    std::function<void(std::stop_token)> factory_action;
    pipe::PipeHostError last_write_error{pipe::PipeHostError::none};
    std::size_t emit_payload_size{};
    bool retry_after_write_failure{};
    bool use_write_frame{};
    bool concurrent_retry_during_throw{};
    std::shared_ptr<WriteRaceGate> write_race_gate;
    pipe::PipeHostError second_write_error{pipe::PipeHostError::none};
    bool callback_entered{};
    bool cancellation_observed{};
};

class RecordingHandler final : public pipe::PipeChannelHandler {
public:
    explicit RecordingHandler(std::shared_ptr<FactoryState> state)
        : state_(std::move(state))
    {}

    pipe::PipeHandlerResult on_frame(
        const bpip::Frame&, pipe::PipeConnectionWriter& writer,
        const std::stop_token stop_token) override
    {
        bool throw_now{};
        bool emit{};
        std::function<void(std::stop_token)> action;
        std::size_t emit_payload_size{};
        bool retry_after_write_failure{};
        bool use_write_frame{};
        bool concurrent_retry{};
        std::shared_ptr<WriteRaceGate> write_race_gate;
        {
            std::lock_guard lock(state_->mutex);
            ++state_->frames;
            throw_now = state_->throw_on_frame;
            emit = state_->emit_batch;
            action = state_->action;
            emit_payload_size = state_->emit_payload_size;
            retry_after_write_failure = state_->retry_after_write_failure;
            use_write_frame = state_->use_write_frame;
            concurrent_retry = state_->concurrent_retry_during_throw;
            write_race_gate = state_->write_race_gate;
        }
        if (action) action(stop_token);
        if (throw_now) throw std::runtime_error("handler failure");
        if (emit) {
            std::array output{
                bpip::Frame{bpip::kind_value(bpip::FrameKind::json), {}},
                bpip::Frame{bpip::kind_value(bpip::FrameKind::bytes), {}},
            };
            if (emit_payload_size != 0) {
                output[0].payload.resize(emit_payload_size, std::byte{0x78});
            } else {
                output[0].payload.assign(bytes(R"({"type":"reply"})").begin(),
                    bytes(R"({"type":"reply"})").end());
            }
            pipe::PipeHostError concurrent_error{pipe::PipeHostError::none};
            std::thread concurrent_retry_thread;
            if (concurrent_retry) {
                concurrent_retry_thread = std::thread([&] {
                    std::unique_lock gate_lock(write_race_gate->mutex);
                    write_race_gate->changed.wait_for(gate_lock, 2s, [&] {
                        return write_race_gate->io_entered;
                    });
                    write_race_gate->retry_attempted = true;
                    write_race_gate->changed.notify_all();
                    gate_lock.unlock();
                    concurrent_error = writer.write_frame(
                        bpip::FrameKind::json, bytes("{}"));
                });
            }
            const auto error = use_write_frame
                ? writer.write_frame(bpip::FrameKind::json, output[0].payload)
                : writer.write_batch(output);
            if (concurrent_retry_thread.joinable()) concurrent_retry_thread.join();
            const auto second_error = concurrent_retry ? concurrent_error
                : error != pipe::PipeHostError::none
                    && retry_after_write_failure
                ? writer.write_frame(bpip::FrameKind::json, bytes("{}"))
                : pipe::PipeHostError::none;
            {
                std::lock_guard lock(state_->mutex);
                state_->last_write_error = error;
                state_->second_write_error = second_error;
            }
            return {pipe::PipeHandlerAction::continue_connection, error};
        }
        return {};
    }

    void on_close(std::stop_token) noexcept override
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
        const pipe::PipeOpenRequest& request,
        const std::stop_token stop_token) override
    {
        std::function<void(std::stop_token)> action;
        {
            std::lock_guard lock(state_->mutex);
            state_->requests.push_back(request);
            action = state_->factory_action;
        }
        if (action) action(stop_token);
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
        auto never_started_listener = std::make_unique<FakeListener>();
        pipe::PipeHost never_started{std::move(never_started_listener),
            std::make_shared<RecordingFactory>(std::make_shared<FactoryState>())};
        never_started.stop();
        never_started.join();
        check(!never_started.start(),
              "stop before start must consume the one-shot listener");
    }
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
        {
            std::lock_guard lock(stream->mutex);
            check(stream->writes.size() == 1,
                  "partial open_ok must poison the writer and forbid ERROR+CLOSE retry");
        }
        check(host.state() == pipe::PipeHostState::stopped,
              "stop and join must reach a terminal stopped state");
        check(!host.start(), "a stopped PipeHost must reject restart as one-shot");
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

void test_partial_handler_write_is_never_followed_by_close()
{
    for (const bool throw_after_partial : {false, true}) {
        auto listener = std::make_unique<FakeListener>();
        auto* view = listener.get();
        const auto stream = std::make_shared<StreamState>();
        if (throw_after_partial) stream->throw_after_partial_on_call = 2;
        else stream->partial_write_on_call = 2;
        stream->reads.push_back(open_and(
            R"({"type":"open","channel":"sync","name":"partial-handler"})",
            {frame(bpip::FrameKind::json, "{}") }));
        view->push(std::make_unique<FakeStream>(stream));
        const auto factory = std::make_shared<FactoryState>();
        factory->emit_batch = true;
        factory->retry_after_write_failure = true;
        if (throw_after_partial) {
            const auto gate = std::make_shared<WriteRaceGate>();
            stream->write_race_gate = gate;
            factory->write_race_gate = gate;
            factory->concurrent_retry_during_throw = true;
        }
        pipe::PipeHost host{std::move(listener),
            std::make_shared<RecordingFactory>(factory)};
        check(host.start(), "partial handler-write host must start");
        check(wait_until([&] { return host.stats().completed == 1; }),
              "partial or throwing handler output must close the connection");
        host.stop();
        host.join();
        {
            std::lock_guard lock(stream->mutex);
            check(stream->writes.size() == 2,
                  "poisoned handler retry and host terminal path must issue zero writes");
        }
        std::lock_guard lock(factory->mutex);
        check(factory->last_write_error == pipe::PipeHostError::write_failed
                  && factory->second_write_error == pipe::PipeHostError::write_failed,
              "partial and throw-after-partial writes must permanently poison writer");
    }
}

void test_declared_oversized_open_is_rejected_from_header()
{
    auto listener = std::make_unique<FakeListener>();
    auto* view = listener.get();
    const auto stream = std::make_shared<StreamState>();
    const auto oversized = bpip::encode_header(
        bpip::FrameKind::json, 1U * 1'024U * 1'024U);
    stream->reads.push_back({oversized.header.begin(), oversized.header.end()});
    stream->block_when_empty = true;
    view->push(std::make_unique<FakeStream>(stream));
    const auto factory = std::make_shared<FactoryState>();
    pipe::PipeHost host{std::move(listener),
        std::make_shared<RecordingFactory>(factory)};
    check(host.start(), "oversized-open host must start");
    check(wait_until([&] { return host.stats().completed == 1; }),
          "declared oversized open must fail after its ten-byte header");
    host.stop();
    host.join();
    std::lock_guard factory_lock(factory->mutex);
    check(factory->requests.empty(),
          "oversized declared open must never allocate payload or reach factory");
}

void test_global_retained_byte_budgets()
{
    auto listener = std::make_unique<FakeListener>();
    auto* view = listener.get();
    std::array<std::shared_ptr<StreamState>, 2> streams{
        std::make_shared<StreamState>(), std::make_shared<StreamState>()};
    const auto barrier = std::make_shared<ReadBarrier>();
    barrier->target = streams.size();
    const auto declared = bpip::encode_header(bpip::FrameKind::json, 100);
    for (std::size_t index = 0; index < streams.size(); ++index) {
        streams[index]->reads.push_back(open_and(
            std::string{"{\"type\":\"open\",\"channel\":\"remote\",\"name\":\""}
                + std::to_string(index) + "\"}"));
        bpip::Bytes partial{declared.header.begin(), declared.header.end()};
        partial.push_back(std::byte{0x7b});
        streams[index]->reads.push_back(std::move(partial));
        streams[index]->read_barrier = barrier;
        streams[index]->barrier_read_call = 2;
        streams[index]->block_when_empty = true;
        view->push(std::make_unique<FakeStream>(streams[index]));
    }
    pipe::PipeHostLimits limits;
    limits.max_connections = 2;
    limits.max_total_ingress_retained_bytes = 150;
    limits.max_total_egress_retained_bytes = 128;
    const auto factory = std::make_shared<FactoryState>();
    pipe::PipeHost host{std::move(listener),
        std::make_shared<RecordingFactory>(factory), limits};
    check(host.start(), "aggregate-budget host must start");
    const auto observed_ingress_rejection = wait_until(
        [&] { return host.stats().ingress_budget_rejections == 1; });
    if (!observed_ingress_rejection) {
        const auto observed = host.stats();
        std::cerr << "budget diagnostics: accepted=" << observed.accepted
                  << " completed=" << observed.completed
                  << " retained=" << observed.ingress_retained_bytes
                  << " peak=" << observed.peak_ingress_retained_bytes
                  << " rejects=" << observed.ingress_budget_rejections << '\n';
    }
    check(observed_ingress_rejection,
          "two declared frames must contend for one global ingress budget");
    host.stop();
    host.join();
    const auto stats = host.stats();
    check(stats.peak_ingress_retained_bytes <= 150
              && stats.ingress_retained_bytes == 0,
          "aggregate ingress reservation must never exceed its host-wide cap");

    auto egress_listener = std::make_unique<FakeListener>();
    auto* egress_view = egress_listener.get();
    const auto egress_stream = std::make_shared<StreamState>();
    egress_stream->reads.push_back(open_and(
        R"({"type":"open","channel":"provider","name":"egress"})",
        {frame(bpip::FrameKind::json, "{}") }));
    egress_view->push(std::make_unique<FakeStream>(egress_stream));
    pipe::PipeHostLimits egress_limits;
    egress_limits.max_connections = 1;
    egress_limits.max_total_egress_retained_bytes = 64;
    const auto egress_factory = std::make_shared<FactoryState>();
    egress_factory->emit_batch = true;
    egress_factory->emit_payload_size = 1U * 1'024U * 1'024U;
    egress_factory->use_write_frame = true;
    pipe::PipeHost egress_host{std::move(egress_listener),
        std::make_shared<RecordingFactory>(egress_factory), egress_limits};
    check(egress_host.start(), "egress-budget host must start");
    check(wait_until([&] { return egress_host.stats().completed == 1; }),
          "oversized aggregate egress reservation must fail closed");
    egress_host.stop();
    egress_host.join();
    {
        std::lock_guard lock(egress_factory->mutex);
        check(egress_factory->last_write_error
                  == pipe::PipeHostError::egress_budget_exhausted,
              "handler must propagate the host-wide egress budget error");
    }
    check(egress_host.stats().peak_egress_retained_bytes <= 64
              && egress_host.stats().egress_retained_bytes == 0,
          "aggregate egress reservation must be released on every path");
}

void test_stop_token_cancels_factory_and_handler_callbacks()
{
    for (const bool block_factory : {true, false}) {
        auto listener = std::make_unique<FakeListener>();
        auto* view = listener.get();
        const auto stream = std::make_shared<StreamState>();
        stream->reads.push_back(open_and(
            R"({"type":"open","channel":"trigger","name":"cancel"})",
            block_factory ? std::vector<bpip::Bytes>{}
                          : std::vector<bpip::Bytes>{frame(
                              bpip::FrameKind::json, "{}")}));
        view->push(std::make_unique<FakeStream>(stream));
        const auto factory = std::make_shared<FactoryState>();
        const auto blocking = [factory](const std::stop_token token) {
            {
                std::lock_guard lock(factory->mutex);
                factory->callback_entered = true;
            }
            while (!token.stop_requested()) std::this_thread::sleep_for(1ms);
            std::lock_guard lock(factory->mutex);
            factory->cancellation_observed = true;
        };
        if (block_factory) factory->factory_action = blocking;
        else factory->action = blocking;
        pipe::PipeHost host{std::move(listener),
            std::make_shared<RecordingFactory>(factory)};
        check(host.start(), "cancellable callback host must start");
        check(wait_until([&] {
            std::lock_guard lock(factory->mutex);
            return factory->callback_entered;
        }), "factory/handler callback must enter before cancellation");
        host.stop();
        host.join();
        std::lock_guard lock(factory->mutex);
        check(factory->cancellation_observed,
              "factory and handler callbacks must cooperatively observe stop_token");
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
            factory->action = [&](std::stop_token) {
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
              && pipe::pipe_host_error_name(ingress_budget_exhausted)
                  == "ingress_budget_exhausted"
              && pipe::pipe_host_error_name(egress_budget_exhausted)
                  == "egress_budget_exhausted"
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
    test_partial_handler_write_is_never_followed_by_close();
    test_declared_oversized_open_is_rejected_from_header();
    test_global_retained_byte_budgets();
    test_open_timeout_and_hard_write_limit();
    test_absolute_open_deadline_rejects_drip_feed();
    test_stop_token_cancels_factory_and_handler_callbacks();
    test_handler_self_join_and_external_join_orders();
    test_error_names_are_stable();
    if (failures != 0) {
        std::cerr << failures << " pipe host test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "pipe host tests passed\n";
    return EXIT_SUCCESS;
}
