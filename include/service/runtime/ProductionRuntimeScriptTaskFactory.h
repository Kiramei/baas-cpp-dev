#pragma once

#include "resources/ResourceSnapshot.h"
#include "runtime/procedure/CoDetectProductionAdapter.h"
#include "runtime/repository/RuntimeRepositoryReadView.h"
#include "runtime/script/RuntimeScriptRepositoryTrustEvidence.h"
#include "script/host/HostRuntimeComposition.h"
#include "script/host/ProcedureHost.h"
#include "script/host/ResourceHost.h"
#include "script/runtime/LogHost.h"
#include "service/runtime/RuntimeScriptTaskBackend.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::runtime {

// Exact logical binding from one activated procedure to its dynamic support
// bundle. The resource bytes remain owned by the pinned resources read view;
// neither a native path nor an embedded payload crosses this boundary.
struct ProductionRuntimeProcedureSupport final {
    std::string procedure_id;
    std::string resource_id;
    friend bool operator==(
        const ProductionRuntimeProcedureSupport&,
        const ProductionRuntimeProcedureSupport&) = default;
};

// Immutable application-owned config publication. A provider creates a new
// object when the user edits config/profile/device/capability state. The
// factory retains this exact object for the complete ordered task plan.
struct ProductionRuntimeScriptConfigSnapshot final {
    std::string config_id;
    std::string snapshot_id;
    std::string device_id;
    std::string locale;
    ::baas::runtime::procedure::CoDetectProfile profile{
        ::baas::runtime::procedure::CoDetectProfile::cn};
    ::baas::resources::ResourceSelector resource_selector;
    std::vector<std::string> policy_capabilities;
    std::vector<std::string> platform_capabilities;
    std::vector<std::string> log_secrets;
    std::vector<ProductionRuntimeProcedureSupport> procedure_support;
};

struct ProductionRuntimeScriptExtensionIdentity final {
    std::string config_snapshot_id;
    std::string generation;
    std::string scripts_commit;
    std::string resources_commit;
};

// Optional immutable extension publication for later OCR/config/clock/device
// Hosts and additional typed procedure engines. Every contribution is rebuilt
// for one task and passes through final HostRuntimeComposition validation;
// duplicate/orphan bindings and permission mismatches therefore fail closed.
// Implementations retain only state covered by identity() and return fresh
// request-local adapters that do not consult mutable registries.
class ProductionRuntimeScriptExtensions {
public:
    virtual ~ProductionRuntimeScriptExtensions() = default;
    ProductionRuntimeScriptExtensions(
        const ProductionRuntimeScriptExtensions&) = delete;
    ProductionRuntimeScriptExtensions& operator=(
        const ProductionRuntimeScriptExtensions&) = delete;

    [[nodiscard]] virtual const ProductionRuntimeScriptExtensionIdentity&
    identity() const noexcept = 0;
    [[nodiscard]] virtual std::vector<
        ::baas::script::host::HostRuntimeContribution>
    make_host_contributions(
        const ::baas::runtime::script::RuntimeScriptExecutionPlan& plan,
        std::shared_ptr<const ::baas::script::runtime::HostCancellationProbe>
            cancellation) const = 0;
    [[nodiscard]] virtual std::shared_ptr<
        ::baas::script::host::ProcedureExecutor>
    make_procedure_executor(
        std::shared_ptr<const
            ::baas::runtime::procedure::RuntimeProcedureActivation> activation,
        std::string_view procedure_id,
        const RuntimeScriptTaskExecutionControl& control) const = 0;

protected:
    ProductionRuntimeScriptExtensions() = default;
};

// One provider publication. The repository bundle is already opened from one
// immutable metadata snapshot and retains both anchored read views. Trust
// evidence must cover that exact generation/scripts commit. Device identity is
// an owner-created immutable token and must still be current at create time.
struct ProductionRuntimeScriptTaskInputs final {
    std::shared_ptr<const ProductionRuntimeScriptConfigSnapshot> config;
    std::shared_ptr<const ::baas::runtime::repository::RuntimeRepositoryReadBundle>
        repositories;
    std::shared_ptr<const ::baas::runtime::script::RuntimeScriptRepositoryTrustEvidence>
        trust_evidence;
    std::shared_ptr<::baas::runtime::procedure::CoDetectProductionDevicePort>
        device;
    std::shared_ptr<const ::baas::runtime::procedure::CoDetectProductionDeviceIdentity>
        device_identity;
    std::shared_ptr<::baas::script::runtime::StructuredLogSink> log_sink;
    std::shared_ptr<const ProductionRuntimeScriptExtensions> extensions;
};

// Thread-safe application boundary. pin() is called exactly once for a create
// request and returns a fully immutable publication. The returned runtime never
// calls the provider again and never reopens mutable config/current state.
class ProductionRuntimeScriptTaskProvider {
public:
    virtual ~ProductionRuntimeScriptTaskProvider() = default;

    ProductionRuntimeScriptTaskProvider(
        const ProductionRuntimeScriptTaskProvider&) = delete;
    ProductionRuntimeScriptTaskProvider& operator=(
        const ProductionRuntimeScriptTaskProvider&) = delete;

    [[nodiscard]] virtual std::optional<ProductionRuntimeScriptTaskInputs> pin(
        const RuntimeTaskRequest& request,
        std::span<const std::string> requested_task_plan,
        const RuntimeScriptTaskExecutionControl& control) const = 0;

protected:
    ProductionRuntimeScriptTaskProvider() = default;
};

struct ProductionRuntimeScriptTaskFactoryLimits final {
    std::size_t max_tasks{256};
    std::size_t max_identity_bytes{1'024};
    std::size_t max_capabilities{4'096};
    std::size_t max_secrets{64};
    std::size_t max_procedure_support{4'096};
    ::baas::runtime::repository::RuntimeRepositoryReadLimits repository{};
    ::baas::runtime::resources::RuntimeResourceSnapshotLoaderLimits resources{};
    ::baas::runtime::script::RuntimeScriptCatalogLimits catalog{};
    ::baas::runtime::script::RuntimeScriptExecutionPlanLimits execution_plan{};
    ::baas::runtime::procedure::RuntimeProcedureActivationLimits procedures{};
    ::baas::runtime::procedure::CoDetectSupportBundleLimits support_bundle{};
    ::baas::runtime::procedure::CoDetectProductionAdapterLimits device_adapter{};
    ::baas::script::host::ResourceHostLimits resource_host{};
    ::baas::script::host::ProcedureHostLimits procedure_host{};
    ::baas::script::host::PhysicalDeviceCoordinatorLimits device_coordinator{};
    ::baas::script::host::HostRuntimeCompositionLimits host_composition{};
    ::baas::script::runtime::QueuedLogHostLimits log_host{};
    ::baas::script::runtime::EvaluatorLimits evaluator{};
    ::baas::script::runtime::HeapLimits heap{};
};

// Additive production factory. It does not register a service command or
// replace the legacy Python/Tauri path. Every create() returns a fresh runtime
// with request-local evaluators and Host owners; only the physical-device
// coordinator is intentionally shared to serialize one concrete device.
[[nodiscard]] std::shared_ptr<const RuntimeScriptTaskRuntimeFactory>
make_production_runtime_script_task_factory(
    std::shared_ptr<const ProductionRuntimeScriptTaskProvider> provider,
    ProductionRuntimeScriptTaskFactoryLimits limits = {});

} // namespace baas::service::runtime
