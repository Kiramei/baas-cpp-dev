#pragma once

#include "service/auth/AuthOwner.h"
#include "service/auth/SecretStream.h"
#include "service/websocket/ControlSessionFactory.h"
#include "service/websocket/WebSocketOwner.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace baas::service::websocket {

struct BusinessSessionConfig {
    std::size_t max_handshake_json_bytes{64U * 1'024U};
    std::size_t max_preauth_json_bytes{64U * 1'024U};
    std::size_t max_ciphertext_bytes{websocket_max_frame_bytes};
    std::size_t max_handler_messages{2};
    std::size_t max_handler_output_bytes{
        websocket_max_frame_bytes - auth::secretstream_overhead_bytes};
    std::chrono::milliseconds final_close_timeout{5'000};
};

struct BusinessSessionContext {
    auth::BusinessChannel channel{auth::BusinessChannel::provider};
    std::string session_id;
    std::string socket_id;
    std::int64_t expires_at{};
    std::uint64_t pwd_epoch{};
};

struct BusinessOutboundMessage {
    std::string payload;
    bool final{};
};

enum class BusinessHandlerStatus : std::uint8_t {
    ok,
    protocol_failed,
    capacity,
    internal_error,
    complete,
};

struct BusinessHandlerResult {
    std::vector<BusinessOutboundMessage> messages;
    BusinessHandlerStatus status{BusinessHandlerStatus::ok};
};

enum class BusinessEmitResult : std::uint8_t {
    accepted,
    closed,
    message_too_large,
    queue_full,
    queued_bytes_exceeded,
    resource_exhausted,
};

// Thread-safe immediate plaintext admission. One shared writer serializes
// SecretStreamPush sequence allocation with OutboundSink queue order.
class BusinessPlaintextSink {
public:
    virtual ~BusinessPlaintextSink() = default;
    [[nodiscard]] virtual BusinessEmitResult emit(
        BusinessOutboundMessage message) noexcept = 0;
    [[nodiscard]] virtual BusinessEmitResult emit_batch(
        std::vector<BusinessOutboundMessage> messages) noexcept = 0;
};

enum class BusinessCloseReason : std::uint8_t {
    clean_final,
    truncated,
    authentication_failed,
    protocol_failed,
    internal_error,
    stopped,
};

// Handlers see authenticated plaintext only. They do not parse WebSocket
// frames, own encryption keys, or implement the resume state machine.
class BusinessChannelHandler {
public:
    virtual ~BusinessChannelHandler() = default;
    [[nodiscard]] virtual BusinessHandlerResult ready(std::stop_token stop) = 0;
    [[nodiscard]] virtual BusinessHandlerResult input(
        auth::SecretBuffer plaintext,
        bool peer_final,
        std::stop_token stop) = 0;
    [[nodiscard]] virtual BusinessHandlerResult heartbeat(std::stop_token stop) = 0;
    virtual void closed(BusinessCloseReason reason) noexcept = 0;
};

enum class BusinessHandlerCreateError : std::uint8_t {
    none,
    capacity,
    internal_error,
};

struct BusinessHandlerCreateResult {
    std::unique_ptr<BusinessChannelHandler> handler;
    BusinessHandlerCreateError error{BusinessHandlerCreateError::none};
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == BusinessHandlerCreateError::none && handler != nullptr;
    }
};

class BusinessChannelHandlerFactory {
public:
    virtual ~BusinessChannelHandlerFactory() = default;
    [[nodiscard]] virtual BusinessHandlerCreateResult create(
        BusinessSessionContext context,
        std::shared_ptr<BusinessPlaintextSink> output,
        std::stop_token stop) = 0;
};

struct BusinessHandlerFactories {
    std::shared_ptr<BusinessChannelHandlerFactory> provider;
    std::shared_ptr<BusinessChannelHandlerFactory> sync;
    std::shared_ptr<BusinessChannelHandlerFactory> trigger;
    std::shared_ptr<BusinessChannelHandlerFactory> remote;
};

enum class RemoteChannelPolicy : std::uint8_t {
    disabled,
    desktop_only,
};

class BusinessSessionFactory final : public SessionFactory {
public:
    BusinessSessionFactory(
        std::shared_ptr<auth::AuthOwner> authentication,
        BusinessHandlerFactories handlers,
        BusinessSessionConfig config = {},
        RemoteChannelPolicy remote = RemoteChannelPolicy::desktop_only);

    [[nodiscard]] std::unique_ptr<SessionDriver> create(
        RequestMetadata request,
        std::shared_ptr<OutboundSink> outbound,
        std::stop_token stop) override;

private:
    std::shared_ptr<auth::AuthOwner> authentication_;
    BusinessHandlerFactories handlers_;
    BusinessSessionConfig config_;
    RemoteChannelPolicy remote_policy_;
};

// Exact production path multiplexer. Control remains delegated to the existing
// driver; business authentication and encryption remain delegated above.
class ProductionSessionFactory final : public SessionFactory {
public:
    ProductionSessionFactory(
        std::shared_ptr<auth::AuthOwner> authentication,
        BusinessHandlerFactories handlers,
        ControlSessionConfig control_config = {},
        BusinessSessionConfig business_config = {},
        RemoteChannelPolicy remote = RemoteChannelPolicy::desktop_only);

    [[nodiscard]] std::unique_ptr<SessionDriver> create(
        RequestMetadata request,
        std::shared_ptr<OutboundSink> outbound,
        std::stop_token stop) override;

private:
    std::shared_ptr<ControlSessionFactory> control_;
    std::shared_ptr<BusinessSessionFactory> business_;
};

}  // namespace baas::service::websocket
