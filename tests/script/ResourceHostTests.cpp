#include "script/host/ResourceHost.h"
#include "script/runtime/SynchronousEvaluator.h"

#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <string_view>

namespace host = baas::script::host;
namespace resources = baas::resources;
namespace runtime = baas::script::runtime;
namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::shared_ptr<const std::vector<std::byte>> bytes(const std::string_view text)
{
    auto result = std::make_shared<std::vector<std::byte>>();
    result->reserve(text.size());
    for (const auto value : text)
        result->push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    return result;
}

resources::ResourcePayload payload(
    std::string id,
    const std::string_view text,
    std::optional<std::string> locale = std::nullopt)
{
    auto value = bytes(text);
    return {std::move(id), std::move(locale), std::nullopt,
            "application/octet-stream", value->size(),
            resources::sha256_hex(*value), std::move(value)};
}

std::shared_ptr<const resources::ResourceSnapshot> snapshot()
{
    std::string binary{"BAAS"};
    binary.push_back('\0');
    binary += "CN";
    return resources::ResourceSnapshot::build(
        {"CN", std::nullopt},
        {payload("binary/sample", binary, "CN"),
         payload("binary/sample", "BAAS-JP", "JP"),
         payload("json/generic", "{}")});
}

runtime::SynchronousHostOptions options(const host::ResourceHostRuntime& owner)
{
    runtime::SynchronousHostOptions result;
    result.metadata = owner.metadata;
    result.bindings = owner.bindings;
    result.handles = owner.handles;
    result.permissions.declared_modules.push_back({"baas/resource", 1, 0});
    result.permissions.declared_capabilities.push_back("resource.read");
    result.permissions.policy_capabilities.push_back("resource.read");
    result.permissions.platform_capabilities.push_back("resource.read");
    result.permissions.task_capabilities.push_back("resource.read");
    return result;
}

template <class Function>
void expect_evaluation(
    const runtime::LanguageErrorCode code,
    Function&& function,
    const std::string_view message)
{
    try {
        std::forward<Function>(function)();
        check(false, message);
    } catch (const runtime::EvaluationError& error) {
        check(error.code() == code, message);
    } catch (...) {
        check(false, message);
    }
}

void test_catalog_contract_and_invalid_lookup()
{
    const auto snap = snapshot();
    const auto owner = host::make_resource_host_runtime(snap);
    runtime::Heap heap;
    owner.handles->attach_context(heap);
    check(owner.handles->snapshot_id() == snap->numeric_snapshot_id(),
          "ResourceHost dispatcher must use the immutable snapshot identity");
    const auto resolution = owner.metadata->resolve({
        {{"baas/resource", 1, 0}}, {"resource.read"},
        {{"baas/resource", {"resolve", "read"}}},
        {"resource.read"}, {"resource.read"}, {"resource.read"}});
    check(resolution.modules.size() == 1 &&
              resolution.modules[0].bindings.size() == 2 &&
              resolution.modules[0].bindings[0].binding_id == "host.resource.read.v1" &&
              resolution.modules[0].bindings[1].binding_id == "host.resource.resolve.v1",
          "baas/resource 1.0 must publish the exact catalog bindings");
    const auto* resolve = owner.bindings->find("host.resource.resolve.v1");
    const auto* read = owner.bindings->find("host.resource.read.v1");
    check(resolve && resolve->contract.result == runtime::HostValueType::HostResource &&
              resolve->contract.cancellation == runtime::HostCancellationMode::Preflight &&
              read && read->contract.result == runtime::HostValueType::Bytes &&
              read->contract.cancellation == runtime::HostCancellationMode::Cooperative,
          "native resolve/read contracts must match host-capabilities.v1");

    const runtime::HostCallContext context{
        "baas/resource", "resolve", "host.resource.resolve.v1", {1, 0}, 1};
    const auto invalid = runtime::invoke_host_callback(
        *resolve, context,
        {runtime::HostValue("../ambient"), std::nullopt},
        owner.bindings->limits(), owner.handles.get());
    const auto missing = runtime::invoke_host_callback(
        *resolve, context,
        {runtime::HostValue("image/missing"), std::nullopt},
        owner.bindings->limits(), owner.handles.get());
    check(invalid.has_error() &&
              invalid.error().code == runtime::HostErrorCode::InvalidArgument &&
              missing.has_error() &&
              missing.error().code == runtime::HostErrorCode::ResourceNotFound,
          "invalid IDs and absent manifest entries must remain distinguishable");
    check(owner.handles->teardown(heap),
          "catalog fixture dispatcher must teardown without native owners");
}

void test_evaluator_resolve_read_close_vertical()
{
    auto owner = host::make_resource_host_runtime(snapshot());
    runtime::HeapLimits heap_limits;
    heap_limits.max_external_bytes = 1U * 1024U * 1024U;
    runtime::SynchronousEvaluator evaluator(
        {{"main",
          "import \"baas/resource\" as resource;\n"
          "let h = resource.resolve(\"binary/sample\");\n"
          "let result = resource.read(h, 7);\n"
          "h.close(); h.close();\n"}},
        options(owner), {}, heap_limits);
    (void)evaluator.execute("main");
    const auto result = evaluator.module_export("main", "result");
    const auto output = evaluator.heap().bytes_copy(result.as_heap_ref());
    check(output.size() == 7 && output[4] == std::byte{0} &&
              output[5] == std::byte{'C'} && output[6] == std::byte{'N'},
          "producer -> script -> consumer must preserve exact binary bytes including NUL");
    const auto stats = owner.host->stats();
    check(stats.open_handles == 0 && stats.resolved_handles == 1 &&
              stats.released_handles == 1 && stats.read_calls == 1 &&
              stats.read_bytes == 7,
          "script close must release the native snapshot entry exactly once");
    check(evaluator.close() && owner.handles->destruction_safe(),
          "resource evaluator teardown must complete all release ownership");

    auto jp_owner = host::make_resource_host_runtime(snapshot());
    runtime::SynchronousEvaluator jp(
        {{"main",
          "import \"baas/resource\" as resource;\n"
          "let h = resource.resolve(\"binary/sample\", \"JP\");\n"
          "let result = resource.read(h, 7);\n"}},
        options(jp_owner));
    (void)jp.execute("main");
    const auto jp_bytes = jp.heap().bytes_copy(
        jp.module_export("main", "result").as_heap_ref());
    check(jp_bytes.size() == 7 && jp_bytes.back() == std::byte{'P'},
          "explicit locale must select the immutable locale variant");
    check(jp.close() && jp_owner.host->stats().released_handles == 1,
          "teardown must release an unclosed Resource handle");
}

void test_budget_and_cancellation_fail_closed()
{
    host::ResourceHostLimits limits;
    limits.max_single_read_bytes = 4;
    limits.max_total_read_bytes = 8;
    auto owner = host::make_resource_host_runtime(snapshot(), limits);
    runtime::SynchronousEvaluator evaluator(
        {{"main",
          "import \"baas/resource\" as resource;\n"
          "let h = resource.resolve(\"binary/sample\");\n"
          "let result = resource.read(h, 7);\n"}},
        options(owner));
    expect_evaluation(runtime::LanguageErrorCode::MemoryLimitExceeded, [&] {
        (void)evaluator.execute("main");
    }, "resource byte exhaustion must use the external-memory budget scope");
    check(evaluator.close() && owner.host->stats().released_handles == 1,
          "failed reads must still release their produced Resource handle");

    struct Probe final : runtime::HostCancellationProbe {
        bool cancel{};
        bool deadline{};
        [[nodiscard]] bool cancelled() const noexcept override { return cancel; }
        [[nodiscard]] bool deadline_exceeded() const noexcept override { return deadline; }
    };
    auto direct = host::make_resource_host_runtime(snapshot());
    runtime::Heap heap;
    direct.handles->attach_context(heap);
    const auto* resolve = direct.bindings->find("host.resource.resolve.v1");
    const auto* read = direct.bindings->find("host.resource.read.v1");
    const runtime::HostCallContext resolve_context{
        "baas/resource", "resolve", resolve->binding_id, {1, 0}, 1};
    auto produced = runtime::invoke_host_callback(
        *resolve, resolve_context,
        {runtime::HostValue("binary/sample"), std::nullopt},
        direct.bindings->limits(), direct.handles.get());
    const auto published = runtime::host_to_heap_value(
        heap, produced.value(), runtime::HostValueType::HostResource,
        {}, direct.handles.get());

    auto borrow = runtime::heap_to_host_value(
        heap, published, runtime::HostValueType::HostResource,
        {}, direct.handles.get());
    const runtime::HostCallContext bounded_context{
        "baas/resource", "read", read->binding_id, {1, 0}, 2};
    const auto zero_budget = runtime::invoke_host_callback(
        *read, bounded_context,
        {std::move(borrow), runtime::HostValue(std::int64_t{0})},
        direct.bindings->limits(), direct.handles.get());
    check(zero_budget.has_error() &&
              zero_budget.error().code == runtime::HostErrorCode::BudgetExceeded &&
              runtime::translate_host_error(zero_budget.error()).code ==
                  runtime::LanguageErrorCode::MemoryLimitExceeded,
          "non-positive max_bytes must stay within the declared external-memory error set");

    auto probe = std::make_shared<Probe>();
    probe->cancel = true;
    runtime::HostCallContext read_context{
        "baas/resource", "read", read->binding_id, {1, 0}, 2, probe};
    borrow = runtime::heap_to_host_value(
        heap, published, runtime::HostValueType::HostResource,
        {}, direct.handles.get());
    const auto cancelled = runtime::invoke_host_callback(
        *read, read_context,
        {std::move(borrow), runtime::HostValue(std::int64_t{7})},
        direct.bindings->limits(), direct.handles.get());
    check(cancelled.has_error() &&
              cancelled.error().code == runtime::HostErrorCode::Cancelled &&
              direct.host->stats().read_bytes == 0,
          "pre-cancelled cooperative read must perform no byte work");

    probe->cancel = false;
    probe->deadline = true;
    borrow = runtime::heap_to_host_value(
        heap, published, runtime::HostValueType::HostResource,
        {}, direct.handles.get());
    const auto deadline = runtime::invoke_host_callback(
        *read, read_context,
        {std::move(borrow), runtime::HostValue(std::int64_t{7})},
        direct.bindings->limits(), direct.handles.get());
    check(deadline.has_error() &&
              deadline.error().code == runtime::HostErrorCode::DeadlineExceeded &&
              !deadline.error().retryable &&
              direct.host->stats().read_bytes == 0,
          "expired cooperative read must perform no byte work");
    (void)heap.close_host_handle(published.as_heap_ref());
    direct.handles->dispatch_all(heap);
    check(direct.handles->teardown(heap) && direct.host->stats().released_handles == 1,
          "direct cancellation fixture must retain reliable handle teardown");
}

void test_thread_safe_snapshot_across_execution_contexts()
{
    const auto shared = snapshot();
    const auto worker = [shared](const std::string locale) {
        auto owner = host::make_resource_host_runtime(shared);
        runtime::SynchronousEvaluator evaluator(
            {{"main",
              "import \"baas/resource\" as resource;\n"
              "let h = resource.resolve(\"binary/sample\", \"" + locale + "\");\n"
              "let result = resource.read(h, 7);\n"}},
            options(owner));
        (void)evaluator.execute("main");
        const auto output = evaluator.heap().bytes_copy(
            evaluator.module_export("main", "result").as_heap_ref());
        return evaluator.close() && output.size() == 7;
    };
    auto cn = std::async(std::launch::async, worker, "CN");
    auto jp = std::async(std::launch::async, worker, "JP");
    check(cn.get() && jp.get(),
          "thread-safe Resource adapters must run concurrently across owning contexts");
}

void test_host_uses_snapshot_validation_limits()
{
    resources::ResourceSnapshotLimits limits;
    limits.max_resource_id_bytes = 1'200;
    const std::string long_id(1'100, 'a');
    const auto shared = resources::ResourceSnapshot::build(
        {"CN", std::nullopt}, {payload(long_id, "value")}, limits);
    auto owner = host::make_resource_host_runtime(shared);
    runtime::Heap heap;
    owner.handles->attach_context(heap);
    const auto* resolve = owner.bindings->find("host.resource.resolve.v1");
    const runtime::HostCallContext context{
        "baas/resource", "resolve", resolve->binding_id, {1, 0}, 1};
    const auto result = runtime::invoke_host_callback(
        *resolve, context, {runtime::HostValue(long_id), std::nullopt},
        owner.bindings->limits(), owner.handles.get());
    check(result.ok(),
          "Resource Host validation must use the limits frozen into its snapshot");
    if (result.ok())
        owner.handles->abandon(
            std::get<runtime::HostHandleValue>(result.value().storage()));
    owner.handles->dispatch_all(heap);
    check(owner.handles->teardown(heap),
          "custom-limit Host fixture must release its unpublished handle");
}

}  // namespace

int main()
{
    try {
        test_catalog_contract_and_invalid_lookup();
        test_evaluator_resolve_read_close_vertical();
        test_budget_and_cancellation_fail_closed();
        test_thread_safe_snapshot_across_execution_contexts();
        test_host_uses_snapshot_validation_limits();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "script Resource Host tests passed\n";
    return EXIT_SUCCESS;
}
