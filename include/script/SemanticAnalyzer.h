#pragma once

#include "script/Ast.h"
#include "script/Diagnostic.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace baas::script {

namespace semantic_diagnostic_code {
inline constexpr std::string_view unknown_name{"SEM001"};
inline constexpr std::string_view duplicate_declaration{"SEM002"};
inline constexpr std::string_view uninitialized_read{"SEM003"};
inline constexpr std::string_view duplicate_parameter{"SEM004"};
// SEM005 is reserved for the future declaration/type-validation layer.
inline constexpr std::string_view node_limit{"SEM006"};
inline constexpr std::string_view nesting_limit{"SEM007"};
inline constexpr std::string_view malformed_ast{"SEM008"};
inline constexpr std::string_view cleanup_control{"SEM009"};
}  // namespace semantic_diagnostic_code

using BindingId = std::size_t;
using FunctionId = std::size_t;

enum class BindingKind {
    Let,
    Parameter,
    Function,
    Import,
    For,
    Catch,
};

struct SemanticOptions {
    std::size_t max_ast_nodes{100'000};
    // Values above the implementation safety ceiling (1024) are clamped so a
    // hostile AST cannot exhaust the native call stack.
    std::size_t max_nesting_depth{256};
};

struct BindingInfo {
    BindingId id{};
    std::string name;
    BindingKind kind{BindingKind::Let};
    SourceSpan declaration_span{};
    std::size_t scope_depth{};
    std::optional<FunctionId> owner_function;
    bool initialized{false};
    bool captured{false};
};

struct ReferenceResolution {
    // Valid while the analyzed immutable AST remains alive.
    const ast::IdentifierExpression* reference{};
    BindingId binding{};
    std::size_t lexical_distance{};
    bool is_write{false};
    bool requires_read{true};
    bool captured{false};
};

struct FunctionInfo {
    FunctionId id{};
    // FunctionExpression or FunctionDeclaration; valid while the AST is alive.
    const ast::Node* node{};
    std::optional<FunctionId> parent;
    std::vector<BindingId> captures;
};

struct SemanticResult {
    std::vector<Diagnostic> diagnostics;
    std::vector<BindingInfo> bindings;
    std::vector<ReferenceResolution> references;
    std::vector<FunctionInfo> functions;
    std::size_t visited_ast_nodes{};

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] const ReferenceResolution* resolution_for(
        const ast::IdentifierExpression& reference) const noexcept;
};

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(SemanticOptions options = {});

    // The result's node pointers borrow from program. All names, diagnostics,
    // binding records, and capture lists are owned by the result.
    [[nodiscard]] SemanticResult analyze(const ast::Program& program) const;

private:
    SemanticOptions options_;
};

[[nodiscard]] SemanticResult analyze_semantics(
    const ast::Program& program, SemanticOptions options = {});

}  // namespace baas::script
