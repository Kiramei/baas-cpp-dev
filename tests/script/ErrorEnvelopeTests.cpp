#include "script/runtime/ErrorEnvelope.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace baas::script;
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

HeapLimits generous_heap_limits()
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

ErrorMetadata metadata(const LanguageErrorCode code = LanguageErrorCode::HostValidationFailed,
                       std::string message = "failure")
{
    ErrorMetadata result;
    result.code = code;
    result.message = std::move(message);
    result.origin = ErrorOrigin::Host;
    return result;
}

SourceReference source(const std::size_t offset = 0)
{
    return {"snapshot-a", "workflow/main",
            SourceSpan{{offset, 2, offset + 1}, {offset + 3, 2, offset + 4}}};
}

struct Serialized {
    ErrorEnvelopeResult result;
    std::string bytes;
};

Serialized serialize(const Heap& heap, const Value error,
                     const ErrorEnvelopeLimits& limits = {},
                     const std::size_t capacity = 2U * 1024U * 1024U)
{
    std::vector<char> output(capacity);
    const auto result = serialize_error_envelope(heap, error, output, limits);
    return {result, std::string(output.data(), result.bytes_written)};
}

void check_order(const std::string& bytes, const std::vector<std::string_view>& fields,
                 const std::string_view message)
{
    std::size_t position = 0;
    for (const auto field : fields) {
        const auto found = bytes.find(field, position);
        if (found == std::string::npos) {
            check(false, message);
            return;
        }
        position = found + field.size();
    }
}

void test_stable_minimal_bytes_and_derived_catchability()
{
    Heap heap(generous_heap_limits());
    const auto error = heap.allocate_error(metadata(
        LanguageErrorCode::HostValidationFailed, "bad \"input\"\n\xE7\x95\x8C"));
    const auto serialized = serialize(heap, error);
    const std::string expected =
        "{\"schema\":\"baas.script.error/v1\",\"code\":\"HostValidationFailed\","
        "\"message\":\"bad \\\"input\\\"\\n\xE7\x95\x8C\",\"origin\":\"host\",\"catchable\":true,"
        "\"source\":null,\"stack\":[],\"cause\":null,\"suppressed\":[],\"details\":{},"
        "\"context\":{\"task_id\":null,\"session_id\":null,\"package_id\":null,"
        "\"snapshot_id\":null,\"language_version\":null,\"correlation_id\":null},"
        "\"truncated\":{\"stack_frames\":0,\"cause_errors\":0,"
        "\"suppressed_errors\":0,\"message_bytes\":0,\"detail_bytes\":0,"
        "\"details_replaced\":false,\"fallback\":false}}";
    check(serialized.result.complete(), "minimal Error should serialize without fallback");
    check(serialized.bytes == expected,
          "minimal envelope bytes and all twelve fields must be stable");

    const auto terminal = heap.allocate_error(metadata(
        LanguageErrorCode::DeadlineExceeded, "deadline"));
    const auto terminal_bytes = serialize(heap, terminal).bytes;
    check(terminal_bytes.find("\"code\":\"DeadlineExceeded\"") != std::string::npos &&
              terminal_bytes.find("\"catchable\":false") != std::string::npos,
          "catchability must derive from ERR-004 during serialization");
}

void test_source_frame_nested_error_and_detail_order()
{
    Heap heap(generous_heap_limits());
    const auto leaf = heap.allocate_error(metadata(LanguageErrorCode::Timeout, "leaf"));
    const auto first = heap.allocate_error(metadata(LanguageErrorCode::HostInternal, "first"));
    const auto second = heap.allocate_error(metadata(LanguageErrorCode::Cancelled, "second"));
    const auto list = heap.allocate_list({Value(std::int64_t{7}), heap.allocate_string("safe")});
    const auto map = heap.allocate_map({{"z", Value(true)}, {"a", list}});

    auto primary = metadata(LanguageErrorCode::ArgumentInvalid, "primary");
    primary.origin = ErrorOrigin::Script;
    primary.source = source();
    primary.stack.push_back({ErrorFrameKind::Script, "workflow/main", "run",
                             ErrorFramePhase::Body, source(10), source(20), std::nullopt});
    primary.cause = leaf;
    primary.suppressed = {first, second};
    primary.details = {{"ordered", map}, {"answer", Value(std::int64_t{42})},
                       {"fraction", Value(2.5)}, {"negative_zero", Value(-0.0)}};
    primary.context = {"task-a", "session-a", "package-a", "snapshot-a", "0.1", "corr-a"};
    const auto error = heap.allocate_error(std::move(primary));
    const auto serialized = serialize(heap, error);

    check(serialized.result.complete(), "nested valid Error graph should serialize completely");
    check_order(serialized.bytes,
                {"\"schema\"", "\"code\"", "\"message\"", "\"origin\"",
                 "\"catchable\"", "\"source\"", "\"stack\"", "\"cause\"",
                 "\"suppressed\"", "\"details\"", "\"context\"", "\"truncated\""},
                "ERR-003 top-level field order must be exact");
    check(serialized.bytes.find(
              "\"source\":{\"snapshot_id\":\"snapshot-a\",\"module\":\"workflow/main\","
              "\"span\":{\"begin\":{\"byte_offset\":0,\"line\":2,\"column\":1},"
              "\"end\":{\"byte_offset\":3,\"line\":2,\"column\":4}}}") !=
              std::string::npos,
          "SourceReference nested field order must be stable");
    check(serialized.bytes.find(
              "\"kind\":\"script\",\"module\":\"workflow/main\",\"function\":\"run\","
              "\"phase\":\"body\",\"call_source\":") != std::string::npos,
          "stack-frame allowlist and order must be stable");
    check(serialized.bytes.find(
              "\"details\":{\"ordered\":{\"z\":true,\"a\":[7,\"safe\"]},\"answer\":42,"
              "\"fraction\":2.5,\"negative_zero\":0}") !=
              std::string::npos,
          "nested details, finite floats, and ordered-map insertion order must be byte-stable");
    check(serialized.bytes.find("\"message\":\"leaf\"") <
              serialized.bytes.find("\"message\":\"first\"") &&
              serialized.bytes.find("\"message\":\"first\"") <
              serialized.bytes.find("\"message\":\"second\""),
          "cause and earliest suppressed Errors must retain deterministic order");
}

void test_message_cause_suppressed_and_detail_boundaries()
{
    Heap heap(generous_heap_limits());
    const auto leaf = heap.allocate_error(metadata(LanguageErrorCode::Timeout, "leaf"));
    auto middle_input = metadata(LanguageErrorCode::HostInternal, "middle");
    middle_input.cause = leaf;
    const auto middle = heap.allocate_error(std::move(middle_input));
    const auto first = heap.allocate_error(metadata(LanguageErrorCode::ArgumentInvalid, "first"));
    const auto second = heap.allocate_error(metadata(LanguageErrorCode::TypeMismatch, "second"));

    auto primary = metadata(LanguageErrorCode::HostValidationFailed, "abcd\xE7\x95\x8C");
    primary.cause = middle;
    primary.suppressed = {first, second};
    primary.details = {{"a", heap.allocate_string("012345")}};
    const auto error = heap.allocate_error(std::move(primary));

    ErrorEnvelopeLimits limits;
    limits.max_message_bytes = 6;
    limits.max_cause_depth = 1;
    limits.max_suppressed_errors = 1;
    limits.max_detail_bytes = 13;
    const auto serialized = serialize(heap, error, limits);
    check(serialized.result.complete(), "configured ERR-008 truncation should remain a valid envelope");
    check(serialized.bytes.find("\"message\":\"abcd\"") != std::string::npos &&
              serialized.bytes.find("\"message_bytes\":3") != std::string::npos,
          "message truncation must stop on a UTF-8 scalar boundary and count omitted bytes");
    check(serialized.bytes.find("\"message\":\"middle\"") != std::string::npos &&
              serialized.bytes.find("\"message\":\"leaf\"") == std::string::npos &&
              serialized.bytes.find("\"cause_errors\":1") != std::string::npos,
          "cause limit must preserve nearest causes and count the omitted tail");
    check(serialized.bytes.find("\"message\":\"first\"") != std::string::npos &&
              serialized.bytes.find("\"message\":\"second\"") == std::string::npos &&
              serialized.bytes.find("\"suppressed_errors\":1") != std::string::npos,
          "suppressed limit must preserve the earliest observed failure");
    check(serialized.bytes.find("\"details\":{\"kind\":\"limit\"}") != std::string::npos &&
              serialized.bytes.find("\"detail_bytes\":14") != std::string::npos &&
              serialized.bytes.find("\"details_replaced\":true") != std::string::npos,
          "detail byte limit must replace oversized details with bounded evidence");
}

void test_detail_preflight_prefers_local_marker_in_tight_output_buffer()
{
    Heap heap(generous_heap_limits());
    auto input = metadata(LanguageErrorCode::ArgumentInvalid, "large details");
    input.details = {{"blob", heap.allocate_string(std::string(2000, 'x'))}};
    const auto error = heap.allocate_error(std::move(input));

    ErrorEnvelopeLimits marker_limits;
    marker_limits.max_detail_bytes = 32;
    const auto marker = serialize(heap, error, marker_limits);
    check(marker.result.complete() &&
              marker.bytes.find("\"details\":{\"kind\":\"limit\"}") != std::string::npos &&
              marker.bytes.find("\"detail_bytes\":2011") != std::string::npos,
          "detail preflight must count every byte of the fully replaced original object");

    ErrorEnvelopeLimits original_limits;
    original_limits.max_detail_bytes = 4096;
    const auto original = serialize(heap, error, original_limits);
    check(original.result.complete() && original.bytes.size() > marker.bytes.size(),
          "the untruncated detail envelope should exceed the marker envelope");

    marker_limits.max_output_bytes = marker.bytes.size();
    const auto tight = serialize(heap, error, marker_limits, marker.bytes.size());
    check(tight.result.complete() && tight.bytes == marker.bytes,
          "a buffer that fits only the final marker envelope must not force whole-envelope fallback");
}

void test_invalid_details_are_marked_without_identity_or_secret_leaks()
{
    Heap heap(generous_heap_limits());
    const auto details = heap.allocate_list();
    auto input = metadata(LanguageErrorCode::HostInternal, "safe message");
    input.details = {{"payload", details}};
    input.source = source();
    input.context.task_id = "task-visible";
    input.context.correlation_id = "corr-visible";
    const auto error = heap.allocate_error(std::move(input));
    const auto root = heap.add_root(error);

    const auto secret = heap.allocate_string("DO_NOT_LEAK_CAPTURED_SECRET");
    const auto function = heap.allocate_function({CallableKind::Script, 99, {secret}});
    heap.list_append(details.as_heap_ref(), function);
    heap.list_append(details.as_heap_ref(), Value(std::numeric_limits<double>::infinity()));
    heap.list_append(details.as_heap_ref(), details);

    const auto serialized = serialize(heap, error);
    check(serialized.result.complete(), "invalid nested detail values should be safely replaced");
    check(serialized.bytes.find(
              "\"payload\":[{\"kind\":\"function\"},{\"kind\":\"nonfinite\"},"
              "{\"kind\":\"cycle\"}]") != std::string::npos,
          "unsupported, non-finite, and cyclic details need allowlisted kind markers");
    check(serialized.bytes.find("\"details_replaced\":true") != std::string::npos &&
              serialized.bytes.find("DO_NOT_LEAK_CAPTURED_SECRET") == std::string::npos &&
              serialized.bytes.find("function_id") == std::string::npos &&
              serialized.bytes.find("heap_identity") == std::string::npos,
          "detail replacement must not serialize identity internals or captured payloads");
    check(heap.remove_root(root), "privacy test root should be removable");
}

void test_existing_utf8_and_duplicate_rules_reject_invalid_envelopes_upstream()
{
    Heap heap(generous_heap_limits());
    auto invalid_utf8 = metadata();
    invalid_utf8.message.assign(1, static_cast<char>(0xFF));
    check_error(RuntimeErrorCode::InvalidUtf8,
                [&] { (void)heap.allocate_error(std::move(invalid_utf8)); },
                "invalid UTF-8 Error strings must be rejected before serialization");

    auto duplicates = metadata();
    duplicates.details = {{"same", Value::null()}, {"same", Value(true)}};
    check_error(RuntimeErrorCode::JsonDuplicateKey,
                [&] { (void)heap.allocate_error(std::move(duplicates)); },
                "duplicate Error detail keys must retain the existing JSON rejection rule");
}

template <typename Setter>
std::size_t minimum_complete_limit(const Heap& heap, const Value error, Setter setter,
                                   const std::size_t search_limit)
{
    for (std::size_t value = 0; value <= search_limit; ++value) {
        ErrorEnvelopeLimits limits;
        setter(limits, value);
        if (serialize(heap, error, limits).result.complete()) return value;
    }
    return search_limit + 1;
}

void test_depth_node_work_string_and_output_boundaries()
{
    Heap heap(generous_heap_limits());
    const auto error = heap.allocate_error(metadata(LanguageErrorCode::Timeout, "four"));

    const auto depth = minimum_complete_limit(
        heap, error, [](auto& limits, const auto value) { limits.max_depth = value; }, 32);
    const auto nodes = minimum_complete_limit(
        heap, error, [](auto& limits, const auto value) { limits.max_nodes = value; }, 128);
    const auto work = minimum_complete_limit(
        heap, error, [](auto& limits, const auto value) { limits.max_work = value; }, 256);
    check(depth <= 32 && nodes <= 128 && work <= 256,
          "minimal envelope should have finite discoverable structural budgets");

    ErrorEnvelopeLimits exact;
    exact.max_depth = depth;
    check(serialize(heap, error, exact).result.complete(), "exact depth budget should pass");
    exact.max_depth = depth - 1;
    check(serialize(heap, error, exact).result.used_fallback(),
          "one-below depth budget should fall back");
    exact = {};
    exact.max_nodes = nodes;
    check(serialize(heap, error, exact).result.complete(), "exact node budget should pass");
    exact.max_nodes = nodes - 1;
    check(serialize(heap, error, exact).result.used_fallback(),
          "one-below node budget should fall back");
    exact = {};
    exact.max_work = work;
    check(serialize(heap, error, exact).result.complete(), "exact work budget should pass");
    exact.max_work = work - 1;
    check(serialize(heap, error, exact).result.used_fallback(),
          "one-below work budget should fall back");
    exact = {};
    exact.max_string_bytes = 4;
    check(serialize(heap, error, exact).result.complete(), "exact dynamic string budget should pass");
    exact.max_string_bytes = 3;
    check(serialize(heap, error, exact).result.used_fallback(),
          "one-below dynamic string budget should fall back");

    const auto full = serialize(heap, error);
    exact = {};
    exact.max_output_bytes = full.bytes.size();
    check(serialize(heap, error, exact, full.bytes.size()).result.complete(),
          "exact output byte budget and capacity should pass");
    exact.max_output_bytes = full.bytes.size() - 1;
    check(!serialize(heap, error, exact, full.bytes.size()).result.complete(),
          "one-below output byte budget must never publish a partial normal envelope");
}

void test_fallback_privacy_gc_stale_and_capacity()
{
    Heap heap(generous_heap_limits());
    auto input = metadata(LanguageErrorCode::Timeout, "bounded timeout");
    input.context.task_id = "TASK_MUST_BE_REDACTED";
    input.context.session_id = "SESSION_MUST_BE_REDACTED";
    input.context.correlation_id = "corr-allowlisted";
    const auto error = heap.allocate_error(std::move(input));
    const auto root = heap.add_root(error);
    heap.collect();
    check(serialize(heap, error).result.complete(),
          "a rooted Error and its metadata must survive GC serialization");

    ErrorEnvelopeLimits tiny;
    tiny.max_nodes = 0;
    const auto fallback = serialize(heap, error, tiny);
    check(fallback.result.used_fallback(), "budget exhaustion should return a complete fallback");
    check(fallback.bytes.find("\"code\":\"Timeout\"") != std::string::npos &&
              fallback.bytes.find("\"message\":\"bounded timeout\"") != std::string::npos &&
              fallback.bytes.find("\"correlation_id\":\"corr-allowlisted\"") != std::string::npos &&
              fallback.bytes.find("TASK_MUST_BE_REDACTED") == std::string::npos &&
              fallback.bytes.find("SESSION_MUST_BE_REDACTED") == std::string::npos &&
              fallback.bytes.find("\"fallback\":true") != std::string::npos,
          "fallback must preserve safe primary evidence and only allowlist correlation context");

    Heap foreign(generous_heap_limits());
    check(serialize(foreign, error).result.used_fallback() &&
              serialize(heap, Value(true)).result.used_fallback(),
          "cross-heap and wrong-kind roots must fail closed as corrupt input");

    check(heap.remove_root(root), "GC test root should be removable");
    heap.collect();
    const auto stale = serialize(heap, error);
    check(stale.result.used_fallback() &&
              stale.bytes.find("\"code\":\"InternalInvariant\"") != std::string::npos,
          "stale Error input must fail closed without throwing or dereferencing corrupt state");

    ErrorEnvelopeLimits exact;
    exact.max_output_bytes = stale.bytes.size();
    const auto exact_fallback = serialize(heap, error, exact, stale.bytes.size());
    check(exact_fallback.result.used_fallback() && exact_fallback.bytes == stale.bytes,
          "fallback must fit exactly at its byte boundary");
    exact.max_output_bytes = stale.bytes.size() - 1;
    const auto too_small = serialize(heap, error, exact, stale.bytes.size());
    check(too_small.result.status == ErrorEnvelopeStatus::InsufficientCapacity &&
              too_small.result.bytes_written == 0,
          "a buffer below the minimum full fallback must report zero published bytes");
}

}  // namespace

int main()
{
    test_stable_minimal_bytes_and_derived_catchability();
    test_source_frame_nested_error_and_detail_order();
    test_message_cause_suppressed_and_detail_boundaries();
    test_detail_preflight_prefers_local_marker_in_tight_output_buffer();
    test_invalid_details_are_marked_without_identity_or_secret_leaks();
    test_existing_utf8_and_duplicate_rules_reject_invalid_envelopes_upstream();
    test_depth_node_work_string_and_output_boundaries();
    test_fallback_privacy_gc_stale_and_capacity();
    if (failures != 0) {
        std::cerr << failures << " Error envelope test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Error envelope tests passed\n";
    return EXIT_SUCCESS;
}
