#include "script/runtime/SynchronousEvaluator.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace runtime = baas::script::runtime;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::int64_t integer_export(
    runtime::SynchronousEvaluator& evaluator,
    const std::string_view module,
    const std::string_view name)
{
    return evaluator.module_export(module, name).as_integer();
}

template <typename Function>
runtime::EvaluationError expect_error(
    const runtime::LanguageErrorCode expected,
    Function&& function,
    const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const runtime::EvaluationError& error) {
        check(error.code() == expected, message);
        return error;
    }
    return {expected, "missing expected evaluator error", {}, {}, 0};
}

void test_values_collections_operators_and_short_circuit()
{
    runtime::SynchronousEvaluator evaluator({{
        "main",
        "let calls = 0;\n"
        "fn mark() { calls += 1; return true; }\n"
        "let skipped_and = false and mark();\n"
        "let skipped_or = true or mark();\n"
        "let values = [1, 2, 3];\n"
        "values[1] = 5;\n"
        "let picked = values[0:3:2];\n"
        "let ordered = {\"first\": 1, \"second\": 2, \"first\": 3};\n"
        "ordered.second += 5;\n"
        "ordered[\"third\"] = 11;\n"
        "let text = \"甲乙丙\";\n"
        "let scalar = text[1];\n"
        "let text_slice = text[0:3:2];\n"
        "let contains = 5 in values and \"乙\" in text and \"third\" in ordered;\n"
        "let exact_order = 9007199254740993 > 9007199254740992;\n"
        "let result = values[1] + ordered.first + ordered.second + ordered.third;\n",
    }});
    const auto result = evaluator.execute("main");
    check(evaluator.heap().kind(result.module_namespace) == runtime::ValueKind::Module,
          "execution must publish a module namespace value");
    check(integer_export(evaluator, "main", "calls") == 0,
          "and/or must skip the unselected operand and preserve side effects");
    check(integer_export(evaluator, "main", "result") == 26,
          "list and ordered-map access and mutation must compose deterministically");
    check(evaluator.module_export("main", "contains").as_boolean(),
          "list, map and string membership must use the specified domains");
    check(evaluator.module_export("main", "exact_order").as_boolean(),
          "same-kind integer ordering must not lose precision through float conversion");

    const auto picked = evaluator.heap().list_values(
        evaluator.module_export("main", "picked").as_heap_ref());
    check(picked == std::vector<runtime::Value>{runtime::Value(std::int64_t{1}), runtime::Value(std::int64_t{3})},
          "list slicing must retain selected values in order");
    check(evaluator.heap().string_copy(
              evaluator.module_export("main", "scalar").as_heap_ref()) == "乙",
          "string indexing must operate on Unicode scalar values");
    check(evaluator.heap().string_copy(
              evaluator.module_export("main", "text_slice").as_heap_ref()) == "甲丙",
          "string slicing must preserve complete UTF-8 scalars");

    const auto entries = evaluator.heap().map_entries(
        evaluator.module_export("main", "ordered").as_heap_ref());
    check(entries.size() == 3 && entries[0].first == "first"
              && entries[1].first == "second" && entries[2].first == "third",
          "map update must retain first insertion position and append new keys");
}

void test_control_flow_closures_defaults_and_recursion()
{
    runtime::SynchronousEvaluator evaluator({{
        "main",
        "fn factorial(n) {\n"
        "  if (n <= 1) { return 1; }\n"
        "  return n * factorial(n - 1);\n"
        "}\n"
        "fn make_counter(start = 0) {\n"
        "  let value = start;\n"
        "  return fn(step = 1) { value += step; return value; };\n"
        "}\n"
        "fn defaults(first = 2, second = first + 3) { return second; }\n"
        "let counter = make_counter(10);\n"
        "let first = counter();\n"
        "let second = counter(step = 4);\n"
        "let total = 0;\n"
        "for (item in [1, 2, 3, 4, 5]) {\n"
        "  if (item == 2) { continue; }\n"
        "  if (item == 5) { break; }\n"
        "  total += item;\n"
        "}\n"
        "let index = 0;\n"
        "while (index < 3) { index += 1; }\n"
        "let result = factorial(6) + first + second + total + index + defaults();\n",
    }});
    static_cast<void>(evaluator.execute("main"));
    check(integer_export(evaluator, "main", "first") == 11
              && integer_export(evaluator, "main", "second") == 15,
          "closures must capture one shared mutable lexical binding cell");
    check(integer_export(evaluator, "main", "total") == 8,
          "for/break/continue must target the nearest loop deterministically");
    check(integer_export(evaluator, "main", "result") == 762,
          "recursion, defaults, closure calls and loops must execute as one program");
    check(evaluator.stats().peak_call_depth >= 6,
          "recursive calls must be reflected in deterministic evaluator stats");
}

void test_multi_module_cache_and_namespace_calls()
{
    std::vector<runtime::SourceModule> modules{
        {"tasks/main",
         "import \"tasks/common\" as first;\n"
         "import \"tasks/common\" as second;\n"
         "let same = first is second;\n"
         "let result = first.scale(14) + second.initialized;\n"},
        {"tasks/common",
         "let initialized = 0;\n"
         "initialized += 1;\n"
         "let factor = 3;\n"
         "fn scale(value) { return value * factor; }\n"
         "let _private = 99;\n"},
    };
    runtime::SynchronousEvaluator evaluator(std::move(modules));
    static_cast<void>(evaluator.execute("tasks/main"));
    check(integer_export(evaluator, "tasks/main", "result") == 43,
          "imported closure calls must resolve through the dependency module environment");
    check(evaluator.module_export("tasks/main", "same").as_boolean(),
          "re-import must return the same cached namespace identity");
    check(evaluator.stats().initialized_modules == 2,
          "each package module must initialize at most once per evaluator context");
    expect_error(runtime::LanguageErrorCode::ModuleMemberMissing,
                 [&] { static_cast<void>(evaluator.module_export("tasks/common", "_private")); },
                 "private module bindings must not be exported");
}

void test_module_failure_cache_and_lazy_initialization()
{
    runtime::SynchronousEvaluator evaluator({
        {"entry",
         "import \"broken\" as broken;\n"
         "let result = broken.value;\n"},
        {"broken",
         "let counted_side_effect = 0;\n"
         "counted_side_effect += 1;\n"
         "let value = 1 / 0;\n"},
        {"unused", "let value = 99;\n"},
    });

    const auto first = expect_error(
        runtime::LanguageErrorCode::DivisionByZero,
        [&] { static_cast<void>(evaluator.execute("entry")); },
        "a dependency initialization failure must retain its stable dynamic error");
    const auto after_first = evaluator.stats();
    check(after_first.steps > 0 && after_first.initialized_modules == 0,
          "failed initialization must execute observable work without publishing a module");

    const auto second = expect_error(
        runtime::LanguageErrorCode::DivisionByZero,
        [&] { static_cast<void>(evaluator.execute("entry")); },
        "re-execution must rethrow the cached dependency failure");
    const auto after_second = evaluator.stats();
    check(second.code() == first.code() && second.module() == first.module()
              && second.span() == first.span() && second.steps() == first.steps(),
          "cached module failure must preserve code, module, span and failure step");
    check(after_second.steps == after_first.steps
              && after_second.initialized_modules == after_first.initialized_modules,
          "cached module failure must not repeat top-level work or publish modules");
    expect_error(runtime::LanguageErrorCode::ModuleInitializationFailed,
                 [&] { static_cast<void>(evaluator.module_export("unused", "value")); },
                 "an unimported package module must remain lazy and unpublished");
    check(evaluator.stats().initialized_modules == 0,
          "observing an uninitialized module must not initialize it implicitly");
}

void test_constructive_two_counter_program()
{
    runtime::SynchronousEvaluator evaluator({{
        "machine",
        "fn run(input) {\n"
        "  let counter_a = 0;\n"
        "  let counter_b = input;\n"
        "  while (counter_b > 0) { counter_a += 1; counter_b -= 1; }\n"
        "  while (counter_a > 0) { counter_a -= 1; counter_b += 2; }\n"
        "  return counter_b;\n"
        "}\n"
        "let result = run(12);\n",
    }});
    static_cast<void>(evaluator.execute("machine"));
    check(integer_export(evaluator, "machine", "result") == 24,
          "two mutable counters must support increment, decrement and zero-test loops");
}

void test_deterministic_limits_and_runtime_errors()
{
    runtime::EvaluatorLimits limits;
    limits.max_steps = 50;
    runtime::SynchronousEvaluator looping({{
        "main", "let value = 0; while (true) { value += 1; }\n"}}, limits);
    const auto instruction = expect_error(
        runtime::LanguageErrorCode::InstructionLimitExceeded,
        [&] { static_cast<void>(looping.execute("main")); },
        "an unbounded loop must stop at the exact step budget");
    check(instruction.steps() == 50 && !instruction.catchable(),
          "instruction exhaustion must expose stable terminal metadata");

    limits = {};
    limits.max_call_depth = 8;
    runtime::SynchronousEvaluator recursive({{
        "main",
        "fn descend(n) { return descend(n + 1); }\n"
        "let result = descend(0);\n"}}, limits);
    expect_error(runtime::LanguageErrorCode::StackLimitExceeded,
                 [&] { static_cast<void>(recursive.execute("main")); },
                 "recursive calls must obey the call-frame budget");

    limits = {};
    limits.max_value_stack = 3;
    runtime::SynchronousEvaluator deep_expression({{
        "main", "let result = not not not not false;\n"}}, limits);
    expect_error(runtime::LanguageErrorCode::StackLimitExceeded,
                 [&] { static_cast<void>(deep_expression.execute("main")); },
                 "nested AST evaluation must obey the value-stack budget");

    limits = {};
    limits.max_container_elements = 3;
    runtime::SynchronousEvaluator large_collection({{
        "main", "let values = [1, 2, 3, 4];\n"}}, limits);
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
                 [&] { static_cast<void>(large_collection.execute("main")); },
                 "collection construction must obey the container-element budget");

    runtime::HeapLimits heap_limits;
    heap_limits.max_string_bytes = 3;
    runtime::SynchronousEvaluator heap_limited(
        {{"main", "let text = \"abcd\";\n"}}, {}, heap_limits);
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
                 [&] { static_cast<void>(heap_limited.execute("main")); },
                 "heap RuntimeError limits must translate through stable language errors");
}

void test_source_and_expanded_collection_preflights()
{
    const std::string source = "let value = 1;\n";
    runtime::EvaluatorLimits limits;
    limits.max_module_source_bytes = source.size();
    limits.max_total_source_bytes = source.size();
    runtime::SynchronousEvaluator exact({{"main", source}}, limits);
    static_cast<void>(exact.execute("main"));
    check(integer_export(exact, "main", "value") == 1,
          "source byte gates must admit the exact per-module and package boundary");

    limits.max_module_source_bytes = source.size() - 1;
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
                 [&] {
                     runtime::SynchronousEvaluator over({{"main", source}}, limits);
                 },
                 "per-module source bytes must fail before parsing over the boundary");

    limits = {};
    limits.max_module_source_bytes = source.size();
    limits.max_total_source_bytes = source.size() * 2 - 1;
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
                 [&] {
                     runtime::SynchronousEvaluator total(
                         {{"a", source}, {"b", source}}, limits);
                 },
                 "aggregate source bytes must use an overflow-safe pre-parse gate");

    limits = {};
    limits.max_module_source_bytes = 3;
    limits.max_total_source_bytes = 3;
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
                 [&] {
                     runtime::SynchronousEvaluator invalid({{"main", "@@@@"}}, limits);
                 },
                 "source size rejection must win before lexer/parser allocation or diagnostics");

    limits = {};
    limits.max_container_elements = 3;
    runtime::SynchronousEvaluator concatenated({{
        "main", "let result = \"ab\" + \"cd\";\n"}}, limits);
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
                 [&] { static_cast<void>(concatenated.execute("main")); },
                 "string concatenation must preflight its expanded scalar count");
    check(concatenated.stats().collection_work == 4,
          "failed concatenation must not charge or allocate the rejected expansion");

    limits = {};
    limits.max_container_elements = 4;
    limits.max_collection_work = 7;
    runtime::SynchronousEvaluator indexed({{
        "main", "let text = \"abcd\"; let scalar = text[0];\n"}}, limits);
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
                 [&] { static_cast<void>(indexed.execute("main")); },
                 "string indexing must charge traversal before scalar-range allocation");
    check(indexed.stats().collection_work == 4,
          "failed string indexing must leave collection work at the admitted literal");

    runtime::SynchronousEvaluator iterated({{
        "main", "let text = \"abcd\"; for (item in text) { let seen = item; }\n"}}, limits);
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
                 [&] { static_cast<void>(iterated.execute("main")); },
                 "string iteration must preflight its snapshot before expansion");
    check(iterated.stats().collection_work == 4,
          "failed string iteration must not materialize a rejected snapshot");

    runtime::SynchronousEvaluator ordered({{
        "main", "let result = \"ab\" < \"cd\";\n"}}, limits);
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
                 [&] { static_cast<void>(ordered.execute("main")); },
                 "string ordering must charge both operands before copying either string");
    check(ordered.stats().collection_work == 4,
          "failed string ordering must not copy strings after its rejected work charge");

    limits = {};
    limits.max_collection_work = 1;
    runtime::SynchronousEvaluator binding({{
        "main", "fn identity(value) { return value; } let result = identity(1);\n"}}, limits);
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
                 [&] { static_cast<void>(binding.execute("main")); },
                 "calls must charge argument and parameter binding before copying call state");
    check(binding.stats().collection_work == 0,
          "rejected call binding must not evaluate arguments or charge partial binding work");
}

void test_closure_side_table_survives_heap_collection()
{
    runtime::SynchronousEvaluator evaluator({
        {"factory",
         "fn make() {\n"
         "  let value = 40;\n"
         "  return fn(delta) { value += delta; return value; };\n"
         "}\n"
         "let counter = make();\n"},
        {"consumer",
         "import \"factory\" as factory;\n"
         "let first = factory.counter(1);\n"
         "let second = factory.counter(2);\n"
         "let result = first + second;\n"},
    });
    static_cast<void>(evaluator.execute("factory"));
    evaluator.heap().collect();
    static_cast<void>(evaluator.execute("consumer"));
    check(integer_export(evaluator, "consumer", "result") == 84,
          "bounded evaluator-owned closure environments must remain rooted across collection");
}

void test_dynamic_failures_compile_gate_and_explicit_boundaries()
{
    runtime::SynchronousEvaluator uninitialized({{
        "main", "if (false) let hidden = 1; let result = hidden;\n"}});
    const auto error = expect_error(
        runtime::LanguageErrorCode::UninitializedBinding,
        [&] { static_cast<void>(uninitialized.execute("main")); },
        "a conditionally skipped declaration must remain an uninitialized slot");
    check(error.module() == "main" && error.span().begin.byte_offset > 0,
          "dynamic errors must retain stable module and AST source attribution");

    runtime::SynchronousEvaluator overflow({{
        "main", "let result = 9223372036854775807 + 1;\n"}});
    expect_error(runtime::LanguageErrorCode::NumericOverflow,
                 [&] { static_cast<void>(overflow.execute("main")); },
                 "checked integer operations must reject overflow");

    runtime::SynchronousEvaluator bad_call({{
        "main", "fn one(value) { return value; } let result = one(1, value = 2);\n"}});
    expect_error(runtime::LanguageErrorCode::CallArgumentDuplicate,
                 [&] { static_cast<void>(bad_call.execute("main")); },
                 "positional and named binding of one parameter must be rejected");

    runtime::SynchronousEvaluator host_import({{
        "main", "import \"baas/log\" as log; let result = 1;\n"}});
    expect_error(runtime::LanguageErrorCode::HostUnavailable,
                 [&] { static_cast<void>(host_import.execute("main")); },
                 "the evaluator must not imply that a Host adapter exists");

    runtime::SynchronousEvaluator structured_error({{
        "main", "fn fail() { throw 1; } let result = fail();\n"}});
    expect_error(runtime::LanguageErrorCode::ArgumentInvalid,
                 [&] { static_cast<void>(structured_error.execute("main")); },
                 "throw/catch/defer must fail explicitly until the Error unwinder is integrated");

    try {
        runtime::SynchronousEvaluator invalid({{
            "main", "let value = missing;\n"}});
        check(false, "semantic errors must prevent evaluator construction");
    } catch (const runtime::EvaluationCompileError& compile_error) {
        check(compile_error.diagnostics().size() == 1
                  && compile_error.diagnostics()[0].module == "main"
                  && compile_error.diagnostics()[0].diagnostic.code == "SEM001",
              "compile diagnostics must remain stable and module-qualified");
    }
}

void test_input_order_independent_results_and_stats()
{
    const std::vector<runtime::SourceModule> first_order{
        {"main", "import \"lib\" as lib; let result = lib.compute(9);\n"},
        {"lib", "fn compute(value) { return value * value; }\n"},
    };
    auto second_order = first_order;
    std::reverse(second_order.begin(), second_order.end());
    runtime::SynchronousEvaluator first(first_order);
    runtime::SynchronousEvaluator second(second_order);
    static_cast<void>(first.execute("main"));
    static_cast<void>(second.execute("main"));
    check(integer_export(first, "main", "result") == 81
              && integer_export(second, "main", "result") == 81,
          "source module input order must not affect evaluation output");
    const auto a = first.stats();
    const auto b = second.stats();
    check(a.steps == b.steps && a.peak_call_depth == b.peak_call_depth
              && a.peak_value_stack == b.peak_value_stack
              && a.collection_work == b.collection_work,
          "valid deterministic execution must publish input-order-independent stats");
}

void test_nested_imports_participate_in_the_complete_module_graph()
{
    runtime::SynchronousEvaluator nested({
        {"main",
         "fn load() { import \"value\" as value; return value.answer; }\n"
         "let result = load();\n"},
        {"value", "let answer = 42;\n"},
    });
    static_cast<void>(nested.execute("main"));
    check(integer_export(nested, "main", "result") == 42
              && nested.stats().initialized_modules == 2,
          "function-body imports must load and initialize their dependency");

    expect_error(
        runtime::LanguageErrorCode::ModuleInitializationFailed,
        [] {
            runtime::SynchronousEvaluator missing({{
                "main",
                "let hidden = fn() { import \"missing\" as dependency; "
                "return dependency.value; }; let result = 1;\n",
            }});
        },
        "an uncalled function-expression import must still close the module snapshot");

    expect_error(
        runtime::LanguageErrorCode::ImportCycle,
        [] {
            runtime::SynchronousEvaluator cyclic({
                {"a", "let hidden = fn() { import \"b\" as b; return b.value; }; let result = 1;\n"},
                {"b", "fn hidden() { import \"a\" as a; return a.result; } let value = 2;\n"},
            });
        },
        "nested imports must participate in deterministic cycle rejection");
}

}  // namespace

int main()
{
    try {
    test_values_collections_operators_and_short_circuit();
    test_control_flow_closures_defaults_and_recursion();
    test_multi_module_cache_and_namespace_calls();
    test_module_failure_cache_and_lazy_initialization();
    test_constructive_two_counter_program();
    test_deterministic_limits_and_runtime_errors();
    test_source_and_expanded_collection_preflights();
    test_closure_side_table_survives_heap_collection();
    test_dynamic_failures_compile_gate_and_explicit_boundaries();
    test_input_order_independent_results_and_stats();
    test_nested_imports_participate_in_the_complete_module_graph();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    if (failures != 0) {
        std::cerr << failures << " synchronous evaluator test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "synchronous evaluator tests passed\n";
    return EXIT_SUCCESS;
}
