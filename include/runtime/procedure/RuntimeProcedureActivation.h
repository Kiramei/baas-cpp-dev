#pragma once

#include "runtime/repository/RuntimeRepositoryReadView.h"
#include "runtime/resources/RuntimeResourceSnapshotLoader.h"
#include "runtime/script/RuntimeScriptExecutionPlan.h"
#include "script/host/ProcedureSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace baas::runtime::procedure {

inline constexpr std::string_view runtime_procedure_manifest_path = "baas.procedures.json";
inline constexpr std::string_view runtime_procedure_manifest_schema = "baas.procedures/v1";
inline constexpr std::string_view runtime_procedure_definition_schema =
    "baas.procedure-definition/v1";
inline constexpr std::string_view runtime_procedure_legacy_engine =
    "legacy.appear_then_click/v1";

enum class RuntimeProcedureActivationError : std::uint8_t {
    none,
    invalid_limits,
    wrong_repository,
    plan_mismatch,
    resource_activation_required,
    generation_mismatch,
    procedure_requirements_empty,
    manifest_not_found,
    manifest_too_large,
    repository_read_failed,
    invalid_utf8,
    invalid_json,
    invalid_manifest,
    entry_limit_exceeded,
    terminal_limit_exceeded,
    effect_limit_exceeded,
    resource_limit_exceeded,
    string_limit_exceeded,
    work_limit_exceeded,
    duplicate_entry,
    procedure_id_case_collision,
    required_procedure_missing,
    invalid_definition_path,
    path_not_manifested,
    manifest_entry_mismatch,
    definition_too_large,
    total_definition_bytes_exceeded,
    definition_digest_mismatch,
    invalid_definition,
    unsupported_engine,
    resource_not_found,
    snapshot_validation_failed,
    cancelled,
    resource_exhausted,
    internal_failure,
    result_schema_limit_exceeded,
};

[[nodiscard]] std::string_view runtime_procedure_activation_error_name(
    RuntimeProcedureActivationError error) noexcept;

struct RuntimeProcedureActivationLimits final {
    std::size_t max_manifest_bytes{4U * 1024U * 1024U};
    std::size_t max_entries{4'096};
    std::size_t max_definition_bytes{16U * 1024U * 1024U};
    std::size_t max_total_definition_bytes{64U * 1024U * 1024U};
    std::size_t max_terminals_per_procedure{256};
    std::size_t max_effects_per_procedure{5};
    std::size_t max_resources_per_procedure{4'096};
    std::size_t max_result_schema_nodes_per_procedure{16'384};
    std::size_t max_result_schema_depth{32};
    std::size_t max_string_bytes{1'024};
    std::size_t max_total_string_bytes{16U * 1024U * 1024U};
    std::size_t max_json_depth{32};
    std::size_t max_json_nodes{200'000};
    std::size_t max_work{512U * 1024U * 1024U};
};

struct RuntimeProcedureActivationLoadResult;

struct RuntimeProcedureTerminalBinding final {
    std::string source;
    std::string id;
};

class RuntimeProcedureDefinition final {
public:
    RuntimeProcedureDefinition(const RuntimeProcedureDefinition&) = delete;
    RuntimeProcedureDefinition& operator=(const RuntimeProcedureDefinition&) = delete;
    RuntimeProcedureDefinition(RuntimeProcedureDefinition&&) = delete;
    RuntimeProcedureDefinition& operator=(RuntimeProcedureDefinition&&) = delete;

    [[nodiscard]] const std::string& procedure_id() const noexcept;
    [[nodiscard]] const std::string& engine() const noexcept;
    [[nodiscard]] const std::string& sha256() const noexcept;
    [[nodiscard]] const std::string& implementation_sha256() const noexcept;
    [[nodiscard]] std::span<const RuntimeProcedureTerminalBinding> terminals() const noexcept;
    [[nodiscard]] std::span<const ::baas::script::host::ProcedureResultFieldSchema>
        result_schema() const noexcept;
    // Exact verified wrapper bytes. A later engine adapter may parse payload
    // without consulting a native path or the mutable repository checkout.
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

private:
    RuntimeProcedureDefinition(
        std::string procedure_id,
        std::string engine,
        std::string sha256,
        std::string implementation_sha256,
        std::vector<RuntimeProcedureTerminalBinding> terminals,
        std::vector<::baas::script::host::ProcedureResultFieldSchema> result_schema,
        std::shared_ptr<const std::vector<std::byte>> bytes) noexcept;

    std::string procedure_id_;
    std::string engine_;
    std::string sha256_;
    std::string implementation_sha256_;
    std::vector<RuntimeProcedureTerminalBinding> terminals_;
    std::vector<::baas::script::host::ProcedureResultFieldSchema> result_schema_;
    std::shared_ptr<const std::vector<std::byte>> bytes_;

    friend class RuntimeProcedureActivation;
    friend struct RuntimeProcedureActivationLoadResult;
    friend RuntimeProcedureActivationLoadResult load_runtime_procedure_activation(
        const repository::RuntimeRepositoryReadView&,
        const script::RuntimeScriptExecutionPlan&,
        std::shared_ptr<const resources::RuntimeResourceSnapshotActivation>,
        const RuntimeProcedureActivationLimits&,
        std::stop_token) noexcept;
};

// An unforgeable, immutable publication of one exact script/resource generation.
// The non-copyable activation owns every definition byte and both snapshots.
class RuntimeProcedureActivation final {
public:
    ~RuntimeProcedureActivation();
    RuntimeProcedureActivation(const RuntimeProcedureActivation&) = delete;
    RuntimeProcedureActivation& operator=(const RuntimeProcedureActivation&) = delete;
    RuntimeProcedureActivation(RuntimeProcedureActivation&&) = delete;
    RuntimeProcedureActivation& operator=(RuntimeProcedureActivation&&) = delete;

    [[nodiscard]] const std::string& generation() const noexcept;
    [[nodiscard]] const std::string& scripts_commit() const noexcept;
    [[nodiscard]] const std::string& resources_commit() const noexcept;
    [[nodiscard]] const std::string& activation_id() const noexcept;
    [[nodiscard]] const std::shared_ptr<const ::baas::script::host::ProcedureSnapshot>&
    snapshot() const noexcept;
    [[nodiscard]] std::shared_ptr<const RuntimeProcedureDefinition> resolve_definition(
        std::string_view procedure_id) const noexcept;
    [[nodiscard]] std::size_t procedure_count() const noexcept;

private:
    struct Impl;
    explicit RuntimeProcedureActivation(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;

    friend RuntimeProcedureActivationLoadResult load_runtime_procedure_activation(
        const repository::RuntimeRepositoryReadView&,
        const script::RuntimeScriptExecutionPlan&,
        std::shared_ptr<const resources::RuntimeResourceSnapshotActivation>,
        const RuntimeProcedureActivationLimits&,
        std::stop_token) noexcept;
};

struct RuntimeProcedureActivationLoadResult final {
    std::shared_ptr<const RuntimeProcedureActivation> activation;
    RuntimeProcedureActivationError error{RuntimeProcedureActivationError::none};
    std::string procedure_id;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == RuntimeProcedureActivationError::none &&
               static_cast<bool>(activation) && static_cast<bool>(activation->snapshot());
    }
};

[[nodiscard]] RuntimeProcedureActivationLoadResult load_runtime_procedure_activation(
    const repository::RuntimeRepositoryReadView& scripts,
    const script::RuntimeScriptExecutionPlan& plan,
    std::shared_ptr<const resources::RuntimeResourceSnapshotActivation> resources,
    const RuntimeProcedureActivationLimits& limits = {},
    std::stop_token stop_token = {}) noexcept;

enum class RuntimeProcedureActivationHookPoint : std::uint8_t {
    before_manifest_read,
    after_manifest_parse,
    before_definition_read,
    before_snapshot_build,
    before_publication,
};
#ifdef BAAS_RUNTIME_PROCEDURE_ACTIVATION_TESTING
using RuntimeProcedureActivationHook = void (*)(RuntimeProcedureActivationHookPoint);
void set_runtime_procedure_activation_hook(RuntimeProcedureActivationHook hook) noexcept;
#endif

}  // namespace baas::runtime::procedure
