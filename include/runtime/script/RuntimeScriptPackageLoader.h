#pragma once

#include "runtime/repository/RuntimeRepositoryReadView.h"
#include "script/Diagnostic.h"
#include "script/SemanticAnalyzer.h"
#include "script/runtime/ModuleGraph.h"
#include "script/runtime/SynchronousEvaluator.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace baas::runtime::script {

enum class RuntimeScriptPackageLoadError : std::uint8_t {
    none,
    invalid_limits,
    wrong_repository,
    invalid_entry_module,
    host_entry_module,
    module_not_manifested,
    repository_read_failed,
    source_file_too_large,
    total_source_bytes_exceeded,
    module_limit_exceeded,
    import_edge_limit_exceeded,
    import_depth_exceeded,
    work_limit_exceeded,
    parse_failed,
    semantic_failed,
    invalid_import_specifier,
    missing_package_module,
    import_cycle,
    graph_validation_failed,
    invalid_package_manifest,
    package_pin_mismatch,
    module_outside_package,
    module_manifest_mismatch,
    unexpected_package_module,
    cancelled,
    resource_exhausted,
};

struct RuntimeScriptPackagePin {
    std::string_view generation;
    std::string_view commit;
};

struct RuntimeScriptPackageModuleManifest {
    std::string_view canonical_module;
    // Exact repository-root logical path supplied by the package boundary.
    // The loader never derives a package root from this value.
    std::string_view logical_path;
    std::uintmax_t size{};
    std::string_view sha256;
};

[[nodiscard]] std::string_view runtime_script_package_load_error_name(
    RuntimeScriptPackageLoadError error) noexcept;

struct RuntimeScriptPackageLoaderLimits {
    std::size_t max_modules{4'096};
    std::size_t max_source_file_bytes{4U * 1'024U * 1'024U};
    std::size_t max_total_source_bytes{64U * 1'024U * 1'024U};
    std::size_t max_import_depth{128};
    std::size_t max_import_edges{65'536};
    std::size_t max_work{128U * 1'024U * 1'024U};
    std::size_t max_ast_nodes_per_module{100'000};
    std::size_t max_semantic_nesting_depth{256};
    ::baas::script::runtime::ModuleSpecifierLimits specifier{};
    ::baas::script::runtime::NfcPredicate is_nfc{nullptr};
};

struct RuntimeScriptPackage {
    std::string entry_module;
    // Package modules only. Sources are owned bytes obtained from the immutable
    // RuntimeRepositoryReadView capability and are ready for evaluator input.
    std::vector<::baas::script::runtime::SourceModule> modules;
    // Unique host module IDs in first-seen source order. Host imports remain
    // graph edges but are never translated to manifest paths or repository reads.
    std::vector<std::string> host_imports;
    ::baas::script::runtime::ValidatedModuleGraph graph;
    std::size_t total_source_bytes{};
    std::size_t import_edges{};
    std::size_t work{};
};

struct RuntimeScriptPackageLoadResult {
    std::optional<RuntimeScriptPackage> package;
    RuntimeScriptPackageLoadError error{RuntimeScriptPackageLoadError::none};
    // Canonical logical module ID only; this API never returns a native path.
    std::string module;
    std::vector<::baas::script::Diagnostic> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == RuntimeScriptPackageLoadError::none
            && package.has_value();
    }
};

// Discovers one complete BAAS Script package from the scripts repository read
// capability. The entry and every package import are canonical extensionless
// module IDs; reads use ModuleSpecifier::manifest_source_path() and therefore
// remain exact, case-sensitive, and manifest-gated. No code is executed.
[[nodiscard]] RuntimeScriptPackageLoadResult load_runtime_script_package(
    const repository::RuntimeRepositoryReadView& scripts,
    std::string_view canonical_entry_module,
    const RuntimeScriptPackageLoaderLimits& limits = {},
    std::stop_token stop_token = {}) noexcept;

// Strict package-bound discovery. Every package source read must be present in
// the supplied exact allowlist with matching repository size/digest, and every
// allowlisted module must be reachable from the entry. The caller pin is
// checked before discovery and again before publication.
[[nodiscard]] RuntimeScriptPackageLoadResult load_manifested_runtime_script_package(
    const repository::RuntimeRepositoryReadView& scripts,
    RuntimeScriptPackagePin expected,
    std::string_view canonical_entry_module,
    std::span<const RuntimeScriptPackageModuleManifest> modules,
    const RuntimeScriptPackageLoaderLimits& limits = {},
    std::stop_token stop_token = {}) noexcept;

}  // namespace baas::runtime::script
