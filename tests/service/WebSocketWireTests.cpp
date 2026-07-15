#include "service/http/HttpHost.h"

#include <httplib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace service_http = baas::service::http;
namespace service_router = baas::service::router;
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

template <typename Predicate>
[[nodiscard]] bool wait_until(
    Predicate predicate,
    const std::chrono::milliseconds timeout = 1s
)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

#ifdef _WIN32
using NativeSocket = SOCKET;
inline constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
inline constexpr NativeSocket invalid_socket = -1;
#endif

class SocketRuntime final {
public:
    SocketRuntime()
    {
#ifdef _WIN32
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
    }

    ~SocketRuntime()
    {
#ifdef _WIN32
        if (ready_) WSACleanup();
#endif
    }

    [[nodiscard]] bool ready() const noexcept
    {
#ifdef _WIN32
        return ready_;
#else
        return true;
#endif
    }

private:
#ifdef _WIN32
    bool ready_{};
#endif
};

enum class ReadState { data, closed, timed_out, failed };

class RawConnection final {
public:
    RawConnection() = default;

    explicit RawConnection(const std::uint16_t port)
    {
        socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == invalid_socket) return;

#ifdef _WIN32
        const DWORD timeout = 1'500;
        static_cast<void>(setsockopt(
            socket_, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout), sizeof(timeout)
        ));
        static_cast<void>(setsockopt(
            socket_, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char*>(&timeout), sizeof(timeout)
        ));
#else
        const timeval timeout{1, 500'000};
        static_cast<void>(setsockopt(
            socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)
        ));
        static_cast<void>(setsockopt(
            socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)
        ));
#ifdef SO_NOSIGPIPE
        const int enabled = 1;
        static_cast<void>(setsockopt(
            socket_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)
        ));
#endif
#endif

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1
            || ::connect(
                   socket_, reinterpret_cast<const sockaddr*>(&address),
                   static_cast<int>(sizeof(address))
               ) != 0) {
            close();
        }
    }

    ~RawConnection() { close(); }

    RawConnection(const RawConnection&) = delete;
    RawConnection& operator=(const RawConnection&) = delete;

    RawConnection(RawConnection&& other) noexcept
        : socket_(std::exchange(other.socket_, invalid_socket)),
          buffered_(std::move(other.buffered_))
    {}

    RawConnection& operator=(RawConnection&& other) noexcept
    {
        if (this != &other) {
            close();
            socket_ = std::exchange(other.socket_, invalid_socket);
            buffered_ = std::move(other.buffered_);
        }
        return *this;
    }

    [[nodiscard]] bool connected() const noexcept
    {
        return socket_ != invalid_socket;
    }

    [[nodiscard]] bool send_all(const std::string_view bytes) const
    {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
#ifdef MSG_NOSIGNAL
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const auto sent = ::send(
                socket_, bytes.data() + offset,
                static_cast<int>(bytes.size() - offset), flags
            );
            if (sent <= 0) return false;
            offset += static_cast<std::size_t>(sent);
        }
        return true;
    }

    [[nodiscard]] std::optional<std::string> read_through(
        const std::string_view delimiter
    )
    {
        while (true) {
            const auto position = buffered_.find(delimiter);
            if (position != std::string::npos) {
                const auto end = position + delimiter.size();
                auto result = buffered_.substr(0, end);
                buffered_.erase(0, end);
                return result;
            }
            const auto state = receive_more();
            if (state != ReadState::data) return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<std::string> read_exact(const std::size_t size)
    {
        while (buffered_.size() < size) {
            const auto state = receive_more();
            if (state != ReadState::data) return std::nullopt;
        }
        auto result = buffered_.substr(0, size);
        buffered_.erase(0, size);
        return result;
    }

    [[nodiscard]] std::pair<ReadState, std::string> read_until_end()
    {
        std::string result = std::exchange(buffered_, {});
        while (true) {
            std::array<char, 4'096> bytes{};
            const auto count = ::recv(
                socket_, bytes.data(), static_cast<int>(bytes.size()), 0
            );
            if (count > 0) {
                result.append(bytes.data(), static_cast<std::size_t>(count));
                continue;
            }
            if (count == 0) return {ReadState::closed, std::move(result)};
            return {last_read_state(), std::move(result)};
        }
    }

private:
    [[nodiscard]] static ReadState last_read_state() noexcept
    {
#ifdef _WIN32
        const auto error = WSAGetLastError();
        if (error == WSAECONNRESET || error == WSAESHUTDOWN
            || error == WSAENOTCONN) {
            return ReadState::closed;
        }
        if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
            return ReadState::timed_out;
        }
#else
        if (errno == ECONNRESET || errno == ENOTCONN) return ReadState::closed;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
            return ReadState::timed_out;
        }
#endif
        return ReadState::failed;
    }

    [[nodiscard]] ReadState receive_more()
    {
        std::array<char, 4'096> bytes{};
        const auto count = ::recv(
            socket_, bytes.data(), static_cast<int>(bytes.size()), 0
        );
        if (count > 0) {
            buffered_.append(bytes.data(), static_cast<std::size_t>(count));
            return ReadState::data;
        }
        if (count == 0) return ReadState::closed;
        return last_read_state();
    }

    void close() noexcept
    {
        if (socket_ == invalid_socket) return;
#ifdef _WIN32
        static_cast<void>(shutdown(socket_, SD_BOTH));
        static_cast<void>(closesocket(socket_));
#else
        static_cast<void>(shutdown(socket_, SHUT_RDWR));
        static_cast<void>(::close(socket_));
#endif
        socket_ = invalid_socket;
    }

    NativeSocket socket_{invalid_socket};
    std::string buffered_;
};

struct HttpResponseHead {
    int status{};
    std::vector<std::pair<std::string, std::string>> headers;

    [[nodiscard]] std::optional<std::string_view> header(
        const std::string_view name
    ) const noexcept
    {
        const auto found = std::find_if(headers.begin(), headers.end(), [&](const auto& item) {
            return item.first == name;
        });
        if (found == headers.end()) return std::nullopt;
        return found->second;
    }
};

[[nodiscard]] std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return value;
}

[[nodiscard]] std::optional<HttpResponseHead> parse_response_head(
    const std::string& raw
)
{
    std::istringstream input{raw};
    std::string line;
    if (!std::getline(input, line)) return std::nullopt;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream status_line{line};
    std::string version;
    HttpResponseHead result;
    if (!(status_line >> version >> result.status) || version != "HTTP/1.1") {
        return std::nullopt;
    }
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        const auto colon = line.find(':');
        if (colon == std::string::npos) return std::nullopt;
        auto name = lowercase(line.substr(0, colon));
        auto value = line.substr(colon + 1);
        const auto first = value.find_first_not_of(" \t");
        value = first == std::string::npos ? std::string{} : value.substr(first);
        result.headers.emplace_back(std::move(name), std::move(value));
    }
    return result;
}

[[nodiscard]] std::string websocket_request(
    const std::uint16_t port,
    const std::string_view target,
    const std::string_view version = "13",
    const std::optional<std::string_view> origin = std::nullopt,
    const std::string_view extra_headers = {}
)
{
    std::string result = "GET ";
    result.append(target);
    result.append(" HTTP/1.1\r\nHost: 127.0.0.1:");
    result.append(std::to_string(port));
    result.append(
        "\r\nUpgrade: websocket"
        "\r\nConnection: Upgrade"
        "\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ=="
        "\r\nSec-WebSocket-Version: "
    );
    result.append(version);
    result.append("\r\n");
    if (origin) {
        result.append("Origin: ");
        result.append(*origin);
        result.append("\r\n");
    }
    result.append(extra_headers);
    result.append("\r\n");
    return result;
}

[[nodiscard]] std::optional<HttpResponseHead> transact_head(
    RawConnection& connection,
    const std::string_view request
)
{
    if (!connection.connected() || !connection.send_all(request)) return std::nullopt;
    const auto raw = connection.read_through("\r\n\r\n");
    return raw ? parse_response_head(*raw) : std::nullopt;
}

[[nodiscard]] std::optional<std::uint16_t> read_close_code(
    RawConnection& connection
)
{
    for (int frame_index = 0; frame_index < 4; ++frame_index) {
        const auto prefix = connection.read_exact(2);
        if (!prefix) return std::nullopt;
        const auto opcode = static_cast<unsigned char>((*prefix)[0]) & 0x0fU;
        const auto second = static_cast<unsigned char>((*prefix)[1]);
        if ((second & 0x80U) != 0) return std::nullopt;
        std::uint64_t length = second & 0x7fU;
        if (length == 126U) {
            const auto extended = connection.read_exact(2);
            if (!extended) return std::nullopt;
            length = (static_cast<std::uint64_t>(static_cast<unsigned char>((*extended)[0])) << 8U)
                | static_cast<unsigned char>((*extended)[1]);
        } else if (length == 127U) {
            const auto extended = connection.read_exact(8);
            if (!extended) return std::nullopt;
            length = 0;
            for (const auto byte : *extended) {
                length = (length << 8U) | static_cast<unsigned char>(byte);
            }
        }
        if (length > 1'024U) return std::nullopt;
        const auto payload = connection.read_exact(static_cast<std::size_t>(length));
        if (!payload) return std::nullopt;
        if (opcode != 0x08U) continue;
        if (payload->size() < 2) return std::uint16_t{1005};
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(static_cast<unsigned char>((*payload)[0])) << 8U)
            | static_cast<unsigned char>((*payload)[1])
        );
    }
    return std::nullopt;
}

class IdleDriver final : public service_ws::SessionDriver {
public:
    [[nodiscard]] service_ws::DriverResult input(
        service_ws::Frame,
        std::stop_token
    ) override
    {
        return {};
    }

    [[nodiscard]] service_ws::DriverResult heartbeat(std::stop_token) override
    {
        return {};
    }

    void closed() noexcept override { closed_.store(true); }

private:
    std::atomic<bool> closed_{};
};

class TestFactory final : public service_ws::SessionFactory {
public:
    explicit TestFactory(const bool fail = false) : fail_(fail) {}

    [[nodiscard]] std::unique_ptr<service_ws::SessionDriver> create(
        const service_ws::RequestMetadata&,
        std::shared_ptr<service_ws::OutboundSink>,
        std::stop_token
    ) override
    {
        creates_.fetch_add(1);
        if (fail_) return {};
        return std::make_unique<IdleDriver>();
    }

    [[nodiscard]] std::size_t creates() const noexcept { return creates_.load(); }

private:
    bool fail_{};
    std::atomic<std::size_t> creates_{};
};

struct ReceivedFrame final {
    std::mutex mutex;
    std::optional<service_ws::Frame> value;
};

class RecordingDriver final : public service_ws::SessionDriver {
public:
    explicit RecordingDriver(std::shared_ptr<ReceivedFrame> received)
        : received_(std::move(received))
    {}

    [[nodiscard]] service_ws::DriverResult input(
        service_ws::Frame frame,
        std::stop_token
    ) override
    {
        {
            std::lock_guard<std::mutex> lock{received_->mutex};
            received_->value = std::move(frame);
        }
        return {
            service_ws::SessionPhase::streaming,
            service_ws::TerminalAction::complete,
            {},
        };
    }

    [[nodiscard]] service_ws::DriverResult heartbeat(std::stop_token) override
    {
        return {};
    }

    void closed() noexcept override {}

private:
    std::shared_ptr<ReceivedFrame> received_;
};

class RecordingFactory final : public service_ws::SessionFactory {
public:
    explicit RecordingFactory(std::shared_ptr<ReceivedFrame> received)
        : received_(std::move(received))
    {}

    [[nodiscard]] std::unique_ptr<service_ws::SessionDriver> create(
        const service_ws::RequestMetadata&,
        std::shared_ptr<service_ws::OutboundSink>,
        std::stop_token
    ) override
    {
        return std::make_unique<RecordingDriver>(received_);
    }

private:
    std::shared_ptr<ReceivedFrame> received_;
};

[[nodiscard]] service_http::HttpHostRouterConfig router_config(
    std::shared_ptr<service_ws::SessionFactory> sessions
)
{
    service_http::HttpHostRouterConfig result;
    result.service = {"BAAS WebSocket Wire Test", "test"};
    result.health_snapshot = service_router::HealthSnapshot{
        {{"wire", service_router::HealthValue{service_router::HealthObject{
            {"running", service_router::HealthValue{true}},
        }}}},
        {true, 1, "d2lyZS10ZXN0LWtleQ=="},
    };
    result.websocket_sessions = std::move(sessions);
    return result;
}

[[nodiscard]] service_http::HttpHostConfig host_config()
{
    service_http::HttpHostConfig result;
    result.worker_count = service_ws::websocket_platform_min_connections + 2;
    result.max_queued_requests = 16;
    result.ready_timeout = 1s;
    // Keep this longer than the asserted shutdown bound so the teardown test
    // proves that WebSocket interrupt wakes a blocked read instead of merely
    // waiting for cpp-httplib's ordinary socket timeout.
    result.read_timeout = 5s;
    result.write_timeout = 1s;
    result.idle_interval = 10ms;
    result.websocket.max_connections = service_ws::websocket_platform_min_connections;
    result.websocket.http_worker_reserve = 2;
    result.websocket.heartbeat_interval = 100ms;
    // Keep protocol timeouts comfortably above client socket-timeout and
    // capacity assertions so slow CI machines do not close valid sessions.
    result.websocket.handshake_timeout = 5s;
    result.websocket.close_grace = 20ms;
    result.websocket.shutdown_timeout = 1s;
    return result;
}

class RunningHost final {
public:
    explicit RunningHost(std::shared_ptr<service_ws::SessionFactory> sessions)
        : host_(router_config(std::move(sessions)), {}, host_config()),
          start_(host_.start())
    {
        check(start_.started && start_.port != 0,
              "wire test host must bind an ephemeral IPv4 loopback port");
    }

    ~RunningHost() { host_.stop(); }

    [[nodiscard]] std::uint16_t port() const noexcept { return start_.port; }
    [[nodiscard]] bool started() const noexcept { return start_.started; }
    [[nodiscard]] service_http::HttpHost& host() noexcept { return host_; }

private:
    service_http::HttpHost host_;
    service_http::HttpHostStartResult start_;
};

void test_handshake_rejections_are_visible_on_the_wire()
{
    auto factory = std::make_shared<TestFactory>();
    RunningHost running{factory};
    if (!running.started()) return;

    for (const auto [target, label] : {
             std::pair{std::string_view{"/ws/control?x=1"},
                       std::string_view{"query target must be rejected"}},
             std::pair{std::string_view{"/ws/%63ontrol"},
                       std::string_view{"percent-encoded route must be rejected"}},
             std::pair{std::string_view{"/ws/control#fragment"},
                       std::string_view{"fragment target must be rejected"}},
         }) {
        RawConnection connection{running.port()};
        const auto response = transact_head(
            connection, websocket_request(running.port(), target)
        );
        check(response && response->status == 400, label);
    }
    check(factory->creates() == 0,
          "pre-upgrade rejection must not create a WebSocket session");

    RawConnection version{running.port()};
    const auto unsupported = transact_head(
        version, websocket_request(running.port(), "/ws/control", "12")
    );
    check(unsupported && unsupported->status == 426,
          "unsupported WebSocket version must return 426");
    check(unsupported && unsupported->header("sec-websocket-version")
              == std::optional<std::string_view>{"13"},
          "426 response must advertise Sec-WebSocket-Version 13");
}

void test_rejected_body_cannot_become_a_pipelined_request()
{
    auto factory = std::make_shared<TestFactory>();
    RunningHost running{factory};
    if (!running.started()) return;

    RawConnection connection{running.port()};
    auto request = websocket_request(
        running.port(), "/ws/control?rejected=1", "13", std::nullopt,
        "Content-Length: 5\r\n"
    );
    request.append("abcdeGET /health HTTP/1.1\r\nHost: 127.0.0.1:");
    request.append(std::to_string(running.port()));
    request.append("\r\nConnection: close\r\n\r\n");
    check(connection.send_all(request),
          "rejected upgrade plus pipelined body must reach the loopback host");
    const auto response_head = connection.read_through("\r\n\r\n");
    const auto parsed = response_head ? parse_response_head(*response_head) : std::nullopt;
    check(parsed && parsed->status == 400,
          "upgrade with a non-zero body must be rejected before routing");
    const auto [state, remainder] = connection.read_until_end();
    check(state == ReadState::closed,
          "pre-upgrade rejection must close instead of retaining keep-alive");
    const auto combined = (response_head ? *response_head : std::string{}) + remainder;
    const auto first = combined.find("HTTP/1.1 ");
    const auto second = first == std::string::npos
        ? std::string::npos : combined.find("HTTP/1.1 ", first + 1);
    check(first != std::string::npos && second == std::string::npos,
          "rejected request body must never be parsed as a second HTTP request");
}

void test_post_upgrade_terminal_codes()
{
    {
        auto factory = std::make_shared<TestFactory>();
        RunningHost running{factory};
        if (running.started()) {
            RawConnection connection{running.port()};
            const auto response = transact_head(
                connection,
                websocket_request(
                    running.port(), "/ws/control", "13",
                    "https://evil.example"
                )
            );
            check(response && response->status == 101,
                  "Origin policy must run after a valid protocol upgrade");
            check(read_close_code(connection)
                      == std::optional<std::uint16_t>{
                          service_ws::websocket_close_origin_rejected},
                  "rejected Origin must emit close code 4403");
            check(factory->creates() == 0,
                  "Origin rejection must precede SessionFactory creation");
        }
    }

    {
        auto factory = std::make_shared<TestFactory>(true);
        RunningHost running{factory};
        if (running.started()) {
            RawConnection connection{running.port()};
            const auto response = transact_head(
                connection, websocket_request(running.port(), "/ws/control")
            );
            check(response && response->status == 101,
                  "factory failure must occur after a valid 101 upgrade");
            check(read_close_code(connection)
                      == std::optional<std::uint16_t>{
                          service_ws::websocket_close_internal_error},
                  "factory failure must emit close code 1011");
            check(factory->creates() == 1,
                  "factory failure test must exercise exactly one creation attempt");
        }
    }
}

void test_client_read_timeout_is_not_overridden_by_server_polling()
{
    auto factory = std::make_shared<TestFactory>();
    RunningHost running{factory};
    if (!running.started()) return;

    httplib::ws::WebSocketClient client{
        "ws://127.0.0.1:" + std::to_string(running.port()) + "/ws/control"
    };
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(0, 250'000);
    check(client.is_valid() && client.connect(),
          "cpp-httplib WebSocketClient must connect to the loopback host");
    if (!client.is_open()) return;

    std::string message;
    const auto started = std::chrono::steady_clock::now();
    const auto result = client.read(message);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    check(result == httplib::ws::Fail,
          "an idle WebSocketClient read must report its configured timeout");
    check(elapsed < 2s,
          "server interrupt polling must not replace the client's read timeout");
}

void test_interrupt_polling_preserves_a_slow_partial_frame()
{
    auto received = std::make_shared<ReceivedFrame>();
    auto factory = std::make_shared<RecordingFactory>(received);
    RunningHost running{factory};
    if (!running.started()) return;

    RawConnection connection{running.port()};
    const auto response = transact_head(
        connection, websocket_request(running.port(), "/ws/control")
    );
    check(response && response->status == 101,
          "slow-frame test must complete a valid WebSocket upgrade");
    if (!response || response->status != 101) return;

    constexpr std::array<unsigned char, 4> mask{0x11, 0x22, 0x33, 0x44};
    constexpr std::string_view payload{"hello"};
    std::string frame;
    frame.push_back(static_cast<char>(0x81));
    frame.push_back(static_cast<char>(0x80U | payload.size()));
    for (const auto byte : mask) frame.push_back(static_cast<char>(byte));
    for (std::size_t index = 0; index < payload.size(); ++index) {
        frame.push_back(static_cast<char>(
            static_cast<unsigned char>(payload[index]) ^ mask[index % mask.size()]
        ));
    }

    check(connection.send_all(std::string_view{frame}.substr(0, 1)),
          "first partial frame byte must be sent");
    std::this_thread::sleep_for(150ms);
    check(connection.send_all(std::string_view{frame}.substr(1, 3)),
          "partial header and mask bytes must be sent after one poll interval");
    std::this_thread::sleep_for(150ms);
    check(connection.send_all(std::string_view{frame}.substr(4, 3)),
          "remaining mask and first payload byte must cross another poll interval");
    std::this_thread::sleep_for(150ms);
    check(connection.send_all(std::string_view{frame}.substr(7)),
          "remaining payload bytes must be sent after a third poll interval");

    check(read_close_code(connection) == std::optional<std::uint16_t>{1000},
          "a slow valid frame must reach the driver and complete normally");
    std::lock_guard<std::mutex> lock{received->mutex};
    check(received->value
              && received->value->kind == service_ws::FrameKind::text
              && received->value->payload == payload,
          "poll timeouts must preserve partial header, mask, and payload bytes");
}

void test_capacity_keeps_health_reserve_and_idle_stop_is_bounded()
{
    auto factory = std::make_shared<TestFactory>();
    RunningHost running{factory};
    if (!running.started()) return;

    std::vector<RawConnection> sessions;
    sessions.reserve(service_ws::websocket_platform_min_connections);
    for (std::size_t index = 0;
         index < service_ws::websocket_platform_min_connections; ++index) {
        RawConnection connection{running.port()};
        const auto response = transact_head(
            connection, websocket_request(running.port(), "/ws/control")
        );
        check(response && response->status == 101,
              "every configured steady-state WebSocket slot must upgrade with 101");
        if (index == 0) {
            check(response && response->header("sec-websocket-accept")
                      == std::optional<std::string_view>{
                          "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="},
                  "101 must carry the RFC 6455 accept value for the client key");
        }
        sessions.push_back(std::move(connection));
    }
    check(wait_until([&] {
              return factory->creates()
                  == service_ws::websocket_platform_min_connections;
          }),
          "configured WebSocket capacity must be admitted on real sockets");
    check(wait_until([&] {
              return running.host().websocket_stats().active_connections
                  == service_ws::websocket_platform_min_connections;
          }),
          "real sockets must occupy every configured WebSocket slot");

    RawConnection health{running.port()};
    std::string health_request = "GET /health HTTP/1.1\r\nHost: 127.0.0.1:";
    health_request.append(std::to_string(running.port()));
    health_request.append("\r\nConnection: close\r\n\r\n");
    const auto health_response = transact_head(health, health_request);
    check(health_response && health_response->status == 200,
          "HTTP health must remain available at full WebSocket capacity");

    RawConnection overflow{running.port()};
    const auto overflow_response = transact_head(
        overflow, websocket_request(running.port(), "/ws/control")
    );
    check(overflow_response && overflow_response->status == 101,
          "capacity rejection must use a WebSocket close after protocol upgrade");
    check(read_close_code(overflow)
              == std::optional<std::uint16_t>{service_ws::websocket_close_capacity},
          "capacity plus one must emit close code 1013");

    const auto started = std::chrono::steady_clock::now();
    running.host().stop();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    check(elapsed < 2s,
          "stopping idle real WebSocket connections must remain bounded");
    if (running.host().state() != service_http::HttpHostState::stopped) {
        const auto stats = running.host().websocket_stats();
        std::cerr << "stop diagnostics: state="
                  << static_cast<int>(running.host().state())
                  << " active=" << stats.active_connections
                  << " shutdown_interrupts=" << stats.shutdown_interrupts
                  << " shutdown_timeouts=" << stats.shutdown_timeouts
                  << " last_error='" << running.host().last_error_message() << "'\n";
    }
    check(running.host().state() == service_http::HttpHostState::stopped,
          "bounded idle stop must leave HttpHost stopped");

    if (running.host().state() == service_http::HttpHostState::stopped) {
        const auto restarted = running.host().start();
        check(restarted.started && restarted.port != 0,
              "the same WebSocket-enabled HttpHost must restart after stop");
        if (restarted.started) {
            RawConnection restarted_session{restarted.port};
            const auto restarted_response = transact_head(
                restarted_session,
                websocket_request(restarted.port, "/ws/control")
            );
            check(restarted_response && restarted_response->status == 101,
                  "the restarted HttpHost must accept a WebSocket upgrade");
            running.host().stop();
            check(running.host().state() == service_http::HttpHostState::stopped,
                  "the restarted WebSocket-enabled HttpHost must stop again");
        }
    }
}

}  // namespace

int main()
{
    SocketRuntime sockets;
    if (!sockets.ready()) {
        std::cerr << "FAIL: native socket runtime initialization failed\n";
        return EXIT_FAILURE;
    }
    test_handshake_rejections_are_visible_on_the_wire();
    test_rejected_body_cannot_become_a_pipelined_request();
    test_post_upgrade_terminal_codes();
    test_client_read_timeout_is_not_overridden_by_server_polling();
    test_interrupt_polling_preserves_a_slow_partial_frame();
    test_capacity_keeps_health_reserve_and_idle_stop_is_bounded();
    if (failures != 0) {
        std::cerr << failures << " WebSocket wire test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "WebSocket wire tests passed\n";
    return EXIT_SUCCESS;
}
