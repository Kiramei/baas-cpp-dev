#pragma once

#include "service/app/RuntimeTaskTriggerRegistration.h"
#include "service/app/ServiceApplication.h"
#include "service/runtime/ProductionRuntimeScriptTaskFactory.h"
#include "service/runtime/RuntimeTaskOwner.h"

#include <memory>

namespace baas::service::app {

// Production two-phase adapter from Trigger commands to the service-owned
// RuntimeTaskOwner.  The native script catalog currently contains one-shot
// solve routes only, so start_scheduler fails closed instead of manufacturing
// an empty scheduler plan.  stop_scheduler remains the keyed compatibility
// stop for any task owned by the config.
class ProductionRuntimeTaskControl final : public RuntimeTaskControl {
public:
    explicit ProductionRuntimeTaskControl(
        std::shared_ptr<runtime::RuntimeTaskOwner> owner,
        std::shared_ptr<const runtime::RuntimeScriptTaskRuntimeFactory> factory,
        runtime::RuntimeScriptTaskRepositoryBinding repository,
        runtime::RuntimeScriptTaskBackendOptions options = {});
    ~ProductionRuntimeTaskControl() override;

    ProductionRuntimeTaskControl(const ProductionRuntimeTaskControl&) = delete;
    ProductionRuntimeTaskControl& operator=(
        const ProductionRuntimeTaskControl&) = delete;

    [[nodiscard]] RuntimeTaskPrepareResult prepare_start_scheduler(
        std::string_view config_id) override;
    [[nodiscard]] RuntimeTaskPrepareResult prepare_stop_scheduler(
        std::string_view config_id) override;
    [[nodiscard]] RuntimeTaskPrepareResult prepare_start_task(
        std::string_view config_id,
        std::string_view requested_task,
        std::stop_token stop_token = {}) override;
    [[nodiscard]] RuntimeTaskPrepareResult prepare_stop_all_tasks() override;

private:
    std::shared_ptr<runtime::RuntimeTaskOwner> owner_;
    std::shared_ptr<const runtime::RuntimeScriptTaskRuntimeFactory> factory_;
    runtime::RuntimeScriptTaskRepositoryBinding repository_;
    runtime::RuntimeScriptTaskBackendOptions options_;
};

[[nodiscard]] std::shared_ptr<ServiceRuntimeTaskCompositionFactory>
make_production_runtime_task_composition_factory(
    std::shared_ptr<const runtime::ProductionRuntimeScriptTaskProvider> provider,
    runtime::RuntimeScriptTaskBackendOptions backend_options = {},
    runtime::RuntimeTaskLimits owner_limits = {});

}  // namespace baas::service::app
