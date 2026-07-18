#pragma once

#include "service/app/RuntimeTaskTriggerRegistration.h"
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
        std::shared_ptr<runtime::RuntimeTaskOwner> owner);
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
        std::string_view requested_task) override;
    [[nodiscard]] RuntimeTaskPrepareResult prepare_stop_all_tasks() override;

private:
    std::shared_ptr<runtime::RuntimeTaskOwner> owner_;
};

[[nodiscard]] std::shared_ptr<RuntimeTaskControl>
make_production_runtime_task_control(
    std::shared_ptr<runtime::RuntimeTaskOwner> owner);

}  // namespace baas::service::app
