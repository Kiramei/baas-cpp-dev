#pragma once

#include "service/protocol/TriggerIngress.h"
#include "service/trigger/TriggerExecutor.h"
#include "service/websocket/BusinessSessionFactory.h"

#include <cstddef>
#include <memory>

namespace baas::service::channels {

struct TriggerHandlerLimits {
    protocol::trigger::TriggerIngressLimits ingress{};
    protocol::trigger::TriggerSessionLimits session{};
    std::size_t max_tasks_per_connection{};
};

// Production adapter for one authenticated trigger business connection.
// Execution remains owned by TriggerExecutor; this class only binds bounded
// plaintext ingress and ordered, completion-confirmed plaintext egress.
class TriggerHandlerFactory final
    : public websocket::BusinessChannelHandlerFactory {
public:
    explicit TriggerHandlerFactory(
        std::shared_ptr<trigger::TriggerExecutor> executor,
        TriggerHandlerLimits limits = {});

    [[nodiscard]] websocket::BusinessHandlerCreateResult create(
        websocket::BusinessSessionContext context,
        std::shared_ptr<websocket::BusinessPlaintextSink> output,
        std::stop_token stop) override;

private:
    std::shared_ptr<trigger::TriggerExecutor> executor_;
    TriggerHandlerLimits limits_;
};

#if defined(BAAS_SERVICE_TRIGGER_HANDLER_TEST_HOOKS)
namespace detail {
using TriggerBeforeSubmitHook = void (*)(void*) noexcept;
void set_trigger_before_submit_hook_for_test(
    TriggerBeforeSubmitHook hook, void* context) noexcept;
void fail_next_trigger_completion_allocation_for_test() noexcept;
}
#endif

}  // namespace baas::service::channels
