#pragma once

#include "script/Diagnostic.h"
#include "script/SemanticAnalyzer.h"
#include "script/runtime/ModuleSpecifier.h"
#include "script/runtime/SynchronousHost.h"
#include "script/runtime/ValueHeap.h"

#include <cstddef>
#include <stdexcept>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace baas::script::runtime {

struct SourceModule {
    std::string canonical_id;
    std::string source;
};

struct EvaluatorLimits {
    std::size_t max_module_source_bytes{4U * 1024U * 1024U};
    std::size_t max_total_source_bytes{64U * 1024U * 1024U};
    std::size_t max_steps{1'000'000};
    std::size_t max_call_depth{256};
    std::size_t max_value_stack{1'024};
    std::size_t max_container_elements{100'000};
    std::size_t max_collection_work{1'000'000};
    std::size_t max_functions{100'000};
    std::size_t max_import_depth{128};
    std::size_t max_modules{4'096};
    std::size_t max_defers_per_frame{1'024};
    std::size_t max_cleanup_steps{100'000};
    std::size_t max_cleanup_call_depth{128};
};

struct HostPermissionInput {
    std::vector<HostModuleRequirement> declared_modules;
    std::vector<std::string> declared_capabilities;
    std::vector<std::string> policy_capabilities;
    std::vector<std::string> platform_capabilities;
    std::vector<std::string> task_capabilities;
};

struct EvaluatorHostLimits {
    std::size_t max_host_modules{512};
    std::size_t max_permission_entries{4'096};
    std::size_t max_permission_string_bytes{4U * 1024U * 1024U};
    std::size_t max_authorized_exports{4'096};
    std::size_t max_host_calls{100'000};
    std::size_t max_host_arguments{64};
    std::size_t max_conversion_nodes{100'000};
    std::size_t max_conversion_bytes{64U * 1024U * 1024U};
    std::size_t max_conversion_work{500'000};
    std::size_t max_registry_validation_work{500'000};
};

struct SynchronousHostOptions {
    std::shared_ptr<const HostModuleRegistry> metadata;
    std::shared_ptr<const SynchronousNativeBindingSet> bindings;
    HostPermissionInput permissions;
    EvaluatorHostLimits limits{};
    // Missing scopes use max_host_calls. Duplicate scopes are rejected.
    std::vector<std::pair<std::string, std::size_t>> budget_limits;
    // Appended for aggregate source compatibility. Required by any exact
    // host<T> contract; one dispatcher owns only one evaluator/Heap context.
    std::shared_ptr<HostReleaseDispatcher> handles;
};

struct ModuleDiagnostic {
    std::string module;
    Diagnostic diagnostic;
};

class EvaluationCompileError final : public std::runtime_error {
public:
    explicit EvaluationCompileError(std::vector<ModuleDiagnostic> diagnostics);

    [[nodiscard]] const std::vector<ModuleDiagnostic>& diagnostics() const noexcept
    {
        return diagnostics_;
    }

private:
    std::vector<ModuleDiagnostic> diagnostics_;
};

class EvaluationError final : public std::runtime_error {
public:
    EvaluationError(
        LanguageErrorCode code,
        std::string message,
        std::string module,
        SourceSpan span,
        std::size_t steps,
        std::string structured_error = {});

    [[nodiscard]] LanguageErrorCode code() const noexcept { return code_; }
    [[nodiscard]] std::string_view code_name() const noexcept
    {
        return language_error_code_name(code_);
    }
    [[nodiscard]] bool catchable() const noexcept
    {
        return language_error_code_catchable(code_);
    }
    [[nodiscard]] const std::string& module() const noexcept { return module_; }
    [[nodiscard]] SourceSpan span() const noexcept { return span_; }
    [[nodiscard]] std::size_t steps() const noexcept { return steps_; }
    [[nodiscard]] bool has_structured_error() const noexcept
    {
        return !structured_error_.empty();
    }
    // Bounded compact baas.script.error/v1 JSON. This survives evaluator
    // teardown and preserves the full public Error envelope at the boundary.
    [[nodiscard]] const std::string& structured_error() const noexcept
    {
        return structured_error_;
    }

private:
    LanguageErrorCode code_;
    std::string module_;
    SourceSpan span_;
    std::size_t steps_;
    std::string structured_error_;
};

struct EvaluationStats {
    std::size_t steps{};
    std::size_t peak_call_depth{};
    std::size_t peak_value_stack{};
    std::size_t collection_work{};
    std::size_t initialized_modules{};
    std::size_t created_functions{};
    std::size_t registered_defers{};
    std::size_t executed_defers{};
    std::size_t cleanup_steps{};
    std::size_t peak_cleanup_call_depth{};
    std::size_t host_authorization_attempts{};
    std::size_t authorized_host_exports{};
    std::size_t host_calls{};
    std::size_t host_conversion_nodes{};
    std::size_t host_conversion_bytes{};
};

struct EvaluationResult {
    Value module_namespace;
    EvaluationStats stats;
};

// Dependency-free synchronous conformance evaluator. It executes validated
// package ASTs only; bytecode, async/tasks, and asynchronous Host adapters
// remain separate runtime boundaries. Synchronous structured errors and defer
// cleanup are implemented as the conformance oracle for ERR-009 through ERR-015.
class SynchronousEvaluator final {
public:
    explicit SynchronousEvaluator(
        std::vector<SourceModule> modules,
        EvaluatorLimits limits = {},
        HeapLimits heap_limits = {},
        SemanticOptions semantic_options = {},
        NfcPredicate is_nfc = nullptr);
    SynchronousEvaluator(
        std::vector<SourceModule> modules,
        SynchronousHostOptions host_options,
        EvaluatorLimits limits = {},
        HeapLimits heap_limits = {},
        SemanticOptions semantic_options = {},
        NfcPredicate is_nfc = nullptr);
    ~SynchronousEvaluator();

    SynchronousEvaluator(const SynchronousEvaluator&) = delete;
    SynchronousEvaluator& operator=(const SynchronousEvaluator&) = delete;
    SynchronousEvaluator(SynchronousEvaluator&&) = delete;
    SynchronousEvaluator& operator=(SynchronousEvaluator&&) = delete;

    [[nodiscard]] EvaluationResult execute(std::string_view entry_module);
    // Permanently rejects new execution and transfers all pending host release
    // ownership to the shared dispatcher. False means the caller must retain
    // that dispatcher and retry its detached releases; no Heap lifetime is needed.
    [[nodiscard]] bool close() noexcept;
    [[nodiscard]] Value module_export(
        std::string_view module, std::string_view export_name) const;

    [[nodiscard]] Heap& heap() noexcept;
    [[nodiscard]] const Heap& heap() const noexcept;
    [[nodiscard]] EvaluationStats stats() const noexcept;

private:
    struct Impl;
    [[nodiscard]] static Impl* create_impl(
        std::vector<SourceModule> modules,
        EvaluatorLimits limits,
        HeapLimits heap_limits,
        SemanticOptions semantic_options,
        NfcPredicate is_nfc,
        std::optional<SynchronousHostOptions> host_options);
    Impl* impl_;
};

}  // namespace baas::script::runtime
