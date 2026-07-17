#pragma once

#include "runtime/repository/RuntimeRepositoryReadView.h"
#include "script/runtime/ModuleSpecifier.h"

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

inline constexpr std::string_view runtime_script_catalog_manifest =
    "baas-script-catalog.json";
inline constexpr std::string_view runtime_script_catalog_schema =
    "baas.runtime-script.catalog/v2";

enum class RuntimeScriptCatalogError : std::uint8_t {
    none,
    invalid_limits,
    wrong_repository,
    manifest_not_found,
    manifest_too_large,
    repository_read_failed,
    invalid_utf8,
    invalid_json,
    limit_exceeded,
    invalid_schema,
    invalid_field_set,
    generation_mismatch,
    commit_mismatch,
    invalid_value,
    duplicate_route,
    missing_package_manifest,
    missing_entry_module,
    cancelled,
    resource_exhausted,
};

[[nodiscard]] std::string_view runtime_script_catalog_error_name(
    RuntimeScriptCatalogError error) noexcept;

struct RuntimeScriptCatalogLimits {
    std::size_t max_manifest_bytes{1U * 1'024U * 1'024U};
    std::size_t max_json_depth{16};
    std::size_t max_json_nodes{200'000};
    std::size_t max_tasks{4'096};
    std::size_t max_aliases_per_task{64};
    std::size_t max_total_aliases{16'384};
    std::size_t max_host_modules_per_task{256};
    std::size_t max_capabilities_per_module{256};
    std::size_t max_total_host_modules{65'536};
    std::size_t max_total_capabilities{262'144};
    std::size_t max_string_bytes{1'024};
    std::size_t max_total_string_bytes{8U * 1'024U * 1'024U};
    std::size_t max_work{16U * 1'024U * 1'024U};
    ::baas::script::runtime::ModuleSpecifierLimits module_specifier{};
    ::baas::script::runtime::NfcPredicate is_nfc{nullptr};
};

struct RuntimeScriptCatalogPin {
    std::string_view generation;
    std::string_view commit;
};

struct RuntimeScriptLanguageVersion {
    std::uint32_t major{};
    std::uint32_t minor{};
    friend bool operator==(const RuntimeScriptLanguageVersion&,
                           const RuntimeScriptLanguageVersion&) = default;
};

struct RuntimeScriptHostRequirement {
    std::string canonical_id;
    std::uint32_t major{};
    std::uint32_t min_minor{};
    std::vector<std::string> capabilities;
    friend bool operator==(const RuntimeScriptHostRequirement&,
                           const RuntimeScriptHostRequirement&) = default;
};

struct RuntimeScriptTaskDescriptor {
    std::string run_mode;
    std::string canonical_task;
    std::string package_root;
    std::string package_manifest;
    std::string entry_module;
    std::string entry_export;
    RuntimeScriptLanguageVersion language_version;
    std::vector<RuntimeScriptHostRequirement> host_modules;
    std::vector<std::string> legacy_aliases;
    friend bool operator==(const RuntimeScriptTaskDescriptor&,
                           const RuntimeScriptTaskDescriptor&) = default;
};

class RuntimeScriptCatalogResolution final {
public:
    RuntimeScriptCatalogResolution(const RuntimeScriptCatalogResolution&) noexcept = default;
    RuntimeScriptCatalogResolution& operator=(
        const RuntimeScriptCatalogResolution&) noexcept = default;

    // Keep the source owning its published views too. The public pointer/view
    // therefore remain safe to inspect after either a copy or a move.
    RuntimeScriptCatalogResolution(RuntimeScriptCatalogResolution&& other) noexcept;
    RuntimeScriptCatalogResolution& operator=(
        RuntimeScriptCatalogResolution&& other) noexcept;
    ~RuntimeScriptCatalogResolution() = default;

    const RuntimeScriptTaskDescriptor* task{};
    std::string_view requested_task;
    bool legacy_alias{};

    [[nodiscard]] std::string_view generation() const noexcept
    {
        return generation_;
    }
    [[nodiscard]] std::string_view commit() const noexcept { return commit_; }
    [[nodiscard]] const RuntimeScriptTaskDescriptor* resolved_task() const noexcept
    {
        return resolved_task_;
    }
    [[nodiscard]] std::string_view resolved_requested_task() const noexcept
    {
        return resolved_requested_task_;
    }
    [[nodiscard]] bool resolved_legacy_alias() const noexcept
    {
        return resolved_legacy_alias_;
    }

private:
    RuntimeScriptCatalogResolution(
        std::shared_ptr<const void> owner,
        const RuntimeScriptTaskDescriptor* task,
        std::string_view requested_task,
        bool legacy_alias,
        std::string_view generation,
        std::string_view commit) noexcept;

    // Type-erased so the resolution can be declared before the catalog's
    // private Impl. Callers cannot reset the owner independently of the views.
    std::shared_ptr<const void> owner_;
    std::string_view generation_;
    std::string_view commit_;
    const RuntimeScriptTaskDescriptor* resolved_task_{};
    std::string_view resolved_requested_task_;
    bool resolved_legacy_alias_{};

    friend class RuntimeScriptCatalog;
};

class RuntimeScriptCatalog;
struct RuntimeScriptCatalogLoadResult;
[[nodiscard]] RuntimeScriptCatalogLoadResult load_runtime_script_catalog(
    const repository::RuntimeRepositoryReadView& scripts,
    RuntimeScriptCatalogPin expected,
    const RuntimeScriptCatalogLimits& limits,
    std::stop_token stop_token) noexcept;

// Immutable after publication. All strings are owned catalog data read from a
// single pinned scripts view; no native path or source payload is retained.
class RuntimeScriptCatalog final {
public:
    struct Impl;

    RuntimeScriptCatalog(const RuntimeScriptCatalog&) noexcept = default;
    RuntimeScriptCatalog(RuntimeScriptCatalog&&) noexcept = default;
    RuntimeScriptCatalog& operator=(const RuntimeScriptCatalog&) noexcept = default;
    RuntimeScriptCatalog& operator=(RuntimeScriptCatalog&&) noexcept = default;
    ~RuntimeScriptCatalog();

    [[nodiscard]] const std::string& generation() const noexcept;
    [[nodiscard]] const std::string& commit() const noexcept;
    [[nodiscard]] std::span<const RuntimeScriptTaskDescriptor> tasks() const noexcept;

    // Lookup is byte-exact and case-sensitive. No normalization, wildcard,
    // prefix match, path derivation, or fallback is performed.
    [[nodiscard]] std::optional<RuntimeScriptCatalogResolution> resolve(
        std::string_view run_mode,
        std::string_view requested_task) const noexcept;

private:
    explicit RuntimeScriptCatalog(std::shared_ptr<const Impl> impl) noexcept;
    std::shared_ptr<const Impl> impl_;

    friend struct RuntimeScriptCatalogLoadResult;
    friend RuntimeScriptCatalogLoadResult load_runtime_script_catalog(
        const repository::RuntimeRepositoryReadView&,
        RuntimeScriptCatalogPin,
        const RuntimeScriptCatalogLimits&,
        std::stop_token) noexcept;
};

struct RuntimeScriptCatalogLoadResult {
    std::optional<RuntimeScriptCatalog> catalog;
    RuntimeScriptCatalogError error{RuntimeScriptCatalogError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == RuntimeScriptCatalogError::none && catalog.has_value();
    }
};

// Reads exactly runtime_script_catalog_manifest from a pinned scripts view,
// verifies the caller's pinned generation/commit and every exact package/source
// reference, then publishes one immutable deterministic catalog snapshot.
[[nodiscard]] RuntimeScriptCatalogLoadResult load_runtime_script_catalog(
    const repository::RuntimeRepositoryReadView& scripts,
    RuntimeScriptCatalogPin expected,
    const RuntimeScriptCatalogLimits& limits = {},
    std::stop_token stop_token = {}) noexcept;

}  // namespace baas::runtime::script
