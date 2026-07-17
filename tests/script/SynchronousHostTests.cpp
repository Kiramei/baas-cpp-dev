#include "script/runtime/SynchronousHost.h"

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace baas::script::runtime;

namespace {

int failures = 0;

class TestCancellationProbe final : public HostCancellationProbe {
public:
    [[nodiscard]] bool cancelled() const noexcept override { return cancel; }
    [[nodiscard]] bool deadline_exceeded() const noexcept override { return deadline; }
    bool cancel{};
    bool deadline{};
};

static_assert(static_cast<unsigned>(HostValueType::HostDevice) == 10);
static_assert(static_cast<unsigned>(HostValueType::Bytes) == 11);

void check(const bool condition, const std::string_view message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

template <typename Function>
void check_binding_error(
    const HostBindingErrorCode code, Function&& function, const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const HostBindingError& error) {
        check(error.code() == code, message);
    } catch (...) {
        check(false, message);
    }
}

SynchronousNativeBinding log_binding(SynchronousHostCallback callback)
{
    return {
        "host.log.emit.v1",
        {{{"level", HostValueType::String, true},
          {"message", HostValueType::String, true},
          {"fields", HostValueType::OrderedStringJsonMap, false}},
         HostValueType::Null,
         "log_events",
         HostExecutionMode::ThreadSafe,
         HostCancellationMode::Preflight},
        std::move(callback)};
}

HostError error_with_detail(
    const HostErrorCode code, const char* key, const char* value)
{
    return {code, "safe", false, HostEffectState::NotStarted,
            JsonValue(JsonObject{{key, JsonValue(value)}})};
}

void test_error_taxonomy_and_total_translation()
{
    const std::vector<std::pair<HostErrorCode, LanguageErrorCode>> defaults{
        {HostErrorCode::CapabilityDenied, LanguageErrorCode::CapabilityDenied},
        {HostErrorCode::InvalidArgument, LanguageErrorCode::HostValidationFailed},
        {HostErrorCode::Cancelled, LanguageErrorCode::Cancelled},
        {HostErrorCode::Unavailable, LanguageErrorCode::HostUnavailable},
        {HostErrorCode::IoError, LanguageErrorCode::HostUnavailable},
        {HostErrorCode::DeviceDisconnected, LanguageErrorCode::DeviceDisconnected},
        {HostErrorCode::ConfigConflict, LanguageErrorCode::HostValidationFailed},
        {HostErrorCode::ResourceNotFound, LanguageErrorCode::ResourceMissing},
        {HostErrorCode::ModelUnavailable, LanguageErrorCode::OcrModelUnavailable},
        {HostErrorCode::PolicyDenied, LanguageErrorCode::CapabilityDenied},
        {HostErrorCode::ProtocolError, LanguageErrorCode::HostUnavailable},
        {HostErrorCode::Internal, LanguageErrorCode::HostInternal},
        {HostErrorCode::HandleClosed, LanguageErrorCode::HostValidationFailed},
        {HostErrorCode::Backpressure, LanguageErrorCode::HostUnavailable},
    };
    for (const auto [host, language] : defaults) {
        const HostError error{host, "safe", false, HostEffectState::NotStarted, std::nullopt};
        const auto translated = translate_host_error(error);
        check(translated.declared_status && translated.code == language,
              "default Host status must have the normative language mapping");
        check(host_error_code_name(host).starts_with("HOST"),
              "every Host status must expose its stable HOSTnnn name");
    }

    check(translate_host_error(error_with_detail(
              HostErrorCode::DeadlineExceeded, "deadline_scope", "context")).code ==
              LanguageErrorCode::DeadlineExceeded,
          "context deadline must be terminal DeadlineExceeded");
    check(translate_host_error(error_with_detail(
              HostErrorCode::DeadlineExceeded, "deadline_scope", "call")).code ==
              LanguageErrorCode::Timeout,
          "call deadline must be Timeout");
    check(translate_host_error(error_with_detail(
              HostErrorCode::BudgetExceeded, "budget_scope", "external_memory")).code ==
              LanguageErrorCode::MemoryLimitExceeded,
          "external memory budget must map to MemoryLimitExceeded");
    check(translate_host_error(error_with_detail(
              HostErrorCode::BudgetExceeded, "budget_scope", "host_operation")).code ==
              LanguageErrorCode::TaskLimitExceeded,
          "host operation budget must map to TaskLimitExceeded");

    const HostError missing_deadline{HostErrorCode::DeadlineExceeded, "safe", false,
                                     HostEffectState::Unknown, std::nullopt};
    check(!translate_host_error(missing_deadline).declared_status &&
              translate_host_error(missing_deadline).code == LanguageErrorCode::HostInternal,
          "missing deadline discriminator must be an undeclared adapter status");
    check(!translate_host_error(error_with_detail(
              HostErrorCode::BudgetExceeded, "budget_scope", "wrong")).declared_status,
          "unknown budget discriminator must be an undeclared adapter status");
    HostError invalid{static_cast<HostErrorCode>(99), "safe", false,
                      HostEffectState::NotStarted, std::nullopt};
    check(!translate_host_error(invalid).declared_status,
          "out-of-range Host status must map safely to HostInternal");
}

void test_result_runtime_error_translation()
{
    for (const auto code : {
             RuntimeErrorCode::TypeMismatch,
             RuntimeErrorCode::InvalidUtf8,
             RuntimeErrorCode::JsonCycle,
             RuntimeErrorCode::JsonNonFinite,
             RuntimeErrorCode::JsonUnsupported,
             RuntimeErrorCode::JsonDepthLimitExceeded,
             RuntimeErrorCode::JsonNodeLimitExceeded,
             RuntimeErrorCode::JsonStringLimitExceeded,
             RuntimeErrorCode::JsonByteLimitExceeded,
             RuntimeErrorCode::JsonWorkLimitExceeded,
             RuntimeErrorCode::JsonDuplicateKey}) {
        check(translate_host_result_runtime_error(code) == LanguageErrorCode::HostInternal,
              "invalid successful Host results must translate to HostInternal");
    }
    for (const auto code : {
             RuntimeErrorCode::MemoryLimitExceeded,
             RuntimeErrorCode::CellLimitExceeded,
             RuntimeErrorCode::SingleAllocationExceeded,
             RuntimeErrorCode::StringLimitExceeded,
             RuntimeErrorCode::ExternalMemoryLimitExceeded,
             RuntimeErrorCode::CollectionWorkLimitExceeded}) {
        check(translate_host_result_runtime_error(code) ==
                  LanguageErrorCode::MemoryLimitExceeded,
              "terminal heap failures while publishing Host results must remain memory errors");
    }
    for (const auto code : {
             RuntimeErrorCode::CrossHeapReference,
             RuntimeErrorCode::StaleReference,
             RuntimeErrorCode::CellKindMismatch,
             RuntimeErrorCode::HeapTornDown,
             RuntimeErrorCode::HeapBusy,
             RuntimeErrorCode::IndexOutOfRange,
             RuntimeErrorCode::ReleaseQueueLimitExceeded}) {
        check(translate_host_result_runtime_error(code) ==
                  LanguageErrorCode::InternalInvariant,
              "invalid evaluator state while publishing Host results must remain internal");
    }
}

void test_immutable_binding_set_validation()
{
    auto callback = [](const HostCallContext&, const HostArguments&) {
        return HostResult::success();
    };
    SynchronousNativeBindingSet set({log_binding(callback)});
    check(set.size() == 1 && set.find("host.log.emit.v1") != nullptr &&
              set.find("host.log.missing.v1") == nullptr,
          "immutable binding set must provide exact binding-id lookup");

    auto duplicate = log_binding(callback);
    check_binding_error(HostBindingErrorCode::DuplicateBinding,
        [&] { SynchronousNativeBindingSet ignored({log_binding(callback), duplicate}); },
        "duplicate binding ids must be rejected");
    auto no_callback = log_binding({});
    check_binding_error(HostBindingErrorCode::MissingCallback,
        [&] { SynchronousNativeBindingSet ignored({no_callback}); },
        "missing callbacks must be rejected");
    auto strand = log_binding(callback);
    strand.contract.execution = HostExecutionMode::ContextStrand;
    check_binding_error(HostBindingErrorCode::UnsupportedExecutionMode,
        [&] { SynchronousNativeBindingSet ignored({strand}); },
        "synchronous slice must reject strand execution rather than faking affinity");
    auto cooperative = log_binding(callback);
    cooperative.contract.cancellation = HostCancellationMode::Cooperative;
    SynchronousNativeBindingSet cooperative_set({cooperative});
    check(cooperative_set.find(cooperative.binding_id) != nullptr,
          "synchronous callbacks must accept cooperative contracts with an injected probe");
    auto invalid_cancellation = log_binding(callback);
    invalid_cancellation.contract.cancellation = static_cast<HostCancellationMode>(99);
    check_binding_error(HostBindingErrorCode::UnsupportedCancellationMode,
        [&] { SynchronousNativeBindingSet ignored({invalid_cancellation}); },
        "out-of-range cancellation modes must be rejected");
    auto duplicate_parameter = log_binding(callback);
    duplicate_parameter.contract.parameters.push_back(
        {"level", HostValueType::String, false});
    check_binding_error(HostBindingErrorCode::DuplicateParameter,
        [&] { SynchronousNativeBindingSet ignored({duplicate_parameter}); },
        "duplicate parameter names must be rejected before publication");
    auto invalid_order = log_binding(callback);
    invalid_order.contract.parameters.push_back({"required_late", HostValueType::String, true});
    check_binding_error(HostBindingErrorCode::InvalidParameter,
        [&] { SynchronousNativeBindingSet ignored({invalid_order}); },
        "required parameters must not follow optional parameters");
}

void test_owning_scalar_and_json_conversion()
{
    Heap heap;
    const auto string = heap.allocate_string("owned");
    const auto list = heap.allocate_list({Value(std::int64_t{7}), string});
    const auto map = heap.allocate_map({{"payload", list}});

    const auto copied_string = heap_to_host_value(heap, string, HostValueType::String);
    const auto copied_json = heap_to_host_value(heap, map, HostValueType::Json);
    heap.map_set(map.as_heap_ref(), "payload", Value::null());
    check(std::get<std::string>(copied_string.storage()) == "owned" &&
              std::get<JsonValue>(copied_json.storage()).kind() == JsonKind::Object,
          "Host values must own scalar and JSON data independently of the heap");

    const auto round_trip = host_to_heap_value(heap, copied_json, HostValueType::Json);
    check(heap.kind(round_trip) == ValueKind::OrderedMap && round_trip != map,
          "Host JSON results must materialize as fresh heap graphs");

    try {
        (void)heap_to_host_value(heap, Value(true), HostValueType::String);
        check(false, "scalar type mismatch must fail before callback entry");
    } catch (const RuntimeError& error) {
        check(error.code() == RuntimeErrorCode::TypeMismatch,
              "scalar type mismatch must use the bounded runtime error");
    }
    const auto function = heap.allocate_function({CallableKind::Native, 1, {}});
    try {
        (void)heap_to_host_value(heap, function, HostValueType::Json);
        check(false, "identity-bearing values must not cross JSON-safe ABI");
    } catch (const RuntimeError& error) {
        check(error.code() == RuntimeErrorCode::JsonUnsupported,
              "identity-bearing values must be rejected by JSON conversion");
    }
}

void test_owning_bytes_conversion_and_measurement()
{
    Heap heap;
    const std::vector<std::byte> payload{
        std::byte{0x00}, std::byte{0x80}, std::byte{0xff}};
    const auto bytes = heap.allocate_bytes(payload);
    const auto host = heap_to_host_value(heap, bytes, HostValueType::Bytes);
    check(host.type() == HostValueType::Bytes &&
              std::get<std::vector<std::byte>>(host.storage()) == payload,
          "Host byte arguments must own an exact binary copy");

    const auto round_trip = host_to_heap_value(heap, host, HostValueType::Bytes);
    check(heap.kind(round_trip) == ValueKind::Bytes && round_trip != bytes &&
              heap.bytes_copy(round_trip.as_heap_ref()) == payload,
          "Host byte results must materialize as fresh immutable byte cells");

    const auto metrics = measure_host_value(host);
    check(metrics.nodes == 1 && metrics.work == 1 &&
              metrics.string_bytes == 0 && metrics.total_bytes == payload.size() + 1,
          "Host byte measurement must charge one kind byte plus payload bytes");

    JsonBridgeLimits tight;
    tight.max_total_bytes = payload.size();
    try {
        (void)measure_host_value(host, tight);
        check(false, "Host byte measurement must enforce total-byte limits");
    } catch (const RuntimeError& error) {
        check(error.code() == RuntimeErrorCode::JsonByteLimitExceeded,
              "oversized Host bytes must use the stable aggregate-byte failure");
    }
    try {
        (void)host_to_heap_value(heap, host, HostValueType::Bytes, tight);
        check(false, "Host byte result conversion must preflight aggregate bytes");
    } catch (const RuntimeError& error) {
        check(error.code() == RuntimeErrorCode::JsonByteLimitExceeded,
              "Host byte result conversion must fail before heap allocation");
    }
    try {
        (void)heap_to_host_value(heap, bytes, HostValueType::Bytes, tight);
        check(false, "Host byte argument conversion must preflight aggregate bytes");
    } catch (const RuntimeError& error) {
        check(error.code() == RuntimeErrorCode::JsonByteLimitExceeded,
              "Host byte argument conversion must fail before callback entry");
    }

    auto binding = SynchronousNativeBinding{
        "host.resource.echo_bytes.v1",
        {{{"payload", HostValueType::Bytes, true}}, HostValueType::Bytes,
         "resource_bytes", HostExecutionMode::ThreadSafe,
         HostCancellationMode::Preflight},
        [&](const HostCallContext&, const HostArguments& arguments) {
            check(arguments.size() == 1 && arguments[0] &&
                      arguments[0]->type() == HostValueType::Bytes &&
                      std::get<std::vector<std::byte>>(
                          arguments[0]->storage()) == payload,
                  "guarded Host callback must receive owning bytes");
            return HostResult::success(HostValue(payload));
        }};
    SynchronousNativeBindingSet bindings({binding});
    check(bindings.find(binding.binding_id) != nullptr,
          "binding validation must accept appended Bytes contracts");
    const HostCallContext context{
        "baas/resource", "echo_bytes", binding.binding_id, {1, 0}, 1};
    const auto callback_result = invoke_host_callback(
        binding, context, {HostValue(payload)}, SynchronousHostLimits{});
    check(callback_result.ok() && callback_result.value().type() == HostValueType::Bytes &&
              std::get<std::vector<std::byte>>(
                  callback_result.value().storage()) == payload,
          "guarded Host callback must return owning bytes");

    SynchronousHostLimits callback_limits;
    callback_limits.json_limits.max_total_bytes = payload.size();
    const auto rejected = invoke_host_callback(
        binding, context, {HostValue(payload)}, callback_limits);
    check(!rejected.ok() && !rejected.has_error() &&
              rejected.boundary_failure() ==
                  HostResult::BoundaryFailure::CallbackException,
          "guarded Host callback must reject an oversized byte result");
}

void test_guarded_callback_result_and_exception_redaction()
{
    const HostCallContext context{"baas/log", "emit", "host.log.emit.v1", {1, 0}, 1};
    HostArguments arguments{HostValue("info"), HostValue("message"), std::nullopt};
    SynchronousHostLimits limits;

    auto good = log_binding([](const HostCallContext& call, const HostArguments& args) {
        check(call.selected_version == HostApiVersion{1, 0} && args.size() == 3,
              "callback receives owning arguments and exact selected version");
        return HostResult::success();
    });
    check(invoke_host_callback(good, context, arguments, limits).ok(),
          "valid null result must pass the guarded callback boundary");

    auto wrong_result = log_binding([](const HostCallContext&, const HostArguments&) {
        return HostResult::success(HostValue("wrong"));
    });
    const auto invalid = invoke_host_callback(wrong_result, context, arguments, limits);
    check(!invalid.ok() && !invalid.has_error() &&
              invalid.boundary_failure() == HostResult::BoundaryFailure::CallbackException,
          "adapter result contract violations must become safe HostInternal");

    auto throwing = log_binding([](const HostCallContext&, const HostArguments&) -> HostResult {
        throw std::runtime_error("secret credential and command line");
    });
    const auto redacted = invoke_host_callback(throwing, context, arguments, limits);
    check(!redacted.ok() && !redacted.has_error() &&
              translate_host_boundary_failure(redacted.boundary_failure()) ==
                  LanguageErrorCode::HostInternal,
          "std::exception diagnostics must not cross the Host ABI");

    auto allocation = log_binding([](const HostCallContext&, const HostArguments&) -> HostResult {
        throw std::bad_alloc();
    });
    const auto allocation_error = invoke_host_callback(allocation, context, arguments, limits);
    check(!allocation_error.ok() && !allocation_error.has_error() &&
              translate_host_boundary_failure(allocation_error.boundary_failure()) ==
                  LanguageErrorCode::MemoryLimitExceeded,
          "callback bad_alloc must be contained and deterministically map to MemoryLimitExceeded");

    auto invalid_error = log_binding([](const HostCallContext&, const HostArguments&) {
        return HostResult::failure({HostErrorCode::Internal, std::string(5'000, 'x'), false,
                                    HostEffectState::Unknown, std::nullopt});
    });
    const auto sanitized = invoke_host_callback(invalid_error, context, arguments, limits);
    check(!sanitized.ok() && !sanitized.has_error() &&
              sanitized.boundary_failure() == HostResult::BoundaryFailure::CallbackException,
          "oversized adapter messages must be replaced, not forwarded");
}

void test_preflight_and_cooperative_cancellation()
{
    auto probe = std::make_shared<TestCancellationProbe>();
    std::size_t calls{};
    auto cooperative = log_binding([&](const HostCallContext& context,
                                       const HostArguments&) {
        ++calls;
        check(context.cancellation == probe && !context.cancelled() &&
                  !context.deadline_exceeded(),
              "cooperative callback must observe the exact immutable probe");
        return HostResult::success();
    });
    cooperative.contract.cancellation = HostCancellationMode::Cooperative;
    const HostArguments arguments{
        HostValue("info"), HostValue("message"), std::nullopt};
    HostCallContext context{
        "baas/log", "emit", cooperative.binding_id, {1, 0}, 1, probe};
    check(invoke_host_callback(cooperative, context, arguments, {}).ok() && calls == 1,
          "cooperative callback must run when its probe is clear");

    probe->cancel = true;
    const auto cancelled = invoke_host_callback(
        cooperative, context, arguments, {});
    check(cancelled.has_error() &&
              cancelled.error().code == HostErrorCode::Cancelled && calls == 1,
          "cancelled work must fail before callback entry");

    probe->deadline = true;
    const auto deadline = invoke_host_callback(
        cooperative, context, arguments, {});
    check(deadline.has_error() &&
              deadline.error().code == HostErrorCode::DeadlineExceeded && calls == 1 &&
              translate_host_error(deadline.error()).code == LanguageErrorCode::Timeout,
          "call deadline must take deterministic precedence and retain its scope detail");
}

void test_contract_shapes_utf8_and_strict_string_limit()
{
    Heap heap;
    const auto scalar = Value(std::int64_t{1});
    try {
        (void)heap_to_host_value(
            heap, scalar, HostValueType::OrderedStringJsonMap);
        check(false, "log fields must reject scalar JSON before callback entry");
    } catch (const RuntimeError& error) {
        check(error.code() == RuntimeErrorCode::TypeMismatch,
              "ordered-map shape mismatch must be a bounded type failure");
    }
    const HostValue object(JsonValue(JsonObject{
        {"key", JsonValue(std::int64_t{7})}}));
    const auto object_value = host_to_heap_value(
        heap, object, HostValueType::OrderedStringJsonMap);
    check(heap.kind(object_value) == ValueKind::OrderedMap &&
              heap.map_get(object_value.as_heap_ref(), "key")->as_integer() == 7,
          "ordered-map Host storage must validate and round-trip through the shared JSON variant");

    SynchronousHostLimits limits;
    limits.max_string_bytes = 3;
    limits.json_limits.max_string_bytes = 100;
    check(effective_host_json_limits(limits).max_string_bytes == 3,
          "Host conversion must use the stricter duplicate string limit");
    limits.max_string_bytes = 100;
    limits.json_limits.max_string_bytes = 2;
    check(effective_host_json_limits(limits).max_string_bytes == 2,
          "JSON bridge limit must narrow the outer Host string limit");

    const HostCallContext context{"baas/log", "emit", "host.log.emit.v1", {1, 0}, 1};
    HostArguments arguments{HostValue("info"), HostValue("message"), std::nullopt};
    auto invalid_utf8 = log_binding([](const HostCallContext&, const HostArguments&) {
        return HostResult::failure({HostErrorCode::Internal, std::string("\xC0\xAF", 2), false,
                                    HostEffectState::Unknown, std::nullopt});
    });
    const auto result = invoke_host_callback(invalid_utf8, context, arguments, limits);
    check(!result.ok() && !result.has_error(),
          "invalid UTF-8 safe messages must not cross the native guard");
}

}  // namespace

int main()
{
    test_error_taxonomy_and_total_translation();
    test_result_runtime_error_translation();
    test_immutable_binding_set_validation();
    test_owning_scalar_and_json_conversion();
    test_owning_bytes_conversion_and_measurement();
    test_guarded_callback_result_and_exception_redaction();
    test_preflight_and_cooperative_cancellation();
    test_contract_shapes_utf8_and_strict_string_limit();
    if (failures != 0) {
        std::cerr << failures << " synchronous Host test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "synchronous Host ABI tests passed\n";
    return EXIT_SUCCESS;
}
