#pragma once

#include "service/pipe/PipeHost.h"
#include "service/protocol/TriggerIngress.h"
#include "service/trigger/TriggerExecutor.h"

#include <cstddef>
#include <memory>

namespace baas::service::pipe {

struct TriggerPipeChannelLimits {
    protocol::trigger::TriggerIngressLimits ingress{};
    protocol::trigger::TriggerSessionLimits session{};
    std::size_t max_tasks_per_connection{};
};

// Production BPIP adapter. It deliberately exposes only the trigger channel;
// provider, sync and remote stay unavailable until they have real adapters.
class TriggerPipeChannelFactory final : public PipeChannelFactory {
public:
    explicit TriggerPipeChannelFactory(
        std::shared_ptr<trigger::TriggerExecutor> executor,
        TriggerPipeChannelLimits limits = {});

    [[nodiscard]] std::unique_ptr<PipeChannelHandler> create(
        const PipeOpenRequest& request,
        std::stop_token stop_token) override;

private:
    std::shared_ptr<trigger::TriggerExecutor> executor_;
    TriggerPipeChannelLimits limits_;
};

}  // namespace baas::service::pipe
