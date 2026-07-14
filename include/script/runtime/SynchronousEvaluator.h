#pragma once

#include "script/Diagnostic.h"
#include "script/SemanticAnalyzer.h"
#include "script/runtime/ModuleSpecifier.h"
#include "script/runtime/ValueHeap.h"

#include <cstddef>
#include <stdexcept>
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
        std::size_t steps);

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

private:
    LanguageErrorCode code_;
    std::string module_;
    SourceSpan span_;
    std::size_t steps_;
};

struct EvaluationStats {
    std::size_t steps{};
    std::size_t peak_call_depth{};
    std::size_t peak_value_stack{};
    std::size_t collection_work{};
    std::size_t initialized_modules{};
    std::size_t created_functions{};
};

struct EvaluationResult {
    Value module_namespace;
    EvaluationStats stats;
};

// Dependency-free synchronous conformance evaluator. It executes validated
// package ASTs only; bytecode, async/tasks, Host adapters, and structured
// throw/catch/defer unwinding remain separate runtime boundaries.
class SynchronousEvaluator final {
public:
    explicit SynchronousEvaluator(
        std::vector<SourceModule> modules,
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
        NfcPredicate is_nfc);
    Impl* impl_;
};

}  // namespace baas::script::runtime
