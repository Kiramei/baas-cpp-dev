#pragma once

#include "service/http/HttplibAdapter.h"
#include "service/websocket/WebSocketOwner.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace baas::service::http {

inline constexpr std::string_view http_host_loopback_address = "127.0.0.1";
inline constexpr std::size_t http_host_max_worker_count = 256;
inline constexpr std::size_t http_host_max_queued_requests = 65'536;

using HttpHostListenerThreadFactory =
    std::function<std::thread(std::function<void()>)>;

struct HttpHostConfig {
    // Zero requests an operating-system-selected ephemeral port.
    std::uint16_t port = 0;
    // The default covers the 16-connection WebSocket cap plus two workers
    // reserved for health and ordinary HTTP requests.
    std::size_t worker_count = 18;
    // cpp-httplib counts waiting requests here; active workers are additional.
    std::size_t max_queued_requests = 32;
    std::chrono::milliseconds ready_timeout{2'000};
    std::chrono::milliseconds read_timeout{2'000};
    std::chrono::milliseconds write_timeout{2'000};
    std::chrono::milliseconds idle_interval{100};
    CorsPolicyConfig cors_policy{};
    websocket::WebSocketOwnerConfig websocket{};
    // Empty uses std::thread directly. Injection exists for embedding and
    // deterministic resource-failure tests; the returned thread must own task.
    HttpHostListenerThreadFactory listener_thread_factory;
};

struct HttpHostRouterConfig {
    router::ServiceInfo service;
    router::SizeBudget budget{};
    std::optional<router::HealthSnapshot> health_snapshot;
    std::shared_ptr<router::HealthSnapshotProvider> health_provider;
    std::shared_ptr<router::ShutdownIntent> shutdown_intent;
    std::shared_ptr<router::RouteExtension> route_extension;
    std::shared_ptr<websocket::SessionFactory> websocket_sessions;
};

enum class HttpHostState { stopped, starting, running, stopping, failed };

enum class HttpHostStartError {
    none,
    already_active,
    bind_failed,
    ready_timeout,
    listen_failed,
    listener_start_failed,
    websocket_not_stopped,
};

struct HttpHostStartResult {
    bool started = false;
    HttpHostStartError error = HttpHostStartError::none;
    std::uint16_t port = 0;
};

// Owns the Router, adapter, cpp-httplib Server, listener thread, and shared
// provider/intent lifetimes. The public header deliberately does not expose or
// include cpp-httplib.
class HttpHost final {
public:
    explicit HttpHost(
        HttpHostRouterConfig router_config,
        InputBudget input_budget = {},
        HttpHostConfig host_config = {}
    );
    ~HttpHost();

    HttpHost(const HttpHost&) = delete;
    HttpHost& operator=(const HttpHost&) = delete;
    HttpHost(HttpHost&&) = delete;
    HttpHost& operator=(HttpHost&&) = delete;

    // Starts a background listener and waits only up to ready_timeout for the
    // loopback socket to enter the accepting state.
    [[nodiscard]] HttpHostStartResult start();

    // Stops accepting, drains cpp-httplib's accepted/queued work, and joins.
    // It is safe to call repeatedly and is also called by the destructor.
    // A request-worker call closes accept but defers its self-join to the owner.
    void stop() noexcept;

    [[nodiscard]] HttpHostState state() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] std::string address() const;
    [[nodiscard]] HttpHostStartError last_start_error() const noexcept;
    [[nodiscard]] std::string last_error_message() const;
    [[nodiscard]] std::size_t queue_rejections() const noexcept;
    [[nodiscard]] websocket::WebSocketOwnerStats websocket_stats() const noexcept;
    [[nodiscard]] const HttpHostConfig& config() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace baas::service::http
