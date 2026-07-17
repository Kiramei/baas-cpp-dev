#include "script/runtime/SynchronousEvaluator.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace runtime = baas::script::runtime;

namespace {

int failures = 0;

class ControlledProbe final : public runtime::HostCancellationProbe {
public:
    ControlledProbe(
        const std::size_t cancel_after,
        const bool deadline = false,
        const bool cancelled = false,
        const std::size_t deadline_after = 0) noexcept
        : cancel_after_(cancel_after), deadline_(deadline), cancelled_(cancelled),
          deadline_after_(deadline_after)
    {}

    [[nodiscard]] bool cancelled() const noexcept override
    {
        if (cancelled_.load(std::memory_order_relaxed)) return true;
        const auto observed = polls_.fetch_add(1, std::memory_order_relaxed) + 1;
        const auto threshold = cancel_after_.load(std::memory_order_relaxed);
        return threshold != 0 && observed >= threshold;
    }

    [[nodiscard]] bool deadline_exceeded() const noexcept override
    {
        if (deadline_.load(std::memory_order_relaxed)) return true;
        const auto threshold = deadline_after_.load(std::memory_order_relaxed);
        return threshold != 0
            && polls_.load(std::memory_order_relaxed) + 1 >= threshold;
    }

    [[nodiscard]] std::size_t polls() const noexcept
    {
        return polls_.load(std::memory_order_relaxed);
    }

    void set_cancel_after(const std::size_t value) noexcept
    {
        cancel_after_.store(value, std::memory_order_relaxed);
    }

    void set_cancelled(const bool value) noexcept
    {
        cancelled_.store(value, std::memory_order_relaxed);
    }

    void set_deadline(const bool value) noexcept
    {
        deadline_.store(value, std::memory_order_relaxed);
    }

private:
    std::atomic<std::size_t> cancel_after_{};
    std::atomic<bool> deadline_{};
    std::atomic<bool> cancelled_{};
    std::atomic<std::size_t> deadline_after_{};
    mutable std::atomic<std::size_t> polls_{};
};

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
    expect_error(runtime::LanguageErrorCode::ThrownValue,
                 [&] { static_cast<void>(structured_error.execute("main")); },
                 "an uncaught non-Error throw must cross the evaluator boundary as ThrownValue");

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

void test_structured_errors_and_defer_unwinding()
{
    runtime::SynchronousEvaluator evaluator({{
        "main",
        "let trace = 0;\n"
        "fn ordered() {\n"
        "  defer trace = trace * 10 + 3;\n"
        "  defer trace = trace * 10 + 2;\n"
        "  trace = trace * 10 + 1;\n"
        "  return 7;\n"
        "}\n"
        "let returned = ordered();\n"
        "let held = null;\n"
        "let same_rethrow = false;\n"
        "try {\n"
        "  try { let bad = 1 / 0; } catch (error) { held = error; throw error; }\n"
        "} catch (outer) { same_rethrow = outer is held; }\n"
        "let thrown_code = \"\";\n"
        "let thrown_kind = \"\";\n"
        "try { throw [1, 2]; } catch (error) {\n"
        "  thrown_code = error.code;\n"
        "  thrown_kind = error.details.thrown_kind;\n"
        "}\n"
        "let primary_code = \"\";\n"
        "let suppressed_code = \"\";\n"
        "let derived_primary = false;\n"
        "fn cleanup_failure() {\n"
        "  defer throw 9;\n"
        "  try { let bad = 1 / 0; } catch (error) { held = error; throw error; }\n"
        "}\n"
        "try { cleanup_failure(); } catch (error) {\n"
        "  primary_code = error.code;\n"
        "  suppressed_code = error.suppressed[0].code;\n"
        "  derived_primary = not (error is held);\n"
        "}\n"
        "let registering_catch_ran = false;\n"
        "let outer_catch_ran = false;\n"
        "fn deferred_after_try() {\n"
        "  try { defer throw 11; } catch (error) { registering_catch_ran = true; }\n"
        "}\n"
        "try { deferred_after_try(); } catch (error) { outer_catch_ran = true; }\n"
        "let cleanup_trace = 0;\n"
        "let cleanup_primary = \"\";\n"
        "let cleanup_suppressed = \"\";\n"
        "fn all_cleanups() {\n"
        "  defer { cleanup_trace = cleanup_trace * 10 + 1; throw 1; }\n"
        "  defer { cleanup_trace = cleanup_trace * 10 + 2; throw 2; }\n"
        "}\n"
        "try { all_cleanups(); } catch (error) {\n"
        "  cleanup_primary = error.code;\n"
        "  cleanup_suppressed = error.suppressed[0].code;\n"
        "}\n"
        "let readonly_code = \"\";\n"
        "try {\n"
        "  try { let bad = 1 / 0; } catch (error) { error.code = \"changed\"; }\n"
        "} catch (error) { readonly_code = error.code; }\n",
    }});
    static_cast<void>(evaluator.execute("main"));

    check(integer_export(evaluator, "main", "trace") == 123
              && integer_export(evaluator, "main", "returned") == 7,
          "return value evaluation must precede per-activation LIFO cleanup");
    check(evaluator.module_export("main", "same_rethrow").as_boolean(),
          "throw Error must preserve exact heap identity through catch and rethrow");
    check(evaluator.heap().string_copy(
              evaluator.module_export("main", "thrown_code").as_heap_ref()) == "ThrownValue"
              && evaluator.heap().string_copy(
                  evaluator.module_export("main", "thrown_kind").as_heap_ref()) == "list",
          "throwing a non-Error must materialize only stable ThrownValue metadata");
    check(evaluator.heap().string_copy(
              evaluator.module_export("main", "primary_code").as_heap_ref()) == "DivisionByZero"
              && evaluator.heap().string_copy(
                  evaluator.module_export("main", "suppressed_code").as_heap_ref()) == "ThrownValue"
              && evaluator.module_export("main", "derived_primary").as_boolean(),
          "cleanup failure must derive the existing primary and append a suppressed Error");
    check(!evaluator.module_export("main", "registering_catch_ran").as_boolean()
              && evaluator.module_export("main", "outer_catch_ran").as_boolean(),
          "a defer registered in a try must execute after that try handler is out of scope");
    check(integer_export(evaluator, "main", "cleanup_trace") == 21
              && evaluator.heap().string_copy(
                  evaluator.module_export("main", "cleanup_primary").as_heap_ref()) == "ThrownValue"
              && evaluator.heap().string_copy(
                  evaluator.module_export("main", "cleanup_suppressed").as_heap_ref()) == "ThrownValue",
          "all defers must run after a cleanup failure and preserve LIFO primary ordering");
    check(evaluator.heap().string_copy(
              evaluator.module_export("main", "readonly_code").as_heap_ref()) == "TypeMismatch",
          "Error members must be read-only even though structured members are inspectable");
    check(evaluator.stats().registered_defers == 6
              && evaluator.stats().executed_defers == 6
              && evaluator.stats().cleanup_steps > 0,
          "structured cleanup work must publish stable registration, execution and step stats");
}

void test_terminal_failure_bypasses_catch_and_still_unwinds()
{
    runtime::EvaluatorLimits limits;
    limits.max_steps = 50;
    runtime::SynchronousEvaluator evaluator({{
        "main",
        "let trace = 0;\n"
        "fn spin() {\n"
        "  defer trace = trace * 10 + 1;\n"
        "  defer trace = trace * 10 + 2;\n"
        "  try { while (true) { let work = 1; } } catch (error) { trace = 999; }\n"
        "}\n"
        "spin();\n",
    }}, limits);
    const auto error = expect_error(
        runtime::LanguageErrorCode::InstructionLimitExceeded,
        [&] { static_cast<void>(evaluator.execute("main")); },
        "terminal instruction exhaustion must escape script catch handlers");
    check(!error.catchable() && evaluator.stats().registered_defers == 2
              && evaluator.stats().executed_defers == 2
              && evaluator.stats().cleanup_steps > 0,
          "terminal failure must use the independent cleanup allowance and execute every defer");
}

void test_cooperative_cancellation_safe_points_and_cleanup_masking()
{
    auto already_cancelled = std::make_shared<ControlledProbe>(0, false, true);
    runtime::SynchronousEvaluator empty(
        {{"main", ""}}, {}, {}, {}, nullptr, already_cancelled);
    const auto entry_error = expect_error(
        runtime::LanguageErrorCode::Cancelled,
        [&] { static_cast<void>(empty.execute("main")); },
        "an already-cancelled context must reject an empty entry module");
    check(!entry_error.catchable() && entry_error.steps() == 0,
          "entry cancellation must be terminal before script work starts");

    auto deadline = std::make_shared<ControlledProbe>(0, true, true);
    runtime::SynchronousEvaluator expired(
        {{"main", ""}}, {}, {}, {}, nullptr, deadline);
    const auto deadline_error = expect_error(
        runtime::LanguageErrorCode::DeadlineExceeded,
        [&] { static_cast<void>(expired.execute("main")); },
        "an expired deadline must take priority over a simultaneous stop");
    check(!deadline_error.catchable(),
          "deadline expiry must remain a terminal language error");

    auto running = std::make_shared<ControlledProbe>(12);
    runtime::EvaluatorLimits limits;
    limits.max_steps = 10'000;
    runtime::SynchronousEvaluator evaluator({{
        "main",
        "let trace = 0;\n"
        "fn spin() {\n"
        "  defer trace = trace * 10 + 1;\n"
        "  defer trace = trace * 10 + 2;\n"
        "  try { while (true) { let work = 1; } } catch (error) { trace = 999; }\n"
        "}\n"
        "spin();\n",
    }}, limits, {}, {}, nullptr, running);
    const auto cancelled = expect_error(
        runtime::LanguageErrorCode::Cancelled,
        [&] { static_cast<void>(evaluator.execute("main")); },
        "a running pure-language loop must observe cooperative cancellation");
    check(!cancelled.catchable() && running->polls() >= 12
              && evaluator.stats().steps < limits.max_steps,
          "cancellation must preempt the instruction bound and bypass catch");
    check(evaluator.stats().registered_defers == 2
              && evaluator.stats().executed_defers == 2
              && evaluator.stats().cleanup_steps > 0,
          "cancellation must be masked while every registered defer drains");
}

void test_safe_point_uses_asy_013_terminal_priority()
{
    runtime::EvaluatorLimits limits;
    limits.max_steps = 1;

    auto cancellation = std::make_shared<ControlledProbe>(4);
    runtime::SynchronousEvaluator cancelled(
        {{"main", "let value = 1;\n"}}, limits, {}, {}, nullptr, cancellation);
    const auto cancelled_limit = expect_error(
        runtime::LanguageErrorCode::InstructionLimitExceeded,
        [&] { static_cast<void>(cancelled.execute("main")); },
        "instruction safety must outrank cancellation pending at one safe point");
    check(cancelled_limit.steps() == 1 && cancellation->polls() >= 4,
          "the priority test must reach one safe point with both claims pending");

    auto deadline = std::make_shared<ControlledProbe>(0, false, false, 4);
    runtime::SynchronousEvaluator expired(
        {{"main", "let value = 1;\n"}}, limits, {}, {}, nullptr, deadline);
    const auto deadline_limit = expect_error(
        runtime::LanguageErrorCode::InstructionLimitExceeded,
        [&] { static_cast<void>(expired.execute("main")); },
        "instruction safety must outrank a deadline pending at one safe point");
    check(deadline_limit.steps() == 1 && deadline->polls() >= 4,
          "deadline priority evidence must observe the simultaneous safe point");
}

void test_nested_module_cancellation_is_retryable_and_cache_safe()
{
    std::string dependency;
    for (std::size_t index = 0; index < 64; ++index) {
        dependency += "let value_" + std::to_string(index) + " = "
            + std::to_string(index) + ";\n";
    }
    auto probe = std::make_shared<ControlledProbe>(20);
    runtime::EvaluatorLimits limits;
    limits.max_steps = 10'000;
    runtime::SynchronousEvaluator evaluator(
        {
            {"main", "import \"dependency\" as dependency; let ready = 1;\n"},
            {"dependency", std::move(dependency)},
        },
        limits, {}, {}, nullptr, probe);

    const auto interrupted = expect_error(
        runtime::LanguageErrorCode::Cancelled,
        [&] { static_cast<void>(evaluator.execute("main")); },
        "nested package initialization must observe cancellation");
    check(interrupted.module() == "dependency"
              && evaluator.stats().initialized_modules == 0,
          "cancelled nested and importing modules must both roll back to Uninitialized");

    probe->set_cancel_after(0);
    const auto retried = evaluator.execute("main");
    check(evaluator.stats().initialized_modules == 2
              && integer_export(evaluator, "main", "ready") == 1,
          "a later execution must retry and commit both rolled-back modules");
    const auto committed_steps = evaluator.stats().steps;

    probe->set_cancelled(true);
    expect_error(
        runtime::LanguageErrorCode::Cancelled,
        [&] { static_cast<void>(evaluator.execute("main")); },
        "a repeated execute must still observe its current external context");
    check(evaluator.stats().initialized_modules == 2
              && evaluator.stats().steps == committed_steps,
          "entry cancellation must not invalidate or rerun a Ready module cache");

    probe->set_cancelled(false);
    const auto cached = evaluator.execute("main");
    check(cached.module_namespace == retried.module_namespace
              && evaluator.stats().initialized_modules == 2
              && evaluator.stats().steps == committed_steps,
          "a subsequent non-cancelled execute must reuse the committed namespace exactly");
}

void test_success_boundary_cancellation_preserves_ready_cache()
{
    auto probe = std::make_shared<ControlledProbe>(5);
    runtime::SynchronousEvaluator evaluator(
        {{"main", "let value = 7;\n"}}, {}, {}, {}, nullptr, probe);
    expect_error(
        runtime::LanguageErrorCode::Cancelled,
        [&] { static_cast<void>(evaluator.execute("main")); },
        "a terminal claim at the success boundary must outrank normal success");
    check(evaluator.stats().initialized_modules == 1,
          "success-boundary cancellation must retain the committed Ready module");
    const auto committed_steps = evaluator.stats().steps;

    probe->set_cancel_after(0);
    const auto cached = evaluator.execute("main");
    check(evaluator.heap().kind(cached.module_namespace) == runtime::ValueKind::Module
              && integer_export(evaluator, "main", "value") == 7
              && evaluator.stats().initialized_modules == 1
              && evaluator.stats().steps == committed_steps,
          "a retry after success-boundary cancellation must reuse the exact Ready cache");
}

void test_terminal_cleanup_promotion_and_public_error_envelope()
{
    runtime::EvaluatorLimits limits;
    limits.max_cleanup_steps = 20;
    runtime::SynchronousEvaluator evaluator({{
        "main",
        "let caught = false;\n"
        "fn fail_during_cleanup() {\n"
        "  defer while (true) { let work = 1; }\n"
        "  throw 7;\n"
        "}\n"
        "try { fail_during_cleanup(); } catch (error) { caught = true; }\n",
    }}, limits);
    const auto error = expect_error(
        runtime::LanguageErrorCode::CleanupLimitExceeded,
        [&] { static_cast<void>(evaluator.execute("main")); },
        "terminal cleanup exhaustion must replace a propagating catchable primary");
    const auto& envelope = error.structured_error();
    check(!error.catchable() && error.has_structured_error()
              && envelope.find("\"code\":\"CleanupLimitExceeded\"") != std::string::npos
              && envelope.find("\"phase\":\"cleanup\",\"call_source\":{\"snapshot_id\"")
                     != std::string::npos
              && envelope.find("\"definition_source\":{\"snapshot_id\"")
                     != std::string::npos
              && envelope.find("\"suppressed\":[{\"schema\":\"baas.script.error/v1\",\"code\":\"ThrownValue\"")
                     != std::string::npos,
          "the public Error envelope must expose terminal primary promotion and the displaced primary");
    check(evaluator.stats().registered_defers == 1
              && evaluator.stats().executed_defers == 1,
          "terminal promotion must not skip the cleanup that produced it");
}

void test_publication_failure_still_drains_registered_defers()
{
    runtime::HeapLimits heap_limits;
    heap_limits.max_cells = 1;
    runtime::SynchronousEvaluator evaluator(
        {{
            "main",
            "let trace = 0;\n"
            "fn fail_without_error_cell() {\n"
            "  defer trace = trace * 10 + 1;\n"
            "  defer { let cleanup_bad = 1 / 0; }\n"
            "  let bad = 1 / 0;\n"
            "}\n"
            "fail_without_error_cell();\n",
        }},
        {},
        heap_limits);
    const auto error = expect_error(
        runtime::LanguageErrorCode::MemoryLimitExceeded,
        [&] { static_cast<void>(evaluator.execute("main")); },
        "Error-cell publication exhaustion must fail closed at the public boundary");
    const auto after_first = evaluator.stats();
    const auto repeated = expect_error(
        runtime::LanguageErrorCode::MemoryLimitExceeded,
        [&] { static_cast<void>(evaluator.execute("main")); },
        "a cached boundary failure must remain complete and stable on retry");
    const auto after_second = evaluator.stats();
    check(!error.catchable()
              && std::string(error.what()).find("structured Error publication exhausted")
                     != std::string::npos
              && std::string(repeated.what()) == error.what()
              && after_first.registered_defers == 2
              && evaluator.stats().executed_defers == 2
              && evaluator.stats().cleanup_steps > 0,
          "cleanup publication failure must not replace the original terminal boundary");
    check(after_second.steps == after_first.steps
              && after_second.executed_defers == after_first.executed_defers,
          "a boundary failure cache retry must not expose incomplete Failed state or rerun cleanup");
}

void test_stack_terminal_uses_independent_cleanup_call_depth()
{
    runtime::EvaluatorLimits limits;
    limits.max_call_depth = 2;
    limits.max_cleanup_call_depth = 2;
    runtime::SynchronousEvaluator evaluator({{
        "main",
        "let trace = 0;\n"
        "fn cleanup_helper() { trace = trace + 1; }\n"
        "fn descend() { defer cleanup_helper(); descend(); }\n"
        "descend();\n",
    }}, limits);
    const auto error = expect_error(
        runtime::LanguageErrorCode::StackLimitExceeded,
        [&] { static_cast<void>(evaluator.execute("main")); },
        "ordinary call-depth exhaustion must remain the terminal primary");
    check(!error.catchable() && evaluator.stats().registered_defers == 2
              && evaluator.stats().executed_defers == 2
              && evaluator.stats().peak_cleanup_call_depth == 1,
          "stack-terminal unwind must call bounded cleanup helpers under an independent depth budget");
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
    test_structured_errors_and_defer_unwinding();
    test_terminal_failure_bypasses_catch_and_still_unwinds();
    test_cooperative_cancellation_safe_points_and_cleanup_masking();
    test_safe_point_uses_asy_013_terminal_priority();
    test_nested_module_cancellation_is_retryable_and_cache_safe();
    test_success_boundary_cancellation_preserves_ready_cache();
    test_terminal_cleanup_promotion_and_public_error_envelope();
    test_publication_failure_still_drains_registered_defers();
    test_stack_terminal_uses_independent_cleanup_call_depth();
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
