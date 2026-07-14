#include "script/SemanticAnalyzer.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace baas::script {
namespace {

using namespace ast;

constexpr std::size_t implementation_depth_ceiling = 1024;

enum class AccessMode { Read, Write, ReadWrite };

class Engine {
public:
    explicit Engine(const SemanticOptions options)
        : options_(options), effective_max_depth_(std::min(options.max_nesting_depth,
                                                           implementation_depth_ceiling))
    {
        scopes_.emplace_back(); // module scope
    }

    [[nodiscard]] SemanticResult run(const Program& program)
    {
        for (const auto& statement : program.statements) {
            visit_statement(statement.get(), 1);
        }
        result_.visited_ast_nodes = visited_nodes_;
        return std::move(result_);
    }

private:
    struct ScopeGuard {
        Engine& analyzer;
        explicit ScopeGuard(Engine& analyzer) : analyzer(analyzer) { analyzer.scopes_.emplace_back(); }
        ~ScopeGuard() { analyzer.scopes_.pop_back(); }
    };

    struct FunctionGuard {
        Engine& analyzer;
        std::optional<FunctionId> previous;
        FunctionGuard(Engine& analyzer, const Node* node)
            : analyzer(analyzer), previous(analyzer.current_function_)
        {
            const auto id = analyzer.result_.functions.size();
            analyzer.result_.functions.push_back({id, node, previous, {}});
            analyzer.current_function_ = id;
            analyzer.scopes_.emplace_back();
        }
        ~FunctionGuard()
        {
            analyzer.scopes_.pop_back();
            analyzer.current_function_ = previous;
        }
    };

    [[nodiscard]] bool enter(const Node& node, const std::size_t depth)
    {
        if (visited_nodes_ >= options_.max_ast_nodes) {
            if (!node_limit_reported_) {
                diagnose(semantic_diagnostic_code::node_limit,
                         "maximum AST node count exceeded", node.span);
                node_limit_reported_ = true;
            }
            return false;
        }
        ++visited_nodes_;
        if (depth > effective_max_depth_) {
            if (!depth_limit_reported_) {
                diagnose(semantic_diagnostic_code::nesting_limit,
                         "maximum semantic nesting depth exceeded", node.span);
                depth_limit_reported_ = true;
            }
            return false;
        }
        return true;
    }

    void diagnose(const std::string_view code, std::string message, const SourceSpan span)
    {
        result_.diagnostics.push_back({DiagnosticSeverity::Error, std::string(code),
                                       std::move(message), span});
    }

    [[nodiscard]] std::optional<BindingId> declare(
        const std::string& name, const BindingKind kind, const SourceSpan span,
        const bool initialized, const bool parameter = false)
    {
        auto& scope = scopes_.back();
        if (scope.contains(name)) {
            diagnose(parameter ? semantic_diagnostic_code::duplicate_parameter :
                                 semantic_diagnostic_code::duplicate_declaration,
                     parameter ? "duplicate parameter '" + name + "'" :
                                 "duplicate declaration of '" + name + "'",
                     span);
            return std::nullopt;
        }
        const auto id = result_.bindings.size();
        scope.emplace(name, id);
        result_.bindings.push_back({id, name, kind, span, scopes_.size() - 1,
                                    current_function_, initialized, false});
        return id;
    }

    struct FoundBinding {
        BindingId id;
        std::size_t lexical_distance;
    };

    [[nodiscard]] std::optional<FoundBinding> find(const std::string& name) const
    {
        for (std::size_t distance = 0; distance < scopes_.size(); ++distance) {
            const auto& scope = scopes_[scopes_.size() - 1 - distance];
            const auto found = scope.find(name);
            if (found != scope.end()) return FoundBinding{found->second, distance};
        }
        return std::nullopt;
    }

    void add_capture(const BindingId binding)
    {
        if (!current_function_) return;
        const auto owner = result_.bindings[binding].owner_function;
        auto function_id = current_function_;
        // Propagate through intermediate closures: a middle function must keep
        // the owning environment alive even when only its nested function reads
        // the binding.
        while (function_id && function_id != owner) {
            auto& function = result_.functions[*function_id];
            if (std::find(function.captures.begin(), function.captures.end(), binding) ==
                function.captures.end()) {
                function.captures.push_back(binding);
            }
            function_id = function.parent;
        }
        result_.bindings[binding].captured = true;
    }

    void resolve(const IdentifierExpression& identifier, const AccessMode access)
    {
        const auto found = find(identifier.name);
        if (!found) {
            diagnose(semantic_diagnostic_code::unknown_name,
                     "unknown name '" + identifier.name + "'", identifier.span);
            return;
        }
        auto& binding = result_.bindings[found->id];
        const bool requires_read = access != AccessMode::Write;
        if (requires_read && !binding.initialized) {
            diagnose(semantic_diagnostic_code::uninitialized_read,
                     "binding '" + identifier.name + "' is read before initialization",
                     identifier.span);
        }

        const bool captured = current_function_.has_value() && binding.owner_function.has_value() &&
                              current_function_ != binding.owner_function;
        if (captured) add_capture(found->id);
        result_.references.push_back({&identifier, found->id, found->lexical_distance,
                                      access != AccessMode::Read, requires_read, captured});
    }

    void visit_statement(const Statement* statement, const std::size_t depth)
    {
        if (!statement || !enter(*statement, depth)) return;
        switch (statement->kind) {
            case NodeKind::BlockStatement: {
                const auto& block = static_cast<const BlockStatement&>(*statement);
                ScopeGuard scope(*this);
                for (const auto& child : block.statements) visit_statement(child.get(), depth + 1);
                break;
            }
            case NodeKind::LetStatement: {
                const auto& binding = static_cast<const LetStatement&>(*statement);
                const auto id = declare(binding.name, BindingKind::Let, binding.span, false);
                visit_expression(binding.initializer.get(), depth + 1);
                if (id) result_.bindings[*id].initialized = true;
                break;
            }
            case NodeKind::ExpressionStatement:
                visit_expression(static_cast<const ExpressionStatement&>(*statement).expression.get(), depth + 1);
                break;
            case NodeKind::IfStatement: {
                const auto& branch = static_cast<const IfStatement&>(*statement);
                visit_expression(branch.condition.get(), depth + 1);
                visit_statement(branch.consequent.get(), depth + 1);
                visit_statement(branch.alternate.get(), depth + 1);
                break;
            }
            case NodeKind::WhileStatement: {
                const auto& loop = static_cast<const WhileStatement&>(*statement);
                visit_expression(loop.condition.get(), depth + 1);
                visit_statement(loop.body.get(), depth + 1);
                break;
            }
            case NodeKind::ForStatement: {
                const auto& loop = static_cast<const ForStatement&>(*statement);
                visit_expression(loop.iterable.get(), depth + 1);
                ScopeGuard scope(*this);
                (void)declare(loop.binding, BindingKind::For, loop.span, true);
                visit_statement(loop.body.get(), depth + 1);
                break;
            }
            case NodeKind::FunctionDeclaration: {
                const auto& function = static_cast<const FunctionDeclaration&>(*statement);
                (void)declare(function.name, BindingKind::Function, function.span, true);
                if (function.body) {
                    analyze_function(function, function.parameters, *function.body, depth + 1);
                } else {
                    diagnose(semantic_diagnostic_code::malformed_ast,
                             "function declaration has no body", function.span);
                }
                break;
            }
            case NodeKind::ReturnStatement: {
                const auto& returned = static_cast<const ReturnStatement&>(*statement);
                if (returned.value) visit_expression(returned.value->get(), depth + 1);
                break;
            }
            case NodeKind::BreakStatement:
            case NodeKind::ContinueStatement:
                break;
            case NodeKind::ImportStatement: {
                const auto& imported = static_cast<const ImportStatement&>(*statement);
                (void)declare(imported.alias, BindingKind::Import, imported.span, true);
                break;
            }
            case NodeKind::ThrowStatement:
                visit_expression(static_cast<const ThrowStatement&>(*statement).value.get(), depth + 1);
                break;
            case NodeKind::TryCatchStatement: {
                const auto& attempt = static_cast<const TryCatchStatement&>(*statement);
                visit_statement(attempt.try_block.get(), depth + 1);
                ScopeGuard scope(*this);
                (void)declare(attempt.binding, BindingKind::Catch,
                              attempt.catch_block ? attempt.catch_block->span : attempt.span, true);
                visit_statement(attempt.catch_block.get(), depth + 1);
                break;
            }
            case NodeKind::DeferStatement:
                visit_statement(static_cast<const DeferStatement&>(*statement).statement.get(), depth + 1);
                break;
            default:
                break;
        }
    }

    void analyze_function(const Node& node, const std::vector<Parameter>& parameters,
                          const BlockStatement& body, const std::size_t depth)
    {
        FunctionGuard function(*this, &node);
        for (const auto& parameter : parameters) {
            const auto id = declare(parameter.name, BindingKind::Parameter, parameter.span,
                                    !parameter.default_value.has_value(), true);
            if (parameter.default_value) visit_expression(parameter.default_value->get(), depth);
            if (id) result_.bindings[*id].initialized = true;
        }
        // The function body's braces delimit the function scope itself rather
        // than introducing a second scope between parameters and body lets.
        if (!enter(body, depth)) return;
        for (const auto& statement : body.statements) visit_statement(statement.get(), depth + 1);
    }

    void visit_expression(const Expression* expression, const std::size_t depth,
                          const AccessMode access = AccessMode::Read)
    {
        if (!expression || !enter(*expression, depth)) return;
        switch (expression->kind) {
            case NodeKind::LiteralExpression:
                break;
            case NodeKind::IdentifierExpression:
                resolve(static_cast<const IdentifierExpression&>(*expression), access);
                break;
            case NodeKind::ListExpression:
                for (const auto& element : static_cast<const ListExpression&>(*expression).elements)
                    visit_expression(element.get(), depth + 1);
                break;
            case NodeKind::MapExpression:
                for (const auto& entry : static_cast<const MapExpression&>(*expression).entries) {
                    visit_expression(entry.key.get(), depth + 1);
                    visit_expression(entry.value.get(), depth + 1);
                }
                break;
            case NodeKind::FunctionExpression: {
                const auto& function = static_cast<const FunctionExpression&>(*expression);
                if (function.body) {
                    analyze_function(function, function.parameters, *function.body, depth + 1);
                } else {
                    diagnose(semantic_diagnostic_code::malformed_ast,
                             "function expression has no body", function.span);
                }
                break;
            }
            case NodeKind::UnaryExpression:
                visit_expression(static_cast<const UnaryExpression&>(*expression).operand.get(), depth + 1);
                break;
            case NodeKind::BinaryExpression: {
                const auto& binary = static_cast<const BinaryExpression&>(*expression);
                visit_expression(binary.left.get(), depth + 1);
                visit_expression(binary.right.get(), depth + 1);
                break;
            }
            case NodeKind::AssignmentExpression: {
                const auto& assignment = static_cast<const AssignmentExpression&>(*expression);
                const auto target_access = assignment.operation == AssignmentOperator::Assign ?
                                           AccessMode::Write : AccessMode::ReadWrite;
                visit_assignment_target(assignment.target.get(), depth + 1, target_access);
                visit_expression(assignment.value.get(), depth + 1);
                break;
            }
            case NodeKind::MemberExpression:
                visit_expression(static_cast<const MemberExpression&>(*expression).object.get(), depth + 1);
                break;
            case NodeKind::IndexExpression: {
                const auto& index = static_cast<const IndexExpression&>(*expression);
                visit_expression(index.object.get(), depth + 1);
                visit_expression(index.index.get(), depth + 1);
                break;
            }
            case NodeKind::SliceExpression: {
                const auto& slice = static_cast<const SliceExpression&>(*expression);
                visit_expression(slice.object.get(), depth + 1);
                if (slice.start) visit_expression(slice.start->get(), depth + 1);
                if (slice.stop) visit_expression(slice.stop->get(), depth + 1);
                if (slice.step) visit_expression(slice.step->get(), depth + 1);
                break;
            }
            case NodeKind::CallExpression: {
                const auto& call = static_cast<const CallExpression&>(*expression);
                visit_expression(call.callee.get(), depth + 1);
                for (const auto& argument : call.arguments) visit_expression(argument.value.get(), depth + 1);
                break;
            }
            case NodeKind::AwaitExpression:
                visit_expression(static_cast<const AwaitExpression&>(*expression).value.get(), depth + 1);
                break;
            default:
                break;
        }
    }

    void visit_assignment_target(const Expression* target, const std::size_t depth, const AccessMode access)
    {
        if (!target) return;
        if (target->kind == NodeKind::IdentifierExpression) {
            visit_expression(target, depth, access);
            return;
        }
        // Member/index assignment mutates the referenced object; the object and
        // index expressions are reads, not lexical-binding writes.
        visit_expression(target, depth);
    }

    SemanticOptions options_;
    std::size_t effective_max_depth_;
    SemanticResult result_;
    std::vector<std::unordered_map<std::string, BindingId>> scopes_;
    std::optional<FunctionId> current_function_;
    std::size_t visited_nodes_{0};
    bool node_limit_reported_{false};
    bool depth_limit_reported_{false};
};

}  // namespace

bool SemanticResult::has_errors() const noexcept
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

const ReferenceResolution* SemanticResult::resolution_for(
    const ast::IdentifierExpression& reference) const noexcept
{
    const auto found = std::find_if(references.begin(), references.end(), [&](const auto& resolution) {
        return resolution.reference == &reference;
    });
    return found == references.end() ? nullptr : &*found;
}

SemanticAnalyzer::SemanticAnalyzer(const SemanticOptions options) : options_(options) {}

SemanticResult SemanticAnalyzer::analyze(const ast::Program& program) const
{
    return Engine(options_).run(program);
}

SemanticResult analyze_semantics(const ast::Program& program, const SemanticOptions options)
{
    return SemanticAnalyzer(options).analyze(program);
}

}  // namespace baas::script
