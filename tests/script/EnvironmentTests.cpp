#include "script/runtime/Environment.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace runtime = baas::script::runtime;

namespace {

void check(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void check_runtime_error(
    const runtime::RuntimeErrorCode expected,
    const std::function<void()>& operation,
    const std::string& message)
{
    try {
        operation();
    } catch (const runtime::RuntimeError& error) {
        check(error.code() == expected, message + " (wrong runtime error)");
        return;
    }
    check(false, message + " (no runtime error)");
}

void lexical_lookup_and_assignment()
{
    runtime::Heap heap;
    auto outer = std::make_shared<runtime::Environment>(heap);
    check(outer->define("mutable", runtime::Value(std::int64_t{1})), "define mutable binding");
    check(outer->define(
              "constant", runtime::Value(std::int64_t{2}), runtime::BindingMutability::Immutable),
          "define immutable binding");
    check(!outer->define("mutable", runtime::Value(std::int64_t{9})), "reject duplicate local binding");
    check(!outer->define("", runtime::Value::null()), "reject empty binding name");

    runtime::Environment inner(heap, outer);
    check(inner.lookup("mutable")->as_integer() == 1, "lookup traverses parent chain");
    check(inner.define("mutable", runtime::Value(std::int64_t{10})), "inner binding shadows outer");
    check(inner.lookup("mutable")->as_integer() == 10, "lookup prefers nearest binding");
    check(inner.assign("mutable", runtime::Value(std::int64_t{11})) == runtime::AssignResult::Updated,
          "assignment updates nearest mutable binding");
    check(inner.lookup("mutable")->as_integer() == 11, "inner assignment is observable");
    check(outer->lookup("mutable")->as_integer() == 1, "shadowed outer binding is unchanged");
    check(inner.assign("constant", runtime::Value(std::int64_t{3})) == runtime::AssignResult::Immutable,
          "assignment reports immutable parent binding");
    check(inner.assign("missing", runtime::Value::null()) == runtime::AssignResult::Undefined,
          "assignment reports undefined binding");
    check(!inner.lookup("missing").has_value(), "missing lookup returns no value");
    check(inner.contains_local("mutable") && !inner.contains_local("constant"),
          "local membership excludes parent bindings");
    check(inner.local_size() == 1, "local size counts only the current scope");
}

void bindings_are_gc_roots()
{
    runtime::Heap heap;
    runtime::Value replacement;
    {
        runtime::Environment environment(heap);
        const auto original = heap.allocate_string("original");
        check(environment.define("value", original), "define heap-backed binding");
        heap.collect();
        check(heap.string_copy(original.as_heap_ref()) == "original", "binding keeps value alive");

        replacement = heap.allocate_string("replacement");
        check(environment.assign("value", replacement) == runtime::AssignResult::Updated,
              "replace heap-backed binding");
        heap.collect();
        check_runtime_error(runtime::RuntimeErrorCode::StaleReference,
                            [&] { (void)heap.string_copy(original.as_heap_ref()); },
                            "replaced value is no longer rooted");
        check(heap.string_copy(replacement.as_heap_ref()) == "replacement", "replacement remains rooted");
    }
    heap.collect();
    check_runtime_error(runtime::RuntimeErrorCode::StaleReference,
                        [&] { (void)heap.string_copy(replacement.as_heap_ref()); },
                        "destroyed environment releases binding roots");
}

void heap_boundaries_are_enforced()
{
    runtime::Heap first;
    runtime::Heap second;
    runtime::Environment environment(first);
    const auto foreign = second.allocate_string("foreign");
    check_runtime_error(runtime::RuntimeErrorCode::CrossHeapReference,
                        [&] { (void)environment.define("foreign", foreign); },
                        "definition rejects a cross-heap value");
    check(environment.local_size() == 0, "failed definition is transactional");

    check(environment.define("local", runtime::Value(std::int64_t{7})), "define assignment target");
    check_runtime_error(runtime::RuntimeErrorCode::CrossHeapReference,
                        [&] { (void)environment.assign("local", foreign); },
                        "assignment rejects a cross-heap value");
    check(environment.lookup("local")->as_integer() == 7, "failed assignment is transactional");

    auto foreign_parent = std::make_shared<runtime::Environment>(second);
    try {
        runtime::Environment child(first, foreign_parent);
        (void)child;
    } catch (const std::invalid_argument&) {
        return;
    }
    check(false, "constructor rejects a cross-heap parent");
}

}  // namespace

int main()
{
    lexical_lookup_and_assignment();
    bindings_are_gc_roots();
    heap_boundaries_are_enforced();
    std::cout << "Environment tests passed\n";
    return 0;
}
