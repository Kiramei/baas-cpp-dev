#include "service/websocket/WebSocketOwner.h"
#include "service/websocket/WebSocketHandshake.h"

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace service_ws = baas::service::websocket;

namespace {

using namespace std::chrono_literals;

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <class Predicate>
[[nodiscard]] bool wait_until(
    Predicate predicate,
    const std::chrono::milliseconds timeout = 500ms
)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

[[nodiscard]] service_ws::Frame frame(
    const service_ws::FrameKind kind,
    std::string payload
)
{
    return {kind, std::move(payload)};
}

[[nodiscard]] service_ws::OutboundBatch batch(
    std::initializer_list<service_ws::Frame> frames
)
{
    return {std::vector<service_ws::Frame>{frames}};
}

[[nodiscard]] service_ws::WebSocketOwnerConfig test_config()
{
    service_ws::WebSocketOwnerConfig config;
    config.max_connections = service_ws::websocket_platform_min_connections;
    config.heartbeat_interval = 20ms;
    config.handshake_timeout = 100ms;
    config.close_grace = 2ms;
    config.shutdown_timeout = 250ms;
    return config;
}

class FakeTransport final : public service_ws::Transport {
public:
    [[nodiscard]] bool read(service_ws::Frame& result) override
    {
        const auto concurrent = readers_.fetch_add(1) + 1;
        auto maximum = max_readers_.load();
        while (maximum < concurrent
            && !max_readers_.compare_exchange_weak(maximum, concurrent)) {}
        struct Exit final {
            std::atomic<int>& readers;
            ~Exit() { readers.fetch_sub(1); }
        } exit{readers_};

        std::unique_lock<std::mutex> lock{mutex_};
        changed_.wait(lock, [&] { return interrupted_ || !incoming_.empty(); });
        if (interrupted_) return false;
        result = std::move(incoming_.front());
        incoming_.pop_front();
        return true;
    }

    [[nodiscard]] bool write(const service_ws::Frame& value) override
    {
        std::unique_lock<std::mutex> lock{mutex_};
        ++write_attempts_;
        changed_.notify_all();
        if (block_writes_) {
            changed_.wait(lock, [&] { return interrupted_; });
        }
        if (interrupted_ || fail_writes_) return false;
        written_.push_back(value);
        changed_.notify_all();
        return true;
    }

    [[nodiscard]] bool request_close(
        const std::uint16_t status,
        const std::string_view reason
    ) noexcept override
    {
        try {
            std::lock_guard<std::mutex> lock{mutex_};
            close_codes_.push_back(status);
            close_reasons_.emplace_back(reason);
            changed_.notify_all();
            return true;
        } catch (...) {
            return false;
        }
    }

    void interrupt() noexcept override
    {
        try {
            std::lock_guard<std::mutex> lock{mutex_};
            interrupted_ = true;
            ++interrupts_;
            changed_.notify_all();
        } catch (...) {}
    }

    void push(service_ws::Frame value)
    {
        std::lock_guard<std::mutex> lock{mutex_};
        incoming_.push_back(std::move(value));
        changed_.notify_all();
    }

    void block_writes(const bool value)
    {
        std::lock_guard<std::mutex> lock{mutex_};
        block_writes_ = value;
    }

    void fail_writes(const bool value)
    {
        std::lock_guard<std::mutex> lock{mutex_};
        fail_writes_ = value;
    }

    [[nodiscard]] std::vector<service_ws::Frame> written() const
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return written_;
    }

    [[nodiscard]] std::vector<std::uint16_t> close_codes() const
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return close_codes_;
    }

    [[nodiscard]] std::size_t interrupts() const
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return interrupts_;
    }

    [[nodiscard]] std::size_t write_attempts() const
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return write_attempts_;
    }

    [[nodiscard]] int max_readers() const noexcept { return max_readers_.load(); }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<service_ws::Frame> incoming_;
    std::vector<service_ws::Frame> written_;
    std::vector<std::uint16_t> close_codes_;
    std::vector<std::string> close_reasons_;
    std::atomic<int> readers_{0};
    std::atomic<int> max_readers_{0};
    std::size_t interrupts_{};
    std::size_t write_attempts_{};
    bool interrupted_{};
    bool block_writes_{};
    bool fail_writes_{};
};

class FunctionDriver final : public service_ws::SessionDriver {
public:
    using Callback = std::function<service_ws::DriverResult(service_ws::Frame)>;
    using Heartbeat = std::function<service_ws::DriverResult()>;

    FunctionDriver(Callback input, Heartbeat heartbeat = {})
        : input_(std::move(input)), heartbeat_(std::move(heartbeat))
    {}

    [[nodiscard]] service_ws::DriverResult input(
        service_ws::Frame value,
        std::stop_token
    ) override
    {
        return input_ ? input_(std::move(value)) : service_ws::DriverResult{};
    }

    [[nodiscard]] service_ws::DriverResult heartbeat(std::stop_token) override
    {
        return heartbeat_ ? heartbeat_() : service_ws::DriverResult{
            service_ws::SessionPhase::streaming,
            service_ws::TerminalAction::none,
            {},
        };
    }

    void closed() noexcept override { closed_.store(true); }
    [[nodiscard]] bool was_closed() const noexcept { return closed_.load(); }

private:
    Callback input_;
    Heartbeat heartbeat_;
    std::atomic<bool> closed_{false};
};

class FunctionFactory final : public service_ws::SessionFactory {
public:
    using Builder = std::function<std::unique_ptr<service_ws::SessionDriver>()>;

    explicit FunctionFactory(Builder builder) : builder_(std::move(builder)) {}

    [[nodiscard]] std::unique_ptr<service_ws::SessionDriver> create(
        const service_ws::RequestMetadata& request,
        std::shared_ptr<service_ws::OutboundSink> outbound,
        std::stop_token
    ) override
    {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            ++creates_;
            last_request_ = request;
            outbound_ = std::move(outbound);
        }
        changed_.notify_all();
        return builder_ ? builder_() : nullptr;
    }

    [[nodiscard]] std::size_t creates() const
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return creates_;
    }

    [[nodiscard]] std::shared_ptr<service_ws::OutboundSink> sink() const
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return outbound_;
    }

private:
    Builder builder_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::size_t creates_{};
    service_ws::RequestMetadata last_request_;
    std::shared_ptr<service_ws::OutboundSink> outbound_;
};

[[nodiscard]] service_ws::RequestMetadata metadata()
{
    service_ws::RequestMetadata value;
    value.channel = service_ws::Channel::control;
    value.path = "/ws/control";
    return value;
}

void test_origin_precedes_factory_and_missing_origin_compatibility()
{
    auto factory = std::make_shared<FunctionFactory>([] {
        return std::make_unique<FunctionDriver>(nullptr);
    });
    service_ws::WebSocketOwner owner{test_config(), {}, factory};
    FakeTransport rejected;
    owner.serve("https://evil.example", false, metadata(), rejected);
    check(factory->creates() == 0, "Origin rejection must precede SessionFactory");
    check(rejected.close_codes() == std::vector<std::uint16_t>{4403},
          "disallowed Origin must request close 4403");
    check(rejected.max_readers() == 0, "Origin rejection must not read client_hello");

    FakeTransport native;
    std::jthread session([&] { owner.serve(std::nullopt, false, metadata(), native); });
    check(wait_until([&] { return factory->creates() == 1; }),
          "missing Origin must retain native-loopback compatibility");
    owner.begin_shutdown();
    check(owner.finish_shutdown(), "native session must drain on shutdown");
}

void test_httplib_pre_routing_adapter()
{
    httplib::Request request;
    request.method = "GET";
    request.version = "HTTP/1.1";
    request.target = "/ws/control?x=1";
    request.path = "/ws/control";
    request.headers.emplace("Host", "127.0.0.1:8190");
    request.headers.emplace("Upgrade", "websocket");
    request.headers.emplace("Connection", "Upgrade");
    request.headers.emplace("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    request.headers.emplace("Sec-WebSocket-Version", "13");

    auto result = service_ws::evaluate_httplib_handshake_request(request, true);
    check(result.handshake.error
              == service_ws::HandshakeError::non_canonical_request_target
              && result.rejection_status == 400
              && !result.advertise_supported_version,
          "the httplib adapter must preserve raw query targets for pre-routing rejection");

    request.target = "/ws/%63ontrol";
    result = service_ws::evaluate_httplib_handshake_request(request, true);
    check(result.handshake.error
              == service_ws::HandshakeError::non_canonical_request_target,
          "the httplib adapter must preserve raw percent-encoded targets");

    request.target = request.path;
    auto version = request.headers.find("Sec-WebSocket-Version");
    version->second = "12";
    result = service_ws::evaluate_httplib_handshake_request(request, true);
    check(result.handshake.error
              == service_ws::HandshakeError::websocket_version_unsupported
              && result.rejection_status == 426
              && result.advertise_supported_version,
          "unsupported versions must map to 426 and advertise version 13");
}

void test_terminal_mapping_and_single_reader()
{
    for (const auto [action, code] : {
        std::pair{service_ws::TerminalAction::authentication_failed, std::uint16_t{4401}},
        std::pair{service_ws::TerminalAction::protocol_failed, std::uint16_t{4401}},
        std::pair{service_ws::TerminalAction::internal_error, std::uint16_t{1011}},
    }) {
        auto factory = std::make_shared<FunctionFactory>([action] {
            return std::make_unique<FunctionDriver>([action](service_ws::Frame) {
                return service_ws::DriverResult{
                    service_ws::SessionPhase::handshaking, action, {},
                };
            });
        });
        service_ws::WebSocketOwner owner{test_config(), {}, factory};
        FakeTransport transport;
        transport.push(frame(service_ws::FrameKind::text, "hello"));
        owner.serve(std::nullopt, false, metadata(), transport);
        check(transport.close_codes() == std::vector<std::uint16_t>{code},
              "driver terminal action must map to its stable close code");
        check(transport.max_readers() == 1, "owner must have exactly one reader");
        check(transport.interrupts() >= 1, "close grace must end with interrupt");
    }
}

void test_typed_atomic_batches_drain_before_terminal()
{
    auto factory = std::make_shared<FunctionFactory>([] {
        return std::make_unique<FunctionDriver>([](service_ws::Frame) {
            service_ws::DriverResult result;
            result.phase = service_ws::SessionPhase::streaming;
            result.terminal = service_ws::TerminalAction::complete;
            result.batches = {
                batch({frame(service_ws::FrameKind::text, "text")}),
                batch({frame(service_ws::FrameKind::binary, "binary")}),
                batch({
                    frame(service_ws::FrameKind::text, "json"),
                    frame(service_ws::FrameKind::binary, "adjacent"),
                }),
            };
            return result;
        });
    });
    service_ws::WebSocketOwner owner{test_config(), {}, factory};
    FakeTransport transport;
    transport.push(frame(service_ws::FrameKind::text, "start"));
    owner.serve(std::nullopt, false, metadata(), transport);
    const auto written = transport.written();
    check(written.size() == 4, "terminal marker must drain preceding batches");
    if (written.size() == 4) {
        check(written[0].kind == service_ws::FrameKind::text
                  && written[0].payload == "text",
              "text-only batch must stay text");
        check(written[1].kind == service_ws::FrameKind::binary
                  && written[1].payload == "binary",
              "binary-only batch must stay binary");
        check(written[2].payload == "json" && written[3].payload == "adjacent",
              "JSON and binary frames must remain adjacent");
    }
    check(transport.close_codes() == std::vector<std::uint16_t>{1000},
          "complete must follow drained output with close 1000");
}

void test_async_sink_bounds_and_phase_regression()
{
    auto config = test_config();
    config.max_frame_bytes = 4;
    config.max_queued_bytes = 8;
    config.max_global_queued_bytes = 8;
    config.max_queued_batches = 2;
    auto factory = std::make_shared<FunctionFactory>([] {
        return std::make_unique<FunctionDriver>([](service_ws::Frame) {
            return service_ws::DriverResult{
                service_ws::SessionPhase::streaming,
                service_ws::TerminalAction::none,
                {},
            };
        });
    });
    service_ws::WebSocketOwner owner{config, {}, factory};
    FakeTransport transport;
    // Stay within this test's deliberately tiny four-byte frame cap so the
    // driver can enqueue the blocked outbound batch.
    transport.push(frame(service_ws::FrameKind::text, "go"));
    std::jthread session([&] { owner.serve(std::nullopt, false, metadata(), transport); });
    check(wait_until([&] { return static_cast<bool>(factory->sink()); }),
          "factory must receive the asynchronous sink");
    const auto sink = factory->sink();
    check(sink->enqueue({}) == service_ws::EnqueueResult::empty_batch,
          "empty batch must fail explicitly");
    check(sink->enqueue(batch({
              frame(service_ws::FrameKind::text, "a"),
              frame(service_ws::FrameKind::binary, "b"),
              frame(service_ws::FrameKind::binary, "c"),
          })) == service_ws::EnqueueResult::too_many_frames,
          "three-frame batch must exceed the atomic batch bound");
    check(sink->enqueue(batch({frame(service_ws::FrameKind::binary, "12345")}))
              == service_ws::EnqueueResult::frame_too_large,
          "64 MiB-equivalent maximum plus one must fail before enqueue");
    sink->terminate(service_ws::TerminalAction::complete);
    check(wait_until([&] { return transport.interrupts() > 0; }),
          "async terminal sink must close and interrupt the reader");
}

void test_capacity_and_bounded_shutdown()
{
    auto factory = std::make_shared<FunctionFactory>([] {
        return std::make_unique<FunctionDriver>(nullptr);
    });
    service_ws::WebSocketOwner owner{test_config(), {}, factory};
    const auto capacity = service_ws::websocket_platform_min_connections;
    std::vector<std::unique_ptr<FakeTransport>> transports;
    std::vector<std::jthread> sessions;
    for (std::size_t index = 0; index < capacity; ++index) {
        transports.push_back(std::make_unique<FakeTransport>());
        auto* transport = transports.back().get();
        sessions.emplace_back([&, transport] {
            owner.serve(std::nullopt, false, metadata(), *transport);
        });
    }
    check(wait_until([&] { return owner.stats().active_connections == capacity; }),
          "configured steady-state capacity must be reachable");
    FakeTransport overflow;
    owner.serve(std::nullopt, false, metadata(), overflow);
    check(overflow.close_codes() == std::vector<std::uint16_t>{1013},
          "capacity plus one must fail quickly with 1013");
    check(owner.stats().capacity_rejections == 1,
          "capacity saturation must be observable");
    const auto started = std::chrono::steady_clock::now();
    owner.begin_shutdown();
    check(owner.finish_shutdown(), "shutdown must interrupt every stalled reader");
    check(std::chrono::steady_clock::now() - started < test_config().shutdown_timeout,
          "shutdown must finish inside its configured bound");
}

void test_active_write_retains_global_budget_until_completion()
{
    auto config = test_config();
    config.max_frame_bytes = 4;
    config.max_queued_bytes = 8;
    config.max_global_queued_bytes = 8;
    auto factory = std::make_shared<FunctionFactory>([] {
        return std::make_unique<FunctionDriver>([](service_ws::Frame) {
            return service_ws::DriverResult{
                service_ws::SessionPhase::streaming,
                service_ws::TerminalAction::none,
                {batch({frame(service_ws::FrameKind::binary, "1234")})},
            };
        });
    });
    service_ws::WebSocketOwner owner{config, {}, factory};
    FakeTransport transport;
    transport.block_writes(true);
    // Stay within this test's deliberately tiny four-byte frame cap so the
    // driver can enqueue the blocked outbound batch.
    transport.push(frame(service_ws::FrameKind::text, "go"));
    std::jthread session([&] { owner.serve(std::nullopt, false, metadata(), transport); });
    check(wait_until([&] { return transport.write_attempts() == 1; }),
          "writer must pop the batch and enter the blocked transport write");
    check(owner.stats().global_queued_bytes == 4,
          "an active popped write must remain charged to the global budget");
    owner.begin_shutdown();
    check(owner.finish_shutdown(), "shutdown must interrupt the blocked active write");
    session.join();
    check(owner.stats().global_queued_bytes == 0,
          "active charge must be released exactly once after interrupted write completion");
}

void test_terminal_write_failure_releases_queued_budget()
{
    auto config = test_config();
    config.max_frame_bytes = 4;
    config.max_queued_bytes = 8;
    config.max_global_queued_bytes = 8;
    auto factory = std::make_shared<FunctionFactory>([] {
        return std::make_unique<FunctionDriver>([](service_ws::Frame) {
            return service_ws::DriverResult{
                service_ws::SessionPhase::streaming,
                service_ws::TerminalAction::complete,
                {
                    batch({frame(service_ws::FrameKind::binary, "1234")}),
                    batch({frame(service_ws::FrameKind::binary, "5678")}),
                },
            };
        });
    });
    service_ws::WebSocketOwner owner{config, {}, factory};
    FakeTransport transport;
    transport.fail_writes(true);
    transport.push(frame(service_ws::FrameKind::text, "go"));
    owner.serve(std::nullopt, false, metadata(), transport);
    check(transport.write_attempts() == 1,
          "writer failure must stop before the remaining terminal queue");
    check(owner.stats().global_queued_bytes == 0,
          "terminal writer failure must release every queued global byte");
}

void test_successful_input_wins_handshake_deadline_race()
{
    auto config = test_config();
    config.handshake_timeout = 40ms;
    auto factory = std::make_shared<FunctionFactory>([] {
        return std::make_unique<FunctionDriver>([](service_ws::Frame) {
            std::this_thread::sleep_for(80ms);
            return service_ws::DriverResult{
                service_ws::SessionPhase::streaming,
                service_ws::TerminalAction::none,
                {},
            };
        });
    });
    service_ws::WebSocketOwner owner{config, {}, factory};
    FakeTransport transport;
    transport.push(frame(service_ws::FrameKind::text, "ready"));
    std::jthread session([&] {
        owner.serve(std::nullopt, false, metadata(), transport);
    });
    check(wait_until([&] { return factory->creates() == 1; }),
          "deadline race test must create a session driver");
    std::this_thread::sleep_for(120ms);
    check(transport.close_codes().empty(),
          "input that authenticates under the driver lock must beat a stale timeout");
    check(owner.stats().handshake_timeouts == 0,
          "stale scheduler observations must not increment timeout statistics");
    owner.begin_shutdown();
    check(owner.finish_shutdown(), "deadline race session must shut down");
}

void test_handshake_deadline_and_heartbeat_tick()
{
    auto timeout_config = test_config();
    timeout_config.handshake_timeout = 40ms;
    auto timeout_factory = std::make_shared<FunctionFactory>([] {
        return std::make_unique<FunctionDriver>(nullptr);
    });
    service_ws::WebSocketOwner timeout_owner{timeout_config, {}, timeout_factory};
    FakeTransport timed_out;
    std::jthread timeout_session([&] {
        timeout_owner.serve(std::nullopt, false, metadata(), timed_out);
    });
    check(wait_until([&] { return !timed_out.close_codes().empty(); }, 250ms),
          "handshake must have a 10-second-equivalent deterministic deadline");
    check(timed_out.close_codes() == std::vector<std::uint16_t>{4401},
          "handshake timeout must map to 4401");

    auto heartbeat_factory = std::make_shared<FunctionFactory>([] {
        return std::make_unique<FunctionDriver>(
            [](service_ws::Frame) {
                return service_ws::DriverResult{
                    service_ws::SessionPhase::streaming,
                    service_ws::TerminalAction::none,
                    {},
                };
            },
            [] {
                return service_ws::DriverResult{
                    service_ws::SessionPhase::streaming,
                    service_ws::TerminalAction::none,
                    {batch({frame(service_ws::FrameKind::binary, "encrypted-heartbeat")})},
                };
            }
        );
    });
    service_ws::WebSocketOwner heartbeat_owner{test_config(), {}, heartbeat_factory};
    FakeTransport heartbeat;
    heartbeat.push(frame(service_ws::FrameKind::text, "ready"));
    std::jthread heartbeat_session([&] {
        heartbeat_owner.serve(std::nullopt, false, metadata(), heartbeat);
    });
    check(wait_until([&] { return !heartbeat.written().empty(); }, 250ms),
          "shared scheduler must call the streaming driver heartbeat hook");
    const auto output = heartbeat.written();
    check(!output.empty() && output.front().kind == service_ws::FrameKind::binary,
          "owner must not fabricate a plaintext business heartbeat");
    heartbeat_owner.begin_shutdown();
    check(heartbeat_owner.finish_shutdown(), "heartbeat session must shut down");
}

}  // namespace

int main()
{
    test_httplib_pre_routing_adapter();
    test_origin_precedes_factory_and_missing_origin_compatibility();
    test_terminal_mapping_and_single_reader();
    test_typed_atomic_batches_drain_before_terminal();
    test_async_sink_bounds_and_phase_regression();
    test_capacity_and_bounded_shutdown();
    test_active_write_retains_global_budget_until_completion();
    test_terminal_write_failure_releases_queued_budget();
    test_successful_input_wins_handshake_deadline_race();
    test_handshake_deadline_and_heartbeat_tick();
    if (failures != 0) {
        std::cerr << failures << " WebSocket owner test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "WebSocket owner tests passed\n";
    return EXIT_SUCCESS;
}
