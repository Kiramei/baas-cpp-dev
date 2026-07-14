#pragma once

#include "script/runtime/ModuleSpecifier.h"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace baas::script::runtime {

struct HostApiVersion {
    std::uint32_t major{};
    std::uint32_t minor{};
    friend auto operator<=>(const HostApiVersion&, const HostApiVersion&) = default;
};

struct HostExportDescriptor {
    std::string export_name;
    std::string binding_id;
    std::string capability;
    friend bool operator==(const HostExportDescriptor&, const HostExportDescriptor&) = default;
};

struct HostModuleDescriptor {
    std::string canonical_id;
    HostApiVersion version;
    std::vector<HostExportDescriptor> exports;
};

enum class HostRegistryErrorCode {
    InvalidLimits,
    ModuleVersionLimitExceeded,
    ExportLimitExceeded,
    CapabilityLimitExceeded,
    ImportLimitExceeded,
    StringBudgetExceeded,
    ValidationWorkLimitExceeded,
    InvalidUtf8,
    InvalidModuleId,
    InvalidExportName,
    InvalidCapabilityId,
    InvalidBindingId,
    DuplicateModuleVersion,
    DuplicateExport,
    DuplicateBinding,
    IncompatibleMinorContract,
    DuplicateManifestModule,
    DuplicateCapability,
    DuplicateImport,
    UndeclaredModule,
    ModuleUnavailable,
    VersionIncompatible,
    UnknownExport,
    UndeclaredCapability,
    CapabilityDenied,
};

[[nodiscard]] std::string_view host_registry_error_code_name(
    HostRegistryErrorCode code) noexcept;

class HostRegistryError final : public std::runtime_error {
public:
    HostRegistryError(
        HostRegistryErrorCode code,
        std::string message,
        std::string module = {},
        std::string export_name = {},
        std::string capability = {},
        std::string layer = {});

    [[nodiscard]] HostRegistryErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::string& module() const noexcept { return module_; }
    [[nodiscard]] const std::string& export_name() const noexcept { return export_name_; }
    [[nodiscard]] const std::string& capability() const noexcept { return capability_; }
    [[nodiscard]] const std::string& layer() const noexcept { return layer_; }

private:
    HostRegistryErrorCode code_;
    std::string module_;
    std::string export_name_;
    std::string capability_;
    std::string layer_;
};

struct HostRegistryLimits {
    std::size_t max_module_versions{256};
    std::size_t max_exports_per_module{256};
    std::size_t max_total_exports{4'096};
    std::size_t max_capabilities{1'024};
    std::size_t max_imports{512};
    std::size_t max_string_bytes{1'024};
    std::size_t max_total_string_bytes{1'048'576};
    std::size_t max_validation_work{100'000};
};

struct HostModuleRequirement {
    std::string canonical_id;
    std::uint32_t major{};
    std::uint32_t min_minor{};
};

struct HostImportRequest {
    std::string canonical_id;
    std::vector<std::string> exports;
};

struct HostResolutionRequest {
    std::vector<HostModuleRequirement> declared_modules;
    std::vector<std::string> declared_capabilities;
    std::vector<HostImportRequest> imports;
    std::vector<std::string> policy_capabilities;
    std::vector<std::string> platform_capabilities;
    std::vector<std::string> task_capabilities;
};

struct ResolvedHostBinding {
    std::string export_name;
    std::string binding_id;
    std::string capability;
    friend bool operator==(const ResolvedHostBinding&, const ResolvedHostBinding&) = default;
};

struct ResolvedHostModule {
    std::string canonical_id;
    HostApiVersion selected_version;
    std::vector<ResolvedHostBinding> bindings;
    friend bool operator==(const ResolvedHostModule&, const ResolvedHostModule&) = default;
};

struct HostResolution {
    std::vector<ResolvedHostModule> modules;
    std::vector<std::string> effective_capabilities;
    std::size_t validation_work{};
    friend bool operator==(const HostResolution&, const HostResolution&) = default;
};

// Immutable after construction. Descriptors contain identity metadata only: no
// adapter address, native pointer, native file descriptor, or device operation
// is stored.
class HostModuleRegistry final {
public:
    explicit HostModuleRegistry(
        std::vector<HostModuleDescriptor> descriptors,
        HostRegistryLimits limits = {});

    HostModuleRegistry(const HostModuleRegistry&) = default;
    HostModuleRegistry(HostModuleRegistry&&) noexcept = default;
    HostModuleRegistry& operator=(const HostModuleRegistry&) = delete;
    HostModuleRegistry& operator=(HostModuleRegistry&&) = delete;

    // Thread-safe when the registry has been safely published: this method
    // reads immutable registry state and owns all per-resolution scratch data.
    [[nodiscard]] HostResolution resolve(const HostResolutionRequest& request) const;

    [[nodiscard]] std::vector<std::string> canonical_module_ids() const;
    [[nodiscard]] std::size_t module_version_count() const noexcept
    {
        return module_version_count_;
    }

private:
    using Versions = std::map<HostApiVersion, HostModuleDescriptor>;
    std::map<std::string, Versions, std::less<>> modules_;
    HostRegistryLimits limits_;
    std::size_t module_version_count_{};
};

}  // namespace baas::script::runtime
