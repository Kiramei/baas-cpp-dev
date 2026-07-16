#include "script/runtime/ValueHeap.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace baas::script::runtime;

namespace {

int failures = 0;

static_assert(static_cast<unsigned>(ValueKind::HostHandle) == 12);
static_assert(static_cast<unsigned>(ValueKind::Bytes) == 13);

void check(const bool condition, const std::string_view message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

template <typename Function>
void check_error(const RuntimeErrorCode code, Function&& function, const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const RuntimeError& error) {
        check(error.code() == code, message);
        check(!runtime_error_code_name(error.code()).empty(), "runtime errors must expose stable codes");
    } catch (...) {
        check(false, message);
    }
}

HeapLimits generous_limits()
{
    HeapLimits limits;
    limits.max_live_bytes = 256U * 1024U * 1024U;
    limits.max_string_bytes = 128U * 1024U * 1024U;
    limits.max_single_allocation = 64U * 1024U * 1024U;
    limits.soft_collect_threshold = limits.max_live_bytes;
    limits.max_cells = 200'000;
    limits.max_collection_work = 300'000;
    return limits;
}

void test_primitives_and_every_cell_kind()
{
    Heap heap(generous_limits());
    check(heap.kind(Value::null()) == ValueKind::Null && !heap.truthy(Value::null()), "null is inline and false");
    check(heap.kind(Value(true)) == ValueKind::Boolean && heap.truthy(Value(true)), "boolean is inline");
    check(heap.kind(Value(std::int64_t{42})) == ValueKind::Integer, "integer is inline int64");
    check(heap.kind(Value(2.5)) == ValueKind::Float, "float is inline binary64");

    const auto string = heap.allocate_string("文本");
    const auto list = heap.allocate_list({Value(std::int64_t{1}), string});
    const auto map = heap.allocate_map({{"first", list}, {"second", Value(true)}});
    const auto function = heap.allocate_function({CallableKind::Native, 7, {map}});
    const auto module = heap.allocate_module({"demo", {{"run", function}}});
    ErrorMetadata error_metadata{LanguageErrorCode::HostInternal, "failed", ErrorOrigin::Runtime};
    error_metadata.details.push_back({"payload", map});
    const auto error = heap.allocate_error(std::move(error_metadata));
    const auto task = heap.allocate_task({9, TaskState::Pending, {error, module}});
    const auto host = heap.allocate_host_handle({11, 101, 123, false});

    check(heap.kind(string) == ValueKind::String && heap.string_copy(string.as_heap_ref()) == "文本",
          "immutable UTF-8 string cell should round-trip by copy");
    check(heap.kind(list) == ValueKind::List && heap.list_values(list.as_heap_ref()).size() == 2,
          "list cell should preserve values");
    check(heap.kind(map) == ValueKind::OrderedMap && heap.map_get(map.as_heap_ref(), "first") == list,
          "ordered map should preserve keyed values");
    check(heap.kind(function) == ValueKind::Function &&
              heap.function_metadata(function.as_heap_ref()).kind == CallableKind::Native &&
              heap.function_metadata(function.as_heap_ref()).callable_id == 7,
          "function cell should preserve callable kind, opaque id, and captures");
    check(heap.kind(module) == ValueKind::Module && heap.module_metadata(module.as_heap_ref()).name == "demo",
          "module namespace kind must be present");
    check(heap.kind(error) == ValueKind::Error &&
              heap.error_metadata(error.as_heap_ref()).code == LanguageErrorCode::HostInternal,
          "structured error kind must be present");
    check(heap.kind(task) == ValueKind::Task && heap.task_metadata(task.as_heap_ref()).task_id == 9,
          "task state kind must be present");
    check(heap.kind(host) == ValueKind::HostHandle &&
              heap.host_handle_metadata(host.as_heap_ref()).external_bytes == 123,
          "host wrapper kind must preserve opaque metadata");
    check(!heap.truthy(heap.allocate_string("")) && !heap.truthy(heap.allocate_list()) &&
              !heap.truthy(heap.allocate_map()), "empty string/list/map should be false");
    check(heap.truthy(function) && heap.truthy(error), "identity-bearing values should be true");

    const auto graph_root = heap.add_root(task);
    const auto host_root = heap.add_root(host);
    heap.collect();
    check(heap.stats().live_cells == 8,
          "child visitors should trace every required cell kind and reclaim unrelated empties");
    heap.remove_root(graph_root);
    heap.remove_root(host_root);
    heap.collect();
    check(heap.stats().live_cells == 0, "every required cell kind should be sweepable");
}

void test_ordered_map_and_transactional_mutation()
{
    Heap heap(generous_limits());
    const auto map = heap.allocate_map({{"a", Value(std::int64_t{1})}, {"b", Value(std::int64_t{2})}});
    heap.map_set(map.as_heap_ref(), "a", Value(std::int64_t{3}));
    heap.map_set(map.as_heap_ref(), "c", Value(std::int64_t{4}));
    const auto entries = heap.map_entries(map.as_heap_ref());
    check(entries.size() == 3 && entries[0].first == "a" && entries[1].first == "b" &&
              entries[2].first == "c" && entries[0].second.as_integer() == 3,
          "replacement must not change insertion order and insertion must append");

    Heap probe(generous_limits());
    const auto probe_list = probe.allocate_list();
    const auto before = probe.stats();
    probe.list_append(probe_list.as_heap_ref(), Value(std::int64_t{1}));
    const auto growth = probe.stats().live_bytes - before.live_bytes;

    auto limits = generous_limits();
    limits.max_live_bytes = before.live_bytes + growth - 1;
    limits.soft_collect_threshold = limits.max_live_bytes;
    Heap limited(limits);
    const auto list = limited.allocate_list();
    const auto limited_before = limited.stats();
    check_error(RuntimeErrorCode::MemoryLimitExceeded,
                [&] { limited.list_append(list.as_heap_ref(), Value(std::int64_t{1})); },
                "failed list capacity growth should use the live-byte limit");
    check(limited.list_values(list.as_heap_ref()).empty() &&
              limited.stats().live_bytes == limited_before.live_bytes,
          "failed growth must leave destination and accounting unchanged");

    Heap map_probe(generous_limits());
    const auto probe_map = map_probe.allocate_map();
    const auto map_before = map_probe.stats();
    map_probe.map_set(probe_map.as_heap_ref(), "new", Value(true));
    const auto map_growth = map_probe.stats().live_bytes - map_before.live_bytes;
    auto map_limits = generous_limits();
    map_limits.max_live_bytes = map_before.live_bytes + map_growth - 1;
    map_limits.soft_collect_threshold = map_limits.max_live_bytes;
    Heap map_limited(map_limits);
    const auto limited_map = map_limited.allocate_map();
    const auto map_limited_before = map_limited.stats();
    check_error(RuntimeErrorCode::MemoryLimitExceeded,
                [&] { map_limited.map_set(limited_map.as_heap_ref(), "new", Value(true)); },
                "failed map capacity growth should use the live-byte limit");
    check(map_limited.map_entries(limited_map.as_heap_ref()).empty() &&
              map_limited.stats().live_bytes == map_limited_before.live_bytes,
          "failed map growth must preserve order, content, and accounting");
}

void test_immutable_bytes_value_semantics()
{
    Heap heap(generous_limits());
    const auto before = heap.stats();
    const std::vector<std::byte> payload{
        std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
    const auto value = heap.allocate_bytes(payload);

    check(heap.kind(value) == ValueKind::Bytes &&
              heap.bytes_size(value.as_heap_ref()) == payload.size() &&
              std::ranges::equal(heap.bytes_view(value.as_heap_ref()), payload),
          "immutable byte cell should expose its exact binary payload");
    auto copy = heap.bytes_copy(value.as_heap_ref());
    copy[0] = std::byte{0x55};
    check(heap.bytes_view(value.as_heap_ref())[0] == std::byte{0x00},
          "copying bytes must not expose mutable heap storage");
    check(heap.stats().live_bytes > before.live_bytes &&
              heap.stats().string_bytes == before.string_bytes,
          "byte capacity must count as live memory but not UTF-8 string memory");

    const auto same = heap.allocate_bytes(payload);
    const auto different = heap.allocate_bytes(
        {std::byte{0x00}, std::byte{0x7f}, std::byte{0xfe}});
    const auto empty = heap.allocate_bytes({});
    check(heap.equals(value, same) && !heap.equals(value, different),
          "bytes equality must compare payload values, not heap identity");
    check(heap.truthy(value) && !heap.truthy(empty),
          "bytes truthiness must follow payload emptiness");
    check_error(RuntimeErrorCode::JsonUnsupported,
                [&] { heap.validate_json_safe(value); },
                "bytes must never be JSON-safe");

    auto bounded = generous_limits();
    bounded.max_single_allocation = 64;
    Heap bounded_heap(bounded);
    const auto bounded_before = bounded_heap.stats();
    check_error(RuntimeErrorCode::SingleAllocationExceeded,
                [&] { (void)bounded_heap.allocate_bytes(
                    std::vector<std::byte>(1'024)); },
                "byte cells must obey the single-allocation budget");
    check(bounded_heap.stats().live_cells == bounded_before.live_cells &&
              bounded_heap.stats().live_bytes == bounded_before.live_bytes,
          "rejected byte allocation must leave heap accounting unchanged");
}

void test_roots_cycles_collection_and_generations()
{
    Heap heap(generous_limits());
    const auto self = heap.allocate_list();
    heap.list_append(self.as_heap_ref(), self);
    const auto root = heap.add_root(self);
    heap.collect();
    check(heap.stats().live_cells == 1, "explicit root should retain a self cycle");
    heap.remove_root(root);
    heap.collect();
    check(heap.stats().live_cells == 0, "unrooted self cycle should be swept");
    check_error(RuntimeErrorCode::StaleReference, [&] { (void)heap.kind(self); },
                "swept references must be stale");

    const auto replacement = heap.allocate_list();
    check(replacement.as_heap_ref().slot == self.as_heap_ref().slot &&
              replacement.as_heap_ref().generation != self.as_heap_ref().generation,
          "reused slots must advance generation");

    const auto first = heap.allocate_list();
    const auto second = heap.allocate_list();
    heap.list_append(first.as_heap_ref(), second);
    heap.list_append(second.as_heap_ref(), first);
    const auto mutual_root = heap.add_root(first);
    heap.collect();
    check(heap.stats().live_cells == 2,
          "root should trace the mutual cycle while reclaiming unrelated replacement garbage");
    heap.remove_root(mutual_root);

    Value deep = heap.allocate_list();
    for (int index = 0; index < 10'000; ++index) deep = heap.allocate_list({deep});
    const auto deep_root = heap.add_root(deep);
    heap.collect();
    check(heap.stats().live_cells == 10'001, "non-recursive marker should retain a deep graph");
    heap.remove_root(deep_root);
    heap.collect();
    check(heap.stats().live_cells == 0, "non-recursive sweep should reclaim deep and cyclic graphs");

    const auto scoped = heap.allocate_string("scoped");
    {
        auto roots = heap.root_scope();
        roots.add(scoped);
        heap.collect();
        check(heap.kind(scoped) == ValueKind::String, "RAII root scope should retain local values");
    }
    heap.collect();
    check_error(RuntimeErrorCode::StaleReference, [&] { (void)heap.kind(scoped); },
                "RAII roots should be removed on scope exit");
}

void test_cross_heap_and_kind_validation()
{
    Heap first(generous_limits());
    Heap second(generous_limits());
    check(first.identity() != second.identity(), "live contexts must have distinct opaque heap identities");
    const auto value = first.allocate_string("owned");
    check_error(RuntimeErrorCode::CrossHeapReference, [&] { (void)second.allocate_list({value}); },
                "language edges must reject cross-context references");
    check_error(RuntimeErrorCode::CrossHeapReference, [&] { (void)second.kind(value); },
                "dereference must validate heap identity");
    check_error(RuntimeErrorCode::CellKindMismatch,
                [&] { second.list_append(second.allocate_string("x").as_heap_ref(), Value::null()); },
                "typed APIs must validate cell kind");
}

void test_exact_budgets_and_collect_retry()
{
    Heap probe(generous_limits());
    (void)probe.allocate_string("abc");
    const auto charge = probe.stats();

    auto exact = generous_limits();
    exact.max_live_bytes = charge.live_bytes;
    exact.max_string_bytes = charge.string_bytes;
    exact.max_single_allocation = charge.live_bytes;
    exact.max_cells = 1;
    exact.soft_collect_threshold = exact.max_live_bytes;
    Heap exact_heap(exact);
    (void)exact_heap.allocate_string("abc");
    check(exact_heap.stats().live_bytes == exact.max_live_bytes,
          "allocation should succeed at the exact byte boundary");

    auto byte_short = exact; --byte_short.max_live_bytes; byte_short.soft_collect_threshold = byte_short.max_live_bytes;
    Heap byte_heap(byte_short);
    check_error(RuntimeErrorCode::MemoryLimitExceeded, [&] { (void)byte_heap.allocate_string("abc"); },
                "one byte below exact live charge should fail");
    auto string_short = exact; --string_short.max_string_bytes;
    Heap string_heap(string_short);
    check_error(RuntimeErrorCode::StringLimitExceeded, [&] { (void)string_heap.allocate_string("abc"); },
                "one byte below exact string charge should fail");
    auto allocation_short = exact; --allocation_short.max_single_allocation;
    Heap allocation_heap(allocation_short);
    check_error(RuntimeErrorCode::SingleAllocationExceeded,
                [&] { (void)allocation_heap.allocate_string("abc"); },
                "one byte below exact single-allocation charge should fail");

    auto cell_limits = generous_limits(); cell_limits.max_cells = 1;
    Heap cell_heap(cell_limits);
    const auto cell = cell_heap.allocate_list();
    const auto cell_root = cell_heap.add_root(cell);
    check_error(RuntimeErrorCode::CellLimitExceeded, [&] { (void)cell_heap.allocate_list(); },
                "rooted cell at the exact cell limit should reject another allocation");
    cell_heap.remove_root(cell_root);

    auto external_limits = generous_limits(); external_limits.max_external_bytes = 99;
    Heap external_heap(external_limits);
    check_error(RuntimeErrorCode::ExternalMemoryLimitExceeded,
                [&] { (void)external_heap.allocate_host_handle({1, 1, 100, false}); },
                "external handle charge must be enforced before publication");
    check(external_heap.stats().live_cells == 0 && external_heap.stats().external_bytes == 0,
          "failed external charge must leave heap unchanged");

    auto retry_limits = generous_limits();
    retry_limits.max_cells = 1;
    retry_limits.soft_collect_threshold = 0;
    Heap retry(retry_limits);
    const auto stale = retry.allocate_string("garbage");
    const auto live = retry.allocate_string("retry");
    check(retry.stats().collections >= 2 && retry.stats().live_cells == 1 &&
              retry.string_copy(live.as_heap_ref()) == "retry",
          "soft threshold should collect and retry before enforcing hard cell budget");
    check_error(RuntimeErrorCode::StaleReference, [&] { (void)retry.kind(stale); },
                "collect/retry should invalidate reclaimed references");

    auto work_limits = generous_limits(); work_limits.max_collection_work = 1;
    Heap work_heap(work_limits);
    const auto child = work_heap.allocate_list();
    const auto parent = work_heap.allocate_list({child});
    const auto work_root = work_heap.add_root(parent);
    check_error(RuntimeErrorCode::CollectionWorkLimitExceeded, [&] { work_heap.collect(); },
                "collector must enforce its bounded non-recursive work budget");
    check(work_heap.stats().live_cells == 2,
          "failed marking budget must not partially sweep reachable or unreachable cells");
    work_heap.remove_root(work_root);
}

void test_cycle_equality_identity_and_json_checks()
{
    Heap heap(generous_limits());
    const auto left = heap.allocate_list(); heap.list_append(left.as_heap_ref(), left);
    const auto right = heap.allocate_list(); heap.list_append(right.as_heap_ref(), right);
    check(heap.equals(left, right), "isomorphic self cycles should compare structurally equal");
    heap.list_append(right.as_heap_ref(), Value(std::int64_t{1}));
    check(!heap.equals(left, right), "different cyclic list structures should not compare equal");

    const auto map_a = heap.allocate_map({{"a", Value(std::int64_t{1})}, {"b", Value(true)}});
    const auto map_b = heap.allocate_map({{"b", Value(true)}, {"a", Value(std::int64_t{1})}});
    check(heap.equals(map_a, map_b), "map structural equality should compare keys independent of order");
    const auto cyclic_map_a = heap.allocate_map();
    const auto cyclic_map_b = heap.allocate_map();
    heap.map_set(cyclic_map_a.as_heap_ref(), "self", cyclic_map_a);
    heap.map_set(cyclic_map_b.as_heap_ref(), "self", cyclic_map_b);
    check(heap.equals(cyclic_map_a, cyclic_map_b),
          "isomorphic ordered-map cycles should compare structurally without recursion");
    const auto function_a = heap.allocate_function({CallableKind::Script, 1, {}});
    const auto function_b = heap.allocate_function({CallableKind::Script, 1, {}});
    check(heap.equals(function_a, function_a) && !heap.equals(function_a, function_b),
          "functions should use heap identity equality");
    const auto module_a = heap.allocate_module({"same", {}});
    const auto module_b = heap.allocate_module({"same", {}});
    const auto task_a = heap.allocate_task({1, TaskState::Pending, {}});
    const auto task_b = heap.allocate_task({1, TaskState::Pending, {}});
    const auto host_a = heap.allocate_host_handle({1, 1, 0, false});
    const auto host_b = heap.allocate_host_handle({1, 1, 0, false});
    check(!heap.equals(module_a, module_b) && !heap.equals(task_a, task_b) &&
              !heap.equals(host_a, host_b),
          "module, task and host values should use identity equality");

    heap.validate_json_safe(map_a);
    const auto shared = heap.allocate_list({Value(std::int64_t{1})});
    heap.validate_json_safe(heap.allocate_list({shared, shared}));
    check_error(RuntimeErrorCode::JsonCycle, [&] { heap.validate_json_safe(left); },
                "JSON validation should reject cycles");
    check_error(RuntimeErrorCode::JsonNonFinite,
                [&] { heap.validate_json_safe(Value(std::numeric_limits<double>::infinity())); },
                "JSON validation should reject non-finite floats");
    check_error(RuntimeErrorCode::JsonUnsupported, [&] { heap.validate_json_safe(function_a); },
                "JSON validation should reject identity-bearing values");
}

void test_host_release_queue_and_teardown()
{
    Heap heap(generous_limits());
    const auto explicit_handle = heap.allocate_host_handle({1, 1, 100, false});
    check(heap.close_host_handle(explicit_handle.as_heap_ref()) &&
              !heap.close_host_handle(explicit_handle.as_heap_ref()),
          "host close should be explicit and idempotent");
    auto release = heap.lease_host_release();
    check(release && release->record.handle_id == 1 &&
              release->record.adapter_id == 1 && heap.stats().external_bytes == 100,
          "close should retain its external charge until release ACK");
    check(heap.retry_host_release(release->lease_id) &&
              heap.stats().external_bytes == 100,
          "release retry must retain the record and charge");
    release = heap.lease_host_release();
    check(release && heap.acknowledge_host_release(release->lease_id) &&
              heap.stats().external_bytes == 0,
          "release ACK should debit the transferred external charge once");
    heap.collect();
    check(!heap.lease_host_release(),
          "sweeping a closed wrapper must not queue duplicate release");

    (void)heap.allocate_host_handle({2, 1, 200, false});
    heap.collect();
    release = heap.lease_host_release();
    check(release && release->record.handle_id == 2,
          "sweep should queue release for leaked open wrapper");
    check(heap.acknowledge_host_release(release->lease_id),
          "swept release should be acknowledged");

    (void)heap.allocate_host_handle({3, 2, 300, false});
    const auto releases = heap.teardown();
    check(releases.size() == 1 && releases[0].handle_id == 3 &&
              heap.stats().live_cells == 0,
          "legacy teardown should transfer every leaked handle release record");
    check_error(RuntimeErrorCode::HeapTornDown, [&] { (void)heap.allocate_list(); },
                "teardown should permanently close the heap");

    auto bounded_limits = generous_limits();
    bounded_limits.max_pending_release_records = 1;
    Heap bounded(bounded_limits);
    const auto first = bounded.allocate_host_handle({10, 1, 0, false});
    (void)bounded.allocate_host_handle({11, 1, 0, false});
    check_error(RuntimeErrorCode::ReleaseQueueLimitExceeded, [&] { bounded.collect(); },
                "release queue must be independently bounded before sweep mutates cells");
    check(bounded.stats().live_cells == 2,
          "release queue limit failure must not partially sweep host wrappers");
    check(bounded.close_host_handle(first.as_heap_ref()), "queue can be drained explicitly after backpressure");
    release = bounded.lease_host_release();
    check(release && bounded.acknowledge_host_release(release->lease_id),
          "owning adapter should ACK release backpressure");
    bounded.collect();
    release = bounded.lease_host_release();
    check(release && release->record.handle_id == 11,
          "collection should resume after the owning adapter drains release backpressure");
    check(bounded.acknowledge_host_release(release->lease_id),
          "resumed release should be acknowledgeable");
}

void test_two_independent_heaps_concurrently()
{
    std::atomic<bool> ok{true};
    auto run = [&] {
        try {
            Heap heap(generous_limits());
            for (int round = 0; round < 200; ++round) {
                const auto list = heap.allocate_list();
                heap.list_append(list.as_heap_ref(), list);
                const auto root = heap.add_root(list);
                heap.collect();
                heap.remove_root(root);
                heap.collect();
            }
            if (heap.stats().live_cells != 0) ok.store(false);
        } catch (...) { ok.store(false); }
    };
    std::jthread first(run);
    std::jthread second(run);
    first.join(); second.join();
    check(ok.load(), "independent per-context heaps should collect concurrently on separate strands");
}

}  // namespace

int main()
{
    test_primitives_and_every_cell_kind();
    test_ordered_map_and_transactional_mutation();
    test_immutable_bytes_value_semantics();
    test_roots_cycles_collection_and_generations();
    test_cross_heap_and_kind_validation();
    test_exact_budgets_and_collect_retry();
    test_cycle_equality_identity_and_json_checks();
    test_host_release_queue_and_teardown();
    test_two_independent_heaps_concurrently();
    if (failures != 0) { std::cerr << failures << " assertion(s) failed\n"; return EXIT_FAILURE; }
    std::cout << "All value heap tests passed\n";
    return EXIT_SUCCESS;
}
