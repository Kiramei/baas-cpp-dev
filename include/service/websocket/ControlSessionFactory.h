#pragma once

#include "service/auth/AuthOwner.h"
#include "service/websocket/WebSocketOwner.h"

#include <cstddef>
#include <memory>

namespace baas::service::websocket {

struct ControlSessionConfig {
    std::size_t max_handshake_json_bytes{64U * 1'024U};
    std::size_t max_control_json_bytes{64U * 1'024U};
};

// Creates the authenticated /ws/control driver only. This intentionally is
// not the production multiplexer: business resume/secretstream must exist
// before a ProductionSessionFactory may be wired into WebSocketOwner.
class ControlSessionFactory final : public SessionFactory {
public:
    explicit ControlSessionFactory(
        std::shared_ptr<auth::AuthOwner> authentication,
        ControlSessionConfig config = {});

    [[nodiscard]] std::unique_ptr<SessionDriver> create(
        RequestMetadata request,
        std::shared_ptr<OutboundSink> outbound,
        std::stop_token stop) override;

private:
    std::shared_ptr<auth::AuthOwner> authentication_;
    ControlSessionConfig config_;
};

}  // namespace baas::service::websocket
