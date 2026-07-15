#include "service/websocket/WebSocketOwner.h"
#include "service/websocket/WebSocketHandshake.h"

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#if !defined(BAAS_CPP_HTTPLIB_HAS_WEBSOCKET_INTERRUPT)
#error "BAAS WebSocket owner requires the patched cpp-httplib interrupt API"
#endif

namespace baas::service::websocket {
namespace {

using Clock = std::chrono::steady_clock;

#if defined(BAAS_SERVICE_WEBSOCKET_TEST_HOOKS)
std::atomic<bool> fail_next_enqueue_allocation_for_test{};
thread_local std::size_t rejected_payload_bytes_for_test{};
#endif

struct SharedStats {
    std::atomic<std::size_t> peak_connections{0};
    std::atomic<std::size_t> accepted_connections{0};
    std::atomic<std::size_t> origin_rejections{0};
    std::atomic<std::size_t> capacity_rejections{0};
    std::atomic<std::size_t> authentication_rejections{0};
    std::atomic<std::size_t> protocol_rejections{0};
    std::atomic<std::size_t> internal_failures{0};
    std::atomic<std::size_t> outbound_rejections{0};
    std::atomic<std::size_t> write_failures{0};
    std::atomic<std::size_t> handshake_timeouts{0};
    std::atomic<std::size_t> shutdown_interrupts{0};
    std::atomic<std::size_t> shutdown_timeouts{0};
    std::atomic<std::size_t> heartbeat_ticks{0};
    std::atomic<std::size_t> global_queued_bytes{0};
};

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result
) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    result = left + right;
    return true;
}

[[nodiscard]] std::pair<std::uint16_t, std::string_view> close_for(
    const TerminalAction action
) noexcept
{
    switch (action) {
        case TerminalAction::authentication_failed:
        case TerminalAction::protocol_failed:
            return {websocket_close_authentication_failed, "authentication failed"};
        case TerminalAction::capacity:
            return {websocket_close_capacity, "capacity exhausted"};
        case TerminalAction::internal_error:
            return {websocket_close_internal_error, "internal channel failure"};
        case TerminalAction::complete:
            return {1000, "complete"};
        case TerminalAction::none:
            break;
    }
    return {websocket_close_internal_error, "internal channel failure"};
}

void validate_config(const WebSocketOwnerConfig& config)
{
    if (config.max_connections < websocket_platform_min_connections
        || config.max_connections > 256) {
        throw std::invalid_argument(
            "WebSocket connection cap does not cover the platform steady state"
        );
    }
    if (config.http_worker_reserve < 2 || config.http_worker_reserve > 256) {
        throw std::invalid_argument("WebSocket HTTP worker reserve must be in 2..256");
    }
    if (config.max_frames_per_batch == 0 || config.max_frames_per_batch > 2
        || config.max_queued_batches == 0 || config.max_queued_bytes == 0
        || config.max_frame_bytes == 0
        || config.max_frame_bytes > websocket_max_frame_bytes
        || config.max_frame_bytes > config.max_queued_bytes
        || config.max_global_queued_bytes < config.max_queued_bytes
        || config.max_cookie_bytes == 0) {
        throw std::invalid_argument("WebSocket frame and queue limits are invalid");
    }
    if (config.heartbeat_interval <= std::chrono::milliseconds::zero()
        || config.handshake_timeout <= std::chrono::milliseconds::zero()
        || config.close_grace <= std::chrono::milliseconds::zero()
        || config.shutdown_timeout <= std::chrono::milliseconds::zero()
        || config.heartbeat_interval >= config.handshake_timeout
        || config.close_grace >= config.shutdown_timeout) {
        throw std::invalid_argument("WebSocket lifecycle durations are invalid");
    }
}

class HttplibTransport final : public Transport {
public:
    explicit HttplibTransport(httplib::ws::WebSocket& socket) noexcept : socket_(socket) {}

    [[nodiscard]] bool read(Frame& frame) override
    {
        std::string payload;
        const auto result = socket_.read(payload);
        if (result == httplib::ws::ReadResult::Fail) return false;
        frame.kind = result == httplib::ws::ReadResult::Text
            ? FrameKind::text : FrameKind::binary;
        frame.payload = std::move(payload);
        return true;
    }

    [[nodiscard]] bool write(const Frame& frame) override
    {
        if (frame.kind == FrameKind::binary) {
            return socket_.send(frame.payload.data(), frame.payload.size());
        }
        return socket_.send(frame.payload);
    }

    [[nodiscard]] bool request_close(
        const std::uint16_t status,
        const std::string_view reason
    ) noexcept override
    {
        return socket_.request_close(
            static_cast<httplib::ws::CloseStatus>(status), reason
        );
    }

    void interrupt() noexcept override { socket_.interrupt(); }

private:
    httplib::ws::WebSocket& socket_;
};

[[nodiscard]] std::optional<std::string_view> single_header(
    const httplib::Request& request,
    const char* name,
    bool& malformed
) noexcept
{
    const auto range = request.headers.equal_range(name);
    if (range.first == range.second) return std::nullopt;
    auto next = range.first;
    ++next;
    if (next != range.second) {
        malformed = true;
        return std::nullopt;
    }
    return range.first->second;
}

[[nodiscard]] Channel channel_for_path(const std::string_view path)
{
    if (path == "/ws/control") return Channel::control;
    if (path == "/ws/provider") return Channel::provider;
    if (path == "/ws/sync") return Channel::sync;
    if (path == "/ws/trigger") return Channel::trigger;
    if (path == "/ws/remote") return Channel::remote;
    throw std::invalid_argument("unknown WebSocket channel path");
}

}  // namespace

HttplibHandshakeEvaluation evaluate_httplib_handshake_request(
    const httplib::Request& request,
    const bool remote_enabled
)
{
    std::vector<HandshakeHeaderField> headers;
    headers.reserve(request.headers.size());
    for (const auto& [name, value] : request.headers) {
        headers.push_back({name, value});
    }
    auto handshake = validate_handshake(
        request.method,
        request.version,
        request.target,
        request.path,
        headers,
        remote_enabled
    );
    const bool advertise = handshake.error
        == HandshakeError::websocket_version_unsupported;
    return {std::move(handshake), advertise ? 426 : 400, advertise};
}

class WebSocketOwner::Impl final
    : public std::enable_shared_from_this<WebSocketOwner::Impl> {
public:
    struct Slot final : OutboundSink {
        struct QueuedBatch {
            OutboundBatch batch;
            std::size_t bytes{};
            std::shared_ptr<BatchCompletion> completion;
        };

        Slot(
            const std::uint64_t id_value,
            WebSocketOwnerConfig config_value,
            std::shared_ptr<SharedStats> stats_value,
            Transport& transport_value
        )
            : id(id_value),
              config(std::move(config_value)),
              stats(std::move(stats_value)),
              transport(&transport_value),
              connected_at(Clock::now()),
              next_heartbeat(connected_at + config.heartbeat_interval)
        {}

        [[nodiscard]] EnqueueResult enqueue(
            OutboundBatch batch,
            std::shared_ptr<BatchCompletion> completion
        ) override
        {
            if (batch.frames.empty()) {
                return reject_batch(
                    EnqueueResult::empty_batch,
                    std::move(batch), std::move(completion), 0);
            }
            if (batch.frames.size() > config.max_frames_per_batch) {
                return reject_batch(
                    EnqueueResult::too_many_frames,
                    std::move(batch), std::move(completion), 0);
            }
            std::size_t bytes = 0;
            for (const auto& frame : batch.frames) {
                if (frame.payload.size() > config.max_frame_bytes) {
                    return reject_batch(
                        EnqueueResult::frame_too_large,
                        std::move(batch), std::move(completion), 0);
                }
                if (!checked_add(bytes, frame.payload.size(), bytes)) {
                    return reject_batch(
                        EnqueueResult::queued_bytes_exceeded,
                        std::move(batch), std::move(completion), 0);
                }
            }
            if (!reserve_global(bytes)) {
                return reject_batch(
                    EnqueueResult::queued_bytes_exceeded,
                    std::move(batch), std::move(completion), 0);
            }
            EnqueueResult result = EnqueueResult::accepted;
            {
                std::lock_guard<std::mutex> lock{queue_mutex};
                if (!outbound_open) {
                    result = EnqueueResult::closed;
                } else if (outbound.size() >= config.max_queued_batches) {
                    result = EnqueueResult::queue_full;
                } else {
                    std::size_t updated = 0;
                    if (!checked_add(queued_bytes, bytes, updated)
                        || updated > config.max_queued_bytes) {
                        result = EnqueueResult::queued_bytes_exceeded;
                    } else {
                        try {
                            // Allocate the deque node before moving observer
                            // ownership so an allocation failure can still
                            // complete this enqueue attempt synchronously.
#if defined(BAAS_SERVICE_WEBSOCKET_TEST_HOOKS)
                            if (fail_next_enqueue_allocation_for_test.exchange(
                                    false, std::memory_order_acq_rel)) {
                                throw std::bad_alloc{};
                            }
#endif
                            outbound.emplace_back();
                            auto& queued = outbound.back();
                            queued.batch = std::move(batch);
                            queued.bytes = bytes;
                            queued.completion = std::move(completion);
                            queued_bytes = updated;
                        } catch (...) {
                            result = EnqueueResult::resource_exhausted;
                        }
                    }
                }
            }
            if (result != EnqueueResult::accepted) {
                return reject_batch(
                    result, std::move(batch), std::move(completion), bytes);
            }
            queue_changed.notify_one();
            return result;
        }

        void terminate(const TerminalAction action) noexcept override
        {
            if (action == TerminalAction::none) return;
            TerminalAction expected = TerminalAction::none;
            if (!terminal.compare_exchange_strong(expected, action)) return;
            account_terminal(action);
            stop_source.request_stop();
            {
                std::lock_guard<std::mutex> lock{queue_mutex};
                outbound_open = false;
                terminal_pending = true;
            }
            queue_changed.notify_all();
        }

        void interrupt() noexcept
        {
            std::shared_lock<std::shared_mutex> lock{transport_mutex};
            if (transport != nullptr) transport->interrupt();
        }

        void detach_transport() noexcept
        {
            std::unique_lock<std::shared_mutex> lock{transport_mutex};
            transport = nullptr;
        }

        void close_outbound() noexcept
        {
            std::deque<QueuedBatch> discarded;
            std::size_t released = 0;
            {
                std::lock_guard<std::mutex> lock{queue_mutex};
                outbound_open = false;
                for (const auto& queued : outbound) released += queued.bytes;
                discarded.swap(outbound);
                queued_bytes -= released;
                terminal_pending = false;
            }
            for (auto& queued : discarded) {
                std::vector<Frame> released_frames;
                released_frames.swap(queued.batch.frames);
            }
            release_global(released);
            queue_changed.notify_all();
            for (auto& queued : discarded) {
                notify_completion(
                    std::move(queued.completion), BatchWriteResult::failed);
            }
        }

        void start_writer()
        {
            writer = std::jthread([this](std::stop_token stop) { writer_loop(stop); });
        }

        void stop_threads() noexcept
        {
            stop_source.request_stop();
            close_outbound();
            writer.request_stop();
            queue_changed.notify_all();
            try {
                if (writer.joinable()) writer.join();
            } catch (...) {
                interrupt();
            }
            close_outbound();
        }

        void finish_terminal_writer() noexcept
        {
            const auto deadline = Clock::now() + config.shutdown_timeout;
            {
                std::unique_lock<std::mutex> lock{writer_state_mutex};
                if (!writer_state_changed.wait_until(
                        lock, deadline, [&] { return writer_done; }
                    )) {
                    interrupt();
                }
            }
            writer.request_stop();
            queue_changed.notify_all();
            try {
                if (writer.joinable()) writer.join();
            } catch (...) {
                interrupt();
            }
            close_outbound();
        }

        [[nodiscard]] EnqueueResult reject_batch(
            const EnqueueResult result,
            OutboundBatch batch,
            std::shared_ptr<BatchCompletion> completion,
            const std::size_t reserved_global_bytes
        ) noexcept
        {
#if defined(BAAS_SERVICE_WEBSOCKET_TEST_HOOKS)
            std::size_t retained = 0;
            for (const auto& frame : batch.frames) {
                if (!checked_add(retained, frame.payload.size(), retained)) {
                    retained = std::numeric_limits<std::size_t>::max();
                    break;
                }
            }
            rejected_payload_bytes_for_test = retained;
#endif
            {
                std::vector<Frame> released_frames;
                released_frames.swap(batch.frames);
            }
#if defined(BAAS_SERVICE_WEBSOCKET_TEST_HOOKS)
            rejected_payload_bytes_for_test = 0;
#endif
            release_global(reserved_global_bytes);
            stats->outbound_rejections.fetch_add(1, std::memory_order_relaxed);
            notify_completion(std::move(completion), BatchWriteResult::failed);
            return result;
        }

        static void notify_completion(
            std::shared_ptr<BatchCompletion> completion,
            const BatchWriteResult result
        ) noexcept
        {
            if (completion) completion->complete(result);
        }

        void account_terminal(const TerminalAction action) noexcept
        {
            auto* counter = &stats->internal_failures;
            if (action == TerminalAction::authentication_failed) {
                counter = &stats->authentication_rejections;
            } else if (action == TerminalAction::protocol_failed) {
                counter = &stats->protocol_rejections;
            } else if (action == TerminalAction::capacity) {
                counter = &stats->capacity_rejections;
            } else if (action == TerminalAction::complete) {
                return;
            }
            counter->fetch_add(1, std::memory_order_relaxed);
        }

        void writer_loop(const std::stop_token stop) noexcept
        {
            struct Done final {
                Slot& slot;
                ~Done()
                {
                    {
                        std::lock_guard<std::mutex> lock{slot.writer_state_mutex};
                        slot.writer_done = true;
                    }
                    slot.writer_state_changed.notify_all();
                }
            } done{*this};
            try {
                for (;;) {
                    QueuedBatch queued;
                    bool send_terminal = false;
                    TerminalAction terminal_action = TerminalAction::none;
                    {
                        std::unique_lock<std::mutex> lock{queue_mutex};
                        queue_changed.wait(lock, [&] {
                            return stop.stop_requested() || !outbound.empty()
                                || !outbound_open;
                        });
                        if (stop.stop_requested()) return;
                        if (!outbound.empty()) {
                            queued = std::move(outbound.front());
                            outbound.pop_front();
                        } else if (terminal_pending) {
                            terminal_pending = false;
                            send_terminal = true;
                            terminal_action = terminal.load(std::memory_order_acquire);
                        } else if (!outbound_open) {
                            return;
                        } else {
                            continue;
                        }
                    }
                    class PendingCompletion final {
                    public:
                        explicit PendingCompletion(
                            Slot& slot,
                            std::shared_ptr<BatchCompletion> completion,
                            const bool active) noexcept
                            : slot_(slot), completion_(std::move(completion)), active_(active)
                        {}
                        ~PendingCompletion()
                        {
                            if (!active_) return;
                            // Seal and fail queued work before exposing this
                            // active failure to a re-entrant observer.
                            slot_.close_outbound();
                            Slot::notify_completion(
                                std::move(completion_), BatchWriteResult::failed);
                        }
                        void written() noexcept
                        {
                            active_ = false;
                            Slot::notify_completion(
                                std::move(completion_), BatchWriteResult::written);
                        }

                    private:
                        Slot& slot_;
                        std::shared_ptr<BatchCompletion> completion_;
                        bool active_{};
                    } completion{
                        *this,
                        std::move(queued.completion),
                        !queued.batch.frames.empty()};
                    struct ActiveCharge final {
                        Slot& slot;
                        OutboundBatch& batch;
                        std::size_t bytes;
                        ~ActiveCharge() { release(); }
                        void release() noexcept
                        {
                            if (!batch.frames.empty()) {
                                std::vector<Frame> released_frames;
                                released_frames.swap(batch.frames);
                            }
                            if (bytes == 0) return;
                            const auto value = std::exchange(bytes, 0);
                            slot.release_active(value);
                        }
                    } active_charge{*this, queued.batch, queued.bytes};
                    if (send_terminal) {
                        const auto [code, reason] = close_for(terminal_action);
                        bool requested = false;
                        {
                            std::shared_lock<std::shared_mutex> lock{transport_mutex};
                            if (transport == nullptr) return;
                            std::lock_guard<std::mutex> write_lock{writer_mutex};
                            requested = transport->request_close(code, reason);
                        }
                        if (!requested) {
                            stats->write_failures.fetch_add(
                                1, std::memory_order_relaxed
                            );
                            interrupt();
                            return;
                        }
                        std::this_thread::sleep_for(config.close_grace);
                        interrupt();
                        return;
                    }
                    bool written = true;
                    {
                        std::shared_lock<std::shared_mutex> lock{transport_mutex};
                        if (transport == nullptr) {
                            written = false;
                        } else {
                            std::lock_guard<std::mutex> write_lock{writer_mutex};
                            for (const auto& frame : queued.batch.frames) {
                                if (!transport->write(frame)) {
                                    written = false;
                                    break;
                                }
                            }
                        }
                    }
                    if (!written) {
                        active_charge.release();
                        stats->write_failures.fetch_add(1, std::memory_order_relaxed);
                        stop_source.request_stop();
                        interrupt();
                        return;
                    }
                    active_charge.release();
                    completion.written();
                }
            } catch (...) {
                stats->write_failures.fetch_add(1, std::memory_order_relaxed);
                stop_source.request_stop();
                interrupt();
            }
        }

        [[nodiscard]] bool reserve_global(const std::size_t bytes) noexcept
        {
            auto current = stats->global_queued_bytes.load(std::memory_order_relaxed);
            for (;;) {
                std::size_t updated = 0;
                if (!checked_add(current, bytes, updated)
                    || updated > config.max_global_queued_bytes) {
                    return false;
                }
                if (stats->global_queued_bytes.compare_exchange_weak(
                        current, updated, std::memory_order_acq_rel,
                        std::memory_order_relaxed
                    )) {
                    return true;
                }
            }
        }

        void release_global(const std::size_t bytes) noexcept
        {
            if (bytes != 0) {
                stats->global_queued_bytes.fetch_sub(bytes, std::memory_order_acq_rel);
            }
        }

        void release_active(const std::size_t bytes) noexcept
        {
            if (bytes == 0) return;
            {
                std::lock_guard<std::mutex> lock{queue_mutex};
                queued_bytes -= bytes;
            }
            release_global(bytes);
        }

        std::uint64_t id{};
        WebSocketOwnerConfig config;
        std::shared_ptr<SharedStats> stats;
        std::shared_mutex transport_mutex;
        std::mutex writer_mutex;
        Transport* transport{};
        std::mutex queue_mutex;
        std::condition_variable queue_changed;
        std::deque<QueuedBatch> outbound;
        std::size_t queued_bytes{};
        bool outbound_open{true};
        bool terminal_pending{};
        std::mutex driver_mutex;
        std::unique_ptr<SessionDriver> driver;
        std::atomic<SessionPhase> phase{SessionPhase::handshaking};
        std::atomic<TerminalAction> terminal{TerminalAction::none};
        std::stop_source stop_source;
        const Clock::time_point connected_at;
        Clock::time_point next_heartbeat;
        std::jthread writer;
        std::mutex writer_state_mutex;
        std::condition_variable writer_state_changed;
        bool writer_done{};
    };

    Impl(
        WebSocketOwnerConfig config,
        http::CorsPolicyConfig origin_config,
        std::shared_ptr<SessionFactory> sessions
    )
        : config_(std::move(config)), sessions_(std::move(sessions))
    {
        validate_config(config_);
        origin_policy_ = std::make_unique<http::OriginPolicy>(std::move(origin_config));
        accepting_ = config_.enabled;
    }

    void install(httplib::Server& server)
    {
        if (!config_.enabled) return;
        ensure_scheduler();
        server.set_websocket_ping_interval(0);
        const auto self = shared_from_this();
        server.set_pre_routing_handler([self](
            const httplib::Request& request,
            httplib::Response& response
        ) noexcept {
            try {
#if defined(__ANDROID__)
                constexpr bool remote_enabled = false;
#else
                constexpr bool remote_enabled = true;
#endif
                const auto evaluation = evaluate_httplib_handshake_request(
                    request, remote_enabled);
                const auto& result = evaluation.handshake;
                if (result.decision == HandshakeDecision::not_websocket) {
                    return httplib::Server::HandlerResponse::Unhandled;
                }
                if (result.accepted()) {
                    return httplib::Server::HandlerResponse::Unhandled;
                }
                response.status = evaluation.rejection_status;
                if (evaluation.advertise_supported_version) {
                    response.set_header("Sec-WebSocket-Version", "13");
                }
                response.set_content(
                    std::string{"{\"error\":{\"code\":\"websocket_handshake_rejected\","
                                "\"reason\":\""}
                        + std::string{to_string(result.error)}
                        + "\"},\"ok\":false}",
                    "application/json; charset=utf-8"
                );
                return httplib::Server::HandlerResponse::Handled;
            } catch (...) {
                response.status = 400;
                return httplib::Server::HandlerResponse::Handled;
            }
        });
        install_route(server, "/ws/control", Channel::control, self);
        install_route(server, "/ws/provider", Channel::provider, self);
        install_route(server, "/ws/sync", Channel::sync, self);
        install_route(server, "/ws/trigger", Channel::trigger, self);
#if !defined(__ANDROID__)
        install_route(server, "/ws/remote", Channel::remote, self);
#endif
    }

    void serve(
        const std::optional<std::string_view> origin,
        const bool malformed_origin,
        RequestMetadata request,
        Transport& transport
    ) noexcept
    {
        std::shared_ptr<Slot> slot;
        try {
            ensure_scheduler();
            const auto origin_result = origin_policy_->evaluate({
                origin, "GET", std::nullopt, std::nullopt, malformed_origin,
            });
            if (!origin_result.allowed()) {
                stats_->origin_rejections.fetch_add(1, std::memory_order_relaxed);
                reject_without_slot(
                    transport, websocket_close_origin_rejected, "origin rejected"
                );
                return;
            }

            slot = admit(transport);
            if (!slot) {
                reject_without_slot(
                    transport, websocket_close_capacity, "capacity exhausted"
                );
                return;
            }
            slot->start_writer();
            if (request.malformed_cookie_cardinality
                || request.cookie.size() > config_.max_cookie_bytes) {
                slot->terminate(TerminalAction::protocol_failed);
            } else if (!sessions_) {
                slot->terminate(TerminalAction::internal_error);
            } else {
                std::lock_guard<std::mutex> lock{slot->driver_mutex};
                slot->driver = sessions_->create(
                    std::move(request), slot, slot->stop_source.get_token()
                );
            }
            if (slot->terminal.load(std::memory_order_acquire) == TerminalAction::none
                && !slot->driver) {
                slot->terminate(TerminalAction::internal_error);
            }
            while (slot->terminal.load(std::memory_order_acquire)
                       == TerminalAction::none
                && !slot->stop_source.stop_requested()) {
                Frame frame;
                if (!transport.read(frame)) break;
                if (slot->stop_source.stop_requested()) break;
                if (frame.payload.size() > config_.max_frame_bytes) {
                    slot->terminate(TerminalAction::protocol_failed);
                    break;
                }
                {
                    // This loop already owns the connection's sole reader. A
                    // frame has been removed from the transport queue, so it
                    // must wait for an in-flight heartbeat instead of being
                    // dropped. Only the shared scheduler may skip a busy
                    // driver with try_lock.
                    std::lock_guard<std::mutex> lock{slot->driver_mutex};
                    auto result = slot->driver->input(
                        std::move(frame), slot->stop_source.get_token()
                    );
                    if (!apply_driver_result(*slot, std::move(result))) break;
                }
            }
        } catch (...) {
            // WebSocket handlers are outside cpp-httplib's routing catch.
            if (slot) {
                slot->terminate(TerminalAction::internal_error);
            } else {
                reject_without_slot(
                    transport, websocket_close_internal_error,
                    "internal channel failure"
                );
            }
        }
        if (slot) release(std::move(slot));
    }

    void begin_shutdown() noexcept
    {
        std::vector<std::shared_ptr<Slot>> slots;
        try {
            {
                std::lock_guard<std::mutex> lock{registry_mutex_};
                accepting_ = false;
                slots.reserve(slots_.size());
                for (const auto& [id, slot] : slots_) {
                    static_cast<void>(id);
                    slots.push_back(slot);
                }
            }
            for (const auto& slot : slots) {
                slot->stop_source.request_stop();
            }
        } catch (...) {
            // Preserve the noexcept shutdown boundary even if snapshot
            // allocation fails; stop every registered producer in place.
            try {
                std::lock_guard<std::mutex> lock{registry_mutex_};
                accepting_ = false;
                for (const auto& [id, slot] : slots_) {
                    static_cast<void>(id);
                    slot->stop_source.request_stop();
                }
            } catch (...) {}
        }
        request_scheduler_stop();
    }

    [[nodiscard]] bool finish_shutdown() noexcept
    {
        try {
            std::vector<std::shared_ptr<Slot>> slots;
            {
                std::lock_guard<std::mutex> lock{registry_mutex_};
                accepting_ = false;
                for (const auto& [id, slot] : slots_) {
                    static_cast<void>(id);
                    slots.push_back(slot);
                }
            }
            for (const auto& slot : slots) {
                stats_->shutdown_interrupts.fetch_add(1, std::memory_order_relaxed);
                slot->stop_source.request_stop();
                slot->interrupt();
            }
            const auto deadline = Clock::now() + config_.shutdown_timeout;
            std::unique_lock<std::mutex> lock{registry_mutex_};
            if (!registry_changed_.wait_until(lock, deadline, [&] { return slots_.empty(); })) {
                lock.unlock();
                abandon_scheduler();
                stats_->shutdown_timeouts.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            lock.unlock();
            return join_scheduler();
        } catch (...) {
            abandon_scheduler();
            stats_->shutdown_timeouts.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    [[nodiscard]] bool shutdown() noexcept
    {
        begin_shutdown();
        return finish_shutdown();
    }

    [[nodiscard]] bool prepare_start() noexcept
    {
        try {
            {
                std::lock_guard<std::mutex> lock{scheduler_mutex_};
                if (scheduler_abandoned_) return false;
            }
            {
                std::lock_guard<std::mutex> lock{registry_mutex_};
                if (!slots_.empty()) return false;
                accepting_ = config_.enabled;
            }
            ensure_scheduler();
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] WebSocketOwnerStats stats() const noexcept
    {
        WebSocketOwnerStats result;
        {
            std::lock_guard<std::mutex> lock{registry_mutex_};
            result.active_connections = slots_.size();
            result.accepting = accepting_;
        }
        result.peak_connections = stats_->peak_connections.load(std::memory_order_relaxed);
        result.accepted_connections = stats_->accepted_connections.load(std::memory_order_relaxed);
        result.origin_rejections = stats_->origin_rejections.load(std::memory_order_relaxed);
        result.capacity_rejections = stats_->capacity_rejections.load(std::memory_order_relaxed);
        result.authentication_rejections = stats_->authentication_rejections.load(std::memory_order_relaxed);
        result.protocol_rejections = stats_->protocol_rejections.load(std::memory_order_relaxed);
        result.internal_failures = stats_->internal_failures.load(std::memory_order_relaxed);
        result.outbound_rejections = stats_->outbound_rejections.load(std::memory_order_relaxed);
        result.write_failures = stats_->write_failures.load(std::memory_order_relaxed);
        result.handshake_timeouts = stats_->handshake_timeouts.load(std::memory_order_relaxed);
        result.shutdown_interrupts = stats_->shutdown_interrupts.load(std::memory_order_relaxed);
        result.shutdown_timeouts = stats_->shutdown_timeouts.load(std::memory_order_relaxed);
        result.heartbeat_ticks = stats_->heartbeat_ticks.load(std::memory_order_relaxed);
        result.global_queued_bytes = stats_->global_queued_bytes.load(std::memory_order_relaxed);
        result.max_connections = config_.max_connections;
        result.http_worker_reserve = config_.http_worker_reserve;
        return result;
    }

    [[nodiscard]] const WebSocketOwnerConfig& config() const noexcept { return config_; }

private:
    static void install_route(
        httplib::Server& server,
        const char* path,
        const Channel channel,
        std::shared_ptr<Impl> self
    )
    {
        server.WebSocket(path, [self = std::move(self), channel](
            const httplib::Request& request,
            httplib::ws::WebSocket& socket
        ) noexcept {
            HttplibTransport transport{socket};
            try {
                bool malformed_origin = false;
                const auto origin = single_header(request, "Origin", malformed_origin);
                bool malformed_cookie = false;
                const auto cookie = single_header(request, "Cookie", malformed_cookie);
                RequestMetadata metadata;
                metadata.channel = channel;
                metadata.path = request.path;
                metadata.cookie = cookie.has_value() ? std::string{*cookie} : std::string{};
                metadata.malformed_cookie_cardinality = malformed_cookie;
                metadata.heartbeat_interval = self->config_.heartbeat_interval;
                metadata.handshake_timeout = self->config_.handshake_timeout;
                self->serve(origin, malformed_origin, std::move(metadata), transport);
            } catch (...) {
                static_cast<void>(transport.request_close(
                    websocket_close_internal_error, "internal channel failure"
                ));
                std::this_thread::sleep_for(self->config_.close_grace);
                transport.interrupt();
            }
        });
    }

    [[nodiscard]] std::shared_ptr<Slot> admit(Transport& transport)
    {
        std::lock_guard<std::mutex> lock{registry_mutex_};
        if (!accepting_ || slots_.size() >= config_.max_connections) {
            stats_->capacity_rejections.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
        if (next_slot_id_ == 0) {
            stats_->capacity_rejections.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
        const auto id = next_slot_id_++;
        auto slot = std::make_shared<Slot>(id, config_, stats_, transport);
        slots_.emplace(id, slot);
        const auto active = slots_.size();
        auto peak = stats_->peak_connections.load(std::memory_order_relaxed);
        while (peak < active && !stats_->peak_connections.compare_exchange_weak(
            peak, active, std::memory_order_relaxed
        )) {}
        stats_->accepted_connections.fetch_add(1, std::memory_order_relaxed);
        return slot;
    }

    void release(std::shared_ptr<Slot> slot) noexcept
    {
        slot->stop_source.request_stop();
        if (slot->terminal.load(std::memory_order_acquire) == TerminalAction::none) {
            slot->interrupt();
            slot->detach_transport();
            slot->stop_threads();
        } else {
            slot->finish_terminal_writer();
            slot->detach_transport();
        }
        {
            std::lock_guard<std::mutex> lock{slot->driver_mutex};
            if (slot->driver) slot->driver->closed();
            slot->driver.reset();
        }
        {
            std::lock_guard<std::mutex> lock{registry_mutex_};
            slots_.erase(slot->id);
        }
        registry_changed_.notify_all();
    }

    void reject_without_slot(
        Transport& transport,
        const std::uint16_t code,
        const std::string_view reason
    ) noexcept
    {
        if (!transport.request_close(code, reason)) {
            stats_->write_failures.fetch_add(1, std::memory_order_relaxed);
            transport.interrupt();
            return;
        }
        std::this_thread::sleep_for(config_.close_grace);
        transport.interrupt();
    }

    [[nodiscard]] bool apply_driver_result(Slot& slot, DriverResult result)
    {
        const auto previous = slot.phase.load(std::memory_order_acquire);
        if (previous == SessionPhase::streaming
            && result.phase == SessionPhase::handshaking) {
            slot.terminate(TerminalAction::protocol_failed);
            return false;
        }
        if (result.phase == SessionPhase::streaming) {
            slot.phase.store(SessionPhase::streaming, std::memory_order_release);
        }
        for (auto& batch : result.batches) {
            if (slot.enqueue(std::move(batch), {}) != EnqueueResult::accepted) {
                slot.terminate(TerminalAction::internal_error);
                return false;
            }
        }
        if (result.terminal != TerminalAction::none) {
            slot.terminate(result.terminal);
            return false;
        }
        return !slot.stop_source.stop_requested();
    }

    void ensure_scheduler()
    {
        std::lock_guard<std::mutex> lock{scheduler_mutex_};
        if (scheduler_.joinable() || scheduler_abandoned_ || !config_.enabled) return;
        scheduler_ = std::jthread([self = shared_from_this()](
            const std::stop_token stop
        ) { self->scheduler_loop(stop); });
    }

    void request_scheduler_stop() noexcept
    {
        try {
            std::lock_guard<std::mutex> lock{scheduler_mutex_};
            scheduler_.request_stop();
            scheduler_changed_.notify_all();
        } catch (...) {}
    }

    [[nodiscard]] bool join_scheduler() noexcept
    {
        try {
            std::lock_guard<std::mutex> lock{scheduler_mutex_};
            scheduler_.request_stop();
            scheduler_changed_.notify_all();
            if (scheduler_.joinable()) scheduler_.join();
            return true;
        } catch (...) {
            abandon_scheduler();
            stats_->shutdown_timeouts.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    void abandon_scheduler() noexcept
    {
        try {
            std::lock_guard<std::mutex> lock{scheduler_mutex_};
            scheduler_abandoned_ = true;
            scheduler_.request_stop();
            scheduler_changed_.notify_all();
            if (scheduler_.joinable()) scheduler_.detach();
        } catch (...) {}
    }

    void scheduler_loop(const std::stop_token stop) noexcept
    {
        try {
          while (!stop.stop_requested()) {
            std::vector<std::shared_ptr<Slot>> slots;
            {
                std::lock_guard<std::mutex> lock{registry_mutex_};
                for (const auto& [id, slot] : slots_) {
                    static_cast<void>(id);
                    slots.push_back(slot);
                }
            }
            const auto now = Clock::now();
            for (const auto& slot : slots) {
                if (stop.stop_requested() || slot->stop_source.stop_requested()) continue;
                const auto handshake_deadline = slot->connected_at
                    + slot->config.handshake_timeout;
                if (slot->phase.load(std::memory_order_acquire)
                        == SessionPhase::handshaking
                    && now >= handshake_deadline) {
                    // Serialize the timeout decision with input()/heartbeat()
                    // and re-check after acquiring the driver lock. A frame
                    // that completed authentication at the deadline must win
                    // over the scheduler's stale handshaking observation.
                    std::unique_lock<std::mutex> lock{
                        slot->driver_mutex, std::try_to_lock};
                    if (!lock.owns_lock()) continue;
                    if (!slot->stop_source.stop_requested()
                        && slot->phase.load(std::memory_order_acquire)
                            == SessionPhase::handshaking) {
                        stats_->handshake_timeouts.fetch_add(
                            1, std::memory_order_relaxed
                        );
                        slot->terminate(TerminalAction::authentication_failed);
                    }
                    continue;
                }
                if (now < slot->next_heartbeat) continue;
                slot->next_heartbeat = now + slot->config.heartbeat_interval;
                if (slot->phase.load(std::memory_order_acquire)
                    != SessionPhase::streaming) {
                    continue;
                }
                try {
                    {
                        std::unique_lock<std::mutex> lock{
                            slot->driver_mutex, std::try_to_lock};
                        if (!lock.owns_lock()) continue;
                        if (!slot->driver) continue;
                        auto result = slot->driver->heartbeat(
                            slot->stop_source.get_token()
                        );
                        stats_->heartbeat_ticks.fetch_add(
                            1, std::memory_order_relaxed
                        );
                        static_cast<void>(
                            apply_driver_result(*slot, std::move(result))
                        );
                    }
                } catch (...) {
                    slot->terminate(TerminalAction::internal_error);
                }
            }
            std::unique_lock<std::mutex> lock{scheduler_wait_mutex_};
            scheduler_changed_.wait_for(lock, std::chrono::milliseconds{10}, [&] {
                return stop.stop_requested();
            });
          }
        } catch (...) {
            // No exception may escape this noexcept thread boundary. Fail all
            // active sessions closed without allocating another snapshot.
            try {
                std::lock_guard<std::mutex> lock{registry_mutex_};
                for (const auto& [id, slot] : slots_) {
                    static_cast<void>(id);
                    slot->terminate(TerminalAction::internal_error);
                }
            } catch (...) {}
        }
    }

    WebSocketOwnerConfig config_;
    std::shared_ptr<SessionFactory> sessions_;
    std::unique_ptr<http::OriginPolicy> origin_policy_;
    std::shared_ptr<SharedStats> stats_{std::make_shared<SharedStats>()};
    mutable std::mutex registry_mutex_;
    std::condition_variable registry_changed_;
    std::map<std::uint64_t, std::shared_ptr<Slot>> slots_;
    std::uint64_t next_slot_id_{1};
    bool accepting_{};
    std::mutex scheduler_mutex_;
    std::mutex scheduler_wait_mutex_;
    std::condition_variable scheduler_changed_;
    std::jthread scheduler_;
    bool scheduler_abandoned_{};
};

#if defined(BAAS_SERVICE_WEBSOCKET_TEST_HOOKS)
void WebSocketOwnerTestAccess::fail_next_enqueue_allocation() noexcept
{
    fail_next_enqueue_allocation_for_test.store(true, std::memory_order_release);
}

std::size_t WebSocketOwnerTestAccess::rejected_payload_bytes() noexcept
{
    return rejected_payload_bytes_for_test;
}
#endif

WebSocketOwner::WebSocketOwner(
    WebSocketOwnerConfig config,
    http::CorsPolicyConfig origin_config,
    std::shared_ptr<SessionFactory> sessions
)
    : impl_(std::make_shared<Impl>(
          std::move(config), std::move(origin_config), std::move(sessions)
      ))
{}

WebSocketOwner::~WebSocketOwner()
{
    if (impl_) {
        impl_->begin_shutdown();
        static_cast<void>(impl_->finish_shutdown());
    }
}

void WebSocketOwner::install(httplib::Server& server) { impl_->install(server); }

void WebSocketOwner::serve(
    const std::optional<std::string_view> origin,
    const bool malformed_origin_cardinality,
    RequestMetadata request,
    Transport& transport
)
{
    impl_->serve(
        origin, malformed_origin_cardinality, std::move(request), transport
    );
}

void WebSocketOwner::begin_shutdown() noexcept { impl_->begin_shutdown(); }

bool WebSocketOwner::finish_shutdown() noexcept { return impl_->finish_shutdown(); }

bool WebSocketOwner::shutdown() noexcept { return impl_->shutdown(); }

bool WebSocketOwner::prepare_start() noexcept { return impl_->prepare_start(); }

WebSocketOwnerStats WebSocketOwner::stats() const noexcept { return impl_->stats(); }

const WebSocketOwnerConfig& WebSocketOwner::config() const noexcept
{
    return impl_->config();
}

}  // namespace baas::service::websocket
