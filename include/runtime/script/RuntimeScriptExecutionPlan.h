#pragma once

#include "runtime/script/RuntimeScriptCatalog.h"
#include "runtime/script/RuntimeScriptPackageLoader.h"
#include "runtime/script/RuntimeScriptRepositoryTrustEvidence.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace baas::runtime::script {

enum class RuntimeScriptExecutionPlanError : std::uint8_t {
    none,
    invalid_limits,
    invalid_resolution,
    wrong_repository,
    generation_mismatch,
    commit_mismatch,
    manifest_not_found,
    manifest_too_large,
    repository_read_failed,
    invalid_utf8,
    invalid_json,
    limit_exceeded,
    manifest_schema_unsupported,
    invalid_field_set,
    invalid_value,
    trust_evidence_required,
    trust_evidence_mismatch,
    unsupported_signature,
    unsupported_resources,
    unsupported_profiles,
    language_mismatch,
    entry_mismatch,
    host_requirement_mismatch,
    capability_mismatch,
    module_manifest_mismatch,
    package_load_failed,
    cancelled,
    resource_exhausted,
};

[[nodiscard]] std::string_view runtime_script_execution_plan_error_name(
    RuntimeScriptExecutionPlanError error) noexcept;

struct RuntimeScriptExecutionPlanLimits {
    std::size_t max_manifest_bytes{1U * 1'024U * 1'024U};
    std::size_t max_json_depth{16};
    std::size_t max_json_nodes{200'000};
    std::size_t max_modules{4'096};
    std::size_t max_host_modules{256};
    std::size_t max_capabilities{65'536};
    std::size_t max_string_bytes{1'024};
    std::size_t max_total_string_bytes{8U * 1'024U * 1'024U};
    std::size_t max_work{16U * 1'024U * 1'024U};
    RuntimeScriptPackageLoaderLimits package_loader{};
};

struct RuntimeScriptPackageVersion {
    std::uint32_t major{};
    std::uint32_t minor{};
    std::uint32_t patch{};
    friend bool operator==(const RuntimeScriptPackageVersion&,
                           const RuntimeScriptPackageVersion&) = default;
};

struct RuntimeScriptExecutionModule {
    std::string canonical_module;
    std::string logical_path;
    std::uintmax_t size{};
    std::string sha256;
    friend bool operator==(const RuntimeScriptExecutionModule&,
                           const RuntimeScriptExecutionModule&) = default;
};

struct RuntimeScriptExecutionPlanResult;

// Copyable immutable publication. Every returned string, module source,
// graph, and catalog-derived value is owned by the plan's shared snapshot.
class RuntimeScriptExecutionPlan final {
public:
    struct Impl;

    RuntimeScriptExecutionPlan(const RuntimeScriptExecutionPlan&) noexcept = default;
    RuntimeScriptExecutionPlan(RuntimeScriptExecutionPlan&&) noexcept = default;
    RuntimeScriptExecutionPlan& operator=(const RuntimeScriptExecutionPlan&) noexcept = default;
    RuntimeScriptExecutionPlan& operator=(RuntimeScriptExecutionPlan&&) noexcept = default;
    ~RuntimeScriptExecutionPlan();

    [[nodiscard]] const std::string& generation() const noexcept;
    [[nodiscard]] const std::string& commit() const noexcept;
    [[nodiscard]] const std::string& requested_task() const noexcept;
    [[nodiscard]] bool legacy_alias() const noexcept;
    [[nodiscard]] const RuntimeScriptTaskDescriptor& task() const noexcept;
    [[nodiscard]] const std::string& package_id() const noexcept;
    [[nodiscard]] const RuntimeScriptPackageVersion& package_version() const noexcept;
    [[nodiscard]] const std::string& package_build() const noexcept;
    [[nodiscard]] std::span<const std::string> capabilities() const noexcept;
    [[nodiscard]] std::span<const RuntimeScriptExecutionModule> modules() const noexcept;
    [[nodiscard]] const RuntimeScriptPackage& package() const noexcept;

private:
    explicit RuntimeScriptExecutionPlan(std::shared_ptr<const Impl> impl) noexcept;
    std::shared_ptr<const Impl> impl_;

    friend struct RuntimeScriptExecutionPlanResult;
    friend RuntimeScriptExecutionPlanResult build_runtime_script_execution_plan(
        const repository::RuntimeRepositoryReadView&,
        const RuntimeScriptCatalogResolution&,
        const RuntimeScriptRepositoryTrustEvidence*,
        const RuntimeScriptExecutionPlanLimits&,
        std::stop_token) noexcept;
};

struct RuntimeScriptExecutionPlanResult {
    std::optional<RuntimeScriptExecutionPlan> plan;
    RuntimeScriptExecutionPlanError error{RuntimeScriptExecutionPlanError::none};
    RuntimeScriptPackageLoadError package_error{RuntimeScriptPackageLoadError::none};
    std::string module;
    std::vector<::baas::script::Diagnostic> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == RuntimeScriptExecutionPlanError::none && plan.has_value();
    }
};

[[nodiscard]] RuntimeScriptExecutionPlanResult build_runtime_script_execution_plan(
    const repository::RuntimeRepositoryReadView& scripts,
    const RuntimeScriptCatalogResolution& resolution,
    const RuntimeScriptRepositoryTrustEvidence* trust_evidence,
    const RuntimeScriptExecutionPlanLimits& limits = {},
    std::stop_token stop_token = {}) noexcept;

}  // namespace baas::runtime::script
