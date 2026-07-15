#include "service/adb/ServiceAdbTransport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using baas::service::adb::AdbByteStream;
using baas::service::adb::AdbEndpoint;
using baas::service::adb::AdbStreamIoResult;
using baas::service::adb::AdbStreamOpenResult;
using baas::service::adb::AdbStreamStatus;
using baas::service::adb::AdbTransportError;
using baas::service::adb::ServiceAdbTransport;
using baas::service::adb::ServiceAdbTransportLimits;

namespace {

struct CheckFailure final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define CHECK(expression) do { if (!(expression)) throw CheckFailure(#expression); } while (false)

std::string frame(const std::string_view request)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    const auto size = request.size();
    std::string result;
    result.push_back(hex[(size >> 12U) & 0xfU]);
    result.push_back(hex[(size >> 8U) & 0xfU]);
    result.push_back(hex[(size >> 4U) & 0xfU]);
    result.push_back(hex[size & 0xfU]);
    result.append(request);
    return result;
}

std::string length_response(const std::string_view payload)
{
    return "OKAY" + frame(payload);
}

struct FakeControl {
    std::mutex mutex;
    std::condition_variable changed;
    std::string input;
    std::size_t read_offset{};
    std::size_t max_read{std::numeric_limits<std::size_t>::max()};
    std::size_t max_write{std::numeric_limits<std::size_t>::max()};
    AdbStreamStatus terminal{AdbStreamStatus::eof};
    bool block_reads{};
    bool read_entered{};
    bool closed{};
    std::string written;
};

class FakeStream final : public AdbByteStream {
public:
    explicit FakeStream(std::shared_ptr<FakeControl> control)
        : control_(std::move(control))
    {}

    AdbStreamIoResult read_some(
        const std::span<std::byte> output, const Deadline deadline,
        const std::stop_token stop) override
    {
        std::unique_lock lock(control_->mutex);
        control_->read_entered = true;
        control_->changed.notify_all();
        while (control_->block_reads && !control_->closed
               && !stop.stop_requested()
               && std::chrono::steady_clock::now() < deadline) {
            control_->changed.wait_for(lock, 2ms);
        }
        if (control_->closed) return {0, AdbStreamStatus::error};
        if (stop.stop_requested()) return {0, AdbStreamStatus::cancelled};
        if (control_->block_reads) return {0, AdbStreamStatus::timeout};
        if (control_->read_offset == control_->input.size()) {
            return {0, control_->terminal};
        }
        const auto count = std::min({output.size(), control_->max_read,
                                     control_->input.size() - control_->read_offset});
        std::memcpy(output.data(), control_->input.data() + control_->read_offset, count);
        control_->read_offset += count;
        return {count, AdbStreamStatus::ok};
    }

    AdbStreamIoResult write_some(
        const std::span<const std::byte> input, const Deadline,
        const std::stop_token stop) override
    {
        std::lock_guard lock(control_->mutex);
        if (control_->closed) return {0, AdbStreamStatus::error};
        if (stop.stop_requested()) return {0, AdbStreamStatus::cancelled};
        const auto count = std::min(input.size(), control_->max_write);
        control_->written.append(reinterpret_cast<const char*>(input.data()), count);
        return {count, AdbStreamStatus::ok};
    }

    void close() noexcept override
    {
        std::lock_guard lock(control_->mutex);
        control_->closed = true;
        control_->changed.notify_all();
    }

private:
    std::shared_ptr<FakeControl> control_;
};

struct FakeFactory {
    struct State {
        std::mutex mutex;
        std::deque<std::shared_ptr<FakeControl>> streams;
        std::vector<AdbEndpoint> endpoints;
    };
    std::shared_ptr<State> state{std::make_shared<State>()};

    void push(const std::shared_ptr<FakeControl>& control)
    {
        std::lock_guard lock(state->mutex);
        state->streams.push_back(control);
    }

    auto callback() const
    {
        const auto shared = state;
        return [shared](const AdbEndpoint& endpoint, AdbByteStream::Deadline,
                        std::stop_token) -> AdbStreamOpenResult {
            std::lock_guard lock(shared->mutex);
            shared->endpoints.push_back(endpoint);
            if (shared->streams.empty()) {
                return {nullptr, AdbTransportError::connection_failed,
                        "no fake stream"};
            }
            auto control = shared->streams.front();
            shared->streams.pop_front();
            return {std::make_unique<FakeStream>(std::move(control)),
                    AdbTransportError::none, {}};
        };
    }
};

std::shared_ptr<FakeControl> fake(std::string input, const std::size_t fragment = 1)
{
    auto control = std::make_shared<FakeControl>();
    control->input = std::move(input);
    control->max_read = fragment;
    control->max_write = fragment;
    return control;
}

void fragmented_host_query_and_endpoint()
{
    FakeFactory factory;
    const auto control = fake(length_response("emulator-5556 device product:test\n"));
    factory.push(control);
    ServiceAdbTransport transport({"10.0.0.2", 15037}, factory.callback());
    const auto result = transport.devices_long();
    CHECK(result);
    CHECK(*result.value == "emulator-5556 device product:test\n");
    CHECK(control->written == frame("host:devices-l"));
    CHECK(factory.state->endpoints.size() == 1);
    CHECK(factory.state->endpoints.front().host == "10.0.0.2");
    CHECK(factory.state->endpoints.front().port == 15037);
    ServiceAdbTransport defaults({}, [](
        const AdbEndpoint&, AdbByteStream::Deadline, std::stop_token) {
            return AdbStreamOpenResult{
                nullptr, AdbTransportError::connection_failed, {}};
        });
    CHECK(defaults.endpoint().host == "127.0.0.1");
    CHECK(defaults.endpoint().port == 5037);
}

void strict_lengths_fail_and_caps()
{
    {
        FakeFactory factory;
        factory.push(fake("OKAY00G1"));
        ServiceAdbTransport transport({}, factory.callback());
        CHECK(transport.devices_long().error == AdbTransportError::protocol_error);
    }
    {
        FakeFactory factory;
        factory.push(fake("FAIL0004oops"));
        ServiceAdbTransport transport({}, factory.callback());
        const auto result = transport.devices_long();
        CHECK(result.error == AdbTransportError::adb_fail);
        CHECK(result.message == "oops");
    }
    {
        FakeFactory factory;
        factory.push(fake("OKAY0100"));
        auto limits = ServiceAdbTransportLimits{};
        limits.max_response_bytes = 32;
        ServiceAdbTransport transport({}, factory.callback(), limits);
        CHECK(transport.devices_long().error == AdbTransportError::capacity);
    }
    {
        FakeFactory factory;
        factory.push(fake("FAIL0021"));
        auto limits = ServiceAdbTransportLimits{};
        limits.max_fail_message_bytes = 32;
        ServiceAdbTransport transport({}, factory.callback(), limits);
        CHECK(transport.devices_long().error == AdbTransportError::capacity);
    }
}

void timeout_and_cancel()
{
    {
        FakeFactory factory;
        auto control = fake("");
        control->block_reads = true;
        factory.push(control);
        auto limits = ServiceAdbTransportLimits{};
        limits.io_timeout = 20ms;
        ServiceAdbTransport transport({}, factory.callback(), limits);
        CHECK(transport.devices_long().error == AdbTransportError::timeout);
    }
    {
        FakeFactory factory;
        auto control = fake("");
        control->block_reads = true;
        factory.push(control);
        ServiceAdbTransport transport({}, factory.callback());
        std::stop_source source;
        auto future = std::async(std::launch::async, [&] {
            return transport.devices_long(source.get_token()).error;
        });
        {
            std::unique_lock lock(control->mutex);
            control->changed.wait_for(lock, 1s, [&] { return control->read_entered; });
        }
        source.request_stop();
        CHECK(future.get() == AdbTransportError::cancelled);
    }
}

void exact_transport_shell_and_wrong_serial()
{
    {
        FakeFactory factory;
        auto control = fake("OKAYOKAYhello world", 2);
        factory.push(control);
        ServiceAdbTransport transport({}, factory.callback());
        const auto result = transport.shell_legacy("emulator-5556", "echo hello");
        CHECK(result && *result.value == "hello world");
        CHECK(control->written == frame("host:transport:emulator-5556")
            + frame("shell:echo hello"));
    }
    {
        FakeFactory factory;
        auto control = fake("FAIL000Edevice unknown");
        factory.push(control);
        ServiceAdbTransport transport({}, factory.callback());
        const auto result = transport.open_tcp("wrong-serial", 8886);
        CHECK(result.error == AdbTransportError::adb_fail);
        CHECK(control->written == frame("host:transport:wrong-serial"));
    }
}

void forwarding_and_parsing()
{
    FakeFactory factory;
    auto listed = fake(length_response(
        "emulator-5556 tcp:27183 tcp:8886\nserial-2 localabstract:x tcp:1\n"));
    auto forwarded = fake("OKAY");
    factory.push(listed);
    factory.push(forwarded);
    ServiceAdbTransport transport({}, factory.callback());
    const auto items = transport.list_forwards();
    CHECK(items && items->size() == 2);
    CHECK((*items.value)[0].serial == "emulator-5556");
    CHECK((*items.value)[0].remote == "tcp:8886");
    const auto result = transport.forward(
        "emulator-5556", "tcp:27183", "tcp:8886");
    CHECK(result && *result.value);
    CHECK(forwarded->written == frame(
        "host-serial:emulator-5556:forward:tcp:27183;tcp:8886"));
}

void raw_stream_raii_stop_and_destructor()
{
    FakeFactory factory;
    auto control = fake("OKAYOKAYdata", 4);
    factory.push(control);
    ServiceAdbTransport transport({}, factory.callback());
    auto opened = transport.open_tcp("emulator-5556", 8886);
    CHECK(opened && opened->is_open());
    CHECK(control->written == frame("host:transport:emulator-5556")
        + frame("tcp:8886"));
    const auto chunk = opened->read_some(4);
    CHECK(chunk && chunk->size() == 4);
    CHECK(std::string(reinterpret_cast<const char*>(chunk->data()), chunk->size())
        == "data");
    const std::array outbound{std::byte{'p'}, std::byte{'i'}, std::byte{'n'},
                              std::byte{'g'}};
    const auto written = opened->write_all(outbound);
    CHECK(written && *written.value == outbound.size());
    CHECK(control->written.ends_with("ping"));
    transport.stop();
    CHECK(!opened->is_open());
    CHECK(control->closed);

    FakeFactory second_factory;
    auto second = fake("OKAYOKAY");
    second_factory.push(second);
    std::optional<baas::service::adb::AdbServiceStream> retained;
    {
        ServiceAdbTransport owner({}, second_factory.callback());
        auto stream = owner.open_tcp("emulator-5556", 8886);
        CHECK(stream);
        retained.emplace(std::move(*stream.value));
        CHECK(retained->is_open());
    }
    CHECK(second->closed);
    CHECK(!retained->is_open());
}

void independent_connections()
{
    FakeFactory factory;
    auto blocked = fake("");
    blocked->block_reads = true;
    auto fast = fake(length_response("device"));
    factory.push(blocked);
    factory.push(fast);
    auto limits = ServiceAdbTransportLimits{};
    limits.io_timeout = 2s;
    ServiceAdbTransport transport({}, factory.callback(), limits);
    auto first = std::async(std::launch::async, [&] { return transport.devices_long(); });
    {
        std::unique_lock lock(blocked->mutex);
        CHECK(blocked->changed.wait_for(lock, 1s, [&] { return blocked->read_entered; }));
    }
    const auto second = transport.devices_long();
    CHECK(second && *second.value == "device");
    {
        std::lock_guard lock(blocked->mutex);
        blocked->block_reads = false;
        blocked->terminal = AdbStreamStatus::error;
        blocked->changed.notify_all();
    }
    CHECK(first.get().error == AdbTransportError::connection_failed);
    CHECK(factory.state->endpoints.size() == 2);
}

void input_validation()
{
    FakeFactory factory;
    ServiceAdbTransport transport({}, factory.callback());
    CHECK(transport.get_state("bad\nserial").error
        == AdbTransportError::invalid_argument);
    CHECK(transport.shell_legacy("serial", "bad\ncommand").error
        == AdbTransportError::invalid_argument);
    CHECK(transport.forward("serial", "tcp:1;evil", "tcp:2").error
        == AdbTransportError::invalid_argument);
    CHECK(transport.open_tcp("serial", 0).error
        == AdbTransportError::invalid_argument);
    CHECK(transport.shell_legacy("serial", std::string(65'536, 'x')).error
        == AdbTransportError::invalid_argument);
}

int smoke(const std::string& serial)
{
    ServiceAdbTransport transport;
    const auto devices = transport.devices_long();
    if (!devices) {
        std::cerr << "devices-l failed: " << devices.message << '\n';
        return 2;
    }
    const auto state = transport.get_state(serial);
    if (!state) {
        std::cerr << "get-state failed: " << state.message << '\n';
        return 3;
    }
    std::cout << *devices.value << "state(" << serial << ")=" << *state.value << '\n';
    return 0;
}

}  // namespace

int main(const int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--smoke") {
        return smoke(argv[2]);
    }
    const std::array tests{
        fragmented_host_query_and_endpoint,
        strict_lengths_fail_and_caps,
        timeout_and_cancel,
        exact_transport_shell_and_wrong_serial,
        forwarding_and_parsing,
        raw_stream_raii_stop_and_destructor,
        independent_connections,
        input_validation,
    };
    try {
        for (const auto test : tests) test();
        std::cout << tests.size() << " ServiceAdbTransport tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ServiceAdbTransport test failed: " << error.what() << '\n';
        return 1;
    }
}
