#include "script/SyntaxCheck.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool has_code(const baas::script::SyntaxCheckResult& result, const std::string_view code)
{
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

void test_valid_program_reaches_semantics()
{
    const auto result = baas::script::check_source(R"(
fn factorial(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}
let answer = factorial(5);
)");
    check(!result.has_errors(), "valid recursive program should pass all stages");
    check(result.visited_ast_nodes > 0, "valid program should reach semantic analysis");
}

void test_lexical_and_parse_errors_skip_semantics()
{
    const auto bom = baas::script::check_source("\xef\xbb\xbflet value = 1;");
    check(bom.has_errors() && has_code(bom, "LEX006"), "BOM should retain stable lexer diagnostic");
    check(bom.visited_ast_nodes == 0, "lexical error should skip semantic analysis");

    const auto parse = baas::script::check_source("let value = ;");
    check(parse.has_errors() && has_code(parse, "PAR002"), "invalid expression should report parser code");
    check(parse.visited_ast_nodes == 0, "parse error should skip semantic analysis");
}

void test_semantic_errors_and_limits()
{
    const auto unknown = baas::script::check_source("let value = missing;");
    check(unknown.has_errors() && has_code(unknown, "SEM001"), "unknown name should reach semantic diagnostics");
    check(unknown.visited_ast_nodes > 0, "semantic failure should record visited nodes");

    baas::script::SyntaxCheckOptions options;
    options.semantic.max_ast_nodes = 1;
    const auto limited = baas::script::check_source("let a = 1; let b = 2;", options);
    check(limited.has_errors() && has_code(limited, "SEM006"), "AST budget should be configurable");
}

}  // namespace

int main()
{
    test_valid_program_reaches_semantics();
    test_lexical_and_parse_errors_skip_semantics();
    test_semantic_errors_and_limits();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All syntax check tests passed\n";
    return EXIT_SUCCESS;
}
