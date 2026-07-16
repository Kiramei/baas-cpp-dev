#pragma once

#include "service/adb/ServiceAdbTransport.h"
#include "service/channels/RemoteHandler.h"
#include "service/channels/SyncHandler.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::app {

enum class RemoteWebSocketReadKind : std::uint8_t {
    binary,
    text,
    closed,
    error,
};

struct RemoteWebSocketReadResult {
    RemoteWebSocketReadKind kind{RemoteWebSocketReadKind::error};
    std::string payload;
};

// Narrow wrapper around cpp-httplib's WebSocket client. Tests inject a fake;
// production uses the patched interruptible client implementation.
class RemoteWebSocketClient {
public:
    virtual ~RemoteWebSocketClient() = default;
    [[nodiscard]] virtual bool connect() = 0;
    [[nodiscard]] virtual RemoteWebSocketReadResult read() = 0;
    [[nodiscard]] virtual bool send_binary(std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual bool request_close() noexcept = 0;
    virtual void interrupt() noexcept = 0;
};

// Testable policy boundary around the concrete ADB smart-socket and SYNC
// clients. The production adapter never invokes an adb executable or selects a
// fallback device: every operation carries the exact configured serial.
class RemoteAdbClient {
public:
    virtual ~RemoteAdbClient() = default;
    [[nodiscard]] virtual adb::AdbTransportResult<std::string> get_state(
        std::string_view serial, std::stop_token stop) = 0;
    [[nodiscard]] virtual adb::AdbTransportResult<std::string> shell(
        std::string_view serial, std::string_view command,
        std::stop_token stop) = 0;
    [[nodiscard]] virtual adb::AdbTransportResult<std::uint64_t> push_file(
        std::string_view serial, std::string_view remote_path,
        const std::filesystem::path& local_path, std::stop_token stop) = 0;
    [[nodiscard]] virtual adb::AdbTransportResult<std::vector<adb::AdbForwardItem>>
        list_forwards(std::stop_token stop) = 0;
    [[nodiscard]] virtual adb::AdbTransportResult<std::uint16_t> forward_tcp_zero(
        std::string_view serial, std::uint16_t device_port,
        std::stop_token stop) = 0;
    [[nodiscard]] virtual adb::AdbTransportResult<bool> remove_tcp_forward(
        std::string_view serial, std::uint16_t local_port,
        std::stop_token stop) = 0;
    virtual void stop() noexcept = 0;
};

struct ProductionRemoteBackendLimits {
    std::size_t max_sessions{8};
    std::size_t max_config_bytes{1U * 1'024U * 1'024U};
    std::size_t max_device_frame_bytes{16U * 1'024U * 1'024U};
    std::size_t max_process_candidates{128};
    std::chrono::milliseconds startup_timeout{10'000};
    std::chrono::milliseconds startup_poll_interval{100};
    std::chrono::milliseconds websocket_connect_timeout{3'000};
    std::chrono::milliseconds websocket_write_timeout{5'000};
};

struct ProductionRemoteBackendDependencies {
    using WebSocketFactory = std::function<std::unique_ptr<RemoteWebSocketClient>(
        std::string url,
        std::chrono::milliseconds connect_timeout,
        std::chrono::milliseconds write_timeout)>;
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    using Sleep = std::function<void(std::chrono::milliseconds)>;
    using OwnerTokenFactory = std::function<std::optional<std::string>()>;

    std::shared_ptr<channels::ResourceStore> resources;
    // Tests inject this boundary. Production may leave it empty and supply the
    // concrete transport below.
    std::shared_ptr<RemoteAdbClient> adb;
    std::shared_ptr<adb::ServiceAdbTransport> adb_transport;
    std::filesystem::path server_jar;
    WebSocketFactory websocket_factory;
    Clock clock;
    Sleep sleep;
    // Tests inject deterministic tokens. Production generates a fresh
    // 256-bit lowercase hexadecimal token with libsodium before every launch.
    OwnerTokenFactory owner_token_factory;
};

class ProductionRemoteBackend final : public channels::RemoteBackend {
public:
    explicit ProductionRemoteBackend(
        ProductionRemoteBackendDependencies dependencies,
        ProductionRemoteBackendLimits limits = {});
    ~ProductionRemoteBackend() override;

    ProductionRemoteBackend(const ProductionRemoteBackend&) = delete;
    ProductionRemoteBackend& operator=(const ProductionRemoteBackend&) = delete;

    [[nodiscard]] channels::RemoteOpenResult open(
        std::optional<std::string> config_id,
        channels::RemoteSessionCallbacks callbacks,
        std::stop_token stop) override;

    // Idempotently closes every live proxy before stopping the shared ADB
    // transport. Existing RemoteSession handles remain safe to close again.
    void stop() noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace baas::service::app
