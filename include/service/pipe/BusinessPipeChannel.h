#pragma once

#include "service/pipe/PipeHost.h"
#include "service/websocket/BusinessSessionFactory.h"

#include <memory>

namespace baas::service::pipe {

// Transport-independent business factories reused by the local BPIP adapter.
// Trigger keeps its dedicated Pipe implementation because that implementation
// owns JSON/BYTES pairing and TriggerSession send leases directly.
struct BusinessPipeChannelFactories {
    std::shared_ptr<websocket::BusinessChannelHandlerFactory> provider;
    std::shared_ptr<websocket::BusinessChannelHandlerFactory> sync;
    std::shared_ptr<PipeChannelFactory> trigger;
    std::shared_ptr<websocket::BusinessChannelHandlerFactory> remote;
};

// Routes one BPIP connection to the existing provider/sync/remote business
// handler contract, or delegates trigger to TriggerPipeChannelFactory.  Missing
// channel dependencies fail closed through PipeHost's channel_unavailable.
class BusinessPipeChannelFactory final : public PipeChannelFactory {
public:
    explicit BusinessPipeChannelFactory(BusinessPipeChannelFactories factories);

    [[nodiscard]] std::unique_ptr<PipeChannelHandler> create(
        const PipeOpenRequest& request,
        std::stop_token stop_token) override;

private:
    BusinessPipeChannelFactories factories_;
};

}  // namespace baas::service::pipe
