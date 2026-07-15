#pragma once

#include "service/protocol/PipeFraming.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <stop_token>
#include <thread>
#include <vector>

namespace baas::service::pipe {

namespace bpip = baas::service::protocol::bpip;

enum class PipeChannel : std::uint8_t { provider, sync, trigger, remote };

[[nodiscard]] std::string_view pipe_channel_name(PipeChannel channel) noexcept;

struct PipeOpenRequest {
    PipeChannel channel{PipeChannel::provider};
    std::string name;
};

struct PipeHostLimits {
    std::size_t max_connections{16};
    std::size_t max_open_json_bytes{4U * 1'024U};
    std::size_t max_name_bytes{128};
    std::size_t max_read_chunk_bytes{64U * 1'024U};
    std::size_t max_atomic_write_bytes{72U * 1'024U * 1'024U};
    std::size_t max_total_ingress_retained_bytes{128U * 1'024U * 1'024U};
    std::size_t max_total_egress_retained_bytes{128U * 1'024U * 1'024U};
    std::size_t max_open_json_depth{16};
    std::size_t max_open_json_nodes{256};
    std::chrono::milliseconds open_timeout{5'000};
    std::chrono::milliseconds idle_read_timeout{60'000};
    std::chrono::milliseconds write_timeout{10'000};
};

enum class PipeHostError : std::uint8_t {
    none,
    invalid_limits,
    listener_closed,
    connection_limit,
    read_failed,
    truncated_frame,
    framing_error,
    first_frame_not_json,
    open_json_too_large,
    malformed_open_json,
    duplicate_open_field,
    invalid_open_type,
    unsupported_channel,
    invalid_open_name,
    unsupported_frame_kind,
    nonempty_close,
    channel_unavailable,
    handler_failed,
    atomic_write_too_large,
    ingress_budget_exhausted,
    egress_budget_exhausted,
    write_failed,
    open_timeout,
    read_timeout,
};

[[nodiscard]] std::string_view pipe_host_error_name(PipeHostError error) noexcept;

struct PipeOpenResult {
    std::optional<PipeOpenRequest> request;
    PipeHostError error{PipeHostError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == PipeHostError::none && request.has_value();
    }
};

[[nodiscard]] PipeOpenResult decode_pipe_open(
    std::span<const std::byte> payload,
    const PipeHostLimits& limits = {}
);

[[nodiscard]] bpip::EncodeResult encode_pipe_open_ok(PipeChannel channel);

struct PipeIoResult {
    std::size_t bytes{};
    bool eof{};
    bool error{};
    bool timed_out{};
};

// The platform implementation guarantees one stream owner. A successful
// write_all must consume the entire buffer; partial success is reported as an
// error and is connection-fatal at the host boundary.
class PipeStream {
public:
    virtual ~PipeStream() = default;
    [[nodiscard]] virtual PipeIoResult read(
        std::span<std::byte> output,
        std::chrono::milliseconds timeout
    ) = 0;
    [[nodiscard]] virtual PipeIoResult write_all(
        std::span<const std::byte> input,
        std::chrono::milliseconds timeout
    ) = 0;
    virtual void close() noexcept = 0;
};

class PipeListener {
public:
    virtual ~PipeListener() = default;
    [[nodiscard]] virtual std::unique_ptr<PipeStream> accept() = 0;
    virtual void close() noexcept = 0;
};

class PipeHost;

class PipeConnectionWriter final {
public:
    PipeConnectionWriter(const PipeConnectionWriter&) = delete;
    PipeConnectionWriter& operator=(const PipeConnectionWriter&) = delete;

    [[nodiscard]] PipeHostError write_frame(
        bpip::FrameKind kind,
        std::span<const std::byte> payload
    );
    // Encodes all frames into one owning buffer and performs one logical
    // write_all operation, preserving JSON+BYTES adjacency.
    [[nodiscard]] PipeHostError write_batch(std::span<const bpip::Frame> frames);

private:
    [[nodiscard]] bool transport_poisoned() noexcept;
    PipeConnectionWriter(
        PipeHost& host,
        PipeStream& stream,
        std::size_t max_atomic_write_bytes,
        std::chrono::milliseconds write_timeout)
        : host_(host), stream_(stream), max_atomic_write_bytes_(max_atomic_write_bytes),
          write_timeout_(write_timeout)
    {}

    friend class PipeHost;
    PipeHost& host_;
    PipeStream& stream_;
    std::size_t max_atomic_write_bytes_{};
    std::chrono::milliseconds write_timeout_{};
    std::mutex mutex_;
    bool transport_poisoned_{};
};

enum class PipeHandlerAction : std::uint8_t { continue_connection, close_connection };

struct PipeHandlerResult {
    PipeHandlerAction action{PipeHandlerAction::continue_connection};
    PipeHostError error{PipeHostError::none};
};

class PipeChannelHandler {
public:
    virtual ~PipeChannelHandler() = default;
    // Errors are propagated to the host. A transport-poisoned writer always
    // forces a direct close with no second write, regardless of this result.
    [[nodiscard]] virtual PipeHandlerResult on_frame(
        const bpip::Frame& frame,
        PipeConnectionWriter& writer,
        std::stop_token stop_token
    ) = 0;
    virtual void on_close(std::stop_token stop_token) noexcept = 0;
};

class PipeChannelFactory {
public:
    virtual ~PipeChannelFactory() = default;
    // Production factories and handlers must cooperatively observe this token
    // around potentially blocking work so stop()+join() remains bounded.
    [[nodiscard]] virtual std::unique_ptr<PipeChannelHandler> create(
        const PipeOpenRequest& request,
        std::stop_token stop_token
    ) = 0;
};

enum class PipeHostState : std::uint8_t { stopped, running, stopping };

struct PipeHostStats {
    std::size_t accepted{};
    std::size_t rejected{};
    std::size_t completed{};
    std::size_t active{};
    std::size_t peak_active{};
    std::size_t ingress_retained_bytes{};
    std::size_t peak_ingress_retained_bytes{};
    std::size_t egress_retained_bytes{};
    std::size_t peak_egress_retained_bytes{};
    std::size_t ingress_budget_rejections{};
    std::size_t egress_budget_rejections{};
    PipeHostState state{PipeHostState::stopped};
};

// A bounded, blocking foundation host. Real backends are constructed through
// make_platform_pipe_listener(); tests inject deterministic fake listeners.
// No BAAS application singleton or command handler is referenced here.
class PipeHost final {
public:
    PipeHost(
        std::unique_ptr<PipeListener> listener,
        std::shared_ptr<PipeChannelFactory> factory,
        PipeHostLimits limits = {}
    );
    ~PipeHost();

    PipeHost(const PipeHost&) = delete;
    PipeHost& operator=(const PipeHost&) = delete;
    PipeHost(PipeHost&&) = delete;
    PipeHost& operator=(PipeHost&&) = delete;

    // One-shot. The first call consumes the listener even if thread creation
    // partially fails; every later call returns false.
    [[nodiscard]] bool start();
    void stop() noexcept;
    // Precondition for an external caller: stop() has already been requested.
    // A worker callback may call stop()+join(), but that self-join returns
    // immediately. The external owner must keep this object alive and later
    // call join() from a non-worker thread before destruction.
    void join() noexcept;

    [[nodiscard]] PipeHostState state() const noexcept;
    [[nodiscard]] PipeHostStats stats() const noexcept;
    [[nodiscard]] const PipeHostLimits& limits() const noexcept { return limits_; }

private:
    friend class PipeConnectionWriter;
    [[nodiscard]] bool try_reserve_ingress(std::size_t bytes) noexcept;
    void release_ingress(std::size_t bytes) noexcept;
    [[nodiscard]] bool try_reserve_egress(std::size_t bytes) noexcept;
    void release_egress(std::size_t bytes) noexcept;
    void accept_loop() noexcept;
    void worker_loop() noexcept;
    void connection_loop(std::unique_ptr<PipeStream> stream) noexcept;
    [[nodiscard]] PipeHostError process_frame(
        const bpip::Frame& frame,
        bool& opened,
        bool& continue_connection,
        std::unique_ptr<PipeChannelHandler>& handler,
        PipeConnectionWriter& writer
    );

    std::unique_ptr<PipeListener> listener_;
    std::shared_ptr<PipeChannelFactory> factory_;
    PipeHostLimits limits_;
    mutable std::mutex mutex_;
    std::mutex join_mutex_;
    std::condition_variable stopped_cv_;
    std::condition_variable queue_cv_;
    std::thread accept_thread_;
    std::vector<std::thread> workers_;
    std::deque<std::unique_ptr<PipeStream>> pending_streams_;
    std::vector<PipeStream*> active_streams_;
    PipeHostState state_{PipeHostState::stopped};
    std::size_t accepted_{};
    std::size_t rejected_{};
    std::size_t completed_{};
    std::size_t active_{};
    std::size_t peak_active_{};
    std::size_t ingress_retained_bytes_{};
    std::size_t peak_ingress_retained_bytes_{};
    std::size_t egress_retained_bytes_{};
    std::size_t peak_egress_retained_bytes_{};
    std::size_t ingress_budget_rejections_{};
    std::size_t egress_budget_rejections_{};
    std::stop_source stop_source_;
    bool start_attempted_{};
};

// Creates a compile-time selected Windows named-pipe or Unix-domain listener.
// Construction validates endpoint security but does not accept or start any
// thread. Failure returns nullptr with a stable, non-sensitive diagnostic.
[[nodiscard]] std::unique_ptr<PipeListener> make_platform_pipe_listener(
    std::string_view endpoint,
    std::size_t max_connections,
    std::string& diagnostic
) noexcept;

}  // namespace baas::service::pipe
