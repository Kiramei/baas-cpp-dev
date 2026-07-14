#include "script/runtime/ValueHeap.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace runtime = baas::script::runtime;
using namespace baas::script;
using namespace runtime;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

template <typename Function>
void check_error(const RuntimeErrorCode code, Function&& function,
                 const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const RuntimeError& error) {
        check(error.code() == code, message);
    } catch (...) {
        check(false, message);
    }
}

HeapLimits generous_limits()
{
    HeapLimits limits;
    limits.max_live_bytes = 64U * 1024U * 1024U;
    limits.max_string_bytes = 32U * 1024U * 1024U;
    limits.max_single_allocation = 16U * 1024U * 1024U;
    limits.soft_collect_threshold = limits.max_live_bytes;
    limits.max_cells = 100'000;
    limits.max_collection_work = 200'000;
    return limits;
}

SourceReference source(const std::size_t offset = 0)
{
    return SourceReference{
        "snapshot-a",
        "workflow/main",
        SourceSpan{{offset, 1, offset + 1}, {offset + 2, 1, offset + 3}},
    };
}

ErrorStackFrame frame(const std::string& function, const std::size_t offset)
{
    return ErrorStackFrame{
        ErrorFrameKind::Script,
        "workflow/main",
        function,
        ErrorFramePhase::Body,
        source(offset),
        source(offset + 10),
        std::nullopt,
    };
}

ErrorMetadata metadata(const LanguageErrorCode code = LanguageErrorCode::HostInternal,
                       std::string message = "failure")
{
    ErrorMetadata result;
    result.code = code;
    result.message = std::move(message);
    result.origin = ErrorOrigin::Runtime;
    return result;
}

void test_stable_codes_and_derived_catchability()
{
    constexpr auto first = static_cast<std::size_t>(LanguageErrorCode::ThrownValue);
    constexpr auto last = static_cast<std::size_t>(LanguageErrorCode::InternalInvariant);
    check(first == 0 && last + 1 == 42, "stable language Error inventory should contain 42 codes");
    for (auto index = first; index <= last; ++index) {
        const auto code = static_cast<LanguageErrorCode>(index);
        const auto name = language_error_code_name(code);
        check(!name.empty() && language_error_code_from_name(name) == code,
              "stable language Error names should round-trip");
        const auto terminal = index >= static_cast<std::size_t>(LanguageErrorCode::Cancelled);
        check(language_error_code_catchable(code) == !terminal,
              "catchability must derive only from the stable code table");
    }
    check(!language_error_code_from_name("CallerChosenError"),
          "arbitrary caller-controlled Error codes must be rejected");

    Heap heap(generous_limits());
    const auto catchable = heap.allocate_error(metadata(LanguageErrorCode::Timeout));
    const auto terminal = heap.allocate_error(metadata(LanguageErrorCode::DeadlineExceeded));
    check(heap.error_metadata(catchable.as_heap_ref()).catchable(),
          "Timeout should derive catchable=true");
    check(!heap.error_metadata(terminal.as_heap_ref()).catchable(),
          "DeadlineExceeded should derive catchable=false");
    auto invalid = metadata();
    invalid.code = static_cast<LanguageErrorCode>(999);
    check_error(RuntimeErrorCode::CellKindMismatch,
                [&] { (void)heap.allocate_error(std::move(invalid)); },
                "unknown enum values must not publish an Error");
}

void test_complete_metadata_and_read_only_snapshot()
{
    Heap heap(generous_limits());
    const auto detail = heap.allocate_list({Value(std::int64_t{7}),
                                            heap.allocate_string("safe")});
    auto input = metadata(LanguageErrorCode::HostValidationFailed, "bad argument");
    input.origin = ErrorOrigin::Host;
    input.source = source();
    input.stack.push_back(frame("run", 2));
    input.stack.push_back(ErrorStackFrame{
        ErrorFrameKind::Host, "baas/device", "tap", ErrorFramePhase::Host,
        source(20), std::nullopt, std::nullopt,
    });
    input.details.push_back({"argument", detail});
    input.context.task_id = "task-7";
    input.context.session_id = "session-a";
    input.context.package_id = "package-a";
    input.context.snapshot_id = "snapshot-a";
    input.context.language_version = "0.1";
    input.context.correlation_id = "corr-a";

    const auto error = heap.allocate_error(std::move(input));
    auto snapshot = heap.error_metadata(error.as_heap_ref());
    check(snapshot.code_name() == "HostValidationFailed" && snapshot.catchable(),
          "metadata should expose stable code and derived catchability");
    check(snapshot.origin == ErrorOrigin::Host && snapshot.source == source(),
          "origin and SourceReference should round-trip");
    check(snapshot.stack.size() == 2 && snapshot.stack[0].function == "run" &&
              snapshot.stack[1].kind == ErrorFrameKind::Host,
          "bounded immutable script/host frames should round-trip");
    check(snapshot.details.size() == 1 && snapshot.details[0].first == "argument" &&
              snapshot.details[0].second == detail,
          "insertion-ordered JSON-safe details should round-trip");
    check(snapshot.context.task_id == "task-7" && snapshot.context.correlation_id == "corr-a",
          "allowlisted context fields should round-trip");

    snapshot.message = "mutated copy";
    snapshot.stack.clear();
    snapshot.details.clear();
    const auto reread = heap.error_metadata(error.as_heap_ref());
    check(reread.message == "bad argument" && reread.stack.size() == 2 &&
              reread.details.size() == 1,
          "published Error metadata must remain read-only behind snapshot APIs");
}

void test_deterministic_truncation_and_cause_budget()
{
    auto limits = generous_limits();
    limits.max_error_message_bytes = 6;
    limits.max_error_stack_frames = 3;
    limits.max_error_innermost_frames = 2;
    limits.max_error_suppressed = 2;
    limits.max_error_detail_bytes = 8;
    limits.max_error_cause_depth = 1;
    Heap heap(limits);

    const auto first = heap.allocate_error(metadata(LanguageErrorCode::HostInternal, "first"));
    const auto second = heap.allocate_error(metadata(LanguageErrorCode::Timeout, "second"));
    const auto third = heap.allocate_error(metadata(LanguageErrorCode::Cancelled, "third"));
    const auto detail = heap.allocate_string("0123456789");
    auto bounded = metadata(LanguageErrorCode::HostInternal, "abcd界");
    for (std::size_t index = 0; index < 5; ++index) {
        bounded.stack.push_back(frame("f" + std::to_string(index), index * 20));
    }
    bounded.suppressed = {first, second, third};
    bounded.details.push_back({"x", detail});
    const auto value = heap.allocate_error(std::move(bounded));
    const auto result = heap.error_metadata(value.as_heap_ref());
    check(result.message == "abcd" && result.truncated.message_bytes == 3,
          "message truncation must preserve a valid UTF-8 boundary");
    check(result.stack.size() == 3 && result.stack[0].function == "f0" &&
              result.stack[1].function == "f1" && result.stack[2].function == "f4" &&
              result.truncated.stack_frames == 2,
          "stack truncation must keep configured inner and outer frames deterministically");
    check(result.suppressed == std::vector<Value>({first, second}) &&
              result.truncated.suppressed_errors == 1,
          "suppressed truncation must keep earliest failures in order");
    check(result.details.empty() && result.truncated.details_replaced &&
              result.truncated.detail_bytes == 11,
          "oversized details must be replaced with exact omitted-byte evidence");

    auto with_cause = metadata();
    with_cause.cause = first;
    const auto depth_one = heap.allocate_error(std::move(with_cause));
    auto too_deep = metadata();
    too_deep.cause = depth_one;
    check_error(RuntimeErrorCode::CellKindMismatch,
                [&] { (void)heap.allocate_error(std::move(too_deep)); },
                "cause construction must enforce the bounded immutable depth");
}

void test_cross_heap_invalid_edges_and_obvious_cycles()
{
    Heap first(generous_limits());
    Heap second(generous_limits());
    const auto foreign_error = first.allocate_error(metadata());
    const auto foreign_detail = first.allocate_string("foreign");

    auto cross_cause = metadata();
    cross_cause.cause = foreign_error;
    check_error(RuntimeErrorCode::CrossHeapReference,
                [&] { (void)second.allocate_error(std::move(cross_cause)); },
                "cause edges must reject another heap");
    auto cross_detail = metadata();
    cross_detail.details.push_back({"value", foreign_detail});
    check_error(RuntimeErrorCode::CrossHeapReference,
                [&] { (void)second.allocate_error(std::move(cross_detail)); },
                "detail edges must reject another heap");

    const auto not_error = second.allocate_string("not-error");
    auto wrong_kind = metadata();
    wrong_kind.suppressed.push_back(not_error);
    check_error(RuntimeErrorCode::CellKindMismatch,
                [&] { (void)second.allocate_error(std::move(wrong_kind)); },
                "cause and suppressed edges must target Error cells");

    auto duplicate_relation = metadata();
    const auto local_error = second.allocate_error(metadata());
    duplicate_relation.cause = local_error;
    duplicate_relation.suppressed.push_back(local_error);
    check_error(RuntimeErrorCode::CellKindMismatch,
                [&] { (void)second.allocate_error(std::move(duplicate_relation)); },
                "the same Error cannot be both cause and suppressed");

    auto forged_self = metadata();
    Heap empty(generous_limits());
    forged_self.cause = Value(HeapRef{empty.identity(), 0, 1});
    check_error(RuntimeErrorCode::StaleReference,
                [&] { (void)empty.allocate_error(std::move(forged_self)); },
                "an unpublished forged self reference must be rejected before allocation");

    auto bad_source = metadata();
    bad_source.source = SourceReference{"snapshot-a", "C:\\secret\\file.baas", {}};
    check_error(RuntimeErrorCode::TypeMismatch,
                [&] { (void)second.allocate_error(std::move(bad_source)); },
                "SourceReference must not accept host paths or extensions");

    auto duplicate_details = metadata();
    duplicate_details.details = {{"same", Value::null()}, {"same", Value(true)}};
    check_error(RuntimeErrorCode::JsonDuplicateKey,
                [&] { (void)second.allocate_error(std::move(duplicate_details)); },
                "details must reject duplicate keys");

    const auto cycle = second.allocate_list();
    second.list_append(cycle.as_heap_ref(), cycle);
    auto cyclic_details = metadata();
    cyclic_details.details.push_back({"cycle", cycle});
    check_error(RuntimeErrorCode::JsonCycle,
                [&] { (void)second.allocate_error(std::move(cyclic_details)); },
                "details must reject cyclic JSON graphs with bounded traversal");
}

void test_gc_edges_identity_and_immutable_derivation()
{
    Heap heap(generous_limits());
    const auto leaf = heap.allocate_string("leaf");
    const auto detail = heap.allocate_list({leaf});
    const auto cause = heap.allocate_error(metadata(LanguageErrorCode::HostInternal, "cause"));
    const auto suppressed = heap.allocate_error(metadata(LanguageErrorCode::Timeout, "secondary"));
    auto primary_metadata = metadata(LanguageErrorCode::HostValidationFailed, "primary");
    primary_metadata.cause = cause;
    primary_metadata.suppressed.push_back(suppressed);
    primary_metadata.details.push_back({"payload", detail});
    const auto primary = heap.allocate_error(std::move(primary_metadata));
    const auto root = heap.add_root(primary);
    heap.collect();
    check(heap.stats().live_cells == 5,
          "Error cause, suppressed, details, and nested detail Values must be traced");

    const auto extra = heap.allocate_error(metadata(LanguageErrorCode::Cancelled, "cleanup"));
    const auto derived = heap.derive_error(primary.as_heap_ref(), {{}, {extra}});
    const auto original = heap.error_metadata(primary.as_heap_ref());
    const auto copied = heap.error_metadata(derived.as_heap_ref());
    check(original.suppressed.size() == 1 && copied.suppressed.size() == 2 &&
              copied.suppressed.back() == extra,
          "derivation must append to a new Error without mutating the primary");
    check(heap.equals(primary, primary) && !heap.equals(primary, derived),
          "Error equality must use heap identity, not structural content");
    check_error(RuntimeErrorCode::CellKindMismatch,
                [&] { (void)heap.derive_error(primary.as_heap_ref(), {{}, {primary}}); },
                "derivation must reject a primary-as-suppressed self relation");

    heap.remove_root(root);
    heap.collect();
    check(heap.stats().live_cells == 0,
          "unrooted immutable Error graphs and cycles-free edges must be reclaimable");
}

void test_dynamic_error_metadata_is_heap_budgeted()
{
    auto make = [] {
        auto value = metadata(LanguageErrorCode::HostInternal, std::string(200, 'm'));
        value.source = source();
        value.stack.push_back(frame("budgeted", 2));
        value.context.correlation_id = std::string(100, 'c');
        return value;
    };
    Heap probe(generous_limits());
    (void)probe.allocate_error(make());
    const auto charge = probe.stats();

    auto short_limits = generous_limits();
    short_limits.max_live_bytes = charge.live_bytes - 1;
    short_limits.soft_collect_threshold = short_limits.max_live_bytes;
    Heap limited(short_limits);
    check_error(RuntimeErrorCode::MemoryLimitExceeded,
                [&] { (void)limited.allocate_error(make()); },
                "all dynamic Error metadata must participate in exact heap byte budgets");
    check(limited.stats().live_cells == 0 && limited.stats().live_bytes == 0,
          "failed Error publication must leave heap accounting unchanged");

    auto string_short_limits = generous_limits();
    string_short_limits.max_string_bytes = charge.string_bytes - 1;
    Heap string_limited(string_short_limits);
    check_error(RuntimeErrorCode::StringLimitExceeded,
                [&] { (void)string_limited.allocate_error(make()); },
                "message/source/frame/context strings must all use the heap string budget");
}

}  // namespace

int main()
{
    test_stable_codes_and_derived_catchability();
    test_complete_metadata_and_read_only_snapshot();
    test_deterministic_truncation_and_cause_budget();
    test_cross_heap_invalid_edges_and_obvious_cycles();
    test_gc_edges_identity_and_immutable_derivation();
    test_dynamic_error_metadata_is_heap_budgeted();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All structured Error heap tests passed\n";
    return EXIT_SUCCESS;
}
