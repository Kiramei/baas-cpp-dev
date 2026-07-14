#include "script/Ast.h"
#include "script/Parser.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace baas::script;
using namespace baas::script::ast;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool has_code(const ParseResult& result, const std::string_view code)
{
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

const Expression* expression_at(const ParseResult& result, const std::size_t index)
{
    const auto* statement = dynamic_cast<const ExpressionStatement*>(result.program.statements.at(index).get());
    return statement ? statement->expression.get() : nullptr;
}

void test_complete_program_and_ast_ownership()
{
    auto result = parse(std::string{
        "import \"baas/log\" as log;\n"
        "let ordered = {\"first\": 1, \"second\": [2, 3]};\n"
        "fn make_counter(start = 0) {\n"
        "  let value = start;\n"
        "  return fn(step = 1) { value += step; return value; };\n"
        "}\n"
        "async fn run(items, delay = 0.1) {\n"
        "  defer log.info(\"finished\");\n"
        "  for (item in items) {\n"
        "    if (item < 0) { continue; } else { ordered[item] = item * 2; }\n"
        "  }\n"
        "  while (delay > 0) { break; }\n"
        "  try { await log.flush(delay); } catch (error) { throw error; }\n"
        "  return ordered[0:10:2];\n"
        "}\n"
        "let task = run(items = ordered, delay = 1.0);\n"
    });

    check(!result.has_errors(), "complete representative program should parse");
    check(result.program.statements.size() == 5, "complete program should retain every top-level statement");
    check(result.program.span.begin.byte_offset == 0, "program should start at its first statement");
    check(result.program.span.end.byte_offset > 100, "program should own a full source span");

    const auto* import = dynamic_cast<const ImportStatement*>(result.program.statements[0].get());
    check(import && import->module == "baas/log" && import->alias == "log",
          "import AST should own decoded module and alias strings");
    const auto* declaration = dynamic_cast<const FunctionDeclaration*>(result.program.statements[2].get());
    check(declaration && declaration->parameters.size() == 1 && declaration->body->statements.size() == 2,
          "function declaration should own parameters and body");
    const auto* async_declaration = dynamic_cast<const FunctionDeclaration*>(result.program.statements[3].get());
    check(async_declaration && async_declaration->is_async,
          "async declaration should be represented explicitly");
}

void test_precedence_and_composite_operators()
{
    const auto result = parse(
        "a = b or c and d == e not in f + g * -h ** 2;\n"
        "x not is null;\n"
        "2 ** 3 ** 2;\n"
        "-2 ** 2;\n"
        "root.child[1:9:2](value, mode = \"fast\");\n"
    );
    check(!result.has_errors(), "precedence fixture should parse");

    const auto* assignment = dynamic_cast<const AssignmentExpression*>(expression_at(result, 0));
    check(assignment && assignment->operation == AssignmentOperator::Assign,
          "assignment should be the lowest-precedence root");
    const auto* disjunction = assignment ? dynamic_cast<const BinaryExpression*>(assignment->value.get()) : nullptr;
    check(disjunction && disjunction->operation == BinaryOperator::Or,
          "or should bind more tightly than assignment");
    const auto* conjunction = disjunction ? dynamic_cast<const BinaryExpression*>(disjunction->right.get()) : nullptr;
    check(conjunction && conjunction->operation == BinaryOperator::And,
          "and should bind more tightly than or");
    const auto* equality = conjunction ? dynamic_cast<const BinaryExpression*>(conjunction->right.get()) : nullptr;
    check(equality && equality->operation == BinaryOperator::Equal,
          "equality should bind more tightly than and");
    const auto* membership = equality ? dynamic_cast<const BinaryExpression*>(equality->right.get()) : nullptr;
    check(membership && membership->operation == BinaryOperator::NotIn,
          "'not in' should form one ordering operator");

    const auto* identity = dynamic_cast<const BinaryExpression*>(expression_at(result, 1));
    check(identity && identity->operation == BinaryOperator::NotIs,
          "'not is' should form one identity operator as specified");
    const auto* outer_power = dynamic_cast<const BinaryExpression*>(expression_at(result, 2));
    const auto* inner_power = outer_power ? dynamic_cast<const BinaryExpression*>(outer_power->right.get()) : nullptr;
    check(outer_power && outer_power->operation == BinaryOperator::Power && inner_power &&
              inner_power->operation == BinaryOperator::Power,
          "power should associate to the right");
    const auto* unary = dynamic_cast<const UnaryExpression*>(expression_at(result, 3));
    check(unary && dynamic_cast<const BinaryExpression*>(unary->operand.get()),
          "power should bind more tightly than unary minus");

    const auto* call = dynamic_cast<const CallExpression*>(expression_at(result, 4));
    check(call && call->arguments.size() == 2 && !call->arguments[0].name &&
              call->arguments[1].name == std::optional<std::string>{"mode"},
          "postfix chain and mixed positional/named call arguments should parse");
    const auto* slice = call ? dynamic_cast<const SliceExpression*>(call->callee.get()) : nullptr;
    check(slice && slice->start && slice->stop && slice->step,
          "three-component slice should preserve every optional bound");
}

void test_unicode_spans()
{
    const auto result = parse("let 名称 = {\"键\": 名称};\n名称.属性[0];");
    check(!result.has_errors(), "Unicode identifiers should parse");
    const auto* binding = dynamic_cast<const LetStatement*>(result.program.statements[0].get());
    check(binding && binding->name == "名称", "AST must own UTF-8 identifier bytes");
    check(binding && binding->span.begin == SourceLocation{0, 1, 1},
          "Unicode statement span should begin at byte zero and column one");
    check(binding && binding->span.end == SourceLocation{29, 1, 20},
          "Unicode statement end should use byte offsets and scalar columns");
    const auto* index = dynamic_cast<const IndexExpression*>(expression_at(result, 1));
    const auto* member = index ? dynamic_cast<const MemberExpression*>(index->object.get()) : nullptr;
    check(member && member->member == "属性" && member->span.begin == SourceLocation{30, 2, 1},
          "member chain should retain exact Unicode start span");
}

void test_error_recovery_and_stable_diagnostics()
{
    const auto result = parse(
        "let a = ;\n"
        "let b = 2;\n"
        "fn bad(a = 1, b, a) { return b; }\n"
        "call(x = 1, x = 2, 3);\n"
        "(a + b) = 4;\n"
        "let tail = 5;\n"
    );
    check(result.has_errors(), "invalid program should report syntax errors");
    check(has_code(result, "PAR002"), "missing expression should use PAR002");
    check(has_code(result, "PAR005"), "default parameter ordering should use PAR005");
    check(has_code(result, "PAR004"), "duplicate parameter should use PAR004");
    check(has_code(result, "PAR006"), "duplicate named argument should use PAR006");
    check(has_code(result, "PAR007"), "positional-after-named should use PAR007");
    check(has_code(result, "PAR003"), "invalid assignment target should use PAR003");
    check(result.program.statements.size() >= 5,
          "panic recovery should retain valid statements after independent failures");
    const auto* tail = dynamic_cast<const LetStatement*>(result.program.statements.back().get());
    check(tail && tail->name == "tail", "panic recovery must reach the final statement");
    for (const auto& diagnostic : result.diagnostics) {
        check(!diagnostic.code.empty() && diagnostic.span.end.byte_offset >= diagnostic.span.begin.byte_offset,
              "every diagnostic should carry a stable code and ordered span");
    }
}

void test_control_and_function_context()
{
    const auto invalid = parse(
        "break; continue; return 1; await work(); defer cleanup();\n"
        "while (true) { fn nested() { break; } }\n"
        "async fn outer() { fn sync_nested() { await work(); } await work(); return; }\n"
    );
    check(has_code(invalid, "PAR008"), "break outside the current function's loop should be diagnosed");
    check(has_code(invalid, "PAR009"), "continue outside a loop should be diagnosed");
    check(has_code(invalid, "PAR010"), "top-level return should be diagnosed");
    check(has_code(invalid, "PAR011"), "await outside the current async function should be diagnosed");
    check(has_code(invalid, "PAR014"), "top-level defer should be diagnosed");

    int break_errors = 0;
    int await_errors = 0;
    for (const auto& diagnostic : invalid.diagnostics) {
        if (diagnostic.code == "PAR008") ++break_errors;
        if (diagnostic.code == "PAR011") ++await_errors;
    }
    check(break_errors == 2, "loop context must not leak into a nested function");
    check(await_errors == 2, "async context must not leak into a nested sync function");

    const auto valid = parse(
        "fn sync() { while (true) { break; continue; } defer cleanup(); return 1; }\n"
        "async fn asynchronous() { await work(); return; }\n"
    );
    check(!valid.has_errors(), "valid loop, return, defer and await contexts should parse");

    const auto recovered = parse("while (true) let = ; break; let tail = 1;");
    check(has_code(recovered, "PAR008"),
          "failed loop bodies must restore loop context before panic recovery continues");
    const auto* tail = dynamic_cast<const LetStatement*>(recovered.program.statements.back().get());
    check(tail && tail->name == "tail", "context recovery should still reach subsequent declarations");
}

void test_lexer_diagnostics_are_preserved()
{
    std::string source = "let ok = 1; ";
    source.push_back(static_cast<char>(0xc0));
    source += " let after = 2;";
    const auto result = parse(source);
    check(has_code(result, "LEX001"), "parse convenience API should preserve lexer diagnostics");
    const auto* last = dynamic_cast<const LetStatement*>(result.program.statements.back().get());
    check(last && last->name == "after", "parser should consume recovered lexer output through EOF");
}

}  // namespace

int main()
{
    test_complete_program_and_ast_ownership();
    test_precedence_and_composite_operators();
    test_unicode_spans();
    test_error_recovery_and_stable_diagnostics();
    test_control_and_function_context();
    test_lexer_diagnostics_are_preserved();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All parser tests passed\n";
    return EXIT_SUCCESS;
}
