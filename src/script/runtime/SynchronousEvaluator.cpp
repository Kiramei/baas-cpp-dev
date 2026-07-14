#include "script/runtime/SynchronousEvaluator.h"

#include "script/Ast.h"
#include "script/Parser.h"
#include "script/runtime/Environment.h"
#include "script/runtime/ErrorTranslation.h"
#include "script/runtime/ModuleGraph.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace baas::script::runtime {
namespace {

using namespace ast;

[[nodiscard]] bool diagnostic_less(
    const ModuleDiagnostic& left, const ModuleDiagnostic& right)
{
    return std::tie(
               left.module,
               left.diagnostic.span.begin.byte_offset,
               left.diagnostic.code,
               left.diagnostic.message)
        < std::tie(
               right.module,
               right.diagnostic.span.begin.byte_offset,
               right.diagnostic.code,
               right.diagnostic.message);
}

[[nodiscard]] bool checked_add(
    const std::int64_t left, const std::int64_t right, std::int64_t& result) noexcept
{
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
        || (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_subtract(
    const std::int64_t left, const std::int64_t right, std::int64_t& result) noexcept
{
    if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() + right)
        || (right < 0 && left > std::numeric_limits<std::int64_t>::max() + right)) {
        return false;
    }
    result = left - right;
    return true;
}

[[nodiscard]] bool checked_multiply(
    const std::int64_t left, const std::int64_t right, std::int64_t& result) noexcept
{
    using Limits = std::numeric_limits<std::int64_t>;
    if (left > 0) {
        if ((right > 0 && left > Limits::max() / right)
            || (right < 0 && right < Limits::min() / left)) {
            return false;
        }
    } else if (left < 0) {
        if ((right > 0 && left < Limits::min() / right)
            || (right < 0 && right < Limits::max() / left)) {
            return false;
        }
    }
    result = left * right;
    return true;
}

[[nodiscard]] std::size_t scalar_count(const std::string_view value) noexcept
{
    std::size_t result = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<unsigned char>(value[offset]);
        const std::size_t width = first <= 0x7F ? 1 : first <= 0xDF ? 2 : first <= 0xEF ? 3 : 4;
        offset += width;
        ++result;
    }
    return result;
}

[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> scalar_ranges(
    const std::string_view value, const std::size_t count)
{
    std::vector<std::pair<std::size_t, std::size_t>> result;
    result.reserve(count);
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<unsigned char>(value[offset]);
        const std::size_t width = first <= 0x7F ? 1 : first <= 0xDF ? 2 : first <= 0xEF ? 3 : 4;
        result.emplace_back(offset, offset + width);
        offset += width;
    }
    return result;
}

[[nodiscard]] bool is_public_name(const std::string_view name) noexcept
{
    return name.empty() || name.front() != '_';
}

}  // namespace

EvaluationCompileError::EvaluationCompileError(std::vector<ModuleDiagnostic> diagnostics)
    : std::runtime_error("script package contains compile diagnostics"),
      diagnostics_(std::move(diagnostics))
{
}

EvaluationError::EvaluationError(
    const LanguageErrorCode code,
    std::string message,
    std::string module,
    const SourceSpan span,
    const std::size_t steps)
    : std::runtime_error(std::move(message)), code_(code), module_(std::move(module)),
      span_(span), steps_(steps)
{
}

struct SynchronousEvaluator::Impl {
    struct LexicalEnvironment {
        std::shared_ptr<Environment> values;
        std::shared_ptr<LexicalEnvironment> parent;
        std::map<std::string, bool, std::less<>> initialized;
        std::vector<std::string> declaration_order;
    };

    struct FunctionRecord {
        std::uint64_t id{};
        std::string name;
        std::string module;
        SourceSpan definition_span{};
        std::vector<Parameter> parameters;
        std::shared_ptr<const BlockStatement> body;
        std::shared_ptr<LexicalEnvironment> closure;
    };

    enum class ModuleState { Uninitialized, Loading, Ready, Failed };

    struct ModuleRecord {
        std::string id;
        std::unique_ptr<ParseResult> parsed;
        SemanticResult semantic;
        std::vector<std::string> imports;
        ModuleState state{ModuleState::Uninitialized};
        std::shared_ptr<LexicalEnvironment> environment;
        Value namespace_value;
        std::optional<Heap::RootId> namespace_root;
        std::optional<EvaluationError> failure;
    };

    enum class FlowKind { Normal, Break, Continue, Return };
    struct Flow {
        FlowKind kind{FlowKind::Normal};
        Value value{Value::null()};
    };

    struct StackGuard {
        Impl* evaluator;
        explicit StackGuard(Impl& evaluator, const SourceSpan span) : evaluator(&evaluator)
        {
            evaluator.charge_step(span);
            if (evaluator.value_stack_depth >= evaluator.limits.max_value_stack) {
                evaluator.fail(
                    LanguageErrorCode::StackLimitExceeded,
                    "synchronous evaluator value stack limit exceeded",
                    span);
            }
            ++evaluator.value_stack_depth;
            evaluator.stats.peak_value_stack = std::max(
                evaluator.stats.peak_value_stack, evaluator.value_stack_depth);
        }
        ~StackGuard() { --evaluator->value_stack_depth; }
    };

    struct CallGuard {
        Impl* evaluator;
        explicit CallGuard(Impl& evaluator, const SourceSpan span) : evaluator(&evaluator)
        {
            if (evaluator.call_depth >= evaluator.limits.max_call_depth) {
                evaluator.fail(
                    LanguageErrorCode::StackLimitExceeded,
                    "synchronous evaluator call depth limit exceeded",
                    span);
            }
            ++evaluator.call_depth;
            evaluator.stats.peak_call_depth = std::max(
                evaluator.stats.peak_call_depth, evaluator.call_depth);
        }
        ~CallGuard() { --evaluator->call_depth; }
    };

    struct ModuleGuard {
        Impl* evaluator;
        std::string previous;
        ModuleGuard(Impl& evaluator, std::string module)
            : evaluator(&evaluator), previous(std::move(evaluator.current_module))
        {
            evaluator.current_module = std::move(module);
        }
        ~ModuleGuard() { evaluator->current_module = std::move(previous); }
    };

    EvaluatorLimits limits;
    Heap heap;
    std::map<std::string, std::unique_ptr<ModuleRecord>, std::less<>> modules;
    std::vector<FunctionRecord> functions;
    EvaluationStats stats;
    std::string current_module;
    NfcPredicate nfc{};
    std::size_t value_stack_depth{};
    std::size_t call_depth{};
    std::size_t import_depth{};

    explicit Impl(
        std::vector<SourceModule> sources,
        EvaluatorLimits evaluator_limits,
        HeapLimits heap_limits,
        const SemanticOptions semantic_options,
        const NfcPredicate is_nfc)
        : limits(evaluator_limits), heap(heap_limits, is_nfc), nfc(is_nfc)
    {
        validate_limits();
        compile_modules(std::move(sources), semantic_options, is_nfc);
    }

    [[noreturn]] void fail(
        const LanguageErrorCode code,
        std::string message,
        const SourceSpan span = {}) const
    {
        throw EvaluationError(code, std::move(message), current_module, span, stats.steps);
    }

    void validate_limits() const
    {
        if (limits.max_module_source_bytes == 0 || limits.max_total_source_bytes == 0
            || limits.max_steps == 0 || limits.max_call_depth == 0
            || limits.max_value_stack == 0 || limits.max_container_elements == 0
            || limits.max_collection_work == 0 || limits.max_functions == 0
            || limits.max_import_depth == 0 || limits.max_modules == 0) {
            fail(LanguageErrorCode::ArgumentInvalid, "evaluator limits must be positive");
        }
    }

    void charge_step(const SourceSpan span)
    {
        if (stats.steps >= limits.max_steps) {
            fail(
                LanguageErrorCode::InstructionLimitExceeded,
                "synchronous evaluator step limit exceeded",
                span);
        }
        ++stats.steps;
    }

    void charge_collection(const std::size_t amount, const SourceSpan span)
    {
        if (amount > limits.max_container_elements
            || amount > limits.max_collection_work
            || stats.collection_work > limits.max_collection_work - amount) {
            fail(
                LanguageErrorCode::MemoryLimitExceeded,
                "synchronous evaluator collection budget exceeded",
                span);
        }
        stats.collection_work += amount;
    }

    void compile_modules(
        std::vector<SourceModule> sources,
        const SemanticOptions semantic_options,
        const NfcPredicate is_nfc)
    {
        if (sources.size() > limits.max_modules) {
            fail(LanguageErrorCode::MemoryLimitExceeded, "module count exceeds evaluator limit");
        }

        std::vector<ModuleDiagnostic> diagnostics;
        std::size_t total_source_bytes = 0;
        for (auto& source : sources) {
            if (source.source.size() > limits.max_module_source_bytes
                || source.source.size() > limits.max_total_source_bytes
                || total_source_bytes > limits.max_total_source_bytes - source.source.size()) {
                fail(
                    LanguageErrorCode::MemoryLimitExceeded,
                    "source byte budget exceeded before parsing");
            }
            total_source_bytes += source.source.size();
            ModuleSpecifier specifier;
            try {
                specifier = validate_module_specifier(source.canonical_id, is_nfc);
            } catch (const ModuleSpecifierError&) {
                fail(
                    LanguageErrorCode::ImportSpecifierInvalid,
                    "source module id is not canonical");
            }
            if (specifier.kind != ModuleKind::Package) {
                fail(
                    LanguageErrorCode::ImportSpecifierInvalid,
                    "a package source cannot define a Host module");
            }
            if (modules.contains(specifier.canonical_id)) {
                fail(
                    LanguageErrorCode::ModuleInitializationFailed,
                    "duplicate source module id");
            }

            auto parsed = std::make_unique<ParseResult>(parse(source.source));
            auto semantic = analyze_semantics(parsed->program, semantic_options);
            for (const auto& diagnostic : parsed->diagnostics) {
                diagnostics.push_back({specifier.canonical_id, diagnostic});
            }
            for (const auto& diagnostic : semantic.diagnostics) {
                diagnostics.push_back({specifier.canonical_id, diagnostic});
            }

            std::vector<std::string> imports;
            for (const auto& statement : parsed->program.statements) {
                if (const auto* imported = ast::as<ImportStatement>(statement)) {
                    imports.push_back(imported->module);
                }
            }
            auto record = std::make_unique<ModuleRecord>();
            record->id = specifier.canonical_id;
            record->parsed = std::move(parsed);
            record->semantic = std::move(semantic);
            record->imports = std::move(imports);
            modules.emplace(record->id, std::move(record));
        }

        if (!diagnostics.empty()) {
            std::sort(diagnostics.begin(), diagnostics.end(), diagnostic_less);
            throw EvaluationCompileError(std::move(diagnostics));
        }

        std::vector<ModuleDefinition> definitions;
        definitions.reserve(modules.size());
        for (const auto& [id, module] : modules) {
            definitions.push_back({id, module->imports});
        }
        ModuleGraphLimits graph_limits;
        graph_limits.max_modules = limits.max_modules;
        graph_limits.max_import_edges = limits.max_collection_work;
        graph_limits.max_validation_work = std::max(
            limits.max_collection_work, limits.max_modules);
        try {
            static_cast<void>(validate_module_graph(definitions, is_nfc, graph_limits));
        } catch (const ModuleSpecifierError&) {
            fail(
                LanguageErrorCode::ImportSpecifierInvalid,
                "import module id is not canonical");
        } catch (const ModuleGraphError& error) {
            switch (error.code()) {
                case ModuleGraphErrorCode::ImportCycle:
                    fail(LanguageErrorCode::ImportCycle, "package import graph contains a cycle");
                case ModuleGraphErrorCode::MissingModule:
                    fail(
                        LanguageErrorCode::ModuleInitializationFailed,
                        "package import is absent from the source snapshot");
                case ModuleGraphErrorCode::ModuleLimitExceeded:
                case ModuleGraphErrorCode::ImportEdgeLimitExceeded:
                case ModuleGraphErrorCode::ValidationWorkLimitExceeded:
                    fail(LanguageErrorCode::ImportDepthLimit, "package graph limit exceeded");
                case ModuleGraphErrorCode::DuplicateModule:
                case ModuleGraphErrorCode::HostModuleDefinition:
                    fail(
                        LanguageErrorCode::ModuleInitializationFailed,
                        "package graph validation failed");
            }
        }
    }

    [[nodiscard]] std::shared_ptr<LexicalEnvironment> make_environment(
        const std::shared_ptr<LexicalEnvironment>& parent = {})
    {
        auto result = std::make_shared<LexicalEnvironment>();
        result->parent = parent;
        result->values = std::make_shared<Environment>(
            heap, parent ? parent->values : std::shared_ptr<Environment>{});
        return result;
    }

    void predeclare(
        const std::shared_ptr<LexicalEnvironment>& environment,
        const std::string& name,
        const SourceSpan span)
    {
        if (environment->initialized.contains(name)
            || !environment->values->define(name, Value::null())) {
            fail(LanguageErrorCode::InternalInvariant, "duplicate validated binding", span);
        }
        environment->initialized.emplace(name, false);
        environment->declaration_order.push_back(name);
    }

    void collect_scope_declarations(
        const StmtPtr& statement,
        std::vector<std::pair<std::string, SourceSpan>>& declarations) const
    {
        if (!statement) return;
        switch (statement->kind) {
            case NodeKind::LetStatement: {
                const auto& value = static_cast<const LetStatement&>(*statement);
                declarations.emplace_back(value.name, value.span);
                break;
            }
            case NodeKind::FunctionDeclaration: {
                const auto& value = static_cast<const FunctionDeclaration&>(*statement);
                declarations.emplace_back(value.name, value.span);
                break;
            }
            case NodeKind::ImportStatement: {
                const auto& value = static_cast<const ImportStatement&>(*statement);
                declarations.emplace_back(value.alias, value.span);
                break;
            }
            case NodeKind::IfStatement: {
                const auto& value = static_cast<const IfStatement&>(*statement);
                if (value.consequent && value.consequent->kind != NodeKind::BlockStatement) {
                    collect_scope_declarations(value.consequent, declarations);
                }
                if (value.alternate && value.alternate->kind != NodeKind::BlockStatement) {
                    collect_scope_declarations(value.alternate, declarations);
                }
                break;
            }
            case NodeKind::WhileStatement: {
                const auto& value = static_cast<const WhileStatement&>(*statement);
                if (value.body && value.body->kind != NodeKind::BlockStatement) {
                    collect_scope_declarations(value.body, declarations);
                }
                break;
            }
            case NodeKind::DeferStatement: {
                const auto& value = static_cast<const DeferStatement&>(*statement);
                if (value.statement && value.statement->kind != NodeKind::BlockStatement) {
                    collect_scope_declarations(value.statement, declarations);
                }
                break;
            }
            default: break;
        }
    }

    void predeclare_statements(
        const std::shared_ptr<LexicalEnvironment>& environment,
        const std::vector<StmtPtr>& statements)
    {
        std::vector<std::pair<std::string, SourceSpan>> declarations;
        for (const auto& statement : statements) {
            collect_scope_declarations(statement, declarations);
        }
        for (const auto& [name, span] : declarations) predeclare(environment, name, span);
    }

    void initialize_binding(
        const std::shared_ptr<LexicalEnvironment>& environment,
        const std::string_view name,
        const Value value,
        const SourceSpan span)
    {
        const auto found = environment->initialized.find(name);
        if (found == environment->initialized.end()) {
            fail(LanguageErrorCode::InternalInvariant, "validated binding slot is absent", span);
        }
        if (environment->values->assign(name, value) != AssignResult::Updated) {
            fail(LanguageErrorCode::InternalInvariant, "validated binding update failed", span);
        }
        found->second = true;
    }

    [[nodiscard]] Value read_binding(
        const std::shared_ptr<LexicalEnvironment>& environment,
        const std::string_view name,
        const SourceSpan span) const
    {
        for (auto current = environment; current; current = current->parent) {
            const auto found = current->initialized.find(name);
            if (found == current->initialized.end()) continue;
            if (!found->second) {
                fail(
                    LanguageErrorCode::UninitializedBinding,
                    "binding was read before initialization",
                    span);
            }
            const auto value = current->values->lookup(name);
            if (!value) {
                fail(LanguageErrorCode::InternalInvariant, "binding root is absent", span);
            }
            return *value;
        }
        fail(LanguageErrorCode::NameNotFound, "binding is not defined", span);
    }

    void assign_binding(
        const std::shared_ptr<LexicalEnvironment>& environment,
        const std::string_view name,
        const Value value,
        const SourceSpan span)
    {
        for (auto current = environment; current; current = current->parent) {
            const auto found = current->initialized.find(name);
            if (found == current->initialized.end()) continue;
            if (current->values->assign(name, value) != AssignResult::Updated) {
                fail(LanguageErrorCode::InternalInvariant, "binding assignment failed", span);
            }
            found->second = true;
            return;
        }
        fail(LanguageErrorCode::NameNotFound, "assignment target is not defined", span);
    }

    [[nodiscard]] Value evaluate_expression(
        const ExprPtr& expression,
        const std::shared_ptr<LexicalEnvironment>& environment);
    [[nodiscard]] Value evaluate_expression_impl(
        const Expression& expression,
        const std::shared_ptr<LexicalEnvironment>& environment);
    [[nodiscard]] Flow execute_statement(
        const StmtPtr& statement,
        const std::shared_ptr<LexicalEnvironment>& environment);
    [[nodiscard]] Flow execute_statement_impl(
        const Statement& statement,
        const std::shared_ptr<LexicalEnvironment>& environment);

    [[nodiscard]] Flow execute_statements(
        const std::vector<StmtPtr>& statements,
        const std::shared_ptr<LexicalEnvironment>& environment)
    {
        for (const auto& statement : statements) {
            auto flow = execute_statement(statement, environment);
            if (flow.kind != FlowKind::Normal) return flow;
        }
        return {};
    }

    [[nodiscard]] Flow execute_block(
        const BlockStatement& block,
        const std::shared_ptr<LexicalEnvironment>& parent,
        const bool create_scope)
    {
        auto environment = create_scope ? make_environment(parent) : parent;
        if (create_scope) predeclare_statements(environment, block.statements);
        return execute_statements(block.statements, environment);
    }

    [[nodiscard]] Value create_function(
        std::string name,
        const bool is_async,
        const std::vector<Parameter>& parameters,
        const std::shared_ptr<const BlockStatement>& body,
        const std::shared_ptr<LexicalEnvironment>& closure,
        const SourceSpan span)
    {
        if (is_async) {
            fail(
                LanguageErrorCode::ArgumentInvalid,
                "async functions are outside the synchronous evaluator boundary",
                span);
        }
        if (!body) {
            fail(LanguageErrorCode::InternalInvariant, "function body is absent", span);
        }
        if (functions.size() >= limits.max_functions) {
            fail(
                LanguageErrorCode::MemoryLimitExceeded,
                "function creation limit exceeded",
                span);
        }
        const auto id = static_cast<std::uint64_t>(functions.size() + 1);
        functions.push_back(
            {id, std::move(name), current_module, span, parameters, body, closure});
        try {
            auto value = heap.allocate_function({CallableKind::Script, id, {}});
            stats.created_functions = functions.size();
            return value;
        } catch (...) {
            functions.pop_back();
            throw;
        }
    }

    [[nodiscard]] Value initialize_module(const std::string& id, const SourceSpan import_span)
    {
        const auto found = modules.find(id);
        if (found == modules.end()) {
            fail(
                LanguageErrorCode::ModuleInitializationFailed,
                "package module is absent from the source snapshot",
                import_span);
        }
        auto& module = *found->second;
        if (module.state == ModuleState::Ready) return module.namespace_value;
        if (module.state == ModuleState::Loading) {
            fail(LanguageErrorCode::ImportCycle, "runtime module loading cycle detected", import_span);
        }
        if (module.state == ModuleState::Failed && module.failure) throw *module.failure;
        if (module.state == ModuleState::Failed) {
            fail(LanguageErrorCode::InternalInvariant, "module failure cache is incomplete", import_span);
        }
        if (import_depth >= limits.max_import_depth) {
            fail(LanguageErrorCode::ImportDepthLimit, "runtime import depth limit exceeded", import_span);
        }

        struct InitializationTransaction {
            Impl& evaluator;
            ModuleRecord& module;
            bool committed{false};
            InitializationTransaction(Impl& evaluator, ModuleRecord& module)
                : evaluator(evaluator), module(module)
            {
                ++evaluator.import_depth;
                module.state = ModuleState::Loading;
            }
            ~InitializationTransaction()
            {
                --evaluator.import_depth;
                if (!committed && module.state == ModuleState::Loading) {
                    module.state = ModuleState::Uninitialized;
                    module.environment.reset();
                }
            }
        } transaction(*this, module);
        ModuleGuard module_guard(*this, id);
        try {
            module.environment = make_environment();
            predeclare_statements(module.environment, module.parsed->program.statements);
            const auto flow = execute_statements(
                module.parsed->program.statements, module.environment);
            if (flow.kind != FlowKind::Normal) {
                fail(
                    LanguageErrorCode::InternalInvariant,
                    "non-local control escaped module scope",
                    module.parsed->program.span);
            }

            charge_collection(
                module.environment->declaration_order.size(), module.parsed->program.span);
            std::vector<std::pair<std::string, Value>> exports;
            exports.reserve(module.environment->declaration_order.size());
            for (const auto& name : module.environment->declaration_order) {
                const auto initialized = module.environment->initialized.find(name);
                if (initialized == module.environment->initialized.end()
                    || !initialized->second || !is_public_name(name)) {
                    continue;
                }
                exports.emplace_back(
                    name, read_binding(module.environment, name, module.parsed->program.span));
            }
            module.namespace_value = heap.allocate_module({id, std::move(exports)});
            module.namespace_root = heap.add_root(module.namespace_value);
            module.state = ModuleState::Ready;
            transaction.committed = true;
            ++stats.initialized_modules;
            return module.namespace_value;
        } catch (const EvaluationError& error) {
            module.state = ModuleState::Failed;
            module.failure = error;
            module.environment.reset();
            throw;
        } catch (const RuntimeError& error) {
            EvaluationError translated(
                translate_runtime_error_code(error.code()).code,
                error.what(),
                current_module,
                module.parsed->program.span,
                stats.steps);
            module.state = ModuleState::Failed;
            module.failure = translated;
            module.environment.reset();
            throw translated;
        } catch (const std::bad_alloc&) {
            EvaluationError translated(
                LanguageErrorCode::MemoryLimitExceeded,
                "evaluator allocation failed",
                current_module,
                module.parsed->program.span,
                stats.steps);
            module.state = ModuleState::Failed;
            module.failure = translated;
            module.environment.reset();
            throw translated;
        } catch (const std::length_error&) {
            EvaluationError translated(
                LanguageErrorCode::MemoryLimitExceeded,
                "evaluator container allocation exceeded its bound",
                current_module,
                module.parsed->program.span,
                stats.steps);
            module.state = ModuleState::Failed;
            module.failure = translated;
            module.environment.reset();
            throw translated;
        } catch (const std::exception&) {
            EvaluationError translated(
                LanguageErrorCode::InternalInvariant,
                "unexpected evaluator module failure",
                current_module,
                module.parsed->program.span,
                stats.steps);
            module.state = ModuleState::Failed;
            module.failure = translated;
            module.environment.reset();
            throw translated;
        } catch (...) {
            EvaluationError translated(
                LanguageErrorCode::InternalInvariant,
                "unknown evaluator module failure",
                current_module,
                module.parsed->program.span,
                stats.steps);
            module.state = ModuleState::Failed;
            module.failure = translated;
            module.environment.reset();
            throw translated;
        }
    }

    [[nodiscard]] EvaluationResult execute(const std::string_view entry)
    {
        ModuleSpecifier specifier;
        try {
            specifier = validate_module_specifier(entry, nfc);
        } catch (const ModuleSpecifierError&) {
            fail(LanguageErrorCode::ImportSpecifierInvalid, "entry module id is not canonical");
        }
        if (specifier.kind != ModuleKind::Package) {
            fail(LanguageErrorCode::HostUnavailable, "Host modules cannot be evaluator entries");
        }
        const auto value = initialize_module(specifier.canonical_id, {});
        return {value, stats};
    }

    [[nodiscard]] Value module_export(
        const std::string_view module_id, const std::string_view export_name) const
    {
        const auto module = modules.find(module_id);
        if (module == modules.end() || module->second->state != ModuleState::Ready) {
            fail(
                LanguageErrorCode::ModuleInitializationFailed,
                "module namespace is not ready");
        }
        if (!is_public_name(export_name)) {
            fail(LanguageErrorCode::ModuleMemberMissing, "module export is private or absent");
        }
        const auto metadata = heap.module_metadata(module->second->namespace_value.as_heap_ref());
        const auto found = std::find_if(
            metadata.exports.begin(), metadata.exports.end(), [&](const auto& item) {
                return item.first == export_name;
            });
        if (found == metadata.exports.end()) {
            fail(LanguageErrorCode::ModuleMemberMissing, "module export is private or absent");
        }
        return found->second;
    }

    [[nodiscard]] Value apply_binary(
        BinaryOperator operation, Value left, Value right, SourceSpan span);
    [[nodiscard]] Value apply_arithmetic(
        BinaryOperator operation, Value left, Value right, SourceSpan span);
    [[nodiscard]] Value read_member(Value object, std::string_view member, SourceSpan span);
    [[nodiscard]] Value read_index(Value object, Value index, SourceSpan span);
    [[nodiscard]] Value read_slice(
        const SliceExpression& slice,
        const std::shared_ptr<LexicalEnvironment>& environment);
    [[nodiscard]] Value invoke(
        Value callee,
        const std::vector<CallArgument>& arguments,
        const std::shared_ptr<LexicalEnvironment>& caller,
        SourceSpan span);
    [[nodiscard]] std::vector<Value> iteration_plan(Value iterable, SourceSpan span);
};

Value SynchronousEvaluator::Impl::apply_arithmetic(
    const BinaryOperator operation,
    const Value left,
    const Value right,
    const SourceSpan span)
{
    const auto left_kind = heap.kind(left);
    const auto right_kind = heap.kind(right);
    if (operation == BinaryOperator::Add && left_kind == ValueKind::String
        && right_kind == ValueKind::String) {
        const auto left_scalars = heap.string_scalar_count(left.as_heap_ref());
        const auto right_scalars = heap.string_scalar_count(right.as_heap_ref());
        if (right_scalars > std::numeric_limits<std::size_t>::max() - left_scalars) {
            fail(LanguageErrorCode::MemoryLimitExceeded, "string concatenation size overflow", span);
        }
        charge_collection(left_scalars + right_scalars, span);
        auto value = heap.string_copy(left.as_heap_ref());
        value += heap.string_copy(right.as_heap_ref());
        return heap.allocate_string(std::move(value));
    }
    if (operation == BinaryOperator::Add && left_kind == ValueKind::List
        && right_kind == ValueKind::List) {
        const auto left_size = heap.list_size(left.as_heap_ref());
        const auto right_size = heap.list_size(right.as_heap_ref());
        if (right_size > std::numeric_limits<std::size_t>::max() - left_size) {
            fail(LanguageErrorCode::MemoryLimitExceeded, "list concatenation size overflow", span);
        }
        charge_collection(left_size + right_size, span);
        auto values = heap.list_values(left.as_heap_ref());
        const auto tail = heap.list_values(right.as_heap_ref());
        values.insert(values.end(), tail.begin(), tail.end());
        return heap.allocate_list(std::move(values));
    }

    const bool left_numeric = left_kind == ValueKind::Integer || left_kind == ValueKind::Float;
    const bool right_numeric = right_kind == ValueKind::Integer || right_kind == ValueKind::Float;
    if (!left_numeric || !right_numeric) {
        fail(LanguageErrorCode::TypeMismatch, "arithmetic operands must be numeric", span);
    }

    if (left_kind == ValueKind::Integer && right_kind == ValueKind::Integer
        && operation != BinaryOperator::Divide) {
        const auto a = left.as_integer();
        const auto b = right.as_integer();
        std::int64_t result{};
        switch (operation) {
            case BinaryOperator::Add:
                if (!checked_add(a, b, result)) {
                    fail(LanguageErrorCode::NumericOverflow, "integer addition overflow", span);
                }
                return Value(result);
            case BinaryOperator::Subtract:
                if (!checked_subtract(a, b, result)) {
                    fail(LanguageErrorCode::NumericOverflow, "integer subtraction overflow", span);
                }
                return Value(result);
            case BinaryOperator::Multiply:
                if (!checked_multiply(a, b, result)) {
                    fail(LanguageErrorCode::NumericOverflow, "integer multiplication overflow", span);
                }
                return Value(result);
            case BinaryOperator::FloorDivide:
            case BinaryOperator::Modulo: {
                if (b == 0) {
                    fail(LanguageErrorCode::DivisionByZero, "integer division by zero", span);
                }
                if (a == std::numeric_limits<std::int64_t>::min() && b == -1) {
                    if (operation == BinaryOperator::Modulo) return Value(std::int64_t{0});
                    fail(LanguageErrorCode::NumericOverflow, "integer floor division overflow", span);
                }
                auto quotient = a / b;
                auto remainder = a % b;
                if (remainder != 0 && ((remainder < 0) != (b < 0))) {
                    --quotient;
                    remainder += b;
                }
                return Value(
                    operation == BinaryOperator::FloorDivide ? quotient : remainder);
            }
            case BinaryOperator::Power: {
                if (b < 0) {
                    return Value(std::pow(static_cast<double>(a), static_cast<double>(b)));
                }
                std::int64_t base = a;
                std::uint64_t exponent = static_cast<std::uint64_t>(b);
                std::int64_t power = 1;
                while (exponent != 0) {
                    if ((exponent & 1U) != 0 && !checked_multiply(power, base, power)) {
                        fail(LanguageErrorCode::NumericOverflow, "integer power overflow", span);
                    }
                    exponent >>= 1U;
                    if (exponent != 0 && !checked_multiply(base, base, base)) {
                        fail(LanguageErrorCode::NumericOverflow, "integer power overflow", span);
                    }
                }
                return Value(power);
            }
            default: break;
        }
    }

    const double a = left_kind == ValueKind::Float
        ? left.as_float()
        : static_cast<double>(left.as_integer());
    const double b = right_kind == ValueKind::Float
        ? right.as_float()
        : static_cast<double>(right.as_integer());
    switch (operation) {
        case BinaryOperator::Add: return Value(a + b);
        case BinaryOperator::Subtract: return Value(a - b);
        case BinaryOperator::Multiply: return Value(a * b);
        case BinaryOperator::Divide:
            if (b == 0.0) fail(LanguageErrorCode::DivisionByZero, "float division by zero", span);
            return Value(a / b);
        case BinaryOperator::FloorDivide:
            if (b == 0.0) fail(LanguageErrorCode::DivisionByZero, "float division by zero", span);
            return Value(std::floor(a / b));
        case BinaryOperator::Modulo:
            if (b == 0.0) fail(LanguageErrorCode::DivisionByZero, "float modulo by zero", span);
            return Value(a - std::floor(a / b) * b);
        case BinaryOperator::Power: return Value(std::pow(a, b));
        default: fail(LanguageErrorCode::InternalInvariant, "invalid arithmetic operator", span);
    }
}

Value SynchronousEvaluator::Impl::apply_binary(
    const BinaryOperator operation,
    const Value left,
    const Value right,
    const SourceSpan span)
{
    switch (operation) {
        case BinaryOperator::Equal: return Value(heap.equals(left, right));
        case BinaryOperator::NotEqual: return Value(!heap.equals(left, right));
        case BinaryOperator::Is: return Value(left == right);
        case BinaryOperator::NotIs: return Value(!(left == right));
        case BinaryOperator::Less:
        case BinaryOperator::LessEqual:
        case BinaryOperator::Greater:
        case BinaryOperator::GreaterEqual: {
            const auto left_kind = heap.kind(left);
            const auto right_kind = heap.kind(right);
            bool result{};
            if (left_kind == ValueKind::Integer && right_kind == ValueKind::Integer) {
                const auto a = left.as_integer();
                const auto b = right.as_integer();
                if (operation == BinaryOperator::Less) result = a < b;
                else if (operation == BinaryOperator::LessEqual) result = a <= b;
                else if (operation == BinaryOperator::Greater) result = a > b;
                else result = a >= b;
            } else if ((left_kind == ValueKind::Integer || left_kind == ValueKind::Float)
                && (right_kind == ValueKind::Integer || right_kind == ValueKind::Float)) {
                const double a = left_kind == ValueKind::Float
                    ? left.as_float()
                    : static_cast<double>(left.as_integer());
                const double b = right_kind == ValueKind::Float
                    ? right.as_float()
                    : static_cast<double>(right.as_integer());
                if (operation == BinaryOperator::Less) result = a < b;
                else if (operation == BinaryOperator::LessEqual) result = a <= b;
                else if (operation == BinaryOperator::Greater) result = a > b;
                else result = a >= b;
            } else if (left_kind == ValueKind::String && right_kind == ValueKind::String) {
                const auto left_scalars = heap.string_scalar_count(left.as_heap_ref());
                const auto right_scalars = heap.string_scalar_count(right.as_heap_ref());
                if (right_scalars > std::numeric_limits<std::size_t>::max() - left_scalars) {
                    fail(LanguageErrorCode::MemoryLimitExceeded, "string ordering work overflow", span);
                }
                charge_collection(left_scalars + right_scalars, span);
                const auto a = heap.string_copy(left.as_heap_ref());
                const auto b = heap.string_copy(right.as_heap_ref());
                if (operation == BinaryOperator::Less) result = a < b;
                else if (operation == BinaryOperator::LessEqual) result = a <= b;
                else if (operation == BinaryOperator::Greater) result = a > b;
                else result = a >= b;
            } else {
                fail(LanguageErrorCode::TypeMismatch, "ordering operands are incompatible", span);
            }
            return Value(result);
        }
        case BinaryOperator::In:
        case BinaryOperator::NotIn: {
            bool contains = false;
            const auto right_kind = heap.kind(right);
            if (right_kind == ValueKind::List) {
                const auto size = heap.list_size(right.as_heap_ref());
                charge_collection(size, span);
                const auto values = heap.list_values(right.as_heap_ref());
                contains = std::any_of(values.begin(), values.end(), [&](const Value candidate) {
                    return heap.equals(left, candidate);
                });
            } else if (right_kind == ValueKind::OrderedMap) {
                if (heap.kind(left) != ValueKind::String) {
                    fail(LanguageErrorCode::TypeMismatch, "map membership requires a string key", span);
                }
                charge_collection(heap.map_size(right.as_heap_ref()), span);
                contains = heap.map_get(
                    right.as_heap_ref(), heap.string_copy(left.as_heap_ref())).has_value();
            } else if (right_kind == ValueKind::String) {
                if (heap.kind(left) != ValueKind::String) {
                    fail(LanguageErrorCode::TypeMismatch, "string membership requires a string", span);
                }
                charge_collection(heap.string_scalar_count(right.as_heap_ref()), span);
                const auto haystack = heap.string_copy(right.as_heap_ref());
                contains = haystack.find(heap.string_copy(left.as_heap_ref())) != std::string::npos;
            } else {
                fail(LanguageErrorCode::TypeMismatch, "membership requires list, map, or string", span);
            }
            return Value(operation == BinaryOperator::In ? contains : !contains);
        }
        case BinaryOperator::Or:
        case BinaryOperator::And:
            fail(LanguageErrorCode::InternalInvariant, "short-circuit operator reached eager path", span);
        default: return apply_arithmetic(operation, left, right, span);
    }
}

Value SynchronousEvaluator::Impl::read_member(
    const Value object, const std::string_view member, const SourceSpan span)
{
    const auto kind = heap.kind(object);
    if (kind == ValueKind::OrderedMap) {
        charge_collection(heap.map_size(object.as_heap_ref()), span);
        const auto value = heap.map_get(object.as_heap_ref(), member);
        if (!value) fail(LanguageErrorCode::IndexOutOfRange, "map member is absent", span);
        return *value;
    }
    if (kind == ValueKind::Module) {
        if (!is_public_name(member)) {
            fail(LanguageErrorCode::ModuleMemberMissing, "module export is private or absent", span);
        }
        charge_collection(heap.module_export_count(object.as_heap_ref()), span);
        const auto metadata = heap.module_metadata(object.as_heap_ref());
        const auto found = std::find_if(
            metadata.exports.begin(), metadata.exports.end(), [&](const auto& item) {
                return item.first == member;
            });
        if (found == metadata.exports.end()) {
            fail(LanguageErrorCode::ModuleMemberMissing, "module export is private or absent", span);
        }
        return found->second;
    }
    fail(LanguageErrorCode::TypeMismatch, "member access requires a map or module", span);
}

Value SynchronousEvaluator::Impl::read_index(
    const Value object, const Value index, const SourceSpan span)
{
    const auto kind = heap.kind(object);
    if (kind == ValueKind::OrderedMap) {
        if (heap.kind(index) != ValueKind::String) {
            fail(LanguageErrorCode::TypeMismatch, "map index must be a string", span);
        }
        charge_collection(heap.map_size(object.as_heap_ref()), span);
        const auto value = heap.map_get(
            object.as_heap_ref(), heap.string_copy(index.as_heap_ref()));
        if (!value) fail(LanguageErrorCode::IndexOutOfRange, "map key is absent", span);
        return *value;
    }
    if (heap.kind(index) != ValueKind::Integer) {
        fail(LanguageErrorCode::TypeMismatch, "sequence index must be an integer", span);
    }
    const auto integer = index.as_integer();
    if (integer < 0) fail(LanguageErrorCode::IndexOutOfRange, "negative index is invalid", span);
    const auto position = static_cast<std::size_t>(integer);
    if (kind == ValueKind::List) {
        const auto size = heap.list_size(object.as_heap_ref());
        charge_collection(size, span);
        if (position >= size) {
            fail(LanguageErrorCode::IndexOutOfRange, "list index is out of range", span);
        }
        const auto values = heap.list_values(object.as_heap_ref());
        return values[position];
    }
    if (kind == ValueKind::String) {
        const auto count = heap.string_scalar_count(object.as_heap_ref());
        charge_collection(count, span);
        if (position >= count) {
            fail(LanguageErrorCode::IndexOutOfRange, "string index is out of range", span);
        }
        const auto value = heap.string_copy(object.as_heap_ref());
        const auto ranges = scalar_ranges(value, count);
        const auto [begin, end] = ranges[position];
        return heap.allocate_string(value.substr(begin, end - begin));
    }
    fail(LanguageErrorCode::TypeMismatch, "index access requires list, map, or string", span);
}

Value SynchronousEvaluator::Impl::read_slice(
    const SliceExpression& slice,
    const std::shared_ptr<LexicalEnvironment>& environment)
{
    auto roots = heap.root_scope();
    const auto object = evaluate_expression(slice.object, environment);
    roots.add(object);
    const auto kind = heap.kind(object);
    if (kind != ValueKind::List && kind != ValueKind::String) {
        fail(LanguageErrorCode::TypeMismatch, "slice requires a list or string", slice.span);
    }

    const auto evaluate_bound = [&](const std::optional<ExprPtr>& expression)
        -> std::optional<std::int64_t> {
        if (!expression) return std::nullopt;
        const auto value = evaluate_expression(*expression, environment);
        roots.add(value);
        if (heap.kind(value) != ValueKind::Integer) {
            fail(LanguageErrorCode::TypeMismatch, "slice bound must be an integer", slice.span);
        }
        return value.as_integer();
    };
    const auto start_value = evaluate_bound(slice.start);
    const auto stop_value = evaluate_bound(slice.stop);
    const auto step_value = evaluate_bound(slice.step);
    if ((start_value && *start_value < 0) || (stop_value && *stop_value < 0)
        || (step_value && *step_value <= 0)) {
        fail(LanguageErrorCode::IndexOutOfRange, "slice bounds require nonnegative indices and positive step", slice.span);
    }

    if (kind == ValueKind::List) {
        const auto size = heap.list_size(object.as_heap_ref());
        charge_collection(size, slice.span);
        const auto values = heap.list_values(object.as_heap_ref());
        const auto start = std::min<std::size_t>(
            start_value ? static_cast<std::size_t>(*start_value) : 0, size);
        const auto stop = std::min<std::size_t>(
            stop_value ? static_cast<std::size_t>(*stop_value) : size, size);
        const auto step = static_cast<std::size_t>(step_value.value_or(1));
        std::vector<Value> result;
        if (start < stop) {
            result.reserve((stop - start + step - 1) / step);
            for (auto index = start; index < stop; index += step) result.push_back(values[index]);
        }
        charge_collection(result.size(), slice.span);
        return heap.allocate_list(std::move(result));
    }

    const auto input_count = heap.string_scalar_count(object.as_heap_ref());
    charge_collection(input_count, slice.span);
    const auto value = heap.string_copy(object.as_heap_ref());
    const auto ranges = scalar_ranges(value, input_count);
    const auto start = std::min<std::size_t>(
        start_value ? static_cast<std::size_t>(*start_value) : 0, input_count);
    const auto stop = std::min<std::size_t>(
        stop_value ? static_cast<std::size_t>(*stop_value) : input_count, input_count);
    const auto step = static_cast<std::size_t>(step_value.value_or(1));
    std::string result;
    std::size_t result_count = 0;
    for (auto index = start; index < stop; index += step) {
        const auto [begin, end] = ranges[index];
        result.append(value, begin, end - begin);
        ++result_count;
    }
    charge_collection(result_count, slice.span);
    return heap.allocate_string(std::move(result));
}

Value SynchronousEvaluator::Impl::evaluate_expression(
    const ExprPtr& expression,
    const std::shared_ptr<LexicalEnvironment>& environment)
{
    if (!expression) fail(LanguageErrorCode::InternalInvariant, "expression node is absent");
    StackGuard guard(*this, expression->span);
    try {
        return evaluate_expression_impl(*expression, environment);
    } catch (const EvaluationError&) {
        throw;
    } catch (const RuntimeError& error) {
        fail(
            translate_runtime_error_code(error.code()).code,
            error.what(),
            expression->span);
    } catch (const std::bad_alloc&) {
        fail(LanguageErrorCode::MemoryLimitExceeded, "evaluator allocation failed", expression->span);
    } catch (const std::length_error&) {
        fail(LanguageErrorCode::MemoryLimitExceeded, "evaluator container bound exceeded", expression->span);
    } catch (const std::exception&) {
        fail(LanguageErrorCode::InternalInvariant, "unexpected expression evaluation failure", expression->span);
    } catch (...) {
        fail(LanguageErrorCode::InternalInvariant, "unknown expression evaluation failure", expression->span);
    }
}

Value SynchronousEvaluator::Impl::evaluate_expression_impl(
    const Expression& expression,
    const std::shared_ptr<LexicalEnvironment>& environment)
{
    switch (expression.kind) {
        case NodeKind::LiteralExpression: {
            const auto& literal = static_cast<const LiteralExpression&>(expression);
            return std::visit(
                [&](const auto& value) -> Value {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::monostate>) return Value::null();
                    else if constexpr (std::is_same_v<T, bool>) return Value(value);
                    else if constexpr (std::is_same_v<T, std::int64_t>) return Value(value);
                    else if constexpr (std::is_same_v<T, double>) return Value(value);
                    else {
                        charge_collection(scalar_count(value), literal.span);
                        return heap.allocate_string(value);
                    }
                },
                literal.value);
        }
        case NodeKind::IdentifierExpression: {
            const auto& identifier = static_cast<const IdentifierExpression&>(expression);
            return read_binding(environment, identifier.name, identifier.span);
        }
        case NodeKind::ListExpression: {
            const auto& list = static_cast<const ListExpression&>(expression);
            charge_collection(list.elements.size(), list.span);
            auto roots = heap.root_scope();
            std::vector<Value> values;
            values.reserve(list.elements.size());
            for (const auto& element : list.elements) {
                const auto value = evaluate_expression(element, environment);
                roots.add(value);
                values.push_back(value);
            }
            return heap.allocate_list(std::move(values));
        }
        case NodeKind::MapExpression: {
            const auto& map = static_cast<const MapExpression&>(expression);
            charge_collection(map.entries.size(), map.span);
            auto roots = heap.root_scope();
            const auto result = heap.allocate_map();
            roots.add(result);
            for (const auto& entry : map.entries) {
                const auto key = evaluate_expression(entry.key, environment);
                roots.add(key);
                if (heap.kind(key) != ValueKind::String) {
                    fail(LanguageErrorCode::TypeMismatch, "map literal key must be a string", entry.span);
                }
                const auto value = evaluate_expression(entry.value, environment);
                roots.add(value);
                heap.map_set(
                    result.as_heap_ref(), heap.string_copy(key.as_heap_ref()), value);
            }
            return result;
        }
        case NodeKind::FunctionExpression: {
            const auto& function = static_cast<const FunctionExpression&>(expression);
            return create_function(
                "<anonymous>",
                function.is_async,
                function.parameters,
                function.body,
                environment,
                function.span);
        }
        case NodeKind::UnaryExpression: {
            const auto& unary = static_cast<const UnaryExpression&>(expression);
            const auto operand = evaluate_expression(unary.operand, environment);
            if (unary.operation == UnaryOperator::Not) return Value(!heap.truthy(operand));
            const auto kind = heap.kind(operand);
            if (kind != ValueKind::Integer && kind != ValueKind::Float) {
                fail(LanguageErrorCode::TypeMismatch, "unary numeric operand required", unary.span);
            }
            if (unary.operation == UnaryOperator::Plus) return operand;
            if (kind == ValueKind::Float) return Value(-operand.as_float());
            if (operand.as_integer() == std::numeric_limits<std::int64_t>::min()) {
                fail(LanguageErrorCode::NumericOverflow, "integer negation overflow", unary.span);
            }
            return Value(-operand.as_integer());
        }
        case NodeKind::BinaryExpression: {
            const auto& binary = static_cast<const BinaryExpression&>(expression);
            auto roots = heap.root_scope();
            const auto left = evaluate_expression(binary.left, environment);
            roots.add(left);
            if (binary.operation == BinaryOperator::And && !heap.truthy(left)) return left;
            if (binary.operation == BinaryOperator::Or && heap.truthy(left)) return left;
            const auto right = evaluate_expression(binary.right, environment);
            roots.add(right);
            if (binary.operation == BinaryOperator::And
                || binary.operation == BinaryOperator::Or) {
                return right;
            }
            return apply_binary(binary.operation, left, right, binary.span);
        }
        case NodeKind::AssignmentExpression: {
            const auto& assignment = static_cast<const AssignmentExpression&>(expression);
            const auto arithmetic_operator = [&]() {
                switch (assignment.operation) {
                    case AssignmentOperator::Add: return BinaryOperator::Add;
                    case AssignmentOperator::Subtract: return BinaryOperator::Subtract;
                    case AssignmentOperator::Multiply: return BinaryOperator::Multiply;
                    case AssignmentOperator::Divide: return BinaryOperator::Divide;
                    case AssignmentOperator::FloorDivide: return BinaryOperator::FloorDivide;
                    case AssignmentOperator::Modulo: return BinaryOperator::Modulo;
                    case AssignmentOperator::Assign: return BinaryOperator::Add;
                }
                return BinaryOperator::Add;
            };
            auto roots = heap.root_scope();
            if (const auto* identifier = ast::as<IdentifierExpression>(assignment.target)) {
                std::optional<Value> old;
                if (assignment.operation != AssignmentOperator::Assign) {
                    old = read_binding(environment, identifier->name, identifier->span);
                    roots.add(*old);
                }
                auto value = evaluate_expression(assignment.value, environment);
                roots.add(value);
                if (old) value = apply_arithmetic(arithmetic_operator(), *old, value, assignment.span);
                roots.add(value);
                assign_binding(environment, identifier->name, value, identifier->span);
                return value;
            }

            Value object;
            std::optional<Value> index;
            std::string member;
            if (const auto* indexed = ast::as<IndexExpression>(assignment.target)) {
                object = evaluate_expression(indexed->object, environment);
                roots.add(object);
                index = evaluate_expression(indexed->index, environment);
                roots.add(*index);
            } else if (const auto* selected = ast::as<MemberExpression>(assignment.target)) {
                object = evaluate_expression(selected->object, environment);
                roots.add(object);
                member = selected->member;
            } else {
                fail(LanguageErrorCode::InternalInvariant, "invalid validated assignment target", assignment.span);
            }

            std::optional<Value> old;
            if (assignment.operation != AssignmentOperator::Assign) {
                old = index ? read_index(object, *index, assignment.target->span)
                            : read_member(object, member, assignment.target->span);
                roots.add(*old);
            }
            auto value = evaluate_expression(assignment.value, environment);
            roots.add(value);
            if (old) value = apply_arithmetic(arithmetic_operator(), *old, value, assignment.span);
            roots.add(value);

            const auto object_kind = heap.kind(object);
            if (index && object_kind == ValueKind::List) {
                if (heap.kind(*index) != ValueKind::Integer) {
                    fail(LanguageErrorCode::TypeMismatch, "list index must be an integer", assignment.target->span);
                }
                const auto position = index->as_integer();
                if (position < 0) {
                    fail(LanguageErrorCode::IndexOutOfRange, "negative list index is invalid", assignment.target->span);
                }
                heap.list_set(object.as_heap_ref(), static_cast<std::size_t>(position), value);
            } else if (object_kind == ValueKind::OrderedMap) {
                std::string key;
                if (index) {
                    if (heap.kind(*index) != ValueKind::String) {
                        fail(LanguageErrorCode::TypeMismatch, "map index must be a string", assignment.target->span);
                    }
                    key = heap.string_copy(index->as_heap_ref());
                } else {
                    key = member;
                }
                const auto size = heap.map_size(object.as_heap_ref());
                if (!heap.map_get(object.as_heap_ref(), key) && size >= limits.max_container_elements) {
                    fail(
                        LanguageErrorCode::MemoryLimitExceeded,
                        "map assignment exceeds the container-element budget",
                        assignment.target->span);
                }
                charge_collection(1, assignment.target->span);
                heap.map_set(object.as_heap_ref(), std::move(key), value);
            } else {
                fail(LanguageErrorCode::TypeMismatch, "assignment target is not a mutable collection", assignment.target->span);
            }
            return value;
        }
        case NodeKind::MemberExpression: {
            const auto& member = static_cast<const MemberExpression&>(expression);
            const auto object = evaluate_expression(member.object, environment);
            return read_member(object, member.member, member.span);
        }
        case NodeKind::IndexExpression: {
            const auto& index = static_cast<const IndexExpression&>(expression);
            auto roots = heap.root_scope();
            const auto object = evaluate_expression(index.object, environment);
            roots.add(object);
            const auto key = evaluate_expression(index.index, environment);
            roots.add(key);
            return read_index(object, key, index.span);
        }
        case NodeKind::SliceExpression:
            return read_slice(static_cast<const SliceExpression&>(expression), environment);
        case NodeKind::CallExpression: {
            const auto& call = static_cast<const CallExpression&>(expression);
            const auto callee = evaluate_expression(call.callee, environment);
            return invoke(callee, call.arguments, environment, call.span);
        }
        case NodeKind::AwaitExpression:
            fail(
                LanguageErrorCode::ArgumentInvalid,
                "await is outside the synchronous evaluator boundary",
                expression.span);
        default:
            fail(LanguageErrorCode::InternalInvariant, "statement node used as expression", expression.span);
    }
}

Value SynchronousEvaluator::Impl::invoke(
    const Value callee,
    const std::vector<CallArgument>& arguments,
    const std::shared_ptr<LexicalEnvironment>& caller,
    const SourceSpan span)
{
    auto roots = heap.root_scope();
    roots.add(callee);
    if (heap.kind(callee) != ValueKind::Function) {
        fail(LanguageErrorCode::NotCallable, "call target is not a function", span);
    }
    const auto metadata = heap.function_metadata(callee.as_heap_ref());
    if (metadata.kind != CallableKind::Script || metadata.callable_id == 0 ||
        metadata.callable_id > functions.size()) {
        fail(LanguageErrorCode::InternalInvariant, "function metadata is not evaluator-owned", span);
    }
    const auto& function_record = functions[static_cast<std::size_t>(metadata.callable_id - 1)];
    if (arguments.size() > std::numeric_limits<std::size_t>::max()
            - function_record.parameters.size()) {
        fail(LanguageErrorCode::MemoryLimitExceeded, "call binding size overflow", span);
    }
    charge_collection(arguments.size() + function_record.parameters.size(), span);
    const auto function = function_record;
    CallGuard call_guard(*this, span);

    struct ArgumentValue {
        std::optional<std::string> name;
        Value value;
    };
    std::vector<ArgumentValue> values;
    values.reserve(arguments.size());
    for (const auto& argument : arguments) {
        const auto value = evaluate_expression(argument.value, caller);
        roots.add(value);
        values.push_back({argument.name, value});
    }

    auto environment = make_environment(function.closure);
    for (const auto& parameter : function.parameters) {
        predeclare(environment, parameter.name, parameter.span);
    }
    predeclare_statements(environment, function.body->statements);

    std::vector<std::optional<Value>> bound(function.parameters.size());
    std::size_t next_positional = 0;
    for (const auto& argument : values) {
        std::size_t index{};
        if (argument.name) {
            const auto found = std::find_if(
                function.parameters.begin(), function.parameters.end(), [&](const Parameter& parameter) {
                    return parameter.name == *argument.name;
                });
            if (found == function.parameters.end()) {
                fail(LanguageErrorCode::CallArgumentUnknown, "named argument is unknown", span);
            }
            index = static_cast<std::size_t>(found - function.parameters.begin());
        } else {
            while (next_positional < bound.size() && bound[next_positional]) ++next_positional;
            if (next_positional >= bound.size()) {
                fail(LanguageErrorCode::CallArityMismatch, "too many positional arguments", span);
            }
            index = next_positional++;
        }
        if (bound[index]) {
            fail(LanguageErrorCode::CallArgumentDuplicate, "parameter was supplied more than once", span);
        }
        bound[index] = argument.value;
    }

    ModuleGuard module_guard(*this, function.module);
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const auto& parameter = function.parameters[index];
        Value value;
        if (bound[index]) {
            value = *bound[index];
        } else if (parameter.default_value) {
            value = evaluate_expression(*parameter.default_value, environment);
            roots.add(value);
        } else {
            fail(LanguageErrorCode::CallArityMismatch, "required argument is missing", span);
        }
        initialize_binding(environment, parameter.name, value, parameter.span);
    }

    auto flow = execute_block(*function.body, environment, false);
    if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
        fail(LanguageErrorCode::InternalInvariant, "loop control escaped a function", span);
    }
    if (flow.kind == FlowKind::Return) {
        roots.add(flow.value);
        return flow.value;
    }
    return Value::null();
}

std::vector<Value> SynchronousEvaluator::Impl::iteration_plan(
    const Value iterable, const SourceSpan span)
{
    const auto kind = heap.kind(iterable);
    if (kind == ValueKind::List) {
        const auto size = heap.list_size(iterable.as_heap_ref());
        charge_collection(size, span);
        auto values = heap.list_values(iterable.as_heap_ref());
        return values;
    }
    if (kind == ValueKind::OrderedMap) {
        const auto size = heap.map_size(iterable.as_heap_ref());
        charge_collection(size, span);
        const auto entries = heap.map_entries(iterable.as_heap_ref());
        auto roots = heap.root_scope();
        std::vector<Value> result;
        result.reserve(entries.size());
        for (const auto& [key, value] : entries) {
            static_cast<void>(value);
            const auto item = heap.allocate_string(key);
            roots.add(item);
            result.push_back(item);
        }
        return result;
    }
    if (kind == ValueKind::String) {
        const auto count = heap.string_scalar_count(iterable.as_heap_ref());
        charge_collection(count, span);
        const auto value = heap.string_copy(iterable.as_heap_ref());
        const auto ranges = scalar_ranges(value, count);
        auto roots = heap.root_scope();
        std::vector<Value> result;
        result.reserve(ranges.size());
        for (const auto [begin, end] : ranges) {
            const auto item = heap.allocate_string(value.substr(begin, end - begin));
            roots.add(item);
            result.push_back(item);
        }
        return result;
    }
    fail(LanguageErrorCode::TypeMismatch, "for iterable must be a list, map, or string", span);
}

SynchronousEvaluator::Impl::Flow SynchronousEvaluator::Impl::execute_statement(
    const StmtPtr& statement,
    const std::shared_ptr<LexicalEnvironment>& environment)
{
    if (!statement) fail(LanguageErrorCode::InternalInvariant, "statement node is absent");
    charge_step(statement->span);
    try {
        return execute_statement_impl(*statement, environment);
    } catch (const EvaluationError&) {
        throw;
    } catch (const RuntimeError& error) {
        fail(
            translate_runtime_error_code(error.code()).code,
            error.what(),
            statement->span);
    } catch (const std::bad_alloc&) {
        fail(LanguageErrorCode::MemoryLimitExceeded, "evaluator allocation failed", statement->span);
    } catch (const std::length_error&) {
        fail(LanguageErrorCode::MemoryLimitExceeded, "evaluator container bound exceeded", statement->span);
    } catch (const std::exception&) {
        fail(LanguageErrorCode::InternalInvariant, "unexpected statement evaluation failure", statement->span);
    } catch (...) {
        fail(LanguageErrorCode::InternalInvariant, "unknown statement evaluation failure", statement->span);
    }
}

SynchronousEvaluator::Impl::Flow SynchronousEvaluator::Impl::execute_statement_impl(
    const Statement& statement,
    const std::shared_ptr<LexicalEnvironment>& environment)
{
    switch (statement.kind) {
        case NodeKind::BlockStatement:
            return execute_block(static_cast<const BlockStatement&>(statement), environment, true);
        case NodeKind::LetStatement: {
            const auto& declaration = static_cast<const LetStatement&>(statement);
            const auto value = evaluate_expression(declaration.initializer, environment);
            initialize_binding(environment, declaration.name, value, declaration.span);
            return {};
        }
        case NodeKind::ExpressionStatement: {
            const auto& expression = static_cast<const ExpressionStatement&>(statement);
            static_cast<void>(evaluate_expression(expression.expression, environment));
            return {};
        }
        case NodeKind::IfStatement: {
            const auto& branch = static_cast<const IfStatement&>(statement);
            const auto condition = evaluate_expression(branch.condition, environment);
            if (heap.truthy(condition)) return execute_statement(branch.consequent, environment);
            if (branch.alternate) return execute_statement(branch.alternate, environment);
            return {};
        }
        case NodeKind::WhileStatement: {
            const auto& loop = static_cast<const WhileStatement&>(statement);
            for (;;) {
                const auto condition = evaluate_expression(loop.condition, environment);
                if (!heap.truthy(condition)) return {};
                const auto flow = execute_statement(loop.body, environment);
                if (flow.kind == FlowKind::Break) return {};
                if (flow.kind == FlowKind::Return) return flow;
                if (flow.kind != FlowKind::Normal && flow.kind != FlowKind::Continue) {
                    fail(LanguageErrorCode::InternalInvariant, "invalid while-loop control", loop.span);
                }
            }
        }
        case NodeKind::ForStatement: {
            const auto& loop = static_cast<const ForStatement&>(statement);
            auto roots = heap.root_scope();
            const auto iterable = evaluate_expression(loop.iterable, environment);
            roots.add(iterable);
            auto plan = iteration_plan(iterable, loop.span);
            for (const auto value : plan) roots.add(value);
            auto loop_environment = make_environment(environment);
            predeclare(loop_environment, loop.binding, loop.span);
            initialize_binding(loop_environment, loop.binding, Value::null(), loop.span);
            for (const auto value : plan) {
                initialize_binding(loop_environment, loop.binding, value, loop.span);
                const auto flow = execute_statement(loop.body, loop_environment);
                if (flow.kind == FlowKind::Break) return {};
                if (flow.kind == FlowKind::Return) return flow;
                if (flow.kind != FlowKind::Normal && flow.kind != FlowKind::Continue) {
                    fail(LanguageErrorCode::InternalInvariant, "invalid for-loop control", loop.span);
                }
            }
            return {};
        }
        case NodeKind::FunctionDeclaration: {
            const auto& function = static_cast<const FunctionDeclaration&>(statement);
            const auto value = create_function(
                function.name,
                function.is_async,
                function.parameters,
                function.body,
                environment,
                function.span);
            initialize_binding(environment, function.name, value, function.span);
            return {};
        }
        case NodeKind::ReturnStatement: {
            const auto& returned = static_cast<const ReturnStatement&>(statement);
            return {
                FlowKind::Return,
                returned.value ? evaluate_expression(*returned.value, environment) : Value::null()};
        }
        case NodeKind::BreakStatement: return {FlowKind::Break, Value::null()};
        case NodeKind::ContinueStatement: return {FlowKind::Continue, Value::null()};
        case NodeKind::ImportStatement: {
            const auto& imported = static_cast<const ImportStatement&>(statement);
            ModuleSpecifier specifier;
            try {
                specifier = validate_module_specifier(imported.module, nfc);
            } catch (const ModuleSpecifierError&) {
                fail(
                    LanguageErrorCode::ImportSpecifierInvalid,
                    "import module id is not canonical",
                    imported.span);
            }
            if (specifier.kind == ModuleKind::Host) {
                fail(
                    LanguageErrorCode::HostUnavailable,
                    "Host imports are outside the synchronous evaluator boundary",
                    imported.span);
            }
            const auto module = initialize_module(specifier.canonical_id, imported.span);
            initialize_binding(environment, imported.alias, module, imported.span);
            return {};
        }
        case NodeKind::ThrowStatement:
        case NodeKind::TryCatchStatement:
        case NodeKind::DeferStatement:
            fail(
                LanguageErrorCode::ArgumentInvalid,
                "structured throw/catch/defer requires the pending error unwinder",
                statement.span);
        default:
            fail(LanguageErrorCode::InternalInvariant, "expression node used as statement", statement.span);
    }
}

SynchronousEvaluator::SynchronousEvaluator(
    std::vector<SourceModule> modules,
    const EvaluatorLimits limits,
    const HeapLimits heap_limits,
    const SemanticOptions semantic_options,
    const NfcPredicate is_nfc)
    : impl_(create_impl(
          std::move(modules), limits, heap_limits, semantic_options, is_nfc))
{
}

SynchronousEvaluator::Impl* SynchronousEvaluator::create_impl(
    std::vector<SourceModule> modules,
    const EvaluatorLimits limits,
    const HeapLimits heap_limits,
    const SemanticOptions semantic_options,
    const NfcPredicate is_nfc)
{
    try {
        return new Impl(
            std::move(modules), limits, heap_limits, semantic_options, is_nfc);
    } catch (const EvaluationCompileError&) {
        throw;
    } catch (const EvaluationError&) {
        throw;
    } catch (const RuntimeError& error) {
        throw EvaluationError(
            translate_runtime_error_code(error.code()).code,
            error.what(),
            {},
            {},
            0);
    } catch (const std::bad_alloc&) {
        throw EvaluationError(
            LanguageErrorCode::MemoryLimitExceeded,
            "evaluator construction allocation failed",
            {},
            {},
            0);
    } catch (const std::length_error&) {
        throw EvaluationError(
            LanguageErrorCode::MemoryLimitExceeded,
            "evaluator construction container bound exceeded",
            {},
            {},
            0);
    } catch (const std::exception&) {
        throw EvaluationError(
            LanguageErrorCode::InternalInvariant,
            "unexpected evaluator construction failure",
            {},
            {},
            0);
    } catch (...) {
        throw EvaluationError(
            LanguageErrorCode::InternalInvariant,
            "unknown evaluator construction failure",
            {},
            {},
            0);
    }
}

SynchronousEvaluator::~SynchronousEvaluator() { delete impl_; }

EvaluationResult SynchronousEvaluator::execute(const std::string_view entry_module)
{
    try {
        return impl_->execute(entry_module);
    } catch (const EvaluationError&) {
        throw;
    } catch (const RuntimeError& error) {
        throw EvaluationError(
            translate_runtime_error_code(error.code()).code,
            error.what(),
            impl_->current_module,
            {},
            impl_->stats.steps);
    } catch (const std::bad_alloc&) {
        throw EvaluationError(
            LanguageErrorCode::MemoryLimitExceeded,
            "evaluator execution allocation failed",
            impl_->current_module,
            {},
            impl_->stats.steps);
    } catch (const std::length_error&) {
        throw EvaluationError(
            LanguageErrorCode::MemoryLimitExceeded,
            "evaluator execution container bound exceeded",
            impl_->current_module,
            {},
            impl_->stats.steps);
    } catch (const std::exception&) {
        throw EvaluationError(
            LanguageErrorCode::InternalInvariant,
            "unexpected evaluator execution failure",
            impl_->current_module,
            {},
            impl_->stats.steps);
    } catch (...) {
        throw EvaluationError(
            LanguageErrorCode::InternalInvariant,
            "unknown evaluator execution failure",
            impl_->current_module,
            {},
            impl_->stats.steps);
    }
}

Value SynchronousEvaluator::module_export(
    const std::string_view module, const std::string_view export_name) const
{
    try {
        return impl_->module_export(module, export_name);
    } catch (const EvaluationError&) {
        throw;
    } catch (const RuntimeError& error) {
        throw EvaluationError(
            translate_runtime_error_code(error.code()).code,
            error.what(),
            {},
            {},
            impl_->stats.steps);
    } catch (const std::bad_alloc&) {
        throw EvaluationError(
            LanguageErrorCode::MemoryLimitExceeded,
            "module export allocation failed",
            {},
            {},
            impl_->stats.steps);
    } catch (const std::length_error&) {
        throw EvaluationError(
            LanguageErrorCode::MemoryLimitExceeded,
            "module export container bound exceeded",
            {},
            {},
            impl_->stats.steps);
    } catch (const std::exception&) {
        throw EvaluationError(
            LanguageErrorCode::InternalInvariant,
            "unexpected module export failure",
            {},
            {},
            impl_->stats.steps);
    } catch (...) {
        throw EvaluationError(
            LanguageErrorCode::InternalInvariant,
            "unknown module export failure",
            {},
            {},
            impl_->stats.steps);
    }
}

Heap& SynchronousEvaluator::heap() noexcept { return impl_->heap; }
const Heap& SynchronousEvaluator::heap() const noexcept { return impl_->heap; }
EvaluationStats SynchronousEvaluator::stats() const noexcept { return impl_->stats; }

}  // namespace baas::script::runtime
