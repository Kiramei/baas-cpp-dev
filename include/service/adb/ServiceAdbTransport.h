#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::adb {

enum class AdbTransportError : std::uint8_t {
    none,
    invalid_argument,
    capacity,
    connection_failed,
    timeout,
    cancelled,
    protocol_error,
    adb_fail,
    local_io_error,
    closed,
    internal_error,
};

template <typename T>
struct AdbTransportResult {
    std::optional<T> value;
    AdbTransportError error{AdbTransportError::none};
    std::string message;
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == AdbTransportError::none && value.has_value();
    }
    [[nodiscard]] T* operator->() noexcept { return std::addressof(*value); }
    [[nodiscard]] const T* operator->() const noexcept
    {
        return std::addressof(*value);
    }
};

struct AdbEndpoint {
    // Numeric IPv4 or IPv6 literal. DNS names are deliberately rejected.
    std::string host{"127.0.0.1"};
    std::uint16_t port{5037};
};

struct ServiceAdbTransportLimits {
    std::size_t max_host_bytes{255};
    std::size_t max_serial_bytes{256};
    std::size_t max_request_bytes{65'535};
    std::size_t max_response_bytes{4U * 1'024U * 1'024U};
    std::size_t max_total_bytes{8U * 1'024U * 1'024U};
    std::size_t max_fail_message_bytes{64U * 1'024U};
    std::size_t max_shell_command_bytes{64U * 1'024U};
    std::chrono::milliseconds connect_timeout{3'000};
    std::chrono::milliseconds io_timeout{5'000};
};

enum class AdbStreamStatus : std::uint8_t {
    ok,
    eof,
    timeout,
    cancelled,
    error,
};

struct AdbStreamIoResult {
    std::size_t transferred{};
    AdbStreamStatus status{AdbStreamStatus::error};
};

class AdbByteStream {
public:
    using Deadline = std::chrono::steady_clock::time_point;
    virtual ~AdbByteStream() = default;
    [[nodiscard]] virtual AdbStreamIoResult read_some(
        std::span<std::byte> output,
        Deadline deadline,
        std::stop_token stop) = 0;
    [[nodiscard]] virtual AdbStreamIoResult write_some(
        std::span<const std::byte> input,
        Deadline deadline,
        std::stop_token stop) = 0;
    virtual void close() noexcept = 0;
};

struct AdbStreamOpenResult {
    std::unique_ptr<AdbByteStream> stream;
    AdbTransportError error{AdbTransportError::none};
    std::string message;
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == AdbTransportError::none && stream != nullptr;
    }
};

using AdbStreamFactory = std::function<AdbStreamOpenResult(
    const AdbEndpoint& endpoint,
    AdbByteStream::Deadline deadline,
    std::stop_token stop)>;

// Production numeric TCP connector used when no custom stream factory is set.
[[nodiscard]] AdbStreamOpenResult open_native_adb_stream(
    const AdbEndpoint& endpoint,
    AdbByteStream::Deadline deadline,
    std::stop_token stop = {});

class AdbServiceStream {
public:
    using Deadline = AdbByteStream::Deadline;
    AdbServiceStream() = default;
    ~AdbServiceStream();
    AdbServiceStream(AdbServiceStream&&) noexcept;
    AdbServiceStream& operator=(AdbServiceStream&&) noexcept;
    AdbServiceStream(const AdbServiceStream&) = delete;
    AdbServiceStream& operator=(const AdbServiceStream&) = delete;

    [[nodiscard]] AdbTransportResult<std::vector<std::byte>> read_some(
        std::size_t maximum_bytes,
        std::stop_token stop = {});
    [[nodiscard]] AdbTransportResult<std::vector<std::byte>> read_some_until(
        std::size_t maximum_bytes,
        Deadline deadline,
        std::stop_token stop = {});
    [[nodiscard]] AdbTransportResult<std::size_t> write_all(
        std::span<const std::byte> bytes,
        std::stop_token stop = {});
    [[nodiscard]] AdbTransportResult<std::size_t> write_all_until(
        std::span<const std::byte> bytes,
        Deadline deadline,
        std::stop_token stop = {});
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;

private:
    class State;
    explicit AdbServiceStream(std::shared_ptr<State> state);
    std::shared_ptr<State> state_;
    friend class ServiceAdbTransport;
};

struct AdbForwardItem {
    std::string serial;
    std::string local;
    std::string remote;
};

class ServiceAdbTransport final {
public:
    explicit ServiceAdbTransport(
        AdbEndpoint endpoint = {},
        AdbStreamFactory stream_factory = {},
        ServiceAdbTransportLimits limits = {});
    ~ServiceAdbTransport();
    ServiceAdbTransport(const ServiceAdbTransport&) = delete;
    ServiceAdbTransport& operator=(const ServiceAdbTransport&) = delete;

    [[nodiscard]] AdbTransportResult<std::string> devices_long(
        std::stop_token stop = {});
    [[nodiscard]] AdbTransportResult<std::string> get_state(
        std::string_view exact_serial,
        std::stop_token stop = {});
    [[nodiscard]] AdbTransportResult<std::vector<AdbForwardItem>> list_forwards(
        std::stop_token stop = {});
    [[nodiscard]] AdbTransportResult<bool> forward(
        std::string_view exact_serial,
        std::string_view local,
        std::string_view remote,
        std::stop_token stop = {});
    [[nodiscard]] AdbTransportResult<std::string> shell_legacy(
        std::string_view exact_serial,
        std::string_view command,
        std::stop_token stop = {});
    [[nodiscard]] AdbTransportResult<AdbServiceStream> open_tcp(
        std::string_view exact_serial,
        std::uint16_t device_port,
        std::stop_token stop = {});
    // Opens only the side-effect-free ADB SYNC binary service after selecting
    // the exact device serial. Protocol commands remain the caller's policy.
    [[nodiscard]] AdbTransportResult<AdbServiceStream> open_sync(
        std::string_view exact_serial,
        std::stop_token stop = {});

    void stop() noexcept;
    [[nodiscard]] const AdbEndpoint& endpoint() const noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace baas::service::adb
