#include "script/runtime/SynchronousEvaluator.h"

#include "script/Ast.h"
#include "script/Parser.h"
#include "script/runtime/Environment.h"
#include "script/runtime/ErrorEnvelope.h"
#include "script/runtime/ErrorTranslation.h"
#include "script/runtime/JsonBridge.h"
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
#include <thread>
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

[[nodiscard]] std::string_view value_kind_label(const ValueKind kind) noexcept
{
    switch (kind) {
        case ValueKind::Null: return "null";
        case ValueKind::Boolean: return "boolean";
        case ValueKind::Integer: return "integer";
        case ValueKind::Float: return "float";
        case ValueKind::String: return "string";
        case ValueKind::List: return "list";
        case ValueKind::OrderedMap: return "ordered_map";
        case ValueKind::Function: return "function";
        case ValueKind::Module: return "module";
        case ValueKind::Error: return "error";
        case ValueKind::Task: return "task";
        case ValueKind::HostHandle: return "host_handle";
        case ValueKind::Bytes: return "bytes";
        case ValueKind::HeapReference: return "heap_reference";
    }
    return "unknown";
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
    const std::size_t steps,
    std::string structured_error)
    : std::runtime_error(std::move(message)), code_(code), module_(std::move(module)),
      span_(span), steps_(steps), structured_error_(std::move(structured_error))
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

    struct NativeFunctionRecord {
        std::uint64_t id{};
        std::string module;
        std::string export_name;
        HostApiVersion selected_version{};
        const SynchronousNativeBinding* binding{};
    };

    struct CachedHostFailure {
        LanguageErrorCode code{LanguageErrorCode::HostInternal};
        std::string message;
    };

    struct HostMemberRecord {
        Value callable;
        std::optional<Heap::RootId> callable_root;
        std::optional<CachedHostFailure> failure;
    };

    struct HostModuleRecord {
        std::string id;
        HostApiVersion selected_version{};
        Value namespace_value;
        std::optional<Heap::RootId> namespace_root;
        std::map<std::string, HostMemberRecord, std::less<>> members;
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
        std::optional<Value> failure;
        std::optional<Heap::RootId> failure_root;
        std::optional<EvaluationError> boundary_failure;
    };

    struct ScriptUnwind {
        Value error;
        Heap* heap{};
        Heap::RootId root{};

        ScriptUnwind(Heap& owner, const Value value)
            : error(value), heap(&owner), root(owner.add_root(value)) {}
        ~ScriptUnwind() { if (heap) heap->remove_root(root); }
        ScriptUnwind(const ScriptUnwind& other)
            : error(other.error), heap(other.heap), root(other.heap->add_root(other.error)) {}
        ScriptUnwind& operator=(const ScriptUnwind&) = delete;
        ScriptUnwind(ScriptUnwind&& other) noexcept
            : error(other.error), heap(std::exchange(other.heap, nullptr)), root(other.root) {}
        ScriptUnwind& operator=(ScriptUnwind&&) = delete;

        void replace(const Value value)
        {
            if (!heap->update_root(root, value)) std::terminate();
            error = value;
        }
    };

    struct ActiveFrame {
        std::string module;
        std::string function;
        ErrorFramePhase phase{ErrorFramePhase::Body};
        std::optional<SourceReference> call_source;
        std::optional<SourceReference> definition_source;
        std::optional<SourceReference> defer_source;
    };

    struct DeferredStatement {
        StmtPtr statement;
        std::shared_ptr<LexicalEnvironment> environment;
        ActiveFrame cleanup_frame;
        Heap::RootId failure_root{};
    };

    struct Activation {
        std::string module;
        std::string function;
        Heap::RootId pending_root{};
        std::vector<DeferredStatement> defers;
        // Capacity is admitted before each defer is registered, so cleanup
        // failure accumulation cannot allocate while the activation drains.
        std::vector<Value> cleanup_errors;
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
        bool cleanup{};
        explicit CallGuard(Impl& evaluator, const SourceSpan span)
            : evaluator(&evaluator), cleanup(evaluator.cleanup_depth != 0)
        {
            if (cleanup) {
                if (evaluator.cleanup_call_depth >= evaluator.limits.max_cleanup_call_depth) {
                    evaluator.fail(
                        LanguageErrorCode::CleanupLimitExceeded,
                        "synchronous evaluator cleanup call depth limit exceeded",
                        span);
                }
                ++evaluator.cleanup_call_depth;
                evaluator.stats.peak_cleanup_call_depth = std::max(
                    evaluator.stats.peak_cleanup_call_depth,
                    evaluator.cleanup_call_depth);
                return;
            }
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
        ~CallGuard()
        {
            if (cleanup) --evaluator->cleanup_call_depth;
            else --evaluator->call_depth;
        }
    };

    struct FrameGuard {
        Impl* evaluator;
        bool active{true};
        FrameGuard(Impl& evaluator, ActiveFrame frame) : evaluator(&evaluator)
        {
            evaluator.active_frames.push_back(std::move(frame));
        }
        void leave()
        {
            if (!active) return;
            evaluator->active_frames.pop_back();
            active = false;
        }
        ~FrameGuard() { leave(); }
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
    std::vector<NativeFunctionRecord> native_functions;
    std::map<std::string, HostModuleRecord, std::less<>> host_modules;
    std::optional<SynchronousHostOptions> host_options;
    std::shared_ptr<const HostCancellationProbe> cancellation;
    std::map<std::string, std::size_t, std::less<>> host_budget_limits;
    std::map<std::string, std::size_t, std::less<>> host_budget_used;
    EvaluationStats stats;
    std::string current_module;
    NfcPredicate nfc{};
    std::size_t value_stack_depth{};
    std::size_t call_depth{};
    std::size_t import_depth{};
    std::size_t host_registry_validation_work{};
    std::vector<ActiveFrame> active_frames;
    std::vector<Activation> activations;
    std::thread::id owner_thread{std::this_thread::get_id()};
    bool host_call_active{};
    bool execution_active{};
    bool closed{};
    std::size_t cleanup_depth{};
    std::size_t cleanup_call_depth{};

    explicit Impl(
        std::vector<SourceModule> sources,
        EvaluatorLimits evaluator_limits,
        HeapLimits heap_limits,
        const SemanticOptions semantic_options,
        const NfcPredicate is_nfc,
        std::optional<SynchronousHostOptions> synchronous_host_options,
        std::shared_ptr<const HostCancellationProbe> execution_cancellation)
        : limits(evaluator_limits), heap(heap_limits, is_nfc),
          host_options(std::move(synchronous_host_options)),
          cancellation(host_options && host_options->cancellation
              ? host_options->cancellation : std::move(execution_cancellation)),
          nfc(is_nfc)
    {
        validate_limits();
        compile_modules(std::move(sources), semantic_options, is_nfc);
        configure_host();
    }

    ~Impl()
    {
        if (close()) return;
        if (host_options && host_options->handles) {
            if (!host_options->handles->detach_context_for_destruction(heap))
                std::terminate();
            return;
        }
        try { (void)heap.teardown(); } catch (...) { std::terminate(); }
    }

    [[nodiscard]] bool close() noexcept
    {
        if (std::this_thread::get_id() != owner_thread || execution_active ||
            host_call_active)
            return false;
        closed = true;
        if (host_options && host_options->handles)
            return host_options->handles->teardown(heap);
        try { (void)heap.teardown(); } catch (...) { return false; }
        return true;
    }

    void drain_host_releases() noexcept
    {
        if (host_options && host_options->handles)
            host_options->handles->dispatch_all(heap);
    }

    [[nodiscard]] SourceReference source_reference(
        const std::string& module, const SourceSpan span) const
    {
        return {"synchronous-conformance", module, span};
    }

    [[nodiscard]] Value make_error(
        const LanguageErrorCode code,
        std::string message,
        const SourceSpan span,
        const ErrorOrigin origin = ErrorOrigin::Runtime,
        const std::string_view host_module = {},
        const std::string_view host_function = {},
        std::vector<std::pair<std::string, Value>> details = {})
    {
        ErrorMetadata metadata;
        metadata.code = code;
        metadata.message = std::move(message);
        metadata.origin = origin;
        metadata.details = std::move(details);
        if (!current_module.empty())
            metadata.source = source_reference(current_module, span);
        metadata.stack.reserve(active_frames.size() + (origin == ErrorOrigin::Host ? 1 : 0));
        if (origin == ErrorOrigin::Host) {
            metadata.stack.push_back({
                ErrorFrameKind::Host,
                std::string(host_module),
                std::string(host_function),
                ErrorFramePhase::Host,
                current_module.empty()
                    ? std::nullopt
                    : std::optional<SourceReference>{source_reference(current_module, span)},
                std::nullopt,
                std::nullopt});
        }
        for (auto frame = active_frames.rbegin(); frame != active_frames.rend(); ++frame) {
            metadata.stack.push_back({
                ErrorFrameKind::Script,
                frame->module,
                frame->function,
                frame->phase,
                frame->call_source,
                frame->definition_source,
                frame->defer_source});
        }
        return heap.allocate_error(std::move(metadata));
    }

    [[nodiscard]] Value make_thrown_value_error(
        const Value thrown, const SourceSpan span)
    {
        auto roots = heap.root_scope();
        roots.add(thrown);
        const auto kind = heap.allocate_string(
            std::string(value_kind_label(heap.kind(thrown))));
        roots.add(kind);
        ErrorMetadata metadata;
        metadata.code = LanguageErrorCode::ThrownValue;
        metadata.message = "script threw a non-Error value";
        metadata.origin = ErrorOrigin::Script;
        metadata.source = source_reference(current_module, span);
        metadata.details.emplace_back("thrown_kind", kind);
        metadata.stack.reserve(active_frames.size());
        for (auto frame = active_frames.rbegin(); frame != active_frames.rend(); ++frame) {
            metadata.stack.push_back({
                ErrorFrameKind::Script,
                frame->module,
                frame->function,
                frame->phase,
                frame->call_source,
                frame->definition_source,
                frame->defer_source});
        }
        return heap.allocate_error(std::move(metadata));
    }

    [[noreturn]] void raise(Value error)
    {
        throw ScriptUnwind(heap, error);
    }

    [[noreturn]] void fail(
        const LanguageErrorCode code,
        std::string message,
        const SourceSpan span = {})
    {
        if (execution_active) {
            try {
                raise(make_error(code, std::move(message), span));
            } catch (const ScriptUnwind&) {
                throw;
            } catch (const RuntimeError&) {
                throw EvaluationError(
                    LanguageErrorCode::MemoryLimitExceeded,
                    "structured Error publication exhausted evaluator memory",
                    current_module,
                    span,
                    stats.steps);
            } catch (const std::bad_alloc&) {
                throw EvaluationError(
                    LanguageErrorCode::MemoryLimitExceeded,
                    "structured Error publication allocation failed",
                    current_module,
                    span,
                    stats.steps);
            }
        }
        throw EvaluationError(code, std::move(message), current_module, span, stats.steps);
    }

    [[noreturn]] void fail_host(
        const LanguageErrorCode code,
        std::string message,
        const SourceSpan span,
        const std::string_view host_module,
        const std::string_view host_function)
    {
        if (execution_active) {
            try {
                raise(make_error(
                    code,
                    std::move(message),
                    span,
                    ErrorOrigin::Host,
                    host_module,
                    host_function));
            } catch (const ScriptUnwind&) {
                throw;
            } catch (const RuntimeError&) {
                throw EvaluationError(
                    LanguageErrorCode::MemoryLimitExceeded,
                    "Host Error publication exhausted evaluator memory",
                    current_module,
                    span,
                    stats.steps);
            } catch (const std::bad_alloc&) {
                throw EvaluationError(
                    LanguageErrorCode::MemoryLimitExceeded,
                    "Host Error publication allocation failed",
                    current_module,
                    span,
                    stats.steps);
            }
        }
        throw EvaluationError(code, std::move(message), current_module, span, stats.steps);
    }

    [[noreturn]] void fail_host_result(
        const LanguageErrorCode code,
        std::string message,
        const SourceSpan span,
        const std::string_view host_module,
        const std::string_view host_function,
        const HostError& host_error,
        const bool declared_status)
    {
        if (!execution_active)
            throw EvaluationError(code, std::move(message), current_module, span, stats.steps);
        try {
            auto roots = heap.root_scope();
            std::vector<std::pair<std::string, Value>> details;
            details.reserve(4);
            const auto add_string = [&](std::string key, const std::string_view value) {
                const auto converted = heap.allocate_string(std::string(value));
                roots.add(converted);
                details.emplace_back(std::move(key), converted);
            };
            add_string("host_code", host_error_code_name(host_error.code));
            details.emplace_back("retryable", Value(host_error.retryable));
            add_string(
                "effect_state",
                host_error.effect_state == HostEffectState::NotStarted ? "not_started"
                    : host_error.effect_state == HostEffectState::Committed
                        ? "committed" : "unknown");
            if (declared_status && host_error.details
                && host_error.details->kind() == JsonKind::Object
                && (host_error.code == HostErrorCode::DeadlineExceeded
                    || host_error.code == HostErrorCode::BudgetExceeded)) {
                const auto& entries = std::get<JsonObject>(host_error.details->value());
                const auto bridge_limits = effective_host_json_limits(
                    host_options->bindings->limits());
                JsonObject allowlisted;
                for (const auto& [name, value] : entries) {
                    const auto expected_name = host_error.code == HostErrorCode::DeadlineExceeded
                        ? std::string_view("deadline_scope")
                        : std::string_view("budget_scope");
                    if (name != expected_name || value.kind() != JsonKind::String) continue;
                    const auto& discriminator = std::get<std::string>(value.value());
                    const auto allowed = host_error.code == HostErrorCode::DeadlineExceeded
                        ? discriminator == "context" || discriminator == "call"
                        : discriminator == "external_memory"
                            || discriminator == "host_operation";
                    if (allowed) allowlisted.emplace_back(name, value);
                }
                if (!allowlisted.empty()) {
                    const auto converted = json_to_heap_value(
                        heap, JsonValue(std::move(allowlisted)), bridge_limits);
                    roots.add(converted);
                    details.emplace_back("host_details", converted);
                }
            }
            raise(make_error(
                code,
                std::move(message),
                span,
                ErrorOrigin::Host,
                host_module,
                host_function,
                std::move(details)));
        } catch (const ScriptUnwind&) {
            throw;
        } catch (const RuntimeError&) {
            throw EvaluationError(
                LanguageErrorCode::MemoryLimitExceeded,
                "Host Error detail publication exhausted evaluator memory",
                current_module,
                span,
                stats.steps);
        } catch (const std::bad_alloc&) {
            throw EvaluationError(
                LanguageErrorCode::MemoryLimitExceeded,
                "Host Error detail publication allocation failed",
                current_module,
                span,
                stats.steps);
        }
    }

    void validate_limits()
    {
        if (limits.max_module_source_bytes == 0 || limits.max_total_source_bytes == 0
            || limits.max_steps == 0 || limits.max_call_depth == 0
            || limits.max_value_stack == 0 || limits.max_container_elements == 0
            || limits.max_collection_work == 0 || limits.max_functions == 0
            || limits.max_import_depth == 0 || limits.max_modules == 0
            || limits.max_defers_per_frame == 0 || limits.max_cleanup_steps == 0
            || limits.max_cleanup_call_depth == 0) {
            fail(LanguageErrorCode::ArgumentInvalid, "evaluator limits must be positive");
        }
    }

    [[nodiscard]] static CachedHostFailure translate_registry_failure(
        const HostRegistryError& error)
    {
        using enum HostRegistryErrorCode;
        switch (error.code()) {
            case UndeclaredModule:
            case UndeclaredCapability:
            case CapabilityDenied:
                return {LanguageErrorCode::CapabilityDenied,
                        "Host import or export is not permitted"};
            case ModuleUnavailable:
            case VersionIncompatible:
                return {LanguageErrorCode::HostUnavailable,
                        "Host module version is unavailable"};
            case UnknownExport:
                return {LanguageErrorCode::ModuleMemberMissing,
                        "Host module export is absent"};
            case ModuleVersionLimitExceeded:
            case ExportLimitExceeded:
            case CapabilityLimitExceeded:
            case ImportLimitExceeded:
            case StringBudgetExceeded:
            case ValidationWorkLimitExceeded:
                return {LanguageErrorCode::MemoryLimitExceeded,
                        "Host registry validation limit exceeded"};
            default:
                return {LanguageErrorCode::HostValidationFailed,
                        "Host registry input is invalid"};
        }
    }

    void charge_host_registry_work(const std::size_t work)
    {
        const auto maximum = host_options->limits.max_registry_validation_work;
        if (work > maximum || host_registry_validation_work > maximum - work)
            fail(LanguageErrorCode::MemoryLimitExceeded,
                 "aggregate Host registry validation work exceeded");
        host_registry_validation_work += work;
    }

    [[nodiscard]] HostResolutionRequest host_resolution_request(
        const std::string_view target_module = {},
        const std::string_view target_export = {}) const
    {
        HostResolutionRequest request;
        request.declared_modules = host_options->permissions.declared_modules;
        request.declared_capabilities = host_options->permissions.declared_capabilities;
        request.policy_capabilities = host_options->permissions.policy_capabilities;
        request.platform_capabilities = host_options->permissions.platform_capabilities;
        request.task_capabilities = host_options->permissions.task_capabilities;
        request.imports.reserve(host_modules.size());
        for (const auto& [id, ignored] : host_modules) {
            HostImportRequest imported{id, {}};
            if (id == target_module && !target_export.empty())
                imported.exports.emplace_back(target_export);
            request.imports.push_back(std::move(imported));
        }
        return request;
    }

    void configure_host()
    {
        if (!host_options) return;
        const auto& options = *host_options;
        if (!options.metadata || !options.bindings)
            fail(LanguageErrorCode::ArgumentInvalid,
                 "synchronous Host metadata and binding set are required");
        const auto& host_limits = options.limits;
        if (host_limits.max_host_modules == 0 ||
            host_limits.max_permission_entries == 0 ||
            host_limits.max_permission_string_bytes == 0 ||
            host_limits.max_authorized_exports == 0 ||
            host_limits.max_host_calls == 0 ||
            host_limits.max_host_arguments == 0 ||
            host_limits.max_conversion_nodes == 0 ||
            host_limits.max_conversion_bytes == 0 ||
            host_limits.max_conversion_work == 0 ||
            host_limits.max_registry_validation_work == 0)
            fail(LanguageErrorCode::ArgumentInvalid,
                 "synchronous evaluator Host limits must be positive");

        std::size_t permission_entries = options.permissions.declared_modules.size();
        std::size_t permission_strings{};
        const auto charge_permission_string = [&](const std::string_view value) {
            if (value.size() > host_limits.max_permission_string_bytes ||
                permission_strings > host_limits.max_permission_string_bytes - value.size())
                fail(LanguageErrorCode::MemoryLimitExceeded,
                     "Host permission string budget exceeded");
            permission_strings += value.size();
        };
        for (const auto& requirement : options.permissions.declared_modules)
            charge_permission_string(requirement.canonical_id);
        const auto charge_permission_set = [&](const std::vector<std::string>& values) {
            if (values.size() > host_limits.max_permission_entries -
                    std::min(permission_entries, host_limits.max_permission_entries))
                fail(LanguageErrorCode::MemoryLimitExceeded,
                     "Host permission entry budget exceeded");
            permission_entries += values.size();
            for (const auto& value : values) charge_permission_string(value);
        };
        charge_permission_set(options.permissions.declared_capabilities);
        charge_permission_set(options.permissions.policy_capabilities);
        charge_permission_set(options.permissions.platform_capabilities);
        charge_permission_set(options.permissions.task_capabilities);
        if (permission_entries > host_limits.max_permission_entries)
            fail(LanguageErrorCode::MemoryLimitExceeded,
                 "Host permission entry budget exceeded");
        if (options.budget_limits.size() > host_limits.max_permission_entries -
                permission_entries)
            fail(LanguageErrorCode::MemoryLimitExceeded,
                 "Host budget entry limit exceeded");

        for (const auto& [scope, maximum] : options.budget_limits) {
            charge_permission_string(scope);
            if (scope.empty() || !host_budget_limits.emplace(scope, maximum).second)
                fail(LanguageErrorCode::ArgumentInvalid,
                     "Host budget scopes must be non-empty and unique");
        }

        std::set<std::string, std::less<>> imported_host_modules;
        for (const auto& [id, module] : modules) {
            for (const auto& imported : module->imports) {
                const auto specifier = validate_module_specifier(imported, nfc);
                if (specifier.kind == ModuleKind::Host)
                    imported_host_modules.insert(specifier.canonical_id);
            }
        }
        if (imported_host_modules.size() > host_limits.max_host_modules)
            fail(LanguageErrorCode::MemoryLimitExceeded,
                 "Host import module limit exceeded");

        for (const auto& id : imported_host_modules) {
            HostModuleRecord module;
            module.id = id;
            host_modules.emplace(id, std::move(module));
        }
        if (host_modules.empty()) {
            if (options.handles) options.handles->attach_context(heap);
            return;
        }

        HostResolution resolution;
        try {
            resolution = options.metadata->resolve(host_resolution_request());
        } catch (const HostRegistryError& error) {
            const auto translated = translate_registry_failure(error);
            fail(translated.code, translated.message);
        }
        charge_host_registry_work(resolution.validation_work);
        for (auto& [id, module] : host_modules) {
            const auto selected = std::find_if(
                resolution.modules.begin(), resolution.modules.end(),
                [&](const ResolvedHostModule& candidate) {
                    return candidate.canonical_id == id;
                });
            if (selected == resolution.modules.end())
                fail(LanguageErrorCode::HostUnavailable,
                     "Host module did not resolve to an exact version");
            module.selected_version = selected->selected_version;
            module.namespace_value = heap.allocate_module({id, {}});
            module.namespace_root = heap.add_root(module.namespace_value);
        }
        if (options.handles)
            options.handles->attach_context(heap);
    }

    void charge_step(const SourceSpan span)
    {
        if (cleanup_depth != 0) {
            if (stats.cleanup_steps >= limits.max_cleanup_steps) {
                fail(
                    LanguageErrorCode::CleanupLimitExceeded,
                    "synchronous evaluator cleanup step limit exceeded",
                    span);
            }
            ++stats.cleanup_steps;
            return;
        }
        check_execution_interrupt(span);
        if (stats.steps >= limits.max_steps) {
            fail(
                LanguageErrorCode::InstructionLimitExceeded,
                "synchronous evaluator step limit exceeded",
                span);
        }
        ++stats.steps;
    }

    void check_execution_interrupt(const SourceSpan span)
    {
        // Cancellation and deadlines are masked while ERR-013 cleanup drains.
        // The original terminal remains primary unless cleanup itself reaches
        // an independent terminal limit or failure.
        if (!cancellation || cleanup_depth != 0) return;
        if (cancellation->deadline_exceeded()) {
            fail(
                LanguageErrorCode::DeadlineExceeded,
                "synchronous evaluator deadline exceeded",
                span);
        }
        if (cancellation->cancelled()) {
            fail(
                LanguageErrorCode::Cancelled,
                "synchronous evaluator cancellation requested",
                span);
        }
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
            for (const auto* imported : semantic.imports)
                imports.push_back(imported->module);
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
        const SourceSpan span)
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

    [[nodiscard]] std::optional<Value> drain_activation(
        std::optional<Value> primary,
        const bool boundary_terminal = false)
    {
        if (activations.empty())
            fail(LanguageErrorCode::InternalInvariant, "cleanup activation is absent");
        auto activation = std::move(activations.back());
        activations.pop_back();
        struct ActivationRootGuard {
            Heap& heap;
            Activation& activation;
            ~ActivationRootGuard()
            {
                heap.remove_root(activation.pending_root);
                for (const auto& deferred : activation.defers)
                    heap.remove_root(deferred.failure_root);
            }
        } activation_roots{heap, activation};
        struct CleanupGuard {
            Impl& evaluator;
            explicit CleanupGuard(Impl& evaluator) : evaluator(evaluator)
            {
                ++evaluator.cleanup_depth;
            }
            ~CleanupGuard() { --evaluator.cleanup_depth; }
        } cleanup(*this);
        bool error_publication_failed = false;

        for (auto deferred = activation.defers.rbegin();
            deferred != activation.defers.rend(); ++deferred) {
            try {
                const auto registration_span = deferred->cleanup_frame.defer_source->span;
                // Registration prebuilds this frame. The function body frame
                // has already left active_frames, preserving enough vector
                // capacity for this move-only, allocation-free replacement.
                FrameGuard frame_guard(*this, std::move(deferred->cleanup_frame));
                ++stats.executed_defers;
                const auto flow = execute_statement(deferred->statement, deferred->environment);
                if (flow.kind != FlowKind::Normal)
                    fail(LanguageErrorCode::InternalInvariant,
                         "non-local control escaped cleanup", registration_span);
            } catch (const ScriptUnwind& failure) {
                if (!heap.update_root(deferred->failure_root, failure.error))
                    std::terminate();
                const auto failure_catchable =
                    heap.error_metadata_view(failure.error.as_heap_ref()).catchable();
                if (!primary) {
                    primary = failure.error;
                } else if (!boundary_terminal
                           && heap.error_metadata_view(primary->as_heap_ref()).catchable()
                           && !failure_catchable) {
                    // ERR-014: the first terminal cleanup failure takes
                    // precedence. Preserve the displaced primary before all
                    // cleanup failures already observed, in deterministic
                    // execution order.
                    activation.cleanup_errors.insert(
                        activation.cleanup_errors.begin(), *primary);
                    primary = failure.error;
                } else {
                    activation.cleanup_errors.push_back(failure.error);
                }
            } catch (const EvaluationError&) {
                // Publishing the cleanup failure itself may exhaust the heap. Keep
                // draining the activation, then fail closed with a terminal error.
                error_publication_failed = true;
            } catch (const std::bad_alloc&) {
                error_publication_failed = true;
            }
        }
        if (error_publication_failed && boundary_terminal)
            return std::nullopt;
        if (error_publication_failed)
            fail(LanguageErrorCode::MemoryLimitExceeded,
                 "structured cleanup Error publication failed");
        if (boundary_terminal) return std::nullopt;
        if (primary && !activation.cleanup_errors.empty()) {
            *primary = heap.derive_error(
                primary->as_heap_ref(),
                ErrorDerivation{std::nullopt, activation.cleanup_errors});
        }
        return primary;
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

    void commit_boundary_failure(ModuleRecord& module, EvaluationError failure)
    {
        // Construct the complete payload while the transaction still owns the
        // Loading state. If this move/emplace fails, InitializationTransaction
        // rolls the module back to Uninitialized. Failed is the final publish.
        module.boundary_failure.emplace(std::move(failure));
        module.environment.reset();
        module.state = ModuleState::Failed;
    }

    [[nodiscard]] Value initialize_module(const std::string& id, const SourceSpan import_span)
    {
        check_execution_interrupt(import_span);
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
        if (module.state == ModuleState::Failed && module.failure)
            throw ScriptUnwind(heap, *module.failure);
        if (module.state == ModuleState::Failed && module.boundary_failure)
            throw *module.boundary_failure;
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
        FrameGuard frame_guard(*this, {
            id, "<module>", ErrorFramePhase::ModuleInit,
            std::nullopt, std::nullopt, std::nullopt});
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
        } catch (const ScriptUnwind& unwind) {
            Heap::RootId failure_root{};
            try {
                failure_root = heap.add_root(unwind.error);
            } catch (const RuntimeError&) {
                EvaluationError boundary(
                    LanguageErrorCode::MemoryLimitExceeded,
                    "module Error cache root allocation failed",
                    id,
                    module.parsed->program.span,
                    stats.steps);
                commit_boundary_failure(module, std::move(boundary));
                throw *module.boundary_failure;
            } catch (const std::bad_alloc&) {
                EvaluationError boundary(
                    LanguageErrorCode::MemoryLimitExceeded,
                    "module Error cache allocation failed",
                    id,
                    module.parsed->program.span,
                    stats.steps);
                commit_boundary_failure(module, std::move(boundary));
                throw *module.boundary_failure;
            }
            // Root acquisition is the commit point for structured failures.
            // Value/RootId optionals cannot allocate; publish Failed last.
            module.failure.emplace(unwind.error);
            module.failure_root.emplace(failure_root);
            module.environment.reset();
            module.state = ModuleState::Failed;
            throw;
        } catch (const EvaluationError& error) {
            commit_boundary_failure(module, EvaluationError(error));
            throw;
        } catch (const RuntimeError& error) {
            EvaluationError translated(
                translate_runtime_error_code(error.code()).code,
                error.what(),
                current_module,
                module.parsed->program.span,
                stats.steps);
            commit_boundary_failure(module, std::move(translated));
            throw *module.boundary_failure;
        } catch (const std::bad_alloc&) {
            EvaluationError translated(
                LanguageErrorCode::MemoryLimitExceeded,
                "evaluator allocation failed",
                current_module,
                module.parsed->program.span,
                stats.steps);
            commit_boundary_failure(module, std::move(translated));
            throw *module.boundary_failure;
        } catch (const std::length_error&) {
            EvaluationError translated(
                LanguageErrorCode::MemoryLimitExceeded,
                "evaluator container allocation exceeded its bound",
                current_module,
                module.parsed->program.span,
                stats.steps);
            commit_boundary_failure(module, std::move(translated));
            throw *module.boundary_failure;
        } catch (const std::exception&) {
            EvaluationError translated(
                LanguageErrorCode::InternalInvariant,
                "unexpected evaluator module failure",
                current_module,
                module.parsed->program.span,
                stats.steps);
            commit_boundary_failure(module, std::move(translated));
            throw *module.boundary_failure;
        } catch (...) {
            EvaluationError translated(
                LanguageErrorCode::InternalInvariant,
                "unknown evaluator module failure",
                current_module,
                module.parsed->program.span,
                stats.steps);
            commit_boundary_failure(module, std::move(translated));
            throw *module.boundary_failure;
        }
    }

    [[nodiscard]] Value initialize_host_module(
        const std::string& id, const SourceSpan import_span)
    {
        if (!host_options)
            fail(LanguageErrorCode::HostUnavailable,
                 "Host imports are outside the synchronous evaluator boundary",
                 import_span);
        const auto found = host_modules.find(id);
        if (found == host_modules.end())
            fail(LanguageErrorCode::HostUnavailable,
                 "Host import was not part of the validated source snapshot",
                 import_span);
        return found->second.namespace_value;
    }

    [[nodiscard]] Value authorize_host_member(
        HostModuleRecord& module, const std::string_view member,
        const SourceSpan span)
    {
        if (!is_public_name(member))
            fail(LanguageErrorCode::ModuleMemberMissing,
                 "Host module export is private or absent", span);
        const auto cached = module.members.find(member);
        if (cached != module.members.end()) {
            if (cached->second.failure)
                fail(cached->second.failure->code,
                     cached->second.failure->message, span);
            return cached->second.callable;
        }
        if (stats.host_authorization_attempts >=
            host_options->limits.max_authorized_exports)
            fail(LanguageErrorCode::MemoryLimitExceeded,
                 "Host export authorization limit exceeded", span);
        ++stats.host_authorization_attempts;

        auto [entry, inserted] = module.members.try_emplace(std::string(member));
        (void)inserted;
        using HostMemberMap = std::map<std::string, HostMemberRecord, std::less<>>;
        struct CacheTransaction {
            HostMemberMap* members;
            HostMemberMap::iterator entry;
            bool committed{};
            ~CacheTransaction() { if (!committed) members->erase(entry); }
        } transaction{&module.members, entry};
        auto cache_failure = [&](CachedHostFailure failure) -> Value {
            entry->second.failure = std::move(failure);
            transaction.committed = true;
            fail(entry->second.failure->code, entry->second.failure->message, span);
        };

        HostResolution resolution;
        try {
            resolution = host_options->metadata->resolve(
                host_resolution_request(module.id, member));
        } catch (const HostRegistryError& error) {
            return cache_failure(translate_registry_failure(error));
        }
        charge_host_registry_work(resolution.validation_work);
        const auto resolved_module = std::find_if(
            resolution.modules.begin(), resolution.modules.end(),
            [&](const ResolvedHostModule& candidate) {
                return candidate.canonical_id == module.id;
            });
        if (resolved_module == resolution.modules.end() ||
            resolved_module->selected_version != module.selected_version)
            return cache_failure({LanguageErrorCode::HostInternal,
                                  "Host module resolution changed unexpectedly"});
        const auto resolved_binding = std::find_if(
            resolved_module->bindings.begin(), resolved_module->bindings.end(),
            [&](const ResolvedHostBinding& candidate) {
                return candidate.export_name == member;
            });
        if (resolved_binding == resolved_module->bindings.end())
            return cache_failure({LanguageErrorCode::HostInternal,
                                  "Host export resolution was incomplete"});
        const auto* binding = host_options->bindings->find(resolved_binding->binding_id);
        if (!binding)
            return cache_failure({LanguageErrorCode::HostUnavailable,
                                  "Host export adapter is unavailable"});

        const auto id = static_cast<std::uint64_t>(native_functions.size() + 1);
        native_functions.push_back(
            {id, module.id, std::string(member), module.selected_version, binding});
        try {
            const auto callable = heap.allocate_function(
                {CallableKind::Native, id, {}});
            const auto root = heap.add_root(callable);
            entry->second.callable = callable;
            entry->second.callable_root = root;
            transaction.committed = true;
            ++stats.authorized_host_exports;
            return callable;
        } catch (...) {
            native_functions.pop_back();
            throw;
        }
    }

    [[nodiscard]] std::string structured_error_envelope(const Value error) const
    {
        ErrorEnvelopeLimits envelope_limits;
        envelope_limits.max_output_bytes = 64U * 1024U;
        std::string output(envelope_limits.max_output_bytes, '\0');
        const auto serialized = serialize_error_envelope(
            heap,
            error,
            std::span<char>(output.data(), output.size()),
            envelope_limits);
        if (serialized.status == ErrorEnvelopeStatus::InsufficientCapacity)
            return {};
        output.resize(serialized.bytes_written);
        return output;
    }

    [[nodiscard]] EvaluationResult execute(const std::string_view entry)
    {
        const auto boundary_fail = [&](const LanguageErrorCode code,
                                       std::string message) -> void {
            throw EvaluationError(code, std::move(message), current_module, {}, stats.steps);
        };
        if (std::this_thread::get_id() != owner_thread)
            boundary_fail(LanguageErrorCode::HostUnavailable,
                          "synchronous evaluator called from a non-owning thread");
        if (host_call_active)
            boundary_fail(LanguageErrorCode::HostUnavailable,
                          "Host callback re-entry into the evaluator is forbidden");
        ModuleSpecifier specifier;
        try {
            specifier = validate_module_specifier(entry, nfc);
        } catch (const ModuleSpecifierError&) {
            boundary_fail(LanguageErrorCode::ImportSpecifierInvalid,
                          "entry module id is not canonical");
        }
        if (specifier.kind != ModuleKind::Package) {
            boundary_fail(LanguageErrorCode::HostUnavailable,
                          "Host modules cannot be evaluator entries");
        }
        struct ExecutionGuard {
            Impl& evaluator;
            explicit ExecutionGuard(Impl& evaluator) : evaluator(evaluator)
            {
                evaluator.execution_active = true;
            }
            ~ExecutionGuard() { evaluator.execution_active = false; }
        } execution(*this);
        try {
            check_execution_interrupt({});
            const auto value = initialize_module(specifier.canonical_id, {});
            return {value, stats};
        } catch (const ScriptUnwind& unwind) {
            const auto& metadata = heap.error_metadata_view(unwind.error.as_heap_ref());
            const auto code = metadata.code;
            const auto message = metadata.message;
            const auto module = metadata.source ? metadata.source->module : current_module;
            const auto span = metadata.source ? metadata.source->span : SourceSpan{};
            const auto envelope = structured_error_envelope(unwind.error);
            throw EvaluationError(
                code, message, module, span, stats.steps, envelope);
        }
    }

    [[nodiscard]] Value module_export(
        const std::string_view module_id, const std::string_view export_name)
    {
        if (std::this_thread::get_id() != owner_thread)
            fail(LanguageErrorCode::HostUnavailable,
                 "synchronous evaluator called from a non-owning thread");
        if (host_call_active)
            fail(LanguageErrorCode::HostUnavailable,
                 "Host callback re-entry into the evaluator is forbidden");
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
    [[nodiscard]] Value invoke_native(
        const NativeFunctionRecord& function,
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
    if (kind == ValueKind::Error) {
        const auto& metadata = heap.error_metadata_view(object.as_heap_ref());
        auto roots = heap.root_scope();
        roots.add(object);
        const auto string_value = [&](const std::string_view value) {
            const auto result = heap.allocate_string(std::string(value));
            roots.add(result);
            return result;
        };
        const auto integer_value = [&](const std::size_t value) {
            if (value > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
                fail(LanguageErrorCode::InternalInvariant,
                     "Error metadata integer is out of range", span);
            return Value(static_cast<std::int64_t>(value));
        };
        const auto location_value = [&](const SourceLocation& location) {
            const auto result = heap.allocate_map({
                {"byte_offset", integer_value(location.byte_offset)},
                {"line", integer_value(location.line)},
                {"column", integer_value(location.column)}});
            roots.add(result);
            return result;
        };
        const auto source_value = [&](const std::optional<SourceReference>& source) {
            if (!source) return Value::null();
            const auto begin = location_value(source->span.begin);
            const auto end = location_value(source->span.end);
            const auto span_value = heap.allocate_map({{"begin", begin}, {"end", end}});
            roots.add(span_value);
            const auto result = heap.allocate_map({
                {"snapshot_id", string_value(source->snapshot_id)},
                {"module", string_value(source->module)},
                {"span", span_value}});
            roots.add(result);
            return result;
        };
        if (member == "schema") return string_value("baas.script.error/v1");
        if (member == "code") return string_value(metadata.code_name());
        if (member == "message") return string_value(metadata.message);
        if (member == "origin") {
            return string_value(metadata.origin == ErrorOrigin::Script ? "script"
                : metadata.origin == ErrorOrigin::Host ? "host" : "runtime");
        }
        if (member == "catchable") return Value(metadata.catchable());
        if (member == "source") return source_value(metadata.source);
        if (member == "cause") return metadata.cause.value_or(Value::null());
        if (member == "suppressed") {
            charge_collection(metadata.suppressed.size(), span);
            return heap.allocate_list(metadata.suppressed);
        }
        if (member == "details") {
            charge_collection(metadata.details.size(), span);
            std::vector<std::pair<std::string, Value>> detached;
            detached.reserve(metadata.details.size());
            JsonBridgeLimits bridge_limits;
            bridge_limits.max_depth = 64;
            bridge_limits.max_nodes = limits.max_container_elements;
            bridge_limits.max_total_bytes = heap.limits().max_error_detail_bytes;
            bridge_limits.max_string_bytes = std::min(
                heap.limits().max_string_bytes,
                heap.limits().max_error_detail_bytes);
            bridge_limits.max_work = limits.max_collection_work;
            for (const auto& [name, value] : metadata.details) {
                const auto json = heap_value_to_json(heap, value, bridge_limits);
                const auto copy = json_to_heap_value(heap, json, bridge_limits);
                roots.add(copy);
                detached.emplace_back(name, copy);
            }
            return heap.allocate_map(std::move(detached));
        }
        if (member == "stack") {
            charge_collection(metadata.stack.size(), span);
            std::vector<Value> frames;
            frames.reserve(metadata.stack.size());
            for (const auto& frame : metadata.stack) {
                const auto frame_value = heap.allocate_map({
                    {"kind", string_value(frame.kind == ErrorFrameKind::Script ? "script" : "host")},
                    {"module", string_value(frame.module)},
                    {"function", string_value(frame.function)},
                    {"phase", string_value(frame.phase == ErrorFramePhase::Body ? "body"
                        : frame.phase == ErrorFramePhase::ModuleInit ? "module_init"
                        : frame.phase == ErrorFramePhase::Cleanup ? "cleanup" : "host")},
                    {"call_source", source_value(frame.call_source)},
                    {"definition_source", source_value(frame.definition_source)},
                    {"defer_source", source_value(frame.defer_source)}});
                roots.add(frame_value);
                frames.push_back(frame_value);
            }
            return heap.allocate_list(std::move(frames));
        }
        if (member == "context") {
            const auto optional_string = [&](const std::optional<std::string>& value) {
                return value ? string_value(*value) : Value::null();
            };
            return heap.allocate_map({
                {"task_id", optional_string(metadata.context.task_id)},
                {"session_id", optional_string(metadata.context.session_id)},
                {"package_id", optional_string(metadata.context.package_id)},
                {"snapshot_id", optional_string(metadata.context.snapshot_id)},
                {"language_version", optional_string(metadata.context.language_version)},
                {"correlation_id", optional_string(metadata.context.correlation_id)}});
        }
        if (member == "truncated") {
            return heap.allocate_map({
                {"stack_frames", integer_value(metadata.truncated.stack_frames)},
                {"cause_errors", integer_value(metadata.truncated.cause_errors)},
                {"suppressed_errors", integer_value(metadata.truncated.suppressed_errors)},
                {"message_bytes", integer_value(metadata.truncated.message_bytes)},
                {"detail_bytes", integer_value(metadata.truncated.detail_bytes)},
                {"details_replaced", Value(metadata.truncated.details_replaced)},
                {"fallback", Value(metadata.truncated.fallback)}});
        }
        fail(LanguageErrorCode::NameNotFound, "Error member is absent", span);
    }
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
        const auto metadata = heap.module_metadata(object.as_heap_ref());
        const auto host = host_modules.find(metadata.name);
        if (host != host_modules.end() &&
            host->second.namespace_value == object)
            return authorize_host_member(host->second, member, span);
        charge_collection(metadata.exports.size(), span);
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
    } catch (const ScriptUnwind&) {
        throw;
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
            if (call.callee && call.callee->kind == NodeKind::MemberExpression) {
                const auto& member = static_cast<const MemberExpression&>(*call.callee);
                if (member.member == "close") {
                    auto roots = heap.root_scope();
                    const auto object = evaluate_expression(member.object, environment);
                    roots.add(object);
                    if (heap.kind(object) == ValueKind::HostHandle) {
                        if (!call.arguments.empty())
                            fail(LanguageErrorCode::CallArityMismatch,
                                 "host<T>.close accepts no arguments", call.span);
                        try {
                            (void)heap.close_host_handle(object.as_heap_ref());
                            drain_host_releases();
                            return Value::null();
                        } catch (const RuntimeError& error) {
                            const auto code = error.code() ==
                                    RuntimeErrorCode::ReleaseQueueLimitExceeded
                                ? LanguageErrorCode::TaskLimitExceeded
                                : translate_runtime_error_code(error.code()).code;
                            fail(code, "host<T>.close could not queue release", call.span);
                        }
                    }
                    const auto callee = read_member(object, member.member, member.span);
                    roots.add(callee);
                    return invoke(callee, call.arguments, environment, call.span);
                }
            }
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

Value SynchronousEvaluator::Impl::invoke_native(
    const NativeFunctionRecord& function,
    const std::vector<CallArgument>& arguments,
    const std::shared_ptr<LexicalEnvironment>& caller,
    const SourceSpan span)
{
    if (std::this_thread::get_id() != owner_thread)
        fail(LanguageErrorCode::HostUnavailable,
             "Host binding called from a non-owning thread", span);
    if (host_call_active)
        fail(LanguageErrorCode::HostUnavailable,
             "nested Host callback entry is forbidden", span);
    if (!function.binding)
        fail(LanguageErrorCode::HostUnavailable,
             "Host export adapter is unavailable", span);
    const auto& contract = function.binding->contract;
    const auto& parameters = contract.parameters;
    if (arguments.size() > host_options->limits.max_host_arguments)
        fail(LanguageErrorCode::HostValidationFailed,
             "Host call argument limit exceeded", span);
    if (arguments.size() > std::numeric_limits<std::size_t>::max() -
            parameters.size())
        fail(LanguageErrorCode::MemoryLimitExceeded,
             "Host call binding size overflow", span);
    charge_collection(arguments.size() + parameters.size(), span);

    std::vector<std::optional<std::size_t>> bound(parameters.size());
    std::vector<std::size_t> argument_parameters;
    argument_parameters.reserve(arguments.size());
    std::size_t next_positional = 0;
    for (std::size_t argument_index = 0; argument_index < arguments.size();
         ++argument_index) {
        const auto& argument = arguments[argument_index];
        std::size_t parameter_index{};
        if (argument.name) {
            const auto found = std::find_if(
                parameters.begin(), parameters.end(),
                [&](const HostParameterContract& parameter) {
                    return parameter.name == *argument.name;
                });
            if (found == parameters.end())
                fail(LanguageErrorCode::CallArgumentUnknown,
                     "named Host argument is unknown", span);
            parameter_index = static_cast<std::size_t>(found - parameters.begin());
        } else {
            while (next_positional < bound.size() && bound[next_positional])
                ++next_positional;
            if (next_positional >= bound.size())
                fail(LanguageErrorCode::CallArityMismatch,
                     "too many positional Host arguments", span);
            parameter_index = next_positional++;
        }
        if (bound[parameter_index])
            fail(LanguageErrorCode::CallArgumentDuplicate,
                 "Host parameter was supplied more than once", span);
        bound[parameter_index] = argument_index;
        argument_parameters.push_back(parameter_index);
    }
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (parameters[index].required && !bound[index])
            fail(LanguageErrorCode::CallArityMismatch,
                 "required Host argument is missing", span);
    }

    if (stats.host_calls >= host_options->limits.max_host_calls)
        fail(LanguageErrorCode::TaskLimitExceeded,
             "synchronous Host call limit exceeded", span);
    const auto configured_budget = host_budget_limits.find(contract.budget_scope);
    const auto budget_limit = configured_budget == host_budget_limits.end()
        ? host_options->limits.max_host_calls
        : configured_budget->second;
    auto [budget, inserted] = host_budget_used.try_emplace(contract.budget_scope, 0);
    (void)inserted;
    if (budget->second >= budget_limit)
        fail(LanguageErrorCode::TaskLimitExceeded,
             "Host operation budget exceeded", span);
    ++budget->second;
    struct BudgetReservation {
        std::size_t* used;
        bool committed{};
        ~BudgetReservation() { if (!committed) --*used; }
    } reservation{&budget->second};

    CallGuard call_guard(*this, span);
    auto roots = heap.root_scope();
    HostArguments converted(parameters.size());
    HostValueMetrics aggregate;
    const auto bridge_limits = effective_host_json_limits(
        host_options->bindings->limits());
    auto add_metrics = [&](const HostValueMetrics metrics,
                           const bool adapter_result) {
        const auto add = [&](std::size_t& target, const std::size_t amount,
                             const std::size_t maximum) {
            if (amount > maximum || target > maximum - amount) {
                if (adapter_result)
                    fail_host(
                        LanguageErrorCode::HostInternal,
                        "Host result exceeded the conversion contract",
                        span,
                        function.module,
                        function.export_name);
                fail(
                    LanguageErrorCode::HostValidationFailed,
                    "Host arguments exceeded the aggregate conversion contract",
                    span);
            }
            target += amount;
        };
        add(aggregate.nodes, metrics.nodes,
            host_options->limits.max_conversion_nodes);
        add(aggregate.total_bytes, metrics.total_bytes,
            host_options->limits.max_conversion_bytes);
        add(aggregate.work, metrics.work,
            host_options->limits.max_conversion_work);
    };

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (aggregate.nodes >= host_options->limits.max_conversion_nodes ||
            aggregate.total_bytes >= host_options->limits.max_conversion_bytes ||
            aggregate.work >= host_options->limits.max_conversion_work)
            fail(LanguageErrorCode::HostValidationFailed,
                 "Host arguments exhausted the aggregate conversion contract", span);
        const auto value = evaluate_expression(arguments[index].value, caller);
        roots.add(value);
        const auto parameter_index = argument_parameters[index];
        try {
            auto remaining_limits = bridge_limits;
            remaining_limits.max_nodes = std::min(
                remaining_limits.max_nodes,
                host_options->limits.max_conversion_nodes - aggregate.nodes);
            remaining_limits.max_total_bytes = std::min(
                remaining_limits.max_total_bytes,
                host_options->limits.max_conversion_bytes - aggregate.total_bytes);
            remaining_limits.max_string_bytes = std::min(
                remaining_limits.max_string_bytes,
                remaining_limits.max_total_bytes > 1
                    ? remaining_limits.max_total_bytes - 1 : 0);
            remaining_limits.max_work = std::min(
                remaining_limits.max_work,
                host_options->limits.max_conversion_work - aggregate.work);
            auto host_value = heap_to_host_value(
                heap, value, parameters[parameter_index].type, remaining_limits,
                host_options->handles.get());
            add_metrics(measure_host_value(host_value, remaining_limits), false);
            converted[parameter_index] = std::move(host_value);
        } catch (const RuntimeError& error) {
            using enum RuntimeErrorCode;
            switch (error.code()) {
                case TypeMismatch:
                case InvalidUtf8:
                case JsonCycle:
                case JsonNonFinite:
                case JsonUnsupported:
                case JsonDepthLimitExceeded:
                case JsonNodeLimitExceeded:
                case JsonStringLimitExceeded:
                case JsonByteLimitExceeded:
                case JsonWorkLimitExceeded:
                case JsonDuplicateKey:
                    fail(LanguageErrorCode::HostValidationFailed,
                         "Host argument does not satisfy the binding contract", span);
                case MemoryLimitExceeded:
                case CellLimitExceeded:
                case SingleAllocationExceeded:
                case StringLimitExceeded:
                case ExternalMemoryLimitExceeded:
                case CollectionWorkLimitExceeded:
                    fail(LanguageErrorCode::MemoryLimitExceeded,
                         "Host argument conversion exhausted evaluator memory", span);
                default:
                    fail(LanguageErrorCode::InternalInvariant,
                         "Host argument referenced invalid evaluator state", span);
            }
        }
    }

    ++stats.host_calls;
    reservation.committed = true;
    struct ReentryGuard {
        bool& active;
        explicit ReentryGuard(bool& state) : active(state) { active = true; }
        ~ReentryGuard() { active = false; }
    } reentry_guard(host_call_active);
    auto result = invoke_host_callback(
        *function.binding,
        {function.module, function.export_name, function.binding->binding_id,
         function.selected_version, stats.host_calls, host_options->cancellation},
        converted,
        host_options->bindings->limits(),
        host_options->handles.get());
    struct ProducedResultGuard final {
        HostReleaseDispatcher* handles{};
        Heap* heap{};
        HostResult* result{};
        bool published{};
        ~ProducedResultGuard()
        {
            if (published || !handles || !heap || !result || !result->ok() ||
                !is_host_handle_type(result->value().type()))
                return;
            const auto& handle = std::get<HostHandleValue>(result->value().storage());
            if (handle.transfer_kind() != HostHandleTransferKind::ProducedGrant) return;
            handles->abandon(handle);
            handles->dispatch_all(*heap);
        }
    } produced_result_guard{
        host_options->handles.get(), &heap, &result, false};
    // Borrowed host<T> arguments pin their native generations only through
    // callback exit. Drop every borrow before processing queued releases.
    converted.clear();
    drain_host_releases();
    if (!result.ok()) {
        if (!result.has_error())
        {
            std::string message = "Host binding ";
            message += function.binding->binding_id;
            message += result.boundary_failure() == HostResult::BoundaryFailure::Allocation
                ? " callback allocation failed" : " callback failed";
            fail_host(
                translate_host_boundary_failure(result.boundary_failure()),
                std::move(message), span, function.module, function.export_name);
        }
        const auto translated = translate_host_error(result.error());
        const auto effect = result.error().effect_state == HostEffectState::NotStarted
            ? "not_started"
            : result.error().effect_state == HostEffectState::Committed
                ? "committed" : "unknown";
        std::string message = "Host binding ";
        message += function.binding->binding_id;
        message += " failed (effect_state=";
        message += effect;
        message += ")";
        if (!result.error().message.empty()) {
            message += ": ";
            message += result.error().message;
        }
        fail_host_result(
            translated.code,
            std::move(message),
            span,
            function.module,
            function.export_name,
            result.error(),
            translated.declared_status);
    }

    try {
        auto result_limits = bridge_limits;
        result_limits.max_nodes = std::min(
            result_limits.max_nodes,
            host_options->limits.max_conversion_nodes - aggregate.nodes);
        result_limits.max_total_bytes = std::min(
            result_limits.max_total_bytes,
            host_options->limits.max_conversion_bytes - aggregate.total_bytes);
        result_limits.max_string_bytes = std::min(
            result_limits.max_string_bytes,
            result_limits.max_total_bytes > 1
                ? result_limits.max_total_bytes - 1 : 0);
        result_limits.max_work = std::min(
            result_limits.max_work,
            host_options->limits.max_conversion_work - aggregate.work);
        const auto result_metrics = measure_host_value(
            result.value(), result_limits);
        add_metrics(result_metrics, true);
        if (stats.host_conversion_nodes >
                std::numeric_limits<std::size_t>::max() - aggregate.nodes ||
            stats.host_conversion_bytes >
                std::numeric_limits<std::size_t>::max() - aggregate.total_bytes)
            fail(LanguageErrorCode::MemoryLimitExceeded,
                 "Host conversion statistics overflowed", span);
        stats.host_conversion_nodes += aggregate.nodes;
        stats.host_conversion_bytes += aggregate.total_bytes;
        auto published = host_to_heap_value(
            heap, result.value(), contract.result, result_limits,
            host_options->handles.get());
        produced_result_guard.published = true;
        return published;
    } catch (const EvaluationError&) {
        throw;
    } catch (const RuntimeError& error) {
        const auto translated = translate_host_result_runtime_error(error.code());
        const auto message = translated == LanguageErrorCode::MemoryLimitExceeded
            ? "Host result publication exhausted evaluator memory"
            : translated == LanguageErrorCode::InternalInvariant
                ? "Host result referenced invalid evaluator state"
                : "Host result does not satisfy the binding contract";
        if (translated == LanguageErrorCode::MemoryLimitExceeded)
            fail(translated, message, span);
        fail_host(
            translated,
            message,
            span,
            function.module,
            function.export_name);
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
    if (metadata.kind == CallableKind::Native) {
        if (metadata.callable_id == 0 ||
            metadata.callable_id > native_functions.size())
            fail(LanguageErrorCode::InternalInvariant,
                 "native callable metadata is not evaluator-owned", span);
        const auto function = native_functions[
            static_cast<std::size_t>(metadata.callable_id - 1)];
        return invoke_native(function, arguments, caller, span);
    }
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

    const auto caller_module = current_module;
    ModuleGuard module_guard(*this, function.module);
    FrameGuard frame_guard(*this, {
        function.module,
        function.name,
        ErrorFramePhase::Body,
        caller_module.empty()
            ? std::nullopt
            : std::optional<SourceReference>{source_reference(caller_module, span)},
        source_reference(function.module, function.definition_span),
        std::nullopt});
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

    const auto pending_root = heap.add_root(Value::null());
    try {
        activations.push_back({function.module, function.name, pending_root, {}, {}});
    } catch (...) {
        heap.remove_root(pending_root);
        throw;
    }
    Flow flow;
    Value result = Value::null();
    try {
        flow = execute_block(*function.body, environment, false);
        if (flow.kind == FlowKind::Break || flow.kind == FlowKind::Continue) {
            fail(LanguageErrorCode::InternalInvariant, "loop control escaped a function", span);
        }
        result = flow.kind == FlowKind::Return ? flow.value : Value::null();
        if (!heap.update_root(activations.back().pending_root, result))
            fail(LanguageErrorCode::InternalInvariant,
                 "function pending-value root is absent", span);
    } catch (ScriptUnwind& unwind) {
        frame_guard.leave();
        const auto primary = drain_activation(unwind.error);
        if (!primary)
            fail(LanguageErrorCode::InternalInvariant, "cleanup lost the primary Error", span);
        if (*primary != unwind.error) unwind.replace(*primary);
        throw;
    } catch (const EvaluationError&) {
        frame_guard.leave();
        // Error publication may fail after defers have been registered. The
        // boundary failure is terminal and cannot be represented as a same-heap
        // Error, but every admitted cleanup is still drained fail-closed.
        static_cast<void>(drain_activation(std::nullopt, true));
        throw;
    } catch (const RuntimeError&) {
        frame_guard.leave();
        static_cast<void>(drain_activation(std::nullopt, true));
        throw;
    } catch (const std::bad_alloc&) {
        frame_guard.leave();
        static_cast<void>(drain_activation(std::nullopt, true));
        throw;
    } catch (...) {
        frame_guard.leave();
        static_cast<void>(drain_activation(std::nullopt, true));
        throw;
    }
    frame_guard.leave();
    if (const auto cleanup_error = drain_activation(std::nullopt))
        raise(*cleanup_error);
    roots.add(result);
    return result;
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
    } catch (const ScriptUnwind&) {
        throw;
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
                const auto module = initialize_host_module(
                    specifier.canonical_id, imported.span);
                initialize_binding(environment, imported.alias, module, imported.span);
                return {};
            }
            const auto module = initialize_module(specifier.canonical_id, imported.span);
            initialize_binding(environment, imported.alias, module, imported.span);
            return {};
        }
        case NodeKind::ThrowStatement: {
            const auto& thrown = static_cast<const ThrowStatement&>(statement);
            auto roots = heap.root_scope();
            const auto operand = evaluate_expression(thrown.value, environment);
            roots.add(operand);
            if (heap.kind(operand) == ValueKind::Error) raise(operand);
            const auto error = make_thrown_value_error(operand, thrown.span);
            roots.add(error);
            raise(error);
        }
        case NodeKind::TryCatchStatement: {
            const auto& attempted = static_cast<const TryCatchStatement&>(statement);
            try {
                return execute_block(*attempted.try_block, environment, true);
            } catch (const ScriptUnwind& unwind) {
                const auto& metadata = heap.error_metadata_view(unwind.error.as_heap_ref());
                if (!metadata.catchable()) throw;
                auto catch_environment = make_environment(environment);
                predeclare(catch_environment, attempted.binding, attempted.catch_block->span);
                predeclare_statements(catch_environment, attempted.catch_block->statements);
                initialize_binding(
                    catch_environment, attempted.binding, unwind.error,
                    attempted.catch_block->span);
                return execute_block(*attempted.catch_block, catch_environment, false);
            }
        }
        case NodeKind::DeferStatement: {
            const auto& deferred = static_cast<const DeferStatement&>(statement);
            if (activations.empty())
                fail(LanguageErrorCode::InternalInvariant,
                     "defer executed without a function activation", deferred.span);
            auto& activation = activations.back();
            if (activation.defers.size() >= limits.max_defers_per_frame)
                fail(LanguageErrorCode::CleanupLimitExceeded,
                     "defer registration limit exceeded", deferred.span);
            // Admit every allocation/root required to record one possible
            // cleanup failure before publishing the defer registration.
            activation.cleanup_errors.reserve(activation.defers.size() + 1);
            const auto failure_root = heap.add_root(Value::null());
            try {
                const auto function_frame = std::find_if(
                    active_frames.rbegin(),
                    active_frames.rend(),
                    [&](const ActiveFrame& frame) {
                        return frame.module == activation.module
                            && frame.function == activation.function
                            && frame.phase == ErrorFramePhase::Body;
                    });
                if (function_frame == active_frames.rend())
                    fail(LanguageErrorCode::InternalInvariant,
                         "defer activation frame is absent", deferred.span);
                activation.defers.push_back({
                    deferred.statement,
                    environment,
                    {
                        current_module,
                        activation.function,
                        ErrorFramePhase::Cleanup,
                        function_frame->call_source,
                        function_frame->definition_source,
                        source_reference(current_module, deferred.span),
                    },
                    failure_root});
            } catch (...) {
                heap.remove_root(failure_root);
                throw;
            }
            ++stats.registered_defers;
            return {};
        }
        default:
            fail(LanguageErrorCode::InternalInvariant, "expression node used as statement", statement.span);
    }
}

SynchronousEvaluator::SynchronousEvaluator(
    std::vector<SourceModule> modules,
    const EvaluatorLimits limits,
    const HeapLimits heap_limits,
    const SemanticOptions semantic_options,
    const NfcPredicate is_nfc,
    std::shared_ptr<const HostCancellationProbe> cancellation)
    : impl_(create_impl(
          std::move(modules), limits, heap_limits, semantic_options, is_nfc,
          std::nullopt, std::move(cancellation)))
{
}

SynchronousEvaluator::SynchronousEvaluator(
    std::vector<SourceModule> modules,
    SynchronousHostOptions host_options,
    const EvaluatorLimits limits,
    const HeapLimits heap_limits,
    const SemanticOptions semantic_options,
    const NfcPredicate is_nfc)
    : impl_(create_impl(
          std::move(modules), limits, heap_limits, semantic_options, is_nfc,
          std::move(host_options), {}))
{
}

SynchronousEvaluator::Impl* SynchronousEvaluator::create_impl(
    std::vector<SourceModule> modules,
    const EvaluatorLimits limits,
    const HeapLimits heap_limits,
    const SemanticOptions semantic_options,
    const NfcPredicate is_nfc,
    std::optional<SynchronousHostOptions> host_options,
    std::shared_ptr<const HostCancellationProbe> cancellation)
{
    try {
        return new Impl(
            std::move(modules), limits, heap_limits, semantic_options, is_nfc,
            std::move(host_options), std::move(cancellation));
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
    if (impl_->closed)
        throw EvaluationError(
            LanguageErrorCode::HostUnavailable,
            "evaluator is closed", {}, {}, impl_->stats.steps);
    struct ReleaseDrain final {
        Impl* impl;
        ~ReleaseDrain() { impl->drain_host_releases(); }
    } release_drain{impl_};
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

bool SynchronousEvaluator::close() noexcept
{
    return impl_->close();
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
