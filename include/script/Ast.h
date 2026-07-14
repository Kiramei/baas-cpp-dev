#pragma once

#include "script/SourceLocation.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace baas::script::ast {

enum class NodeKind {
    LiteralExpression, IdentifierExpression, ListExpression, MapExpression,
    FunctionExpression, UnaryExpression, BinaryExpression, AssignmentExpression,
    MemberExpression, IndexExpression, SliceExpression, CallExpression, AwaitExpression,
    BlockStatement, LetStatement, ExpressionStatement, IfStatement, WhileStatement,
    ForStatement, FunctionDeclaration, ReturnStatement, BreakStatement,
    ContinueStatement, ImportStatement, ThrowStatement, TryCatchStatement, DeferStatement,
};

struct Node {
    NodeKind kind;
    SourceSpan span;
    virtual ~Node() = default;

protected:
    Node(NodeKind kind_value, SourceSpan span_value) : kind(kind_value), span(span_value) {}
};

struct Expression : Node {
protected:
    using Node::Node;
};
struct Statement : Node {
protected:
    using Node::Node;
};

using ExprPtr = std::shared_ptr<const Expression>;
using StmtPtr = std::shared_ptr<const Statement>;
using LiteralValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

struct LiteralExpression final : Expression {
    LiteralValue value;
    LiteralExpression(SourceSpan span, LiteralValue value)
        : Expression(NodeKind::LiteralExpression, span), value(std::move(value)) {}
};

struct IdentifierExpression final : Expression {
    std::string name;
    IdentifierExpression(SourceSpan span, std::string name)
        : Expression(NodeKind::IdentifierExpression, span), name(std::move(name)) {}
};

struct ListExpression final : Expression {
    std::vector<ExprPtr> elements;
    ListExpression(SourceSpan span, std::vector<ExprPtr> elements)
        : Expression(NodeKind::ListExpression, span), elements(std::move(elements)) {}
};

struct MapEntry {
    ExprPtr key;
    ExprPtr value;
    SourceSpan span;
};

struct MapExpression final : Expression {
    // Source order is preserved. Runtime validation may restrict keys to strings.
    std::vector<MapEntry> entries;
    MapExpression(SourceSpan span, std::vector<MapEntry> entries)
        : Expression(NodeKind::MapExpression, span), entries(std::move(entries)) {}
};

struct Parameter {
    std::string name;
    std::optional<ExprPtr> default_value;
    SourceSpan span;
};

struct BlockStatement final : Statement {
    std::vector<StmtPtr> statements;
    BlockStatement(SourceSpan span, std::vector<StmtPtr> statements)
        : Statement(NodeKind::BlockStatement, span), statements(std::move(statements)) {}
};

struct FunctionExpression final : Expression {
    bool is_async;
    std::vector<Parameter> parameters;
    std::shared_ptr<const BlockStatement> body;
    FunctionExpression(SourceSpan span, bool is_async, std::vector<Parameter> parameters,
                       std::shared_ptr<const BlockStatement> body)
        : Expression(NodeKind::FunctionExpression, span), is_async(is_async),
          parameters(std::move(parameters)), body(std::move(body)) {}
};

enum class UnaryOperator { Plus, Minus, Not };
enum class BinaryOperator {
    Or, And, Equal, NotEqual, Is, NotIs, Less, LessEqual, Greater, GreaterEqual,
    In, NotIn, Add, Subtract, Multiply, Divide, FloorDivide, Modulo, Power,
};
enum class AssignmentOperator {
    Assign, Add, Subtract, Multiply, Divide, FloorDivide, Modulo,
};

struct UnaryExpression final : Expression {
    UnaryOperator operation;
    ExprPtr operand;
    UnaryExpression(SourceSpan span, UnaryOperator operation, ExprPtr operand)
        : Expression(NodeKind::UnaryExpression, span), operation(operation), operand(std::move(operand)) {}
};

struct BinaryExpression final : Expression {
    BinaryOperator operation;
    ExprPtr left;
    ExprPtr right;
    BinaryExpression(SourceSpan span, BinaryOperator operation, ExprPtr left, ExprPtr right)
        : Expression(NodeKind::BinaryExpression, span), operation(operation),
          left(std::move(left)), right(std::move(right)) {}
};

struct AssignmentExpression final : Expression {
    AssignmentOperator operation;
    ExprPtr target;
    ExprPtr value;
    AssignmentExpression(SourceSpan span, AssignmentOperator operation, ExprPtr target, ExprPtr value)
        : Expression(NodeKind::AssignmentExpression, span), operation(operation),
          target(std::move(target)), value(std::move(value)) {}
};

struct MemberExpression final : Expression {
    ExprPtr object;
    std::string member;
    MemberExpression(SourceSpan span, ExprPtr object, std::string member)
        : Expression(NodeKind::MemberExpression, span), object(std::move(object)), member(std::move(member)) {}
};

struct IndexExpression final : Expression {
    ExprPtr object;
    ExprPtr index;
    IndexExpression(SourceSpan span, ExprPtr object, ExprPtr index)
        : Expression(NodeKind::IndexExpression, span), object(std::move(object)), index(std::move(index)) {}
};

struct SliceExpression final : Expression {
    ExprPtr object;
    std::optional<ExprPtr> start;
    std::optional<ExprPtr> stop;
    std::optional<ExprPtr> step;
    SliceExpression(SourceSpan span, ExprPtr object, std::optional<ExprPtr> start,
                    std::optional<ExprPtr> stop, std::optional<ExprPtr> step)
        : Expression(NodeKind::SliceExpression, span), object(std::move(object)),
          start(std::move(start)), stop(std::move(stop)), step(std::move(step)) {}
};

struct CallArgument {
    std::optional<std::string> name;
    ExprPtr value;
    SourceSpan span;
};

struct CallExpression final : Expression {
    ExprPtr callee;
    std::vector<CallArgument> arguments;
    CallExpression(SourceSpan span, ExprPtr callee, std::vector<CallArgument> arguments)
        : Expression(NodeKind::CallExpression, span), callee(std::move(callee)),
          arguments(std::move(arguments)) {}
};

struct AwaitExpression final : Expression {
    ExprPtr value;
    AwaitExpression(SourceSpan span, ExprPtr value)
        : Expression(NodeKind::AwaitExpression, span), value(std::move(value)) {}
};

struct LetStatement final : Statement {
    std::string name;
    ExprPtr initializer;
    LetStatement(SourceSpan span, std::string name, ExprPtr initializer)
        : Statement(NodeKind::LetStatement, span), name(std::move(name)), initializer(std::move(initializer)) {}
};
struct ExpressionStatement final : Statement {
    ExprPtr expression;
    ExpressionStatement(SourceSpan span, ExprPtr expression)
        : Statement(NodeKind::ExpressionStatement, span), expression(std::move(expression)) {}
};
struct IfStatement final : Statement {
    ExprPtr condition;
    StmtPtr consequent;
    StmtPtr alternate;
    IfStatement(SourceSpan span, ExprPtr condition, StmtPtr consequent, StmtPtr alternate)
        : Statement(NodeKind::IfStatement, span), condition(std::move(condition)),
          consequent(std::move(consequent)), alternate(std::move(alternate)) {}
};
struct WhileStatement final : Statement {
    ExprPtr condition;
    StmtPtr body;
    WhileStatement(SourceSpan span, ExprPtr condition, StmtPtr body)
        : Statement(NodeKind::WhileStatement, span), condition(std::move(condition)), body(std::move(body)) {}
};
struct ForStatement final : Statement {
    std::string binding;
    ExprPtr iterable;
    StmtPtr body;
    ForStatement(SourceSpan span, std::string binding, ExprPtr iterable, StmtPtr body)
        : Statement(NodeKind::ForStatement, span), binding(std::move(binding)),
          iterable(std::move(iterable)), body(std::move(body)) {}
};
struct FunctionDeclaration final : Statement {
    std::string name;
    bool is_async;
    std::vector<Parameter> parameters;
    std::shared_ptr<const BlockStatement> body;
    FunctionDeclaration(SourceSpan span, std::string name, bool is_async,
                        std::vector<Parameter> parameters, std::shared_ptr<const BlockStatement> body)
        : Statement(NodeKind::FunctionDeclaration, span), name(std::move(name)), is_async(is_async),
          parameters(std::move(parameters)), body(std::move(body)) {}
};
struct ReturnStatement final : Statement {
    std::optional<ExprPtr> value;
    ReturnStatement(SourceSpan span, std::optional<ExprPtr> value)
        : Statement(NodeKind::ReturnStatement, span), value(std::move(value)) {}
};
struct BreakStatement final : Statement {
    explicit BreakStatement(SourceSpan span) : Statement(NodeKind::BreakStatement, span) {}
};
struct ContinueStatement final : Statement {
    explicit ContinueStatement(SourceSpan span) : Statement(NodeKind::ContinueStatement, span) {}
};
struct ImportStatement final : Statement {
    std::string module;
    std::string alias;
    ImportStatement(SourceSpan span, std::string module, std::string alias)
        : Statement(NodeKind::ImportStatement, span), module(std::move(module)), alias(std::move(alias)) {}
};
struct ThrowStatement final : Statement {
    ExprPtr value;
    ThrowStatement(SourceSpan span, ExprPtr value)
        : Statement(NodeKind::ThrowStatement, span), value(std::move(value)) {}
};
struct TryCatchStatement final : Statement {
    std::shared_ptr<const BlockStatement> try_block;
    std::string binding;
    std::shared_ptr<const BlockStatement> catch_block;
    TryCatchStatement(SourceSpan span, std::shared_ptr<const BlockStatement> try_block,
                      std::string binding, std::shared_ptr<const BlockStatement> catch_block)
        : Statement(NodeKind::TryCatchStatement, span), try_block(std::move(try_block)),
          binding(std::move(binding)), catch_block(std::move(catch_block)) {}
};
struct DeferStatement final : Statement {
    StmtPtr statement;
    DeferStatement(SourceSpan span, StmtPtr statement)
        : Statement(NodeKind::DeferStatement, span), statement(std::move(statement)) {}
};

struct Program {
    const std::vector<StmtPtr> statements;
    const SourceSpan span{};
};

template <typename T>
[[nodiscard]] const T* as(const Node* node) noexcept { return dynamic_cast<const T*>(node); }
template <typename T>
[[nodiscard]] const T* as(const std::shared_ptr<const Node>& node) noexcept { return as<T>(node.get()); }

}  // namespace baas::script::ast
