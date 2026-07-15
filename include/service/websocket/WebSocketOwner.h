#pragma once

#include "service/http/OriginPolicy.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace httplib {
class Server;
}

namespace baas::service::websocket {

#if defined(BAAS_SERVICE_WEBSOCKET_TEST_HOOKS)
struct WebSocketOwnerTestAccess final {
    static void fail_next_enqueue_allocation() noexcept;
};
#endif

inline constexpr std::size_t websocket_max_frame_bytes = 64U * 1'024U * 1'024U;
inline constexpr std::uint16_t websocket_close_origin_rejected = 4403;
inline constexpr std::uint16_t websocket_close_authentication_failed = 4401;
inline constexpr std::uint16_t websocket_close_internal_error = 1011;
inline constexpr std::uint16_t websocket_close_capacity = 1013;

#if defined(__ANDROID__)
inline constexpr std::size_t websocket_platform_min_connections = 4;
#else
inline constexpr std::size_t websocket_platform_min_connections = 5;
#endif

enum class Channel : std::uint8_t { control, provider, sync, trigger, remote };
enum class FrameKind : std::uint8_t { text, binary };

struct Frame {
    FrameKind kind{FrameKind::text};
    // std::string is an owning byte string for both text and binary. It lets
    // cpp-httplib read payloads move into the driver without a 64 MiB copy.
    std::string payload;
};

// Frames in one batch are written consecutively while holding the connection
// writer lock. This expresses text-only, binary-only, and JSON-plus-binary.
struct OutboundBatch {
    std::vector<Frame> frames;
};

enum class EnqueueResult : std::uint8_t {
    accepted,
    closed,
    empty_batch,
    too_many_frames,
    frame_too_large,
    queue_full,
    queued_bytes_exceeded,
    resource_exhausted,
};

enum class BatchWriteResult : std::uint8_t { written, failed };

// Optional per-enqueue observer. A non-null observer is completed exactly once:
// synchronously with failed when enqueue rejects the batch, or asynchronously
// after an accepted batch is wholly written or made permanently unsendable.
// complete() may run on the enqueue, writer, or teardown thread. Implementations
// must return promptly and must not throw. The owner invokes it without holding
// its queue, registry, transport, or writer mutex.
class BatchCompletion {
public:
    virtual ~BatchCompletion() = default;
    virtual void complete(BatchWriteResult result) noexcept = 0;
};

enum class TerminalAction : std::uint8_t;

class OutboundSink {
public:
    virtual ~OutboundSink() = default;
    [[nodiscard]] virtual EnqueueResult enqueue(
        OutboundBatch batch,
        std::shared_ptr<BatchCompletion> completion = {}
    ) = 0;
    virtual void terminate(TerminalAction action) noexcept = 0;
};

struct RequestMetadata {
    Channel channel{Channel::control};
    std::string path;
    // A bounded copy of Cookie is supplied for the existing control-resume
    // protocol. Authentication remains inside the stateful driver.
    std::string cookie;
    bool malformed_cookie_cardinality{};
    std::chrono::milliseconds heartbeat_interval{3'000};
    std::chrono::milliseconds handshake_timeout{10'000};
};

enum class SessionPhase : std::uint8_t { handshaking, streaming };

enum class TerminalAction : std::uint8_t {
    none,
    authentication_failed,
    protocol_failed,
    capacity,
    internal_error,
    complete,
};

struct DriverResult {
    SessionPhase phase{SessionPhase::handshaking};
    TerminalAction terminal{TerminalAction::none};
    std::vector<OutboundBatch> batches;
};

// One instance owns the complete multi-step client_hello/auth/resume/
// secretstream state for one connection. input() is invoked only by the owner
// reader. heartbeat() is serialized with input() by the owner.
class SessionDriver {
public:
    virtual ~SessionDriver() = default;
    [[nodiscard]] virtual DriverResult input(
        Frame frame,
        std::stop_token stop
    ) = 0;
    [[nodiscard]] virtual DriverResult heartbeat(std::stop_token stop) = 0;
    virtual void closed() noexcept = 0;
};

class SessionFactory {
public:
    virtual ~SessionFactory() = default;

    // Creation allocates channel state only. It MUST NOT authenticate: Origin
    // has been checked, but protocol authentication begins with driver input.
    [[nodiscard]] virtual std::unique_ptr<SessionDriver> create(
        RequestMetadata request,
        std::shared_ptr<OutboundSink> outbound,
        std::stop_token stop
    ) = 0;
};

class Transport {
public:
    virtual ~Transport() = default;
    [[nodiscard]] virtual bool read(Frame& frame) = 0;
    [[nodiscard]] virtual bool write(const Frame& frame) = 0;

    // Sends a close frame without reading the peer response. This is safe from
    // the monitor/owner thread while the sole handler reader is blocked.
    [[nodiscard]] virtual bool request_close(
        std::uint16_t status,
        std::string_view reason
    ) noexcept = 0;

    // Immediately shuts down the socket. Implementations must make every
    // concurrent read(), write(), and request_close() return promptly; the
    // owner relies on this contract to bound handler teardown.
    virtual void interrupt() noexcept = 0;
};

struct WebSocketOwnerConfig {
    bool enabled{true};
    std::size_t max_connections{16};
    std::size_t http_worker_reserve{2};
    std::size_t max_frames_per_batch{2};
    std::size_t max_queued_batches{256};
    std::size_t max_queued_bytes{72U * 1'024U * 1'024U};
    std::size_t max_global_queued_bytes{256U * 1'024U * 1'024U};
    std::size_t max_frame_bytes{websocket_max_frame_bytes};
    std::size_t max_cookie_bytes{4U * 1'024U};
    std::chrono::milliseconds heartbeat_interval{3'000};
    std::chrono::milliseconds handshake_timeout{10'000};
    std::chrono::milliseconds close_grace{250};
    std::chrono::milliseconds shutdown_timeout{2'000};
};

struct WebSocketOwnerStats {
    std::size_t active_connections{};
    std::size_t peak_connections{};
    std::size_t accepted_connections{};
    std::size_t origin_rejections{};
    std::size_t capacity_rejections{};
    std::size_t authentication_rejections{};
    std::size_t protocol_rejections{};
    std::size_t internal_failures{};
    std::size_t outbound_rejections{};
    std::size_t write_failures{};
    std::size_t handshake_timeouts{};
    std::size_t shutdown_interrupts{};
    std::size_t shutdown_timeouts{};
    std::size_t heartbeat_ticks{};
    std::size_t global_queued_bytes{};
    std::size_t max_connections{};
    std::size_t http_worker_reserve{};
    bool accepting{};
};

class WebSocketOwner final {
public:
    WebSocketOwner(
        WebSocketOwnerConfig config,
        http::CorsPolicyConfig origin_config,
        std::shared_ptr<SessionFactory> sessions
    );
    ~WebSocketOwner();

    WebSocketOwner(const WebSocketOwner&) = delete;
    WebSocketOwner& operator=(const WebSocketOwner&) = delete;
    WebSocketOwner(WebSocketOwner&&) = delete;
    WebSocketOwner& operator=(WebSocketOwner&&) = delete;

    // Installs the four common exact routes and desktop-only /ws/remote on the
    // supplied cpp-httplib Server. Library heartbeat is explicitly disabled.
    void install(httplib::Server& server);

    // Deterministic transport-independent entry point used by contract tests.
    void serve(
        std::optional<std::string_view> origin,
        bool malformed_origin_cardinality,
        RequestMetadata request,
        Transport& transport
    );

    void begin_shutdown() noexcept;
    [[nodiscard]] bool finish_shutdown() noexcept;
    [[nodiscard]] bool shutdown() noexcept;
    [[nodiscard]] bool prepare_start() noexcept;
    [[nodiscard]] WebSocketOwnerStats stats() const noexcept;
    [[nodiscard]] const WebSocketOwnerConfig& config() const noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace baas::service::websocket
