#include "service/adb/ServiceAdbTransport.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace baas::service::adb {
namespace {

using Deadline = AdbByteStream::Deadline;

template <typename T>
AdbTransportResult<T> failure(
    const AdbTransportError error, std::string message = {})
{
    return {std::nullopt, error, std::move(message)};
}

bool valid_limits(const ServiceAdbTransportLimits& limits) noexcept
{
    return limits.max_host_bytes != 0 && limits.max_serial_bytes != 0
        && limits.max_request_bytes != 0 && limits.max_request_bytes <= 0xffffU
        && limits.max_response_bytes != 0 && limits.max_total_bytes != 0
        && limits.max_fail_message_bytes != 0
        && limits.max_shell_command_bytes != 0
        && limits.connect_timeout.count() > 0 && limits.io_timeout.count() > 0;
}

bool safe_text(const std::string_view value, const std::size_t maximum,
               const bool allow_space) noexcept
{
    if (value.empty() || value.size() > maximum) return false;
    return std::all_of(value.begin(), value.end(), [allow_space](const unsigned char c) {
        return c >= (allow_space ? 0x20U : 0x21U) && c <= 0x7eU;
    });
}

std::array<std::byte, 4> encode_hex_length(const std::size_t size)
{
    static constexpr char digits[] = "0123456789ABCDEF";
    return {
        static_cast<std::byte>(digits[(size >> 12U) & 0xfU]),
        static_cast<std::byte>(digits[(size >> 8U) & 0xfU]),
        static_cast<std::byte>(digits[(size >> 4U) & 0xfU]),
        static_cast<std::byte>(digits[size & 0xfU]),
    };
}

std::optional<std::size_t> decode_hex_length(
    const std::span<const std::byte, 4> bytes) noexcept
{
    std::size_t result{};
    for (const auto raw : bytes) {
        const auto c = std::to_integer<unsigned char>(raw);
        std::size_t value{};
        if (c >= '0' && c <= '9') value = c - '0';
        else if (c >= 'a' && c <= 'f') value = c - 'a' + 10U;
        else if (c >= 'A' && c <= 'F') value = c - 'A' + 10U;
        else return std::nullopt;
        result = (result << 4U) | value;
    }
    return result;
}

AdbTransportError map_stream_status(const AdbStreamStatus status) noexcept
{
    switch (status) {
        case AdbStreamStatus::timeout: return AdbTransportError::timeout;
        case AdbStreamStatus::cancelled: return AdbTransportError::cancelled;
        case AdbStreamStatus::eof: return AdbTransportError::protocol_error;
        case AdbStreamStatus::error: return AdbTransportError::connection_failed;
        case AdbStreamStatus::ok: break;
    }
    return AdbTransportError::internal_error;
}

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
void close_socket(const NativeSocket socket) noexcept
{
    if (socket != invalid_socket) closesocket(socket);
}
int socket_error() noexcept { return WSAGetLastError(); }
bool would_block(const int error) noexcept
{
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS
        || error == WSAEALREADY;
}
bool interrupted(const int error) noexcept { return error == WSAEINTR; }
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
void close_socket(const NativeSocket socket) noexcept
{
    if (socket != invalid_socket) static_cast<void>(::close(socket));
}
int socket_error() noexcept { return errno; }
bool would_block(const int error) noexcept
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS;
}
bool interrupted(const int error) noexcept { return error == EINTR; }
#endif

class NativeAdbStream final : public AdbByteStream {
public:
    explicit NativeAdbStream(const NativeSocket socket) : socket_(socket) {}
    ~NativeAdbStream() override { close(); }

    AdbStreamIoResult read_some(
        const std::span<std::byte> output, const Deadline deadline,
        const std::stop_token stop) override
    {
        if (output.empty()) return {0, AdbStreamStatus::ok};
        const auto socket = acquire_io();
        if (socket == invalid_socket) return {0, AdbStreamStatus::cancelled};
        IoLease lease{this};
        for (;;) {
            const auto ready = wait(socket, false, deadline, stop);
            if (ready != AdbStreamStatus::ok) return {0, ready};
#if defined(_WIN32)
            const auto size = static_cast<int>(std::min<std::size_t>(
                output.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
            const int received = ::recv(
                socket, reinterpret_cast<char*>(output.data()), size, 0);
#else
            const auto received = ::recv(
                socket, output.data(), output.size(), 0);
#endif
            if (received > 0) {
                return {static_cast<std::size_t>(received), AdbStreamStatus::ok};
            }
            if (received == 0) {
                return {0, closing_.load() ? AdbStreamStatus::cancelled
                                           : AdbStreamStatus::eof};
            }
            const auto error = socket_error();
            if (would_block(error) || interrupted(error)) continue;
            return {0, closing_.load() ? AdbStreamStatus::cancelled
                                       : AdbStreamStatus::error};
        }
    }

    AdbStreamIoResult write_some(
        const std::span<const std::byte> input, const Deadline deadline,
        const std::stop_token stop) override
    {
        if (input.empty()) return {0, AdbStreamStatus::ok};
        const auto socket = acquire_io();
        if (socket == invalid_socket) return {0, AdbStreamStatus::cancelled};
        IoLease lease{this};
        for (;;) {
            const auto ready = wait(socket, true, deadline, stop);
            if (ready != AdbStreamStatus::ok) return {0, ready};
#if defined(_WIN32)
            const auto size = static_cast<int>(std::min<std::size_t>(
                input.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
            const int sent = ::send(
                socket, reinterpret_cast<const char*>(input.data()), size, 0);
#else
#if defined(MSG_NOSIGNAL)
            constexpr int send_flags = MSG_NOSIGNAL;
#else
            constexpr int send_flags = 0;
#endif
            const auto sent = ::send(
                socket, input.data(), input.size(), send_flags);
#endif
            if (sent > 0) {
                return {static_cast<std::size_t>(sent), AdbStreamStatus::ok};
            }
            const auto error = socket_error();
            if (would_block(error) || interrupted(error)) continue;
            return {0, closing_.load() ? AdbStreamStatus::cancelled
                                       : AdbStreamStatus::error};
        }
    }

    void close() noexcept override
    {
        NativeSocket socket{invalid_socket};
        {
            std::unique_lock lock(lifecycle_mutex_);
            if (closing_.exchange(true)) {
                lifecycle_changed_.wait(lock, [this] { return closed_; });
                return;
            }
            socket = socket_;
        }
        if (socket == invalid_socket) {
            std::lock_guard lock(lifecycle_mutex_);
            closed_ = true;
            lifecycle_changed_.notify_all();
            return;
        }
#if defined(_WIN32)
        static_cast<void>(::shutdown(socket, SD_BOTH));
#else
        static_cast<void>(::shutdown(socket, SHUT_RDWR));
#endif
        {
            std::unique_lock lock(lifecycle_mutex_);
            lifecycle_changed_.wait(lock, [this] { return active_io_ == 0; });
            socket_ = invalid_socket;
        }
        close_socket(socket);
        {
            std::lock_guard lock(lifecycle_mutex_);
            closed_ = true;
        }
        lifecycle_changed_.notify_all();
    }

private:
    class IoLease final {
    public:
        explicit IoLease(NativeAdbStream* owner) noexcept : owner_(owner) {}
        ~IoLease() { owner_->release_io(); }
        IoLease(const IoLease&) = delete;
        IoLease& operator=(const IoLease&) = delete;
    private:
        NativeAdbStream* owner_;
    };

    NativeSocket acquire_io() noexcept
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (closing_.load() || socket_ == invalid_socket) return invalid_socket;
        ++active_io_;
        return socket_;
    }

    void release_io() noexcept
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (active_io_ != 0) --active_io_;
        if (active_io_ == 0) lifecycle_changed_.notify_all();
    }

    AdbStreamStatus wait(
        const NativeSocket socket, const bool writing, const Deadline deadline,
        const std::stop_token stop) noexcept
    {
        while (!stop.stop_requested()) {
            if (closing_.load()) return AdbStreamStatus::cancelled;
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return AdbStreamStatus::timeout;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now);
            const auto slice = std::min(remaining, std::chrono::milliseconds(25));
#if defined(_WIN32)
            fd_set set;
            FD_ZERO(&set);
            FD_SET(socket, &set);
            timeval timeout{
                static_cast<long>(slice.count() / 1000),
                static_cast<long>((slice.count() % 1000) * 1000)};
            const int ready = ::select(
                0, writing ? nullptr : &set, writing ? &set : nullptr,
                nullptr, &timeout);
#else
            pollfd descriptor{socket, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
            const int ready = ::poll(&descriptor, 1, static_cast<int>(slice.count()));
#endif
            if (ready > 0) {
                return closing_.load() ? AdbStreamStatus::cancelled
                                       : AdbStreamStatus::ok;
            }
            if (ready < 0) {
#if !defined(_WIN32)
                if (errno == EINTR) continue;
#endif
                return AdbStreamStatus::error;
            }
        }
        return AdbStreamStatus::cancelled;
    }

    std::mutex lifecycle_mutex_;
    std::condition_variable lifecycle_changed_;
    NativeSocket socket_{invalid_socket};
    std::size_t active_io_{};
    std::atomic<bool> closing_{};
    bool closed_{};
};

#if defined(_WIN32)
bool ensure_winsock() noexcept
{
    static const bool ready = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}
#endif

bool numeric_endpoint_host(const std::string& host) noexcept
{
#if defined(_WIN32)
    if (!ensure_winsock()) return false;
#endif
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST;
    addrinfo* addresses{};
    const auto result = getaddrinfo(host.c_str(), nullptr, &hints, &addresses);
    if (addresses) freeaddrinfo(addresses);
    return result == 0;
}

AdbStreamOpenResult open_native_stream_impl(
    const AdbEndpoint& endpoint, const Deadline deadline,
    const std::stop_token stop)
{
#if defined(_WIN32)
    if (!ensure_winsock()) {
        return {nullptr, AdbTransportError::connection_failed, "WSAStartup failed"};
    }
#endif
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    addrinfo* addresses{};
    const auto port = std::to_string(endpoint.port);
    if (getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints, &addresses) != 0) {
        return {nullptr, AdbTransportError::connection_failed, "ADB endpoint resolution failed"};
    }
    struct AddressGuard {
        addrinfo* value;
        ~AddressGuard() { if (value) freeaddrinfo(value); }
    } guard{addresses};

    for (auto* address = addresses; address && !stop.stop_requested();
         address = address->ai_next) {
        const NativeSocket socket = ::socket(
            address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket == invalid_socket) continue;
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
        const int no_sigpipe = 1;
        if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE,
                       &no_sigpipe, sizeof(no_sigpipe)) != 0) {
            close_socket(socket);
            continue;
        }
#endif
#if defined(_WIN32)
        u_long nonblocking = 1;
        if (ioctlsocket(socket, FIONBIO, &nonblocking) != 0) {
            close_socket(socket);
            continue;
        }
#else
        const int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0) {
            close_socket(socket);
            continue;
        }
#endif
        const int connected = ::connect(
            socket, address->ai_addr, static_cast<int>(address->ai_addrlen));
        if (connected == 0) {
            if (stop.stop_requested()) {
                close_socket(socket);
                return {nullptr, AdbTransportError::cancelled,
                        "ADB connect cancelled"};
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                close_socket(socket);
                return {nullptr, AdbTransportError::timeout,
                        "ADB connect timed out"};
            }
            return {std::make_unique<NativeAdbStream>(socket),
                    AdbTransportError::none, {}};
        }
        if (!would_block(socket_error())) {
            close_socket(socket);
            continue;
        }
        while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
            const auto now = std::chrono::steady_clock::now();
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now);
            const auto slice = std::min(remaining, std::chrono::milliseconds(25));
#if defined(_WIN32)
            fd_set set;
            FD_ZERO(&set);
            FD_SET(socket, &set);
            timeval timeout{static_cast<long>(slice.count() / 1000),
                            static_cast<long>((slice.count() % 1000) * 1000)};
            const int ready = select(0, nullptr, &set, nullptr, &timeout);
#else
            pollfd descriptor{socket, POLLOUT, 0};
            const int ready = poll(&descriptor, 1, static_cast<int>(slice.count()));
#endif
            if (ready > 0) {
                int error{};
#if defined(_WIN32)
                int length = sizeof(error);
                const bool valid = getsockopt(
                    socket, SOL_SOCKET, SO_ERROR,
                    reinterpret_cast<char*>(&error), &length) == 0;
#else
                socklen_t length = sizeof(error);
                const bool valid = getsockopt(
                    socket, SOL_SOCKET, SO_ERROR, &error, &length) == 0;
#endif
                if (valid && error == 0) {
                    if (stop.stop_requested()) {
                        close_socket(socket);
                        return {nullptr, AdbTransportError::cancelled,
                                "ADB connect cancelled"};
                    }
                    if (std::chrono::steady_clock::now() >= deadline) {
                        close_socket(socket);
                        return {nullptr, AdbTransportError::timeout,
                                "ADB connect timed out"};
                    }
                    return {std::make_unique<NativeAdbStream>(socket),
                            AdbTransportError::none, {}};
                }
                break;
            }
            if (ready < 0) break;
        }
        close_socket(socket);
    }
    if (stop.stop_requested()) {
        return {nullptr, AdbTransportError::cancelled, "ADB connect cancelled"};
    }
    if (std::chrono::steady_clock::now() >= deadline) {
        return {nullptr, AdbTransportError::timeout, "ADB connect timed out"};
    }
    return {nullptr, AdbTransportError::connection_failed, "ADB server connection failed"};
}

}  // namespace

AdbStreamOpenResult open_native_adb_stream(
    const AdbEndpoint& endpoint, const AdbByteStream::Deadline deadline,
    const std::stop_token stop)
{
    return open_native_stream_impl(endpoint, deadline, stop);
}

class AdbServiceStream::State {
public:
    State(std::unique_ptr<AdbByteStream> stream, ServiceAdbTransportLimits limits)
        : stream(std::move(stream)), limits(std::move(limits))
    {}
    ~State() { close(); }
    void close() noexcept
    {
        if (!closed.exchange(true) && stream) stream->close();
    }
    std::unique_ptr<AdbByteStream> stream;
    ServiceAdbTransportLimits limits;
    std::mutex io_mutex;
    std::atomic<bool> closed{};
};

class ServiceAdbTransport::Impl {
public:
    Impl(AdbEndpoint endpoint, AdbStreamFactory factory,
         ServiceAdbTransportLimits limits)
        : endpoint(std::move(endpoint)), factory(std::move(factory)), limits(limits)
    {}

    std::shared_ptr<AdbServiceStream::State> connect(const std::stop_token stop,
                                                     AdbTransportError& error,
                                                     std::string& message)
    {
        if (stopping.load() || stop.stop_requested()) {
            error = AdbTransportError::cancelled;
            return {};
        }
        auto opened = factory(
            endpoint, std::chrono::steady_clock::now() + limits.connect_timeout, stop);
        if (!opened) {
            error = opened.error == AdbTransportError::none
                ? AdbTransportError::connection_failed : opened.error;
            message = std::move(opened.message);
            return {};
        }
        auto state = std::make_shared<AdbServiceStream::State>(
            std::move(opened.stream), limits);
        {
            std::lock_guard lock(active_mutex);
            if (stopping.load()) {
                state->close();
                error = AdbTransportError::cancelled;
                return {};
            }
            active.erase(std::remove_if(active.begin(), active.end(),
                [](const auto& weak) { return weak.expired(); }), active.end());
            active.push_back(state);
        }
        error = AdbTransportError::none;
        return state;
    }

    AdbTransportError write_all(
        AdbServiceStream::State& state, const std::span<const std::byte> bytes,
        const Deadline deadline, const std::stop_token stop,
        std::size_t& total) const
    {
        std::size_t offset{};
        while (offset < bytes.size()) {
            if (total > limits.max_total_bytes
                || bytes.size() - offset > limits.max_total_bytes - total) {
                return AdbTransportError::capacity;
            }
            const auto result = state.stream->write_some(bytes.subspan(offset), deadline, stop);
            if (result.status != AdbStreamStatus::ok || result.transferred == 0) {
                return map_stream_status(result.status);
            }
            if (result.transferred > bytes.size() - offset) {
                return AdbTransportError::protocol_error;
            }
            offset += result.transferred;
            total += result.transferred;
        }
        return AdbTransportError::none;
    }

    AdbTransportError read_exact(
        AdbServiceStream::State& state, const std::span<std::byte> bytes,
        const Deadline deadline, const std::stop_token stop,
        std::size_t& total) const
    {
        std::size_t offset{};
        while (offset < bytes.size()) {
            if (total > limits.max_total_bytes
                || bytes.size() - offset > limits.max_total_bytes - total) {
                return AdbTransportError::capacity;
            }
            const auto result = state.stream->read_some(bytes.subspan(offset), deadline, stop);
            if (result.status != AdbStreamStatus::ok || result.transferred == 0) {
                return map_stream_status(result.status);
            }
            if (result.transferred > bytes.size() - offset) {
                return AdbTransportError::protocol_error;
            }
            offset += result.transferred;
            total += result.transferred;
        }
        return AdbTransportError::none;
    }

    AdbTransportError send_request(
        AdbServiceStream::State& state, const std::string_view request,
        const Deadline deadline, const std::stop_token stop,
        std::size_t& total) const
    {
        if (request.empty() || request.size() > limits.max_request_bytes) {
            return AdbTransportError::capacity;
        }
        const auto header = encode_hex_length(request.size());
        auto error = write_all(state, header, deadline, stop, total);
        if (error != AdbTransportError::none) return error;
        return write_all(
            state,
            std::as_bytes(std::span<const char>(request.data(), request.size())),
            deadline, stop, total);
    }

    AdbTransportError read_length(
        AdbServiceStream::State& state, std::size_t& length,
        const Deadline deadline, const std::stop_token stop,
        std::size_t& total) const
    {
        std::array<std::byte, 4> header{};
        const auto error = read_exact(state, header, deadline, stop, total);
        if (error != AdbTransportError::none) return error;
        const auto decoded = decode_hex_length(header);
        if (!decoded) return AdbTransportError::protocol_error;
        length = *decoded;
        return AdbTransportError::none;
    }

    AdbTransportResult<bool> expect_okay(
        AdbServiceStream::State& state, const Deadline deadline,
        const std::stop_token stop, std::size_t& total) const
    {
        std::array<std::byte, 4> status{};
        auto error = read_exact(state, status, deadline, stop, total);
        if (error != AdbTransportError::none) return failure<bool>(error);
        const std::string_view word(
            reinterpret_cast<const char*>(status.data()), status.size());
        if (word == "OKAY") return {true, AdbTransportError::none, {}};
        if (word != "FAIL") {
            return failure<bool>(AdbTransportError::protocol_error,
                                 "unexpected ADB status");
        }
        std::size_t size{};
        error = read_length(state, size, deadline, stop, total);
        if (error != AdbTransportError::none) return failure<bool>(error);
        if (size > limits.max_fail_message_bytes || size > limits.max_response_bytes) {
            return failure<bool>(AdbTransportError::capacity,
                                 "ADB FAIL message exceeds limit");
        }
        std::string message(size, '\0');
        error = read_exact(
            state, std::as_writable_bytes(
                       std::span<char>(message.data(), message.size())),
            deadline, stop, total);
        if (error != AdbTransportError::none) return failure<bool>(error);
        return failure<bool>(AdbTransportError::adb_fail, std::move(message));
    }

    AdbTransportResult<std::string> host_query(
        const std::string_view request, const std::stop_token stop)
    {
        AdbTransportError error{};
        std::string message;
        auto state = connect(stop, error, message);
        if (!state) return failure<std::string>(error, std::move(message));
        const auto deadline = std::chrono::steady_clock::now() + limits.io_timeout;
        std::size_t total{};
        std::lock_guard io_lock(state->io_mutex);
        error = send_request(*state, request, deadline, stop, total);
        if (error != AdbTransportError::none) return failure<std::string>(error);
        auto okay = expect_okay(*state, deadline, stop, total);
        if (!okay) return failure<std::string>(okay.error, std::move(okay.message));
        std::size_t size{};
        error = read_length(*state, size, deadline, stop, total);
        if (error != AdbTransportError::none) return failure<std::string>(error);
        if (size > limits.max_response_bytes || total > limits.max_total_bytes
            || size > limits.max_total_bytes - total) {
            return failure<std::string>(AdbTransportError::capacity);
        }
        std::string response(size, '\0');
        error = read_exact(
            *state,
            std::as_writable_bytes(
                std::span<char>(response.data(), response.size())),
            deadline, stop, total);
        if (error != AdbTransportError::none) return failure<std::string>(error);
        return {std::move(response), AdbTransportError::none, {}};
    }

    AdbTransportResult<AdbServiceStream> open_service(
        const std::string_view serial, const std::string_view service,
        const std::stop_token stop)
    {
        if (!safe_text(serial, limits.max_serial_bytes, false)
            || service.empty() || service.size() > limits.max_request_bytes) {
            return failure<AdbServiceStream>(AdbTransportError::invalid_argument);
        }
        AdbTransportError error{};
        std::string message;
        auto state = connect(stop, error, message);
        if (!state) return failure<AdbServiceStream>(error, std::move(message));
        const auto deadline = std::chrono::steady_clock::now() + limits.io_timeout;
        std::size_t total{};
        std::lock_guard io_lock(state->io_mutex);
        const std::string selection = "host:transport:" + std::string(serial);
        error = send_request(*state, selection, deadline, stop, total);
        if (error != AdbTransportError::none) return failure<AdbServiceStream>(error);
        auto okay = expect_okay(*state, deadline, stop, total);
        if (!okay) {
            return failure<AdbServiceStream>(okay.error, std::move(okay.message));
        }
        error = send_request(*state, service, deadline, stop, total);
        if (error != AdbTransportError::none) return failure<AdbServiceStream>(error);
        okay = expect_okay(*state, deadline, stop, total);
        if (!okay) {
            return failure<AdbServiceStream>(okay.error, std::move(okay.message));
        }
        return {AdbServiceStream(std::move(state)), AdbTransportError::none, {}};
    }

    void stop() noexcept
    {
        if (stopping.exchange(true)) return;
        std::vector<std::shared_ptr<AdbServiceStream::State>> streams;
        try {
            std::lock_guard lock(active_mutex);
            streams.reserve(active.size());
            for (const auto& weak : active) {
                if (auto state = weak.lock()) streams.push_back(std::move(state));
            }
            active.clear();
        } catch (...) {
            std::lock_guard lock(active_mutex);
            for (const auto& weak : active) {
                if (auto state = weak.lock()) state->close();
            }
            active.clear();
        }
        for (const auto& state : streams) state->close();
    }

    AdbEndpoint endpoint;
    AdbStreamFactory factory;
    ServiceAdbTransportLimits limits;
    std::atomic<bool> stopping{};
    std::mutex active_mutex;
    std::vector<std::weak_ptr<AdbServiceStream::State>> active;
};

AdbServiceStream::AdbServiceStream(std::shared_ptr<State> state)
    : state_(std::move(state))
{}
AdbServiceStream::~AdbServiceStream() { close(); }
AdbServiceStream::AdbServiceStream(AdbServiceStream&&) noexcept = default;
AdbServiceStream& AdbServiceStream::operator=(AdbServiceStream&&) noexcept = default;

AdbTransportResult<std::vector<std::byte>> AdbServiceStream::read_some(
    const std::size_t maximum_bytes, const std::stop_token stop) try
{
    const auto state = state_;
    if (!state || state->closed.load()) {
        return failure<std::vector<std::byte>>(AdbTransportError::closed);
    }
    if (maximum_bytes == 0 || maximum_bytes > state->limits.max_response_bytes) {
        return failure<std::vector<std::byte>>(AdbTransportError::capacity);
    }
    std::vector<std::byte> bytes(maximum_bytes);
    std::lock_guard lock(state->io_mutex);
    const auto result = state->stream->read_some(
        bytes, std::chrono::steady_clock::now() + state->limits.io_timeout, stop);
    if (result.status == AdbStreamStatus::eof) {
        bytes.clear();
        return {std::move(bytes), AdbTransportError::none, {}};
    }
    if (result.status != AdbStreamStatus::ok || result.transferred == 0) {
        return failure<std::vector<std::byte>>(map_stream_status(result.status));
    }
    if (result.transferred > bytes.size()) {
        return failure<std::vector<std::byte>>(AdbTransportError::protocol_error);
    }
    bytes.resize(result.transferred);
    return {std::move(bytes), AdbTransportError::none, {}};
} catch (...) {
    return failure<std::vector<std::byte>>(AdbTransportError::internal_error);
}

AdbTransportResult<std::size_t> AdbServiceStream::write_all(
    const std::span<const std::byte> bytes, const std::stop_token stop) try
{
    const auto state = state_;
    if (!state || state->closed.load()) {
        return failure<std::size_t>(AdbTransportError::closed);
    }
    if (bytes.size() > state->limits.max_total_bytes) {
        return failure<std::size_t>(AdbTransportError::capacity);
    }
    std::lock_guard lock(state->io_mutex);
    std::size_t offset{};
    const auto deadline = std::chrono::steady_clock::now() + state->limits.io_timeout;
    while (offset < bytes.size()) {
        const auto result = state->stream->write_some(bytes.subspan(offset), deadline, stop);
        if (result.status != AdbStreamStatus::ok || result.transferred == 0) {
            return failure<std::size_t>(map_stream_status(result.status));
        }
        if (result.transferred > bytes.size() - offset) {
            return failure<std::size_t>(AdbTransportError::protocol_error);
        }
        offset += result.transferred;
    }
    return {offset, AdbTransportError::none, {}};
} catch (...) {
    return failure<std::size_t>(AdbTransportError::internal_error);
}

void AdbServiceStream::close() noexcept
{
    if (state_) state_->close();
}
bool AdbServiceStream::is_open() const noexcept
{
    return state_ && !state_->closed.load();
}

ServiceAdbTransport::ServiceAdbTransport(
    AdbEndpoint endpoint, AdbStreamFactory stream_factory,
    ServiceAdbTransportLimits limits)
{
    if (!valid_limits(limits)
        || !safe_text(endpoint.host, limits.max_host_bytes, false)
        || !numeric_endpoint_host(endpoint.host)
        || endpoint.port == 0) {
        throw std::invalid_argument("invalid ADB transport configuration");
    }
    if (!stream_factory) stream_factory = open_native_adb_stream;
    impl_ = std::make_shared<Impl>(
        std::move(endpoint), std::move(stream_factory), limits);
}

ServiceAdbTransport::~ServiceAdbTransport() { stop(); }

AdbTransportResult<std::string> ServiceAdbTransport::devices_long(
    const std::stop_token stop) try
{
    return impl_->host_query("host:devices-l", stop);
} catch (...) {
    return failure<std::string>(AdbTransportError::internal_error);
}

AdbTransportResult<std::string> ServiceAdbTransport::get_state(
    const std::string_view exact_serial, const std::stop_token stop) try
{
    if (!safe_text(exact_serial, impl_->limits.max_serial_bytes, false)) {
        return failure<std::string>(AdbTransportError::invalid_argument);
    }
    return impl_->host_query(
        "host-serial:" + std::string(exact_serial) + ":get-state", stop);
} catch (...) {
    return failure<std::string>(AdbTransportError::internal_error);
}

AdbTransportResult<std::vector<AdbForwardItem>> ServiceAdbTransport::list_forwards(
    const std::stop_token stop) try
{
    auto response = impl_->host_query("host:list-forward", stop);
    if (!response) {
        return failure<std::vector<AdbForwardItem>>(
            response.error, std::move(response.message));
    }
    std::vector<AdbForwardItem> items;
    std::size_t offset{};
    while (offset < response->size()) {
        const auto end = response->find('\n', offset);
        const auto line = std::string_view(*response.value).substr(
            offset, (end == std::string::npos ? response->size() : end) - offset);
        if (!line.empty()) {
            const auto first = line.find_first_of(" \t");
            const auto second = first == std::string_view::npos
                ? std::string_view::npos : line.find_first_not_of(" \t", first);
            const auto third = second == std::string_view::npos
                ? std::string_view::npos : line.find_first_of(" \t", second);
            const auto fourth = third == std::string_view::npos
                ? std::string_view::npos : line.find_first_not_of(" \t", third);
            if (first == std::string_view::npos || second == std::string_view::npos
                || third == std::string_view::npos || fourth == std::string_view::npos) {
                return failure<std::vector<AdbForwardItem>>(
                    AdbTransportError::protocol_error);
            }
            items.push_back({std::string(line.substr(0, first)),
                             std::string(line.substr(second, third - second)),
                             std::string(line.substr(fourth))});
        }
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    return {std::move(items), AdbTransportError::none, {}};
} catch (...) {
    return failure<std::vector<AdbForwardItem>>(AdbTransportError::internal_error);
}

AdbTransportResult<bool> ServiceAdbTransport::forward(
    const std::string_view exact_serial, const std::string_view local,
    const std::string_view remote, const std::stop_token stop) try
{
    if (!safe_text(exact_serial, impl_->limits.max_serial_bytes, false)
        || !safe_text(local, impl_->limits.max_request_bytes, false)
        || !safe_text(remote, impl_->limits.max_request_bytes, false)
        || local.find(';') != std::string_view::npos
        || remote.find(';') != std::string_view::npos) {
        return failure<bool>(AdbTransportError::invalid_argument);
    }
    AdbTransportError error{};
    std::string message;
    auto state = impl_->connect(stop, error, message);
    if (!state) return failure<bool>(error, std::move(message));
    const std::string request = "host-serial:" + std::string(exact_serial)
        + ":forward:" + std::string(local) + ";" + std::string(remote);
    const auto deadline = std::chrono::steady_clock::now() + impl_->limits.io_timeout;
    std::size_t total{};
    std::lock_guard lock(state->io_mutex);
    error = impl_->send_request(*state, request, deadline, stop, total);
    if (error != AdbTransportError::none) return failure<bool>(error);
    return impl_->expect_okay(*state, deadline, stop, total);
} catch (...) {
    return failure<bool>(AdbTransportError::internal_error);
}

AdbTransportResult<std::string> ServiceAdbTransport::shell_legacy(
    const std::string_view exact_serial, const std::string_view command,
    const std::stop_token stop) try
{
    if (!safe_text(command, impl_->limits.max_shell_command_bytes, true)
        || command.find('\0') != std::string_view::npos
        || command.find('\r') != std::string_view::npos
        || command.find('\n') != std::string_view::npos) {
        return failure<std::string>(AdbTransportError::invalid_argument);
    }
    auto opened = impl_->open_service(
        exact_serial, "shell:" + std::string(command), stop);
    if (!opened) return failure<std::string>(opened.error, std::move(opened.message));
    std::string output;
    const auto state = opened->state_;
    const auto deadline = std::chrono::steady_clock::now() + impl_->limits.io_timeout;
    std::lock_guard lock(state->io_mutex);
    for (;;) {
        std::array<std::byte, 65'536> chunk{};
        const auto result = state->stream->read_some(chunk, deadline, stop);
        if (result.status == AdbStreamStatus::eof) break;
        if (result.status != AdbStreamStatus::ok || result.transferred == 0) {
            return failure<std::string>(map_stream_status(result.status));
        }
        if (result.transferred > chunk.size()) {
            return failure<std::string>(AdbTransportError::protocol_error);
        }
        if (output.size() > impl_->limits.max_response_bytes
            || result.transferred > impl_->limits.max_response_bytes - output.size()) {
            return failure<std::string>(AdbTransportError::capacity);
        }
        output.append(reinterpret_cast<const char*>(chunk.data()), result.transferred);
    }
    return {std::move(output), AdbTransportError::none, {}};
} catch (...) {
    return failure<std::string>(AdbTransportError::internal_error);
}

AdbTransportResult<AdbServiceStream> ServiceAdbTransport::open_tcp(
    const std::string_view exact_serial, const std::uint16_t device_port,
    const std::stop_token stop) try
{
    if (device_port == 0) {
        return failure<AdbServiceStream>(AdbTransportError::invalid_argument);
    }
    return impl_->open_service(
        exact_serial, "tcp:" + std::to_string(device_port), stop);
} catch (...) {
    return failure<AdbServiceStream>(AdbTransportError::internal_error);
}

void ServiceAdbTransport::stop() noexcept
{
    if (impl_) impl_->stop();
}
const AdbEndpoint& ServiceAdbTransport::endpoint() const noexcept
{
    return impl_->endpoint;
}

}  // namespace baas::service::adb
