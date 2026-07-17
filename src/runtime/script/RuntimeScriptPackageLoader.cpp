#include "runtime/script/RuntimeScriptPackageLoader.h"

#include "script/Parser.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace baas::runtime::script {
namespace {

namespace language = ::baas::script;
namespace language_runtime = ::baas::script::runtime;
namespace runtime_repository = ::baas::runtime::repository;

struct LoaderFailure {
    RuntimeScriptPackageLoadError error;
    std::string module;
    std::vector<language::Diagnostic> diagnostics;
};

[[noreturn]] void fail(
    const RuntimeScriptPackageLoadError error,
    std::string module = {},
    std::vector<language::Diagnostic> diagnostics = {})
{
    throw LoaderFailure{error, std::move(module), std::move(diagnostics)};
}

void check_cancelled(const std::stop_token stop)
{
    if (stop.stop_requested()) {
        fail(RuntimeScriptPackageLoadError::cancelled);
    }
}

[[nodiscard]] bool valid_limits(
    const RuntimeScriptPackageLoaderLimits& limits) noexcept
{
    return limits.max_modules != 0
        && limits.max_source_file_bytes != 0
        && limits.max_total_source_bytes != 0
        && limits.max_source_file_bytes <= limits.max_total_source_bytes
        && limits.max_import_depth != 0
        && limits.max_import_edges != 0
        && limits.max_work != 0
        && limits.max_ast_nodes_per_module != 0
        && limits.max_semantic_nesting_depth != 0
        && limits.specifier.max_bytes != 0
        && limits.specifier.max_segments != 0;
}

class WorkBudget final {
public:
    explicit WorkBudget(const std::size_t limit) noexcept : limit_(limit) {}

    void charge(const std::size_t amount)
    {
        if (amount > limit_ - used_) {
            fail(RuntimeScriptPackageLoadError::work_limit_exceeded);
        }
        used_ += amount;
    }

    [[nodiscard]] std::size_t used() const noexcept { return used_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return limit_ - used_; }

private:
    std::size_t limit_{};
    std::size_t used_{};
};

[[nodiscard]] std::string owned_source(const std::vector<std::byte>& bytes)
{
    std::string source(bytes.size(), '\0');
    if (!bytes.empty()) {
        std::memcpy(source.data(), bytes.data(), bytes.size());
    }
    return source;
}

[[nodiscard]] const runtime_repository::RuntimeRepositoryReadEntry*
manifest_entry(
    const runtime_repository::RuntimeRepositoryReadView& scripts,
    const std::string_view path) noexcept
{
    const auto entries = scripts.entries();
    const auto found = std::ranges::lower_bound(
        entries, path, {}, &runtime_repository::RuntimeRepositoryReadEntry::path);
    if (found == entries.end() || found->path != path) return nullptr;
    return &*found;
}

[[nodiscard]] language_runtime::ModuleSpecifier canonical_specifier(
    const std::string_view value,
    const RuntimeScriptPackageLoaderLimits& limits,
    const RuntimeScriptPackageLoadError failure)
{
    try {
        return language_runtime::validate_module_specifier(
            value, limits.is_nfc, limits.specifier);
    } catch (const language_runtime::ModuleSpecifierError&) {
        fail(failure, std::string{value});
    }
}

struct LoadedModule {
    language_runtime::SourceModule source;
    std::vector<std::string> imports;
};

[[nodiscard]] RuntimeScriptPackageLoadError graph_error(
    const language_runtime::ModuleGraphErrorCode code) noexcept
{
    using enum language_runtime::ModuleGraphErrorCode;
    switch (code) {
        case ModuleLimitExceeded:
            return RuntimeScriptPackageLoadError::module_limit_exceeded;
        case ImportEdgeLimitExceeded:
            return RuntimeScriptPackageLoadError::import_edge_limit_exceeded;
        case ValidationWorkLimitExceeded:
            return RuntimeScriptPackageLoadError::work_limit_exceeded;
        case MissingModule:
            return RuntimeScriptPackageLoadError::missing_package_module;
        case ImportCycle:
            return RuntimeScriptPackageLoadError::import_cycle;
        case DuplicateModule:
        case HostModuleDefinition:
            return RuntimeScriptPackageLoadError::graph_validation_failed;
    }
    return RuntimeScriptPackageLoadError::graph_validation_failed;
}

}  // namespace

std::string_view runtime_script_package_load_error_name(
    const RuntimeScriptPackageLoadError error) noexcept
{
    using enum RuntimeScriptPackageLoadError;
    switch (error) {
        case none: return "RSP000_NONE";
        case invalid_limits: return "RSP001_INVALID_LIMITS";
        case wrong_repository: return "RSP002_WRONG_REPOSITORY";
        case invalid_entry_module: return "RSP003_INVALID_ENTRY_MODULE";
        case host_entry_module: return "RSP004_HOST_ENTRY_MODULE";
        case module_not_manifested: return "RSP005_MODULE_NOT_MANIFESTED";
        case repository_read_failed: return "RSP006_REPOSITORY_READ_FAILED";
        case source_file_too_large: return "RSP007_SOURCE_FILE_TOO_LARGE";
        case total_source_bytes_exceeded:
            return "RSP008_TOTAL_SOURCE_BYTES_EXCEEDED";
        case module_limit_exceeded: return "RSP009_MODULE_LIMIT_EXCEEDED";
        case import_edge_limit_exceeded:
            return "RSP010_IMPORT_EDGE_LIMIT_EXCEEDED";
        case import_depth_exceeded: return "RSP011_IMPORT_DEPTH_EXCEEDED";
        case work_limit_exceeded: return "RSP012_WORK_LIMIT_EXCEEDED";
        case parse_failed: return "RSP013_PARSE_FAILED";
        case semantic_failed: return "RSP014_SEMANTIC_FAILED";
        case invalid_import_specifier:
            return "RSP015_INVALID_IMPORT_SPECIFIER";
        case missing_package_module: return "RSP016_MISSING_PACKAGE_MODULE";
        case import_cycle: return "RSP017_IMPORT_CYCLE";
        case graph_validation_failed:
            return "RSP018_GRAPH_VALIDATION_FAILED";
        case cancelled: return "RSP019_CANCELLED";
        case resource_exhausted: return "RSP020_RESOURCE_EXHAUSTED";
    }
    return "RSP999_UNKNOWN";
}

RuntimeScriptPackageLoadResult load_runtime_script_package(
    const repository::RuntimeRepositoryReadView& scripts,
    const std::string_view canonical_entry_module,
    const RuntimeScriptPackageLoaderLimits& limits,
    const std::stop_token stop_token) noexcept
{
    try {
        if (!valid_limits(limits)) {
            fail(RuntimeScriptPackageLoadError::invalid_limits);
        }
        if (scripts.repository_id() != "scripts") {
            fail(RuntimeScriptPackageLoadError::wrong_repository);
        }
        check_cancelled(stop_token);

        auto entry = canonical_specifier(
            canonical_entry_module, limits,
            RuntimeScriptPackageLoadError::invalid_entry_module);
        if (entry.kind != language_runtime::ModuleKind::Package) {
            fail(
                RuntimeScriptPackageLoadError::host_entry_module,
                entry.canonical_id);
        }

        WorkBudget work{limits.max_work};
        std::deque<std::string> pending;
        std::set<std::string, std::less<>> discovered;
        std::set<std::string, std::less<>> seen_hosts;
        std::vector<std::string> host_imports;
        std::vector<LoadedModule> loaded;
        std::map<std::string, std::size_t, std::less<>> loaded_indices;
        pending.push_back(entry.canonical_id);
        discovered.insert(entry.canonical_id);

        std::size_t total_source_bytes = 0;
        std::size_t import_edges = 0;
        while (!pending.empty()) {
            check_cancelled(stop_token);
            std::string module = std::move(pending.front());
            pending.pop_front();
            work.charge(1);

            const auto specifier = canonical_specifier(
                module, limits,
                RuntimeScriptPackageLoadError::invalid_import_specifier);
            const auto source_path = specifier.manifest_source_path();
            const auto* manifested = manifest_entry(scripts, source_path);
            if (manifested == nullptr) {
                fail(RuntimeScriptPackageLoadError::module_not_manifested, module);
            }
            if (manifested->size > limits.max_source_file_bytes
                || manifested->size
                    > static_cast<std::uintmax_t>(
                        std::numeric_limits<std::size_t>::max())) {
                fail(RuntimeScriptPackageLoadError::source_file_too_large, module);
            }
            const auto source_size = static_cast<std::size_t>(manifested->size);
            if (source_size > limits.max_total_source_bytes - total_source_bytes) {
                fail(
                    RuntimeScriptPackageLoadError::total_source_bytes_exceeded,
                    module);
            }
            // Reserve the manifest-declared read cost before performing I/O.
            // RuntimeRepositoryReadView verifies that the returned payload has
            // exactly this size, so a package can never execute more read work
            // than the caller admitted.
            work.charge(source_size);

            std::vector<std::byte> bytes;
            try {
                bytes = scripts.read(
                    source_path, limits.max_source_file_bytes, stop_token);
            } catch (const runtime_repository::RuntimeRepositoryReadError& error) {
                if (error.code()
                    == runtime_repository::RuntimeRepositoryReadErrorCode::cancelled) {
                    fail(RuntimeScriptPackageLoadError::cancelled, module);
                }
                if (error.code()
                    == runtime_repository::RuntimeRepositoryReadErrorCode::resource_exhausted) {
                    fail(RuntimeScriptPackageLoadError::resource_exhausted, module);
                }
                fail(RuntimeScriptPackageLoadError::repository_read_failed, module);
            }
            check_cancelled(stop_token);
            total_source_bytes += bytes.size();

            LoadedModule record;
            record.source = {module, owned_source(bytes)};
            auto parsed = language::parse(record.source.source);
            check_cancelled(stop_token);
            if (parsed.has_errors()) {
                fail(
                    RuntimeScriptPackageLoadError::parse_failed,
                    module, std::move(parsed.diagnostics));
            }

            language::SemanticOptions semantic_options;
            const auto semantic_work_limit = std::min(
                limits.max_ast_nodes_per_module, work.remaining());
            if (semantic_work_limit == 0) {
                fail(RuntimeScriptPackageLoadError::work_limit_exceeded, module);
            }
            semantic_options.max_ast_nodes = semantic_work_limit;
            semantic_options.max_nesting_depth =
                limits.max_semantic_nesting_depth;
            auto semantic = language::analyze_semantics(
                parsed.program, semantic_options);
            check_cancelled(stop_token);
            work.charge(semantic.visited_ast_nodes);
            if (semantic.has_errors()) {
                const bool exhausted_shared_work =
                    semantic_work_limit < limits.max_ast_nodes_per_module
                    && std::ranges::any_of(
                        semantic.diagnostics,
                        [](const language::Diagnostic& diagnostic) {
                            return diagnostic.code
                                == language::semantic_diagnostic_code::node_limit;
                        });
                if (exhausted_shared_work) {
                    fail(
                        RuntimeScriptPackageLoadError::work_limit_exceeded,
                        module);
                }
                fail(
                    RuntimeScriptPackageLoadError::semantic_failed,
                    module, std::move(semantic.diagnostics));
            }

            record.imports.reserve(semantic.imports.size());
            for (const auto* imported : semantic.imports) {
                check_cancelled(stop_token);
                if (import_edges >= limits.max_import_edges) {
                    fail(
                        RuntimeScriptPackageLoadError::import_edge_limit_exceeded,
                        module);
                }
                ++import_edges;
                work.charge(1);
                auto imported_specifier = canonical_specifier(
                    imported->module, limits,
                    RuntimeScriptPackageLoadError::invalid_import_specifier);
                record.imports.push_back(imported_specifier.canonical_id);
                if (imported_specifier.kind == language_runtime::ModuleKind::Host) {
                    if (seen_hosts.insert(imported_specifier.canonical_id).second) {
                        host_imports.push_back(imported_specifier.canonical_id);
                    }
                    continue;
                }
                if (discovered.insert(imported_specifier.canonical_id).second) {
                    if (discovered.size() > limits.max_modules) {
                        fail(
                            RuntimeScriptPackageLoadError::module_limit_exceeded,
                            imported_specifier.canonical_id);
                    }
                    pending.push_back(std::move(imported_specifier.canonical_id));
                }
            }

            loaded_indices.emplace(module, loaded.size());
            loaded.push_back(std::move(record));
        }

        check_cancelled(stop_token);
        std::vector<language_runtime::ModuleDefinition> definitions;
        definitions.reserve(loaded.size());
        for (const auto& record : loaded) {
            definitions.push_back(
                {record.source.canonical_id, record.imports});
        }

        language_runtime::ModuleGraphLimits graph_limits;
        graph_limits.max_modules = limits.max_modules;
        graph_limits.max_import_edges = limits.max_import_edges;
        graph_limits.max_validation_work = work.remaining();
        graph_limits.specifier = limits.specifier;
        if (graph_limits.max_validation_work == 0) {
            fail(RuntimeScriptPackageLoadError::work_limit_exceeded);
        }

        language_runtime::ValidatedModuleGraph graph;
        try {
            graph = language_runtime::validate_module_graph(
                definitions, limits.is_nfc, graph_limits);
        } catch (const language_runtime::ModuleSpecifierError&) {
            fail(RuntimeScriptPackageLoadError::invalid_import_specifier);
        } catch (const language_runtime::ModuleGraphError& error) {
            fail(graph_error(error.code()), error.module());
        } catch (const std::invalid_argument&) {
            fail(RuntimeScriptPackageLoadError::invalid_limits);
        }
        work.charge(graph.validation_work);
        check_cancelled(stop_token);

        std::map<std::string, std::size_t, std::less<>> dependency_depth;
        for (const auto& module : graph.initialization_order) {
            check_cancelled(stop_token);
            const auto record = loaded_indices.find(module);
            if (record == loaded_indices.end()) {
                fail(
                    RuntimeScriptPackageLoadError::graph_validation_failed,
                    module);
            }
            std::size_t depth = 0;
            for (const auto& imported : loaded[record->second].imports) {
                check_cancelled(stop_token);
                const auto imported_specifier = canonical_specifier(
                    imported, limits,
                    RuntimeScriptPackageLoadError::invalid_import_specifier);
                if (imported_specifier.kind == language_runtime::ModuleKind::Host) {
                    continue;
                }
                work.charge(1);
                const auto dependency = dependency_depth.find(
                    imported_specifier.canonical_id);
                if (dependency == dependency_depth.end()) {
                    fail(
                        RuntimeScriptPackageLoadError::graph_validation_failed,
                        imported_specifier.canonical_id);
                }
                if (dependency->second >= limits.max_import_depth) {
                    fail(
                        RuntimeScriptPackageLoadError::import_depth_exceeded,
                        module);
                }
                depth = std::max(depth, dependency->second + 1);
            }
            dependency_depth.emplace(module, depth);
        }

        RuntimeScriptPackage package;
        package.entry_module = entry.canonical_id;
        package.host_imports = std::move(host_imports);
        package.total_source_bytes = total_source_bytes;
        package.import_edges = import_edges;
        package.work = work.used();
        package.modules.reserve(loaded.size());
        for (const auto& module : graph.initialization_order) {
            check_cancelled(stop_token);
            const auto record = loaded_indices.find(module);
            package.modules.push_back(
                std::move(loaded[record->second].source));
        }
        package.graph = std::move(graph);
        return {
            std::optional<RuntimeScriptPackage>{std::move(package)},
            RuntimeScriptPackageLoadError::none,
            {},
            {}};
    } catch (LoaderFailure& failure) {
        return {
            std::nullopt,
            failure.error,
            std::move(failure.module),
            std::move(failure.diagnostics)};
    } catch (const std::bad_alloc&) {
        return {
            std::nullopt,
            RuntimeScriptPackageLoadError::resource_exhausted,
            {},
            {}};
    } catch (...) {
        return {
            std::nullopt,
            RuntimeScriptPackageLoadError::graph_validation_failed,
            {},
            {}};
    }
}

}  // namespace baas::runtime::script
