#pragma once

#include "service/websocket/BusinessSessionFactory.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

namespace baas::service::channels {

struct RemoteHandlerLimits {
    std::size_t max_config_json_bytes{64U * 1'024U};
    std::size_t max_config_id_bytes{256};
    std::size_t max_json_depth{32};
    std::size_t max_json_nodes{4'096};
    std::size_t max_device_frame_bytes{16U * 1'024U * 1'024U};
    std::size_t max_in_flight_frames{64};
    std::size_t max_in_flight_bytes{32U * 1'024U * 1'024U};
};

enum class RemoteIoStatus : std::uint8_t {
    accepted,
    closed,
    capacity,
    internal_error,
};

enum class RemoteSessionEnd : std::uint8_t {
    clean,
    device_closed,
    capacity,
    internal_error,
};

struct RemoteSessionCallbacks {
    // Implementations preserve device stream order. The callback is thread-safe
    // and returns capacity/closed instead of silently dropping a scrcpy frame.
    std::function<RemoteIoStatus(std::string payload)> device_bytes;
    std::function<void(RemoteSessionEnd reason)> ended;
};

class RemoteSession {
public:
    virtual ~RemoteSession() = default;

    // Transfers one authenticated WS->ADB payload. Implementations are
    // thread-safe and must not retain references into the SecretBuffer. close
    // may run concurrently and must cause a blocking send to return.
    [[nodiscard]] virtual RemoteIoStatus send_to_device(
        auth::SecretBuffer payload,
        std::stop_token stop) = 0;

    // Idempotent and safe concurrently with send_to_device. On return, no new
    // send/callback may enter and every send/callback that entered before close
    // has returned. Exceptions must be contained.
    virtual void close() noexcept = 0;
};

enum class RemoteBackendError : std::uint8_t {
    none,
    not_found,
    invalid_config,
    capacity,
    internal_error,
};

struct RemoteOpenResult {
    std::unique_ptr<RemoteSession> session;
    RemoteBackendError error{RemoteBackendError::none};
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == RemoteBackendError::none && session != nullptr;
    }
};

// Owns ADB/scrcpy setup outside the channel handler. Implementations are
// thread-safe. open may synchronously invoke callbacks; failed opens must
// release every partially-created device resource before returning.
class RemoteBackend {
public:
    virtual ~RemoteBackend() = default;
    [[nodiscard]] virtual RemoteOpenResult open(
        std::optional<std::string> config_id,
        RemoteSessionCallbacks callbacks,
        std::stop_token stop) = 0;
};

class RemoteHandlerFactory final
    : public websocket::BusinessChannelHandlerFactory {
public:
    explicit RemoteHandlerFactory(
        std::shared_ptr<RemoteBackend> backend,
        RemoteHandlerLimits limits = {});

    [[nodiscard]] websocket::BusinessHandlerCreateResult create(
        websocket::BusinessSessionContext context,
        std::shared_ptr<websocket::BusinessPlaintextSink> output,
        std::stop_token stop) override;

private:
    std::shared_ptr<RemoteBackend> backend_;
    RemoteHandlerLimits limits_;
};

}  // namespace baas::service::channels
