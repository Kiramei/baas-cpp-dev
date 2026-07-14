#include "script/Parser.h"
#include "script/SemanticAnalyzer.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
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

bool has_code(const SemanticResult& result, const std::string_view code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code;
    });
}

const BindingInfo* binding_named(const SemanticResult& result, const std::string_view name,
                                 const BindingKind kind)
{
    const auto found = std::find_if(result.bindings.begin(), result.bindings.end(), [&](const BindingInfo& binding) {
        return binding.name == name && binding.kind == kind;
    });
    return found == result.bindings.end() ? nullptr : &*found;
}

std::vector<const ReferenceResolution*> references_named(const SemanticResult& result,
                                                         const std::string_view name)
{
    std::vector<const ReferenceResolution*> references;
    for (const auto& reference : result.references) {
        if (reference.reference && reference.reference->name == name) references.push_back(&reference);
    }
    return references;
}

void test_success_bindings_and_recursion()
{
    const auto parsed = parse(
        "import \"baas/log\" as log;\n"
        "let values = [1, 2, 3];\n"
        "fn factorial(n) { if (n <= 1) { return 1; } return n * factorial(n - 1); }\n"
        "fn defaults(first = 1, second = first) { return second; }\n"
        "for (item in values) { log.info(item); }\n"
        "try { log.info(factorial(3)); } catch (error) { log.info(error); }\n"
    );
    check(!parsed.has_errors(), "success semantic fixture must parse");
    const auto result = analyze_semantics(parsed.program);
    check(!result.has_errors(), "valid bindings, recursion, loop and catch uses should analyze");
    check(binding_named(result, "log", BindingKind::Import) != nullptr,
          "import alias should create an initialized module binding");
    check(binding_named(result, "item", BindingKind::For) != nullptr,
          "for binding should be represented in semantic output");
    check(binding_named(result, "error", BindingKind::Catch) != nullptr,
          "catch binding should be represented in semantic output");

    const auto* factorial = binding_named(result, "factorial", BindingKind::Function);
    const auto recursive = references_named(result, "factorial");
    check(factorial && recursive.size() == 2,
          "function declaration should be visible to its body and later statements");
    check(factorial && std::all_of(recursive.begin(), recursive.end(), [&](const auto* reference) {
              return reference->binding == factorial->id;
          }), "recursive and later function reads should resolve to the declaration binding");
}

void test_unknown_duplicate_and_initialization_errors()
{
    const auto parsed = parse(
        "let self = self;\n"
        "let known = 1;\n"
        "let known = 2;\n"
        "missing = known;\n"
        "import \"baas/log\" as known;\n"
    );
    const auto result = analyze_semantics(parsed.program);
    check(has_code(result, semantic_diagnostic_code::uninitialized_read),
          "self initializer read should report SEM003");
    check(has_code(result, semantic_diagnostic_code::duplicate_declaration),
          "same-scope duplicate declarations should report SEM002");
    check(has_code(result, semantic_diagnostic_code::unknown_name),
          "unknown assignment target should report SEM001");
    for (const auto& diagnostic : result.diagnostics) {
        check(!diagnostic.code.empty() && diagnostic.span.end.byte_offset >= diagnostic.span.begin.byte_offset,
              "semantic diagnostics should carry stable codes and ordered spans");
    }

    const auto duplicate_parameters = parse("fn invalid(a, a) { return a; }");
    const auto parameter_result = analyze_semantics(duplicate_parameters.program);
    check(has_code(parameter_result, "SEM004"), "duplicate parameters should report SEM004 independently of parser checks");
}

void test_unicode_shadowing_and_nearest_assignment()
{
    const auto parsed = parse(
        "let 名称 = 1;\n"
        "{ let 名称 = 2; 名称 = 3; }\n"
        "名称 = 4;\n"
        "不存在;\n"
    );
    const auto result = analyze_semantics(parsed.program);
    check(has_code(result, "SEM001"), "unknown Unicode identifier should be diagnosed");
    check(result.diagnostics.back().span.begin == SourceLocation{60, 4, 1},
          "Unicode semantic diagnostic should preserve byte-based span and scalar column");

    std::vector<BindingId> unicode_bindings;
    for (const auto& binding : result.bindings) {
        if (binding.name == "名称") unicode_bindings.push_back(binding.id);
    }
    check(unicode_bindings.size() == 2, "nested Unicode let should legally shadow the outer binding");
    const auto references = references_named(result, "名称");
    check(references.size() == 2 && references[0]->binding == unicode_bindings[1] &&
              references[1]->binding == unicode_bindings[0],
          "assignments should resolve to the nearest lexical binding then restore the outer binding");
    check(references.size() == 2 &&
              result.resolution_for(*references[0]->reference) == references[0],
          "public reference lookup should return compiler-ready binding metadata");
    check(references.size() == 2 && references[0]->is_write && !references[0]->requires_read,
          "plain assignment should be recorded as a write without an implicit read");
}

void test_closure_capture_propagation_and_nested_recursion()
{
    const auto parsed = parse(
        "fn outer(seed) {\n"
        "  let value = seed;\n"
        "  fn middle() {\n"
        "    fn recursive(n) { if (n <= 0) { return value; } return recursive(n - 1); }\n"
        "    return recursive;\n"
        "  }\n"
        "  return middle;\n"
        "}\n"
    );
    const auto result = analyze_semantics(parsed.program);
    check(!result.has_errors(), "nested closures and local recursion should resolve");
    check(result.functions.size() == 3, "outer, middle and recursive functions should have metadata");
    const auto* value = binding_named(result, "value", BindingKind::Let);
    const auto* recursive = binding_named(result, "recursive", BindingKind::Function);
    check(value && value->captured, "outer local read by a nested function should be marked captured");
    check(recursive && recursive->captured,
          "nested named function should capture its containing-scope self binding for recursion");

    const auto& middle_info = result.functions[1];
    const auto& recursive_info = result.functions[2];
    check(value && std::find(middle_info.captures.begin(), middle_info.captures.end(), value->id) !=
                       middle_info.captures.end(),
          "capture should propagate through an intermediate closure that creates the reader");
    check(value && std::find(recursive_info.captures.begin(), recursive_info.captures.end(), value->id) !=
                       recursive_info.captures.end(),
          "directly reading nested function should capture the outer local");
    check(recursive && std::find(recursive_info.captures.begin(), recursive_info.captures.end(), recursive->id) !=
                           recursive_info.captures.end(),
          "local recursive call should resolve through explicit capture metadata");
}

void test_declaration_order_is_conservative()
{
    const auto parsed = parse("later(); fn later() { return; }");
    const auto result = analyze_semantics(parsed.program);
    check(has_code(result, "SEM001"),
          "draft does not specify hoisting, so a function is unavailable before its declaration");
    const auto* binding = binding_named(result, "later", BindingKind::Function);
    check(binding && binding->initialized, "function declaration should still bind before analyzing its own body");
}

void test_node_and_depth_limits()
{
    const auto many = parse("let a = 1; let b = a; let c = b; let d = c;");
    const auto node_limited = analyze_semantics(many.program, SemanticOptions{3, 256});
    check(has_code(node_limited, "SEM006"), "node budget exhaustion should report SEM006");
    check(node_limited.visited_ast_nodes == 3, "visited node count should stop exactly at the configured budget");

    std::string nested;
    for (int index = 0; index < 40; ++index) nested += "{";
    nested += "let deep = 1;";
    for (int index = 0; index < 40; ++index) nested += "}";
    const auto deep = parse(nested);
    check(!deep.has_errors(), "deep-limit fixture should be syntactically valid");
    const auto depth_limited = analyze_semantics(deep.program, SemanticOptions{1000, 8});
    check(has_code(depth_limited, "SEM007"), "nesting budget exhaustion should report SEM007 without crashing");
    check(depth_limited.diagnostics.size() == 1, "a rejected deep subtree should produce one stable limit diagnostic");
}

void test_malformed_ast_is_rejected_safely()
{
    const SourceSpan span{{0, 1, 1}, {2, 1, 3}};
    StmtPtr declaration = std::make_shared<const FunctionDeclaration>(
        span, "broken", false, std::vector<Parameter>{}, nullptr);
    const Program program{{std::move(declaration)}, span};
    const auto result = analyze_semantics(program);
    check(has_code(result, semantic_diagnostic_code::malformed_ast),
          "missing required function body should report SEM008 instead of dereferencing null");
}

}  // namespace

int main()
{
    test_success_bindings_and_recursion();
    test_unknown_duplicate_and_initialization_errors();
    test_unicode_shadowing_and_nearest_assignment();
    test_closure_capture_propagation_and_nested_recursion();
    test_declaration_order_is_conservative();
    test_node_and_depth_limits();
    test_malformed_ast_is_rejected_safely();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All semantic analyzer tests passed\n";
    return EXIT_SUCCESS;
}
