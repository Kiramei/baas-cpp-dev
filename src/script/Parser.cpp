#include "script/Parser.h"

#include "script/Lexer.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace baas::script {
namespace {

using namespace ast;

struct ParseFailure {};

[[nodiscard]] SourceSpan joined(const SourceSpan& first, const SourceSpan& last)
{
    return {first.begin, last.end};
}

class Engine {
public:
    Engine(const std::vector<Token>& tokens, std::vector<Diagnostic> diagnostics = {})
        : tokens_(tokens), diagnostics_(std::move(diagnostics)) {}

    [[nodiscard]] ParseResult run()
    {
        std::vector<StmtPtr> statements;
        while (!at_end()) {
            const auto before = current_;
            try {
                statements.push_back(statement());
            } catch (const ParseFailure&) {
                synchronize();
            }
            if (current_ == before && !at_end()) {
                // A hard progress guarantee protects service validation from a
                // malformed-token recovery loop.
                ++current_;
            }
        }
        const auto begin = statements.empty() ? peek().span.begin : statements.front()->span.begin;
        const auto end = peek().span.end;
        return {{std::move(statements), {begin, end}}, std::move(diagnostics_)};
    }

private:
    [[nodiscard]] const Token& peek(const std::size_t distance = 0) const
    {
        const auto index = std::min(current_ + distance, tokens_.size() - 1);
        return tokens_[index];
    }
    [[nodiscard]] const Token& previous() const { return tokens_[current_ - 1]; }
    [[nodiscard]] bool at_end() const { return peek().kind == TokenKind::EndOfFile; }
    [[nodiscard]] bool check(const TokenKind kind) const { return peek().kind == kind; }
    [[nodiscard]] bool check_next(const TokenKind kind) const { return peek(1).kind == kind; }
    const Token& advance()
    {
        if (!at_end()) ++current_;
        return previous();
    }
    bool match(const TokenKind kind)
    {
        if (!check(kind)) return false;
        advance();
        return true;
    }
    template <typename... Kinds>
    bool match_any(Kinds... kinds)
    {
        return (match(kinds) || ...);
    }
    const Token& consume(const TokenKind kind, const char* message)
    {
        if (check(kind)) return advance();
        error("PAR001", message, peek().span);
        throw ParseFailure{};
    }
    void error(std::string code, std::string message, const SourceSpan span)
    {
        diagnostics_.push_back({DiagnosticSeverity::Error, std::move(code), std::move(message), span});
    }

    void synchronize()
    {
        if (at_end()) return;
        if (current_ > 0 && previous().kind == TokenKind::Semicolon) return;
        while (!at_end()) {
            if (check(TokenKind::RightBrace)) return;
            switch (peek().kind) {
                case TokenKind::Let: case TokenKind::Fn: case TokenKind::Async:
                case TokenKind::If: case TokenKind::While: case TokenKind::For:
                case TokenKind::Return: case TokenKind::Break: case TokenKind::Continue:
                case TokenKind::Import: case TokenKind::Throw: case TokenKind::Try:
                case TokenKind::Defer:
                    return;
                default: break;
            }
            if (advance().kind == TokenKind::Semicolon) return;
        }
    }

    [[nodiscard]] StmtPtr statement()
    {
        if (match(TokenKind::LeftBrace)) return block(previous());
        if (match(TokenKind::Let)) return let_statement(previous());
        if (match(TokenKind::If)) return if_statement(previous());
        if (match(TokenKind::While)) return while_statement(previous());
        if (match(TokenKind::For)) return for_statement(previous());
        if (check(TokenKind::Async) && check_next(TokenKind::Fn) && peek(2).kind == TokenKind::Identifier) {
            const auto start = advance();
            advance();
            return function_declaration(start, true);
        }
        if (check(TokenKind::Fn) && check_next(TokenKind::Identifier)) {
            const auto start = advance();
            return function_declaration(start, false);
        }
        if (match(TokenKind::Return)) return return_statement(previous());
        if (match(TokenKind::Break)) return break_statement(previous());
        if (match(TokenKind::Continue)) return continue_statement(previous());
        if (match(TokenKind::Import)) return import_statement(previous());
        if (match(TokenKind::Throw)) return throw_statement(previous());
        if (match(TokenKind::Try)) return try_statement(previous());
        if (match(TokenKind::Defer)) return defer_statement(previous());
        return expression_statement();
    }

    [[nodiscard]] std::shared_ptr<const BlockStatement> block(const Token& open)
    {
        std::vector<StmtPtr> statements;
        while (!check(TokenKind::RightBrace) && !at_end()) {
            const auto before = current_;
            try {
                statements.push_back(statement());
            } catch (const ParseFailure&) {
                synchronize();
            }
            if (current_ == before && !at_end() && !check(TokenKind::RightBrace)) ++current_;
        }
        const auto& close = consume(TokenKind::RightBrace, "expected '}' after block");
        return std::make_shared<const BlockStatement>(joined(open.span, close.span), std::move(statements));
    }

    [[nodiscard]] std::shared_ptr<const BlockStatement> required_block(const char* message)
    {
        const auto& open = consume(TokenKind::LeftBrace, message);
        return block(open);
    }

    [[nodiscard]] StmtPtr let_statement(const Token& start)
    {
        const auto& name = consume(TokenKind::Identifier, "expected binding name after 'let'");
        consume(TokenKind::Equal, "expected '=' after binding name");
        auto initializer = expression();
        const auto& end = consume(TokenKind::Semicolon, "expected ';' after binding");
        return std::make_shared<const LetStatement>(joined(start.span, end.span), name.lexeme,
                                                    std::move(initializer));
    }

    [[nodiscard]] ExprPtr parenthesized_condition(const char* construct)
    {
        consume(TokenKind::LeftParen, construct);
        auto result = expression();
        consume(TokenKind::RightParen, "expected ')' after condition");
        return result;
    }

    [[nodiscard]] StmtPtr if_statement(const Token& start)
    {
        auto condition = parenthesized_condition("expected '(' after 'if'");
        auto consequent = statement();
        StmtPtr alternate;
        if (match(TokenKind::Else)) alternate = statement();
        const auto end_span = alternate ? alternate->span : consequent->span;
        return std::make_shared<const IfStatement>(joined(start.span, end_span), std::move(condition),
                                                   std::move(consequent), std::move(alternate));
    }

    struct LoopContextGuard {
        Engine& parser;
        explicit LoopContextGuard(Engine& parser) : parser(parser) { ++parser.loop_depth_; }
        ~LoopContextGuard() { --parser.loop_depth_; }
    };

    [[nodiscard]] StmtPtr while_statement(const Token& start)
    {
        auto condition = parenthesized_condition("expected '(' after 'while'");
        LoopContextGuard guard(*this);
        auto body = statement();
        return std::make_shared<const WhileStatement>(joined(start.span, body->span),
                                                      std::move(condition), std::move(body));
    }

    [[nodiscard]] StmtPtr for_statement(const Token& start)
    {
        consume(TokenKind::LeftParen, "expected '(' after 'for'");
        const auto& binding = consume(TokenKind::Identifier, "expected loop binding");
        consume(TokenKind::In, "expected 'in' after loop binding");
        auto iterable = expression();
        consume(TokenKind::RightParen, "expected ')' after for iterable");
        LoopContextGuard guard(*this);
        auto body = statement();
        return std::make_shared<const ForStatement>(joined(start.span, body->span), binding.lexeme,
                                                    std::move(iterable), std::move(body));
    }

    [[nodiscard]] std::vector<Parameter> parameters()
    {
        consume(TokenKind::LeftParen, "expected '('");
        std::vector<Parameter> result;
        std::unordered_set<std::string> names;
        bool saw_default = false;
        if (!check(TokenKind::RightParen)) {
            do {
                const auto& name = consume(TokenKind::Identifier, "expected parameter name");
                std::optional<ExprPtr> default_value;
                SourceSpan span = name.span;
                if (match(TokenKind::Equal)) {
                    saw_default = true;
                    default_value = expression();
                    span.end = (*default_value)->span.end;
                } else if (saw_default) {
                    error("PAR005", "required parameter cannot follow a default parameter", name.span);
                }
                if (!names.insert(name.lexeme).second) {
                    error("PAR004", "duplicate parameter '" + name.lexeme + "'", name.span);
                }
                result.push_back({name.lexeme, std::move(default_value), span});
            } while (match(TokenKind::Comma) && !check(TokenKind::RightParen));
        }
        consume(TokenKind::RightParen, "expected ')' after parameters");
        return result;
    }

    struct FunctionContextGuard {
        Engine& parser;
        int old_loop;
        bool old_async;
        FunctionContextGuard(Engine& parser, const bool async)
            : parser(parser), old_loop(parser.loop_depth_), old_async(parser.in_async_function_)
        {
            ++parser.function_depth_;
            parser.loop_depth_ = 0;
            parser.in_async_function_ = async;
        }
        ~FunctionContextGuard()
        {
            --parser.function_depth_;
            parser.loop_depth_ = old_loop;
            parser.in_async_function_ = old_async;
        }
    };

    [[nodiscard]] std::shared_ptr<const BlockStatement> function_body(const bool is_async)
    {
        const auto& open = consume(TokenKind::LeftBrace, "expected '{' before function body");
        FunctionContextGuard guard(*this, is_async);
        return block(open);
    }

    [[nodiscard]] StmtPtr function_declaration(const Token& start, const bool is_async)
    {
        const auto& name = consume(TokenKind::Identifier, "expected function name");
        auto params = parameters();
        auto body = function_body(is_async);
        return std::make_shared<const FunctionDeclaration>(joined(start.span, body->span), name.lexeme,
                                                           is_async, std::move(params), std::move(body));
    }

    [[nodiscard]] StmtPtr return_statement(const Token& start)
    {
        if (function_depth_ == 0) error("PAR010", "'return' is only valid inside a function", start.span);
        std::optional<ExprPtr> value;
        if (!check(TokenKind::Semicolon)) value = expression();
        const auto& end = consume(TokenKind::Semicolon, "expected ';' after return");
        return std::make_shared<const ReturnStatement>(joined(start.span, end.span), std::move(value));
    }

    [[nodiscard]] StmtPtr break_statement(const Token& start)
    {
        if (loop_depth_ == 0) error("PAR008", "'break' is only valid inside a loop", start.span);
        const auto& end = consume(TokenKind::Semicolon, "expected ';' after break");
        return std::make_shared<const BreakStatement>(joined(start.span, end.span));
    }
    [[nodiscard]] StmtPtr continue_statement(const Token& start)
    {
        if (loop_depth_ == 0) error("PAR009", "'continue' is only valid inside a loop", start.span);
        const auto& end = consume(TokenKind::Semicolon, "expected ';' after continue");
        return std::make_shared<const ContinueStatement>(joined(start.span, end.span));
    }
    [[nodiscard]] StmtPtr import_statement(const Token& start)
    {
        const auto& module = consume(TokenKind::String, "expected module string after 'import'");
        consume(TokenKind::As, "expected 'as' after import path");
        const auto& alias = consume(TokenKind::Identifier, "expected import alias");
        const auto& end = consume(TokenKind::Semicolon, "expected ';' after import");
        return std::make_shared<const ImportStatement>(joined(start.span, end.span), module.value, alias.lexeme);
    }
    [[nodiscard]] StmtPtr throw_statement(const Token& start)
    {
        auto value = expression();
        const auto& end = consume(TokenKind::Semicolon, "expected ';' after throw");
        return std::make_shared<const ThrowStatement>(joined(start.span, end.span), std::move(value));
    }
    [[nodiscard]] StmtPtr try_statement(const Token& start)
    {
        auto try_block = required_block("expected '{' after 'try'");
        consume(TokenKind::Catch, "expected 'catch' after try block");
        bool parenthesized = match(TokenKind::LeftParen);
        const auto& binding = consume(TokenKind::Identifier, "expected catch binding");
        if (parenthesized) consume(TokenKind::RightParen, "expected ')' after catch binding");
        auto catch_block = required_block("expected '{' after catch binding");
        return std::make_shared<const TryCatchStatement>(joined(start.span, catch_block->span),
                                                        std::move(try_block), binding.lexeme,
                                                        std::move(catch_block));
    }
    [[nodiscard]] StmtPtr defer_statement(const Token& start)
    {
        if (function_depth_ == 0) error("PAR014", "'defer' is only valid inside a function", start.span);
        auto deferred = statement();
        return std::make_shared<const DeferStatement>(joined(start.span, deferred->span), std::move(deferred));
    }
    [[nodiscard]] StmtPtr expression_statement()
    {
        auto value = expression();
        const auto start = value->span;
        const auto& end = consume(TokenKind::Semicolon, "expected ';' after expression");
        return std::make_shared<const ExpressionStatement>(joined(start, end.span), std::move(value));
    }

    [[nodiscard]] ExprPtr expression() { return assignment(); }

    [[nodiscard]] ExprPtr assignment()
    {
        auto target = logical_or();
        AssignmentOperator operation{};
        bool matched = true;
        if (match(TokenKind::Equal)) operation = AssignmentOperator::Assign;
        else if (match(TokenKind::PlusEqual)) operation = AssignmentOperator::Add;
        else if (match(TokenKind::MinusEqual)) operation = AssignmentOperator::Subtract;
        else if (match(TokenKind::StarEqual)) operation = AssignmentOperator::Multiply;
        else if (match(TokenKind::SlashEqual)) operation = AssignmentOperator::Divide;
        else if (match(TokenKind::FloorDivideEqual)) operation = AssignmentOperator::FloorDivide;
        else if (match(TokenKind::PercentEqual)) operation = AssignmentOperator::Modulo;
        else matched = false;
        if (!matched) return target;
        const auto operator_span = previous().span;
        auto value = assignment();
        if (target->kind != NodeKind::IdentifierExpression && target->kind != NodeKind::MemberExpression &&
            target->kind != NodeKind::IndexExpression) {
            error("PAR003", "invalid assignment target", joined(target->span, operator_span));
        }
        return std::make_shared<const AssignmentExpression>(joined(target->span, value->span), operation,
                                                            std::move(target), std::move(value));
    }

    template <typename Next>
    [[nodiscard]] ExprPtr left_associative(Next next,
        const std::vector<std::pair<TokenKind, BinaryOperator>>& operators)
    {
        auto left = (this->*next)();
        for (;;) {
            std::optional<BinaryOperator> operation;
            for (const auto& [kind, value] : operators) {
                if (match(kind)) { operation = value; break; }
            }
            if (!operation) return left;
            auto right = (this->*next)();
            left = std::make_shared<const BinaryExpression>(joined(left->span, right->span), *operation,
                                                            std::move(left), std::move(right));
        }
    }

    [[nodiscard]] ExprPtr logical_or()
    {
        return left_associative(&Engine::logical_and, {{TokenKind::Or, BinaryOperator::Or}});
    }
    [[nodiscard]] ExprPtr logical_and()
    {
        return left_associative(&Engine::equality, {{TokenKind::And, BinaryOperator::And}});
    }
    [[nodiscard]] ExprPtr equality()
    {
        auto left = ordering();
        for (;;) {
            std::optional<BinaryOperator> operation;
            if (match(TokenKind::EqualEqual)) operation = BinaryOperator::Equal;
            else if (match(TokenKind::BangEqual)) operation = BinaryOperator::NotEqual;
            else if (match(TokenKind::Is)) operation = BinaryOperator::Is;
            else if (check(TokenKind::Not) && check_next(TokenKind::Is)) {
                advance(); advance(); operation = BinaryOperator::NotIs;
            }
            if (!operation) return left;
            auto right = ordering();
            left = std::make_shared<const BinaryExpression>(joined(left->span, right->span), *operation,
                                                            std::move(left), std::move(right));
        }
    }
    [[nodiscard]] ExprPtr ordering()
    {
        auto left = additive();
        for (;;) {
            std::optional<BinaryOperator> operation;
            if (match(TokenKind::Less)) operation = BinaryOperator::Less;
            else if (match(TokenKind::LessEqual)) operation = BinaryOperator::LessEqual;
            else if (match(TokenKind::Greater)) operation = BinaryOperator::Greater;
            else if (match(TokenKind::GreaterEqual)) operation = BinaryOperator::GreaterEqual;
            else if (match(TokenKind::In)) operation = BinaryOperator::In;
            else if (check(TokenKind::Not) && check_next(TokenKind::In)) {
                advance(); advance(); operation = BinaryOperator::NotIn;
            }
            if (!operation) return left;
            auto right = additive();
            left = std::make_shared<const BinaryExpression>(joined(left->span, right->span), *operation,
                                                            std::move(left), std::move(right));
        }
    }
    [[nodiscard]] ExprPtr additive()
    {
        return left_associative(&Engine::multiplicative,
            {{TokenKind::Plus, BinaryOperator::Add}, {TokenKind::Minus, BinaryOperator::Subtract}});
    }
    [[nodiscard]] ExprPtr multiplicative()
    {
        return left_associative(&Engine::unary,
            {{TokenKind::Star, BinaryOperator::Multiply}, {TokenKind::Slash, BinaryOperator::Divide},
             {TokenKind::FloorDivide, BinaryOperator::FloorDivide}, {TokenKind::Percent, BinaryOperator::Modulo}});
    }
    [[nodiscard]] ExprPtr unary()
    {
        if (match_any(TokenKind::Plus, TokenKind::Minus, TokenKind::Not)) {
            const auto token = previous();
            const auto operation = token.kind == TokenKind::Plus ? UnaryOperator::Plus :
                                   token.kind == TokenKind::Minus ? UnaryOperator::Minus : UnaryOperator::Not;
            auto operand = unary();
            return std::make_shared<const UnaryExpression>(joined(token.span, operand->span), operation,
                                                           std::move(operand));
        }
        if (match(TokenKind::Await)) {
            const auto token = previous();
            if (!in_async_function_) error("PAR011", "'await' is only valid inside an async function", token.span);
            auto value = unary();
            return std::make_shared<const AwaitExpression>(joined(token.span, value->span), std::move(value));
        }
        return power();
    }
    [[nodiscard]] ExprPtr power()
    {
        auto left = postfix();
        if (!match(TokenKind::Power)) return left;
        auto right = unary();
        return std::make_shared<const BinaryExpression>(joined(left->span, right->span), BinaryOperator::Power,
                                                        std::move(left), std::move(right));
    }

    [[nodiscard]] ExprPtr postfix()
    {
        auto result = primary();
        for (;;) {
            if (match(TokenKind::Dot)) {
                const auto& member = consume(TokenKind::Identifier, "expected member name after '.'");
                result = std::make_shared<const MemberExpression>(joined(result->span, member.span),
                                                                  std::move(result), member.lexeme);
                continue;
            }
            if (match(TokenKind::LeftParen)) {
                result = finish_call(std::move(result), previous());
                continue;
            }
            if (match(TokenKind::LeftBracket)) {
                result = finish_subscript(std::move(result), previous());
                continue;
            }
            return result;
        }
    }

    [[nodiscard]] ExprPtr finish_call(ExprPtr callee, const Token&)
    {
        std::vector<CallArgument> arguments;
        std::unordered_set<std::string> names;
        bool saw_named = false;
        if (!check(TokenKind::RightParen)) {
            do {
                std::optional<std::string> name;
                SourceLocation begin = peek().span.begin;
                if (check(TokenKind::Identifier) && check_next(TokenKind::Equal)) {
                    const auto& name_token = advance();
                    advance();
                    name = name_token.lexeme;
                    saw_named = true;
                    if (!names.insert(*name).second) {
                        error("PAR006", "duplicate named argument '" + *name + "'", name_token.span);
                    }
                } else if (saw_named) {
                    error("PAR007", "positional argument cannot follow a named argument", peek().span);
                }
                auto value = expression();
                arguments.push_back({std::move(name), std::move(value), {begin, begin}});
                arguments.back().span.end = arguments.back().value->span.end;
            } while (match(TokenKind::Comma) && !check(TokenKind::RightParen));
        }
        const auto& close = consume(TokenKind::RightParen, "expected ')' after arguments");
        return std::make_shared<const CallExpression>(joined(callee->span, close.span), std::move(callee),
                                                      std::move(arguments));
    }

    [[nodiscard]] ExprPtr finish_subscript(ExprPtr object, const Token&)
    {
        std::optional<ExprPtr> first;
        if (!check(TokenKind::Colon) && !check(TokenKind::RightBracket)) first = expression();
        if (!match(TokenKind::Colon)) {
            if (!first) {
                error("PAR002", "expected index expression", peek().span);
                throw ParseFailure{};
            }
            const auto& close = consume(TokenKind::RightBracket, "expected ']' after index");
            return std::make_shared<const IndexExpression>(joined(object->span, close.span), std::move(object),
                                                           std::move(*first));
        }
        std::optional<ExprPtr> stop;
        std::optional<ExprPtr> step;
        if (!check(TokenKind::Colon) && !check(TokenKind::RightBracket)) stop = expression();
        if (match(TokenKind::Colon)) {
            if (!check(TokenKind::RightBracket)) step = expression();
        }
        const auto& close = consume(TokenKind::RightBracket, "expected ']' after slice");
        return std::make_shared<const SliceExpression>(joined(object->span, close.span), std::move(object),
                                                       std::move(first), std::move(stop), std::move(step));
    }

    [[nodiscard]] ExprPtr primary()
    {
        if (match(TokenKind::Null)) return std::make_shared<const LiteralExpression>(previous().span, std::monostate{});
        if (match(TokenKind::True)) return std::make_shared<const LiteralExpression>(previous().span, true);
        if (match(TokenKind::False)) return std::make_shared<const LiteralExpression>(previous().span, false);
        if (match(TokenKind::String)) return std::make_shared<const LiteralExpression>(previous().span, previous().value);
        if (match(TokenKind::Integer)) {
            const auto token = previous();
            std::int64_t value{};
            const auto conversion = std::from_chars(token.lexeme.data(), token.lexeme.data() + token.lexeme.size(), value);
            if (conversion.ec != std::errc{}) error("PAR018", "integer literal is outside int64 range", token.span);
            return std::make_shared<const LiteralExpression>(token.span, value);
        }
        if (match(TokenKind::Float)) {
            const auto token = previous();
            char* end = nullptr;
            const double value = std::strtod(token.lexeme.c_str(), &end);
            if (end != token.lexeme.c_str() + token.lexeme.size() || !std::isfinite(value)) {
                error("PAR019", "invalid finite float literal", token.span);
            }
            return std::make_shared<const LiteralExpression>(token.span, value);
        }
        if (match(TokenKind::Identifier)) {
            return std::make_shared<const IdentifierExpression>(previous().span, previous().lexeme);
        }
        if (match(TokenKind::LeftParen)) {
            auto value = expression();
            consume(TokenKind::RightParen, "expected ')' after expression");
            return value;
        }
        if (match(TokenKind::LeftBracket)) return list_literal(previous());
        if (match(TokenKind::LeftBrace)) return map_literal(previous());
        if (match(TokenKind::Fn)) return function_expression(previous(), false);
        if (match(TokenKind::Async)) {
            const auto start = previous();
            consume(TokenKind::Fn, "expected 'fn' after 'async'");
            return function_expression(start, true);
        }
        error("PAR002", "expected expression", peek().span);
        throw ParseFailure{};
    }

    [[nodiscard]] ExprPtr list_literal(const Token& open)
    {
        std::vector<ExprPtr> elements;
        if (!check(TokenKind::RightBracket)) {
            do { elements.push_back(expression()); }
            while (match(TokenKind::Comma) && !check(TokenKind::RightBracket));
        }
        const auto& close = consume(TokenKind::RightBracket, "expected ']' after list literal");
        return std::make_shared<const ListExpression>(joined(open.span, close.span), std::move(elements));
    }
    [[nodiscard]] ExprPtr map_literal(const Token& open)
    {
        std::vector<MapEntry> entries;
        if (!check(TokenKind::RightBrace)) {
            do {
                auto key = expression();
                consume(TokenKind::Colon, "expected ':' after map key");
                auto value = expression();
                const auto span = joined(key->span, value->span);
                entries.push_back({std::move(key), std::move(value), span});
            } while (match(TokenKind::Comma) && !check(TokenKind::RightBrace));
        }
        const auto& close = consume(TokenKind::RightBrace, "expected '}' after map literal");
        return std::make_shared<const MapExpression>(joined(open.span, close.span), std::move(entries));
    }
    [[nodiscard]] ExprPtr function_expression(const Token& start, const bool is_async)
    {
        auto params = parameters();
        auto body = function_body(is_async);
        return std::make_shared<const FunctionExpression>(joined(start.span, body->span), is_async,
                                                          std::move(params), std::move(body));
    }

    const std::vector<Token>& tokens_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t current_{0};
    int loop_depth_{0};
    int function_depth_{0};
    bool in_async_function_{false};
};

}  // namespace

bool ParseResult::has_errors() const noexcept
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens))
{
    if (tokens_.empty() || tokens_.back().kind != TokenKind::EndOfFile) {
        SourceLocation end{};
        if (!tokens_.empty()) end = tokens_.back().span.end;
        tokens_.push_back({TokenKind::EndOfFile, {}, {}, {end, end}});
    }
}

ParseResult Parser::parse_program()
{
    return Engine(tokens_).run();
}

ParseResult parse(const std::string_view source)
{
    auto lexed = lex(source);
    return Engine(lexed.tokens, std::move(lexed.diagnostics)).run();
}

}  // namespace baas::script
