#include "service/adb/ServiceAdbSync.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using namespace baas::service::adb;

namespace {

struct CheckFailure final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define CHECK(expression) do { if (!(expression)) throw CheckFailure(#expression); } while (false)

std::string smart_frame(const std::string_view value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.push_back(hex[(value.size() >> 12U) & 0xFU]);
    result.push_back(hex[(value.size() >> 8U) & 0xFU]);
    result.push_back(hex[(value.size() >> 4U) & 0xFU]);
    result.push_back(hex[value.size() & 0xFU]);
    result.append(value);
    return result;
}

void append_le32(std::string& output, const std::uint32_t value)
{
    output.push_back(static_cast<char>(value & 0xFFU));
    output.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<char>((value >> 24U) & 0xFFU));
}

std::string sync_frame(const std::string_view id, const std::string_view payload)
{
    std::string result(id);
    append_le32(result, static_cast<std::uint32_t>(payload.size()));
    result.append(payload);
    return result;
}

std::string sync_word(const std::string_view id, const std::uint32_t value)
{
    std::string result(id);
    append_le32(result, value);
    return result;
}

struct FakeControl {
    std::mutex mutex;
    std::string input;
    std::size_t offset{};
    std::size_t fragment{1};
    AdbStreamStatus terminal{AdbStreamStatus::eof};
    bool closed{};
    std::string written;
};

class FakeStream final : public AdbByteStream {
public:
    explicit FakeStream(std::shared_ptr<FakeControl> control)
        : control_(std::move(control))
    {}

    AdbStreamIoResult read_some(
        const std::span<std::byte> output, const Deadline,
        const std::stop_token stop) override
    {
        std::lock_guard lock(control_->mutex);
        if (stop.stop_requested()) return {0, AdbStreamStatus::cancelled};
        if (control_->closed) return {0, AdbStreamStatus::error};
        if (control_->offset == control_->input.size()) {
            return {0, control_->terminal};
        }
        const auto count = std::min({output.size(), control_->fragment,
                                     control_->input.size() - control_->offset});
        std::memcpy(output.data(), control_->input.data() + control_->offset, count);
        control_->offset += count;
        return {count, AdbStreamStatus::ok};
    }

    AdbStreamIoResult write_some(
        const std::span<const std::byte> input, const Deadline,
        const std::stop_token stop) override
    {
        std::lock_guard lock(control_->mutex);
        if (stop.stop_requested()) return {0, AdbStreamStatus::cancelled};
        if (control_->closed) return {0, AdbStreamStatus::error};
        const auto count = std::min(input.size(), control_->fragment);
        control_->written.append(
            reinterpret_cast<const char*>(input.data()), count);
        return {count, AdbStreamStatus::ok};
    }

    void close() noexcept override
    {
        std::lock_guard lock(control_->mutex);
        control_->closed = true;
    }

private:
    std::shared_ptr<FakeControl> control_;
};

struct FakeFactory {
    std::deque<std::shared_ptr<FakeControl>> streams;

    std::shared_ptr<FakeControl> add(
        std::string input, const std::size_t fragment = 1,
        const AdbStreamStatus terminal = AdbStreamStatus::eof)
    {
        auto control = std::make_shared<FakeControl>();
        control->input = std::move(input);
        control->fragment = fragment;
        control->terminal = terminal;
        streams.push_back(control);
        return control;
    }

    AdbStreamFactory callback()
    {
        return [this](const AdbEndpoint&, AdbByteStream::Deadline,
                      std::stop_token) -> AdbStreamOpenResult {
            if (streams.empty()) {
                return {nullptr, AdbTransportError::connection_failed, "empty fake"};
            }
            auto control = streams.front();
            streams.pop_front();
            return {std::make_unique<FakeStream>(std::move(control)),
                    AdbTransportError::none, {}};
        };
    }
};

std::string handshake(const std::string_view response)
{
    return "OKAYOKAY" + std::string(response);
}

std::string expected_prefix(const std::string_view serial)
{
    return smart_frame("host:transport:" + std::string(serial))
        + smart_frame("sync:");
}

void test_fragmented_stat_and_exact_serial()
{
    std::string response = "STAT";
    append_le32(response, 0100755U);
    append_le32(response, 12'345U);
    append_le32(response, 99U);
    FakeFactory factory;
    auto control = factory.add(handshake(response), 1);
    ServiceAdbTransport transport({}, factory.callback());
    ServiceAdbSync sync(transport);

    const auto result = sync.stat("emulator-5556", "/data/local/tmp/a.jar");
    CHECK(result);
    CHECK(result->mode == 0100755U);
    CHECK(result->size == 12'345U);
    CHECK(result->modified_time == 99U);
    CHECK(result->exists());
    CHECK(control->written == expected_prefix("emulator-5556")
        + sync_frame("STAT", "/data/local/tmp/a.jar"));
}

void test_stat_fail_and_malformed_frames()
{
    {
        FakeFactory factory;
        auto control = factory.add(handshake(sync_frame("FAIL", "denied")), 2);
        ServiceAdbTransport transport({}, factory.callback());
        ServiceAdbSync sync(transport);
        const auto result = sync.stat("emulator-5556", "/data/local/tmp/a.jar");
        CHECK(!result);
        CHECK(result.error == AdbTransportError::adb_fail);
        CHECK(result.message == "denied");
        CHECK(control->closed);
    }
    {
        FakeFactory factory;
        factory.add(handshake("STAT\x01\x00"), 1);
        ServiceAdbTransport transport({}, factory.callback());
        ServiceAdbSync sync(transport);
        const auto result = sync.stat("emulator-5556", "/data/local/tmp/a.jar");
        CHECK(!result);
        CHECK(result.error == AdbTransportError::protocol_error);
    }
    {
        FakeFactory factory;
        std::string oversized = "FAIL";
        append_le32(oversized, 65U);
        factory.add(handshake(oversized), 3);
        ServiceAdbTransport transport({}, factory.callback());
        auto limits = AdbSyncLimits{};
        limits.max_fail_message_bytes = 64;
        ServiceAdbSync sync(transport, limits);
        const auto result = sync.stat("emulator-5556", "/data/local/tmp/a.jar");
        CHECK(!result);
        CHECK(result.error == AdbTransportError::capacity);
    }
}

void test_push_data_done_and_okay()
{
    FakeFactory factory;
    auto control = factory.add(handshake(sync_word("OKAY", 0)), 2);
    ServiceAdbTransport transport({}, factory.callback());
    auto limits = AdbSyncLimits{};
    limits.data_chunk_bytes = 3;
    ServiceAdbSync sync(transport, limits);
    const std::array<std::byte, 7> bytes{
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'d'},
        std::byte{'e'}, std::byte{'f'}, std::byte{'g'}};

    const auto result = sync.push("serial:exact", "/data/local/tmp/server.jar",
                                  bytes, 0755, 123U);
    CHECK(result);
    CHECK(*result.value == bytes.size());
    std::string expected = expected_prefix("serial:exact");
    expected += sync_frame("SEND", "/data/local/tmp/server.jar,33261");
    expected += sync_frame("DATA", "abc");
    expected += sync_frame("DATA", "def");
    expected += sync_frame("DATA", "g");
    expected += sync_word("DONE", 123U);
    CHECK(control->written == expected);
}

void test_push_rejects_fail_and_nonempty_okay()
{
    const std::array<std::byte, 1> bytes{std::byte{1}};
    {
        FakeFactory factory;
        factory.add(handshake(sync_frame("FAIL", "no space")), 1);
        ServiceAdbTransport transport({}, factory.callback());
        ServiceAdbSync sync(transport);
        const auto result = sync.push(
            "emulator-5556", "/data/local/tmp/a.jar", bytes);
        CHECK(!result);
        CHECK(result.error == AdbTransportError::adb_fail);
        CHECK(result.message == "no space");
    }
    {
        FakeFactory factory;
        factory.add(handshake(sync_word("OKAY", 1)), 1);
        ServiceAdbTransport transport({}, factory.callback());
        ServiceAdbSync sync(transport);
        const auto result = sync.push(
            "emulator-5556", "/data/local/tmp/a.jar", bytes);
        CHECK(!result);
        CHECK(result.error == AdbTransportError::protocol_error);
    }
}

void test_bounds_and_cancellation_fail_before_sync_side_effects()
{
    FakeFactory factory;
    auto control = factory.add(handshake(sync_word("OKAY", 0)));
    ServiceAdbTransport transport({}, factory.callback());
    auto limits = AdbSyncLimits{};
    limits.max_file_bytes = 2;
    limits.max_path_bytes = 32;
    ServiceAdbSync sync(transport, limits);
    const std::array<std::byte, 3> bytes{};
    CHECK(sync.push("emulator-5556", "/data/local/tmp/a", bytes).error
        == AdbTransportError::capacity);
    CHECK(sync.stat("emulator-5556", "/data/local/../tmp/a").error
        == AdbTransportError::invalid_argument);
    CHECK(sync.stat("emulator-5556", "relative").error
        == AdbTransportError::invalid_argument);
    CHECK(sync.stat("bad\nserial", "/data/local/tmp/a").error
        == AdbTransportError::invalid_argument);
    CHECK(sync.push("emulator-5556", "/data/local/tmp/a", {}, 01000).error
        == AdbTransportError::invalid_argument);
    const std::string send_path = "/" + std::string(30, 'a');
    CHECK(sync.push("emulator-5556", send_path, {}).error
        == AdbTransportError::capacity);
    CHECK(control->written.empty());

    std::stop_source cancelled;
    cancelled.request_stop();
    CHECK(sync.stat("emulator-5556", "/data/local/tmp/a",
                    cancelled.get_token()).error == AdbTransportError::cancelled);
    CHECK(control->written.empty());
    CHECK(sync.push_file("emulator-5556", "/data/local/tmp/a",
        std::filesystem::temp_directory_path()
            / "baas-service-adb-sync-definitely-missing.bin").error
        == AdbTransportError::local_io_error);
    CHECK(control->written.empty());
}

void test_timeout_and_push_file()
{
    {
        FakeFactory factory;
        factory.add(handshake({}), 1, AdbStreamStatus::timeout);
        ServiceAdbTransport transport({}, factory.callback());
        ServiceAdbSync sync(transport);
        const auto result = sync.stat("emulator-5556", "/data/local/tmp/a");
        CHECK(!result);
        CHECK(result.error == AdbTransportError::timeout);
    }
    {
        const auto path = std::filesystem::temp_directory_path()
            / "baas-service-adb-sync-test.bin";
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output << "jar-data";
        }
        FakeFactory factory;
        auto control = factory.add(handshake(sync_word("OKAY", 0)), 4);
        ServiceAdbTransport transport({}, factory.callback());
        auto limits = AdbSyncLimits{};
        limits.data_chunk_bytes = 4;
        ServiceAdbSync sync(transport, limits);
        const auto result = sync.push_file(
            "emulator-5556", "/data/local/tmp/scrcpy-server.jar", path,
            0644, 7);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        CHECK(result);
        CHECK(*result.value == 8U);
        std::string expected = expected_prefix("emulator-5556");
        expected += sync_frame(
            "SEND", "/data/local/tmp/scrcpy-server.jar,33188");
        expected += sync_frame("DATA", "jar-");
        expected += sync_frame("DATA", "data");
        expected += sync_word("DONE", 7U);
        CHECK(control->written == expected);
    }
}

void test_invalid_limits()
{
    ServiceAdbTransport transport;
    auto limits = AdbSyncLimits{};
    limits.data_chunk_bytes = 65'537;
    bool threw{};
    try {
        ServiceAdbSync sync(transport, limits);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

}  // namespace

int main()
{
    const std::vector<void (*)()> tests{
        test_fragmented_stat_and_exact_serial,
        test_stat_fail_and_malformed_frames,
        test_push_data_done_and_okay,
        test_push_rejects_fail_and_nonempty_okay,
        test_bounds_and_cancellation_fail_before_sync_side_effects,
        test_timeout_and_push_file,
        test_invalid_limits,
    };
    try {
        for (const auto test : tests) test();
        std::cout << tests.size() << " ServiceAdbSync tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ServiceAdbSync test failed: " << error.what() << '\n';
        return 1;
    }
}
