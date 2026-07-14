#include "script/runtime/JsonBridge.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace baas::script::runtime;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Function>
void check_error(const RuntimeErrorCode code, Function&& function, const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const RuntimeError& error) {
        check(error.code() == code, message);
        check(!runtime_error_code_name(error.code()).empty(),
              "bridge runtime errors must expose stable names");
    } catch (...) {
        check(false, message);
    }
}

HeapLimits generous_heap_limits()
{
    HeapLimits limits;
    limits.max_live_bytes = 256U * 1024U * 1024U;
    limits.max_cells = 100'000;
    limits.max_single_allocation = 64U * 1024U * 1024U;
    limits.max_string_bytes = 128U * 1024U * 1024U;
    limits.soft_collect_threshold = limits.max_live_bytes;
    limits.max_collection_work = 200'000;
    return limits;
}

JsonBridgeLimits generous_bridge_limits()
{
    JsonBridgeLimits limits;
    limits.max_depth = 1024;
    limits.max_nodes = 100'000;
    limits.max_string_bytes = 128U * 1024U * 1024U;
    limits.max_total_bytes = 256U * 1024U * 1024U;
    limits.max_work = 100'000;
    return limits;
}

void test_nested_round_trip_and_order()
{
    Heap source(generous_heap_limits());
    const auto word = source.allocate_string("kiramei");
    const auto inner = source.allocate_list(
        {Value::null(), Value(true), Value(std::int64_t{-7}), Value(2.5), word});
    const auto root = source.allocate_map(
        {{"first", inner}, {"second", Value(std::int64_t{42})}});
    const auto source_root = source.add_root(root);

    const auto json = heap_value_to_json(source, root, generous_bridge_limits());
    check(json.kind() == JsonKind::Object, "heap map should become a JSON object");
    const auto& object = std::get<JsonObject>(json.storage);
    check(object.size() == 2 && object[0].first == "first" && object[1].first == "second",
          "JSON object should preserve map insertion order");
    const auto& values = std::get<JsonArray>(object[0].second.storage);
    check(values.size() == 5 && values[0].kind() == JsonKind::Null &&
              std::get<bool>(values[1].storage) &&
              std::get<std::int64_t>(values[2].storage) == -7 &&
              std::get<double>(values[3].storage) == 2.5 &&
              std::get<std::string>(values[4].storage) == "kiramei",
          "nested primitive and string values should convert without loss");

    Heap destination(generous_heap_limits());
    const auto copied = json_to_heap_value(destination, json, generous_bridge_limits());
    const auto destination_root = destination.add_root(copied);
    const auto copied_entries = destination.map_entries(copied.as_heap_ref());
    check(copied_entries.size() == 2 && copied_entries[0].first == "first" &&
              copied_entries[1].first == "second",
          "JSON object insertion order should survive destination materialization");
    const auto copied_values = destination.list_values(copied_entries[0].second.as_heap_ref());
    check(copied_values.size() == 5 &&
              destination.string_copy(copied_values[4].as_heap_ref()) == "kiramei",
          "nested JSON arrays and strings should materialize in the destination heap");

    check(source.remove_root(source_root) && destination.remove_root(destination_root),
          "test roots should be removable");
}

void test_dag_copy_isolation_and_checked_references()
{
    Heap source(generous_heap_limits());
    const auto shared = source.allocate_list({Value(std::int64_t{1})});
    const auto source_value = source.allocate_list({shared, shared});
    const auto source_root = source.add_root(source_value);

    Heap destination(generous_heap_limits());
    const auto copied = deep_copy_json_value(
        source, source_value, destination, generous_bridge_limits());
    const auto copied_root = destination.add_root(copied);
    auto children = destination.list_values(copied.as_heap_ref());
    check(children.size() == 2 && children[0].as_heap_ref() != children[1].as_heap_ref(),
          "DAG occurrences must become independent mutable destination objects");
    destination.list_set(children[0].as_heap_ref(), 0, Value(std::int64_t{99}));
    check(destination.list_values(children[1].as_heap_ref())[0].as_integer() == 1,
          "mutating one copied DAG occurrence must not affect another");
    check(source.list_values(shared.as_heap_ref())[0].as_integer() == 1,
          "destination mutation must not affect the source heap");

    check_error(RuntimeErrorCode::CrossHeapReference,
                [&] { (void)deep_copy_json_value(source, source_value, source,
                                                   generous_bridge_limits()); },
                "checked transfer must reject a source heap as its own destination");
    check_error(RuntimeErrorCode::CrossHeapReference,
                [&] { (void)heap_value_to_json(destination, source_value,
                                               generous_bridge_limits()); },
                "heap conversion must reject a foreign heap reference");

    const auto stale = source.allocate_list();
    source.collect();
    check_error(RuntimeErrorCode::StaleReference,
                [&] { (void)heap_value_to_json(source, stale, generous_bridge_limits()); },
                "heap conversion must reject a stale reference");

    check(source.remove_root(source_root) && destination.remove_root(copied_root),
          "copy isolation roots should be removable");
}

void test_cycles_nonfinite_and_identity_values()
{
    Heap heap(generous_heap_limits());
    const auto cycle = heap.allocate_list();
    heap.list_append(cycle.as_heap_ref(), cycle);
    const auto cycle_root = heap.add_root(cycle);
    check_error(RuntimeErrorCode::JsonCycle,
                [&] { (void)heap_value_to_json(heap, cycle, generous_bridge_limits()); },
                "cyclic heap values must be rejected");

    check_error(RuntimeErrorCode::JsonNonFinite,
                [&] { (void)heap_value_to_json(
                    heap, Value(std::numeric_limits<double>::infinity()),
                    generous_bridge_limits()); },
                "non-finite heap floats must be rejected");
    Heap destination(generous_heap_limits());
    const JsonValue nonfinite(std::numeric_limits<double>::quiet_NaN());
    check_error(RuntimeErrorCode::JsonNonFinite,
                [&] { (void)json_to_heap_value(destination, nonfinite,
                                               generous_bridge_limits()); },
                "non-finite JSON floats must be rejected before destination allocation");

    std::vector<Value> identities;
    identities.push_back(heap.allocate_function({1, {}}));
    identities.push_back(heap.allocate_module({"module", {}}));
    identities.push_back(heap.allocate_error({"E", "error", std::nullopt, {}}));
    identities.push_back(heap.allocate_task({2, TaskState::Pending, {}}));
    identities.push_back(heap.allocate_host_handle({3, 4, 0, false}));
    for (const auto identity : identities) {
        check_error(RuntimeErrorCode::JsonUnsupported,
                    [&] { (void)heap_value_to_json(heap, identity,
                                                   generous_bridge_limits()); },
                    "every identity-bearing cell kind must be rejected");
    }
    check(heap.remove_root(cycle_root), "cycle root should be removable");
}

void test_each_bridge_budget_boundary()
{
    const JsonValue depth_three(JsonArray{JsonValue(JsonArray{JsonValue()})});
    auto limits = generous_bridge_limits();
    limits.max_depth = 3;
    Heap exact_depth(generous_heap_limits());
    (void)json_to_heap_value(exact_depth, depth_three, limits);
    limits.max_depth = 2;
    Heap over_depth(generous_heap_limits());
    check_error(RuntimeErrorCode::JsonDepthLimitExceeded,
                [&] { (void)json_to_heap_value(over_depth, depth_three, limits); },
                "depth exactly over the configured boundary must fail");

    const JsonValue three_nodes(JsonArray{
        JsonValue(std::int64_t{1}), JsonValue(std::int64_t{2})});
    limits = generous_bridge_limits();
    limits.max_nodes = 3;
    Heap exact_nodes(generous_heap_limits());
    (void)json_to_heap_value(exact_nodes, three_nodes, limits);
    limits.max_nodes = 2;
    Heap over_nodes(generous_heap_limits());
    check_error(RuntimeErrorCode::JsonNodeLimitExceeded,
                [&] { (void)json_to_heap_value(over_nodes, three_nodes, limits); },
                "node exactly over the configured boundary must fail");

    const JsonValue three_bytes(std::string("abc"));
    limits = generous_bridge_limits();
    limits.max_string_bytes = 3;
    Heap exact_string(generous_heap_limits());
    (void)json_to_heap_value(exact_string, three_bytes, limits);
    limits.max_string_bytes = 2;
    Heap over_string(generous_heap_limits());
    check_error(RuntimeErrorCode::JsonStringLimitExceeded,
                [&] { (void)json_to_heap_value(over_string, three_bytes, limits); },
                "string bytes exactly over the configured boundary must fail");

    limits = generous_bridge_limits();
    limits.max_total_bytes = 4; // one node tag plus three string bytes
    Heap exact_total(generous_heap_limits());
    (void)json_to_heap_value(exact_total, three_bytes, limits);
    limits.max_total_bytes = 3;
    Heap over_total(generous_heap_limits());
    check_error(RuntimeErrorCode::JsonByteLimitExceeded,
                [&] { (void)json_to_heap_value(over_total, three_bytes, limits); },
                "logical bytes exactly over the configured boundary must fail");

    limits = generous_bridge_limits();
    limits.max_work = 5; // three node visits plus two child edges
    Heap exact_work(generous_heap_limits());
    (void)json_to_heap_value(exact_work, three_nodes, limits);
    limits.max_work = 4;
    Heap over_work(generous_heap_limits());
    check_error(RuntimeErrorCode::JsonWorkLimitExceeded,
                [&] { (void)json_to_heap_value(over_work, three_nodes, limits); },
                "work exactly over the configured boundary must fail");
}

void test_destination_gc_rooting_failure_atomicity_and_utf8()
{
    auto limits = generous_heap_limits();
    limits.soft_collect_threshold = 0;
    Heap collecting_destination(limits);
    const JsonValue nested(JsonObject{{"items", JsonValue(JsonArray{
        JsonValue(std::string("one")), JsonValue(std::string("two"))})}});
    const auto result = json_to_heap_value(
        collecting_destination, nested, generous_bridge_limits());
    const auto entries = collecting_destination.map_entries(result.as_heap_ref());
    const auto values = collecting_destination.list_values(entries[0].second.as_heap_ref());
    check(values.size() == 2 &&
              collecting_destination.string_copy(values[0].as_heap_ref()) == "one" &&
              collecting_destination.string_copy(values[1].as_heap_ref()) == "two",
          "destination construction must root every intermediate across soft GC");

    auto hard_limits = generous_heap_limits();
    hard_limits.max_cells = 2;
    hard_limits.soft_collect_threshold = hard_limits.max_live_bytes;
    Heap hard_destination(hard_limits);
    const auto sentinel = hard_destination.allocate_list();
    const auto sentinel_root = hard_destination.add_root(sentinel);
    const JsonValue too_large(JsonArray{JsonValue(std::string("child"))});
    check_error(RuntimeErrorCode::CellLimitExceeded,
                [&] { (void)json_to_heap_value(hard_destination, too_large,
                                               generous_bridge_limits()); },
                "destination heap hard cell limit must fail deterministically");
    hard_destination.collect();
    check(hard_destination.stats().live_cells == 1 &&
              hard_destination.kind(sentinel) == ValueKind::List,
          "failed materialization must publish no partial graph and preserve caller roots");
    check(hard_destination.remove_root(sentinel_root), "sentinel root should be removable");

    std::string invalid_utf8(1, static_cast<char>(0xFF));
    const JsonValue invalid(std::move(invalid_utf8));
    Heap utf8_destination(generous_heap_limits());
    check_error(RuntimeErrorCode::InvalidUtf8,
                [&] { (void)json_to_heap_value(utf8_destination, invalid,
                                               generous_bridge_limits()); },
                "invalid JSON string bytes must be rejected by the destination heap");
    check(utf8_destination.stats().live_cells == 0,
          "invalid UTF-8 must not leave a destination cell");
}

void test_duplicate_object_keys_fail_before_destination_allocation()
{
    Heap destination(generous_heap_limits());
    const auto sentinel = destination.allocate_list();
    const auto sentinel_root = destination.add_root(sentinel);
    const auto before = destination.stats();
    const JsonValue duplicate(JsonArray{JsonValue(JsonObject{
        {"same", JsonValue(std::string("first"))},
        {"same", JsonValue(std::string("second"))},
    })});

    check_error(RuntimeErrorCode::JsonDuplicateKey,
                [&] { (void)json_to_heap_value(destination, duplicate,
                                               generous_bridge_limits()); },
                "nested duplicate JSON object keys must be rejected");
    const auto after = destination.stats();
    check(after.live_cells == before.live_cells && after.live_bytes == before.live_bytes &&
              after.string_bytes == before.string_bytes &&
              after.collections == before.collections &&
              destination.kind(sentinel) == ValueKind::List,
          "duplicate-key rejection must occur before destination allocation or collection");
    check(runtime_error_code_name(RuntimeErrorCode::JsonDuplicateKey) ==
              "RT023_JSON_DUPLICATE_KEY",
          "duplicate keys must expose their stable RT023 name");
    check(destination.remove_root(sentinel_root), "duplicate-key sentinel root should be removable");
}

void dismantle_deep_json(JsonValue& value)
{
    while (value.kind() == JsonKind::Array) {
        auto array = std::move(std::get<JsonArray>(value.storage));
        value = JsonValue();
        if (array.empty()) return;
        value = std::move(array.front());
        array.clear();
    }
}

void test_hostile_ten_thousand_depth_is_rejected_iteratively()
{
    Heap source(generous_heap_limits());
    auto heap_value = Value::null();
    for (std::size_t depth = 0; depth < 10'000; ++depth)
        heap_value = source.allocate_list({heap_value});
    const auto root = source.add_root(heap_value);
    auto limits = generous_bridge_limits();
    limits.max_depth = 20'000;
    check_error(RuntimeErrorCode::JsonDepthLimitExceeded,
                [&] { (void)heap_value_to_json(source, heap_value, limits); },
                "hostile heap depth must hit the stable internal depth ceiling");
    check(source.remove_root(root), "hostile heap root should be removable");

    JsonValue json;
    for (std::size_t depth = 0; depth < 10'000; ++depth) {
        JsonArray parent;
        parent.push_back(std::move(json));
        json = JsonValue(std::move(parent));
    }
    Heap destination(generous_heap_limits());
    check_error(RuntimeErrorCode::JsonDepthLimitExceeded,
                [&] { (void)json_to_heap_value(destination, json, limits); },
                "hostile JSON depth must be rejected before destination construction");
    check(destination.stats().live_cells == 0,
          "early hostile-depth rejection must allocate no destination cells");
    dismantle_deep_json(json);
}

}  // namespace

int main()
{
    test_nested_round_trip_and_order();
    test_dag_copy_isolation_and_checked_references();
    test_cycles_nonfinite_and_identity_values();
    test_each_bridge_budget_boundary();
    test_destination_gc_rooting_failure_atomicity_and_utf8();
    test_duplicate_object_keys_fail_before_destination_allocation();
    test_hostile_ten_thousand_depth_is_rejected_iteratively();

    if (failures != 0) {
        std::cerr << failures << " JSON bridge test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "JSON bridge tests passed\n";
    return EXIT_SUCCESS;
}
