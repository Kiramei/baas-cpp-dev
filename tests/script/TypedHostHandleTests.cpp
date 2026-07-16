#include "script/runtime/SynchronousEvaluator.h"

#include <cstdlib>
#include <atomic>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace baas::script;

namespace {

int failures{};
std::string executable_path;

std::string quote_shell_argument(const std::string_view value)
{
#ifdef _WIN32
    return '"' + std::string(value) + '"';
#else
    std::string result{"'"};
    for (const auto character : value) {
        if (character == '\'') result += "'\\''";
        else result += character;
    }
    result += '\'';
    return result;
#endif
}

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Function>
void expect_runtime(const runtime::RuntimeErrorCode code, Function&& function,
                    const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const runtime::RuntimeError& error) {
        check(error.code() == code, message);
    } catch (...) {
        check(false, message);
    }
}

template <typename Function>
void expect_evaluation(const runtime::LanguageErrorCode code, Function&& function,
                       const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const runtime::EvaluationError& error) {
        check(error.code() == code, message);
    } catch (...) {
        check(false, message);
    }
}

static_assert(static_cast<unsigned>(runtime::HostValueType::Null) == 0);
static_assert(static_cast<unsigned>(runtime::HostValueType::Boolean) == 1);
static_assert(static_cast<unsigned>(runtime::HostValueType::Integer) == 2);
static_assert(static_cast<unsigned>(runtime::HostValueType::Float) == 3);
static_assert(static_cast<unsigned>(runtime::HostValueType::String) == 4);
static_assert(static_cast<unsigned>(runtime::HostValueType::Json) == 5);
static_assert(static_cast<unsigned>(runtime::HostValueType::OrderedStringJsonMap) == 6);
static_assert(static_cast<unsigned>(runtime::HostHandleTypeId::Resource) == 1);
static_assert(static_cast<unsigned>(runtime::HostHandleTypeId::Image) == 2);
static_assert(static_cast<unsigned>(runtime::HostHandleTypeId::OcrModel) == 3);
static_assert(static_cast<unsigned>(runtime::HostHandleTypeId::Device) == 4);

runtime::HeapLimits limits()
{
    runtime::HeapLimits result;
    result.max_live_bytes = 8U * 1024U * 1024U;
    result.max_cells = 4096;
    result.max_external_bytes = 8U * 1024U * 1024U;
    result.soft_collect_threshold = result.max_live_bytes;
    result.max_pending_release_records = 128;
    return result;
}

void test_transactional_external_memory_and_exact_identity()
{
    std::size_t releases{};
    runtime::Heap heap(limits());
    runtime::HostReleaseDispatcher dispatcher(
        91, {{7, [&](const runtime::HostHandleValue& value) {
            ++releases;
            check(value.type_id() == runtime::HostHandleTypeId::Resource,
                  "release adapter must receive the exact type");
            return true;
        }}});
    dispatcher.attach_context(heap);

    {
        auto reservation = dispatcher.reserve(
            runtime::HostHandleTypeId::Resource, 7, 64);
        check(reservation.active() && heap.stats().external_bytes == 64,
              "native creation must reserve external bytes first");
    }
    check(heap.stats().external_bytes == 0,
          "abandoned native reservation must roll back atomically");

    auto reservation = dispatcher.reserve(runtime::HostHandleTypeId::Resource, 7, 96);
    auto grant = dispatcher.adopt(std::move(reservation));
    const auto published = dispatcher.publish(
        heap, grant, runtime::HostHandleTypeId::Resource);
    check(!grant.usable() && heap.stats().external_bytes == 96,
          "published grant must be consumed while its charge remains owned");
    check(heap.close_host_handle(published.as_heap_ref()) &&
              !heap.close_host_handle(published.as_heap_ref()),
          "explicit close must be idempotent");
    check(heap.stats().external_bytes == 96 && releases == 0,
          "close must queue without Host I/O or early debit");
    dispatcher.dispatch_all(heap);
    check(releases == 1 && heap.stats().external_bytes == 0,
          "release ACK must run and debit exactly once");

    expect_runtime(runtime::RuntimeErrorCode::TypeMismatch, [&] {
        (void)dispatcher.publish(heap, grant, runtime::HostHandleTypeId::Resource);
    }, "a consumed producer grant must not be republished");
}

void test_callback_borrow_revoke_and_before_callback_validation()
{
    std::size_t releases{};
    std::size_t callbacks{};
    std::optional<runtime::HostHandleValue> escaped;
    runtime::Heap heap(limits());
    runtime::HostReleaseDispatcher dispatcher(
        92, {{8, [&](const runtime::HostHandleValue&) { ++releases; return true; }}});
    dispatcher.attach_context(heap);
    auto grant = dispatcher.adopt(dispatcher.reserve(
        runtime::HostHandleTypeId::Resource, 8, 32));
    const auto published = dispatcher.publish(
        heap, grant, runtime::HostHandleTypeId::Resource);

    runtime::SynchronousNativeBinding consume{
        "host.test.consume.v1",
        {{{"value", runtime::HostValueType::HostResource, true}},
         runtime::HostValueType::Null, "test.consume",
         runtime::HostExecutionMode::ThreadSafe,
         runtime::HostCancellationMode::Preflight},
        [&](const runtime::HostCallContext&, const runtime::HostArguments& arguments) {
            ++callbacks;
            escaped = std::get<runtime::HostHandleValue>(arguments[0]->storage());
            check(escaped->usable() && escaped->handle_id() != 0,
                  "borrow must be usable during callback entry");
            return runtime::HostResult::success();
        }};
    runtime::HostArguments arguments;
    arguments.emplace_back(runtime::HostValue(dispatcher.borrow(
        heap, published, runtime::HostHandleTypeId::Resource)));
    const auto result = runtime::invoke_host_callback(
        consume, {}, arguments, {}, &dispatcher);
    check(result.ok() && callbacks == 1 && escaped && !escaped->usable(),
          "all copied borrows must be centrally revoked at callback return");
    check(!(*escaped == *escaped),
          "revoked borrow equality must not expose cross-callback identity");
    try {
        (void)escaped->handle_id();
        check(false, "escaped borrow access must fail after callback return");
    } catch (const std::logic_error&) {
    }

    check(heap.close_host_handle(published.as_heap_ref()),
          "revoked escaped copy must no longer pin release");
    dispatcher.dispatch_all(heap);
    check(releases == 1, "release must proceed despite an escaped callback copy");

    runtime::Heap wrong_heap(limits());
    runtime::HostReleaseDispatcher wrong_dispatcher(
        92, {{8, [](const runtime::HostHandleValue&) { return true; }}});
    wrong_dispatcher.attach_context(wrong_heap);
    expect_runtime(runtime::RuntimeErrorCode::TypeMismatch, [&] {
        (void)wrong_dispatcher.borrow(
            heap, published, runtime::HostHandleTypeId::Resource);
    }, "cross-context handle must fail before callback entry");
}

void test_ack_tombstone_gc_json_and_error_details()
{
    std::size_t release_calls{};
    runtime::Heap heap(limits());
    runtime::HostReleaseDispatcher dispatcher(
        93, {{9, [&](const runtime::HostHandleValue&) {
            ++release_calls;
            if (release_calls == 1)
                (void)heap.retry_host_release(1); // inject ACK failure
            return true;
        }}});
    dispatcher.attach_context(heap);
    auto grant = dispatcher.adopt(dispatcher.reserve(
        runtime::HostHandleTypeId::Image, 9, 55));
    auto published = dispatcher.publish(heap, grant, runtime::HostHandleTypeId::Image);
    const auto nested = heap.allocate_list({published});
    expect_runtime(runtime::RuntimeErrorCode::JsonUnsupported, [&] {
        heap.validate_json_safe(nested);
    }, "HostHandle must be prohibited recursively in JSON");
    expect_runtime(runtime::RuntimeErrorCode::JsonUnsupported, [&] {
        runtime::ErrorMetadata error;
        error.details.push_back({"nested", nested});
        (void)heap.allocate_error(std::move(error));
    }, "HostHandle must be prohibited recursively in Error details at construction");

    published = runtime::Value::null();
    heap.collect();
    check(release_calls == 0 && heap.stats().external_bytes == 55,
          "collector must queue handles without performing Host I/O");
    check(!dispatcher.dispatch_one(heap) && release_calls == 1 &&
              heap.stats().external_bytes == 55,
          "native success with failed ACK must retain charge and tombstone");
    dispatcher.dispatch_all(heap);
    check(release_calls == 1 && heap.stats().external_bytes == 0,
          "ACK retry must not call native release a second time");
}

void test_producer_faults_round_robin_and_detached_retry()
{
    bool first_adapter_ready{};
    std::size_t first_calls{};
    std::size_t second_calls{};
    runtime::Heap heap(limits());
    auto dispatcher = std::make_shared<runtime::HostReleaseDispatcher>(
        94, std::vector<runtime::HostReleaseAdapter>{
            {10, [&](const runtime::HostHandleValue&) {
                ++first_calls; return first_adapter_ready;
            }},
            {11, [&](const runtime::HostHandleValue&) {
                ++second_calls; return true;
            }}});
    dispatcher->attach_context(heap);

    std::optional<runtime::HostHandleValue> escaped_grant;
    runtime::SynchronousNativeBinding wrong_result{
        "host.test.wrong.v1",
        {{}, runtime::HostValueType::Integer, "test.wrong",
         runtime::HostExecutionMode::ThreadSafe,
         runtime::HostCancellationMode::Preflight},
        [&](const runtime::HostCallContext&, const runtime::HostArguments&) {
            auto grant = dispatcher->adopt(dispatcher->reserve(
                runtime::HostHandleTypeId::Resource, 10, 40));
            escaped_grant = grant;
            return runtime::HostResult::success(runtime::HostValue(std::move(grant)));
        }};
    const auto invalid = runtime::invoke_host_callback(
        wrong_result, {}, {}, {}, dispatcher.get());
    check(!invalid.ok() && escaped_grant && !escaped_grant->usable(),
          "wrong producer result must revoke every escaped grant copy");

    auto second = dispatcher->adopt(dispatcher->reserve(
        runtime::HostHandleTypeId::Device, 11, 20));
    dispatcher->abandon(second);
    dispatcher->dispatch_all(heap);
    check(second_calls == 1 && heap.stats().external_bytes == 40,
          "one failing unpublished adapter must not starve another adapter");

    check(!dispatcher->teardown(heap),
          "teardown must report retryable adapter ownership explicitly");
    const auto pending = dispatcher->stats();
    check(pending.pending_releases == 1 && pending.pending_external_bytes == 40 &&
              !pending.teardown_complete,
          "failed teardown must preserve detached record and charge evidence");
    first_adapter_ready = true;
    dispatcher->retry_detached_releases();
    const auto complete = dispatcher->stats();
    check(complete.teardown_complete && complete.pending_releases == 0 &&
              complete.pending_external_bytes == 0,
          "dispatcher must complete retry without the Heap queue");
}

runtime::SynchronousHostOptions handle_options(
    const std::shared_ptr<runtime::HostReleaseDispatcher>& dispatcher,
    std::size_t& consume_calls)
{
    auto metadata = std::make_shared<const runtime::HostModuleRegistry>(
        std::vector<runtime::HostModuleDescriptor>{{
            "baas/handles", {1, 0},
            {{"make", "host.handles.make.v1", "handles.use"},
             {"consume", "host.handles.consume.v1", "handles.use"},
             {"consume_image", "host.handles.consume_image.v1", "handles.use"}}}});
    std::vector<runtime::SynchronousNativeBinding> bindings;
    bindings.push_back({
        "host.handles.make.v1",
        {{}, runtime::HostValueType::HostResource, "handles.make",
         runtime::HostExecutionMode::ThreadSafe,
         runtime::HostCancellationMode::Preflight},
        [dispatcher](const runtime::HostCallContext&, const runtime::HostArguments&) {
            auto reservation = dispatcher->reserve(
                runtime::HostHandleTypeId::Resource, 12, 24);
            return runtime::HostResult::success(runtime::HostValue(
                dispatcher->adopt(std::move(reservation))));
        }});
    bindings.push_back({
        "host.handles.consume.v1",
        {{{"value", runtime::HostValueType::HostResource, true}},
         runtime::HostValueType::Integer, "handles.consume",
         runtime::HostExecutionMode::ThreadSafe,
         runtime::HostCancellationMode::Preflight},
        [&consume_calls](const runtime::HostCallContext&,
                         const runtime::HostArguments& arguments) {
            ++consume_calls;
            const auto& handle = std::get<runtime::HostHandleValue>(
                arguments[0]->storage());
            check(handle.transfer_kind() == runtime::HostHandleTransferKind::BorrowedReference,
                  "evaluator consumer must receive only a borrowed handle ref");
            return runtime::HostResult::success(runtime::HostValue(std::int64_t{17}));
        }});
    bindings.push_back({
        "host.handles.consume_image.v1",
        {{{"value", runtime::HostValueType::HostImage, true}},
         runtime::HostValueType::Integer, "handles.consume",
         runtime::HostExecutionMode::ThreadSafe,
         runtime::HostCancellationMode::Preflight},
        [&consume_calls](const runtime::HostCallContext&, const runtime::HostArguments&) {
            ++consume_calls;
            return runtime::HostResult::success(runtime::HostValue(std::int64_t{0}));
        }});
    runtime::SynchronousHostOptions result;
    result.metadata = std::move(metadata);
    result.bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::move(bindings));
    result.handles = dispatcher;
    result.permissions.declared_modules.push_back({"baas/handles", 1, 0});
    result.permissions.declared_capabilities.push_back("handles.use");
    result.permissions.policy_capabilities.push_back("handles.use");
    result.permissions.platform_capabilities.push_back("handles.use");
    result.permissions.task_capabilities.push_back("handles.use");
    return result;
}

void test_evaluator_vertical_and_close()
{
    std::size_t release_calls{};
    std::size_t consume_calls{};
    auto dispatcher = std::make_shared<runtime::HostReleaseDispatcher>(
        95, std::vector<runtime::HostReleaseAdapter>{{
            12, [&](const runtime::HostHandleValue&) { ++release_calls; return true; }}});
    runtime::SynchronousEvaluator evaluator(
        {{"main",
          "import \"baas/handles\" as handles;\n"
          "let h = handles.make();\n"
          "let result = handles.consume(h);\n"
          "h.close(); h.close();\n"}},
        handle_options(dispatcher, consume_calls));
    (void)evaluator.execute("main");
    check(evaluator.module_export("main", "result").as_integer() == 17 &&
              consume_calls == 1 && release_calls == 1,
          "producer -> script -> consumer -> idempotent close must be vertical");
    check(evaluator.close(), "successful evaluator close must complete all release ownership");
    expect_evaluation(runtime::LanguageErrorCode::HostUnavailable, [&] {
        (void)evaluator.execute("main");
    }, "closed evaluator must permanently reject new execution");

    std::size_t wrong_calls{};
    std::size_t wrong_releases{};
    auto wrong_dispatcher = std::make_shared<runtime::HostReleaseDispatcher>(
        96, std::vector<runtime::HostReleaseAdapter>{{
            12, [&](const runtime::HostHandleValue&) { ++wrong_releases; return true; }}});
    runtime::SynchronousEvaluator wrong(
        {{"main",
          "import \"baas/handles\" as handles;\n"
          "let h = handles.make(); handles.consume_image(h);\n"}},
        handle_options(wrong_dispatcher, wrong_calls));
    expect_evaluation(runtime::LanguageErrorCode::HostValidationFailed, [&] {
        (void)wrong.execute("main");
    }, "wrong exact host<T> type must fail in evaluator conversion");
    check(wrong_calls == 0, "wrong-type validation must precede consumer callback");
    check(wrong.close() && wrong_releases == 1,
          "failed script unwind must still release the produced native handle once");
}

void test_external_reservation_can_outlive_heap_safely()
{
    std::optional<runtime::Heap::ExternalReservation> escaped;
    {
        runtime::Heap heap(limits());
        escaped.emplace(heap.reserve_host_external(7));
    }
    escaped.reset();
    check(true, "shared external ledger must prevent reservation-after-Heap UAF");
}

void test_forged_stale_and_multi_argument_rollback()
{
    std::size_t callbacks{};
    std::size_t releases{};
    runtime::Heap heap(limits());
    runtime::HostReleaseDispatcher dispatcher(
        97, {{13, [&](const runtime::HostHandleValue&) { ++releases; return true; }}});
    dispatcher.attach_context(heap);
    auto first_grant = dispatcher.adopt(dispatcher.reserve(
        runtime::HostHandleTypeId::Resource, 13, 0));
    const auto first = dispatcher.publish(
        heap, first_grant, runtime::HostHandleTypeId::Resource);
    const auto authentic = heap.host_handle_metadata(first.as_heap_ref());

    const auto reject = [&](runtime::HostHandleMetadata metadata,
                            const std::string_view message) {
        const auto forged = heap.allocate_host_handle(std::move(metadata));
        expect_runtime(runtime::RuntimeErrorCode::TypeMismatch, [&] {
            (void)runtime::heap_to_host_value(
                heap, forged, runtime::HostValueType::HostResource, {}, &dispatcher);
            ++callbacks;
        }, message);
        (void)heap.close_host_handle(forged.as_heap_ref());
        (void)heap.drain_release_queue();
    };
    auto forged = authentic;
    forged.authentication ^= 1;
    reject(forged, "forged authentication must fail before callback");
    forged = authentic;
    ++forged.generation;
    reject(forged, "stale generation must fail before callback");
    forged = authentic;
    ++forged.snapshot_id;
    reject(forged, "wrong snapshot must fail before callback");
    forged = authentic;
    ++forged.adapter_id;
    reject(forged, "wrong adapter must fail before callback");
    forged = authentic;
    forged.type_id = runtime::HostHandleTypeId::Image;
    reject(forged, "wrong native type must fail before callback");
    check(callbacks == 0, "all forged native identities must be rejected before callback");

    auto second_grant = dispatcher.adopt(dispatcher.reserve(
        runtime::HostHandleTypeId::Resource, 13, 0));
    const auto second = dispatcher.publish(
        heap, second_grant, runtime::HostHandleTypeId::Resource);
    try {
        std::vector<runtime::HostValue> converted;
        converted.push_back(runtime::heap_to_host_value(
            heap, first, runtime::HostValueType::HostResource, {}, &dispatcher));
        (void)runtime::heap_to_host_value(
            heap, second, runtime::HostValueType::HostImage, {}, &dispatcher);
        check(false, "later wrong parameter must reject conversion");
    } catch (const runtime::RuntimeError&) {
    }
    check(heap.close_host_handle(first.as_heap_ref()),
          "earlier successful borrow must roll back when a later parameter fails");
    dispatcher.dispatch_all(heap);
    check(releases == 1, "rolled-back earlier borrow must not pin release");
    (void)heap.close_host_handle(second.as_heap_ref());
    dispatcher.dispatch_all(heap);
    check(releases == 2 && dispatcher.destruction_safe(),
          "validation rollback test must release every authentic native owner");
}

void test_limits_published_fairness_and_poisoned_completion()
{
    auto bounded = limits();
    bounded.max_external_bytes = 5;
    bounded.max_single_allocation = std::numeric_limits<std::size_t>::max();
    runtime::Heap limited(bounded);
    expect_runtime(runtime::RuntimeErrorCode::ExternalMemoryLimitExceeded, [&] {
        (void)limited.reserve_host_external(6);
    }, "external reservation must fail closed at its limit");
    check(limited.stats().external_bytes == 0,
          "failed external reservation must not mutate accounting");

    auto queue_limits = limits();
    queue_limits.max_pending_release_records = 1;
    runtime::Heap queued(queue_limits);
    const auto one = queued.allocate_host_handle({1, 1, 10, false});
    const auto two = queued.allocate_host_handle({2, 1, 10, false});
    check(queued.close_host_handle(one.as_heap_ref()), "first close should occupy queue");
    expect_runtime(runtime::RuntimeErrorCode::ReleaseQueueLimitExceeded, [&] {
        (void)queued.close_host_handle(two.as_heap_ref());
    }, "queue-full close must fail before ownership mutation");
    check(!queued.host_handle_metadata(two.as_heap_ref()).closed &&
              queued.stats().external_bytes == 20,
          "queue-full handle must remain Open and charged");

    bool first_ready{};
    std::size_t first_calls{};
    std::size_t second_calls{};
    runtime::Heap heap(limits());
    runtime::HostReleaseDispatcher dispatcher(
        98, {{14, [&](const runtime::HostHandleValue&) {
                  ++first_calls; return first_ready;
              }},
             {15, [&](const runtime::HostHandleValue&) {
                  ++second_calls; return true;
              }}});
    dispatcher.attach_context(heap);
    auto grant_a = dispatcher.adopt(dispatcher.reserve(
        runtime::HostHandleTypeId::Resource, 14, 30));
    auto grant_b = dispatcher.adopt(dispatcher.reserve(
        runtime::HostHandleTypeId::Resource, 15, 40));
    const auto a = dispatcher.publish(heap, grant_a, runtime::HostHandleTypeId::Resource);
    const auto b = dispatcher.publish(heap, grant_b, runtime::HostHandleTypeId::Resource);
    (void)heap.close_host_handle(a.as_heap_ref());
    (void)heap.close_host_handle(b.as_heap_ref());
    dispatcher.dispatch_all(heap);
    check(second_calls == 1 && heap.stats().external_bytes == 30,
          "failing published head must rotate so later records can release");
    first_ready = true;
    dispatcher.dispatch_all(heap);
    check(heap.stats().external_bytes == 0,
          "rotated published record must remain retryable");

    bool poison_ready{};
    std::size_t poison_callbacks{};
    runtime::Heap poison_heap(limits());
    runtime::HostReleaseDispatcher poison(
        99, {{16, [&](const runtime::HostHandleValue&) {
            ++poison_callbacks; return poison_ready;
        }}});
    poison.attach_context(poison_heap);
    auto poison_grant = poison.adopt(poison.reserve(
        runtime::HostHandleTypeId::Resource, 16, 13));
    const auto poison_value = poison.publish(
        poison_heap, poison_grant, runtime::HostHandleTypeId::Resource);
    (void)poison_heap.close_host_handle(poison_value.as_heap_ref());
    (void)poison.dispatch_one(poison_heap);
    const auto poisoned = poison.stats();
    check(poison_callbacks == 1 && poisoned.pending_releases == 1 &&
              poisoned.pending_external_bytes == 13 && !poisoned.teardown_complete &&
              !poison.destruction_safe(),
          "failed release must retain charge and make dispatcher destruction unsafe");
    poison_ready = true;
    (void)poison.dispatch_one(poison_heap);
    check(poison_callbacks == 2 && poison.destruction_safe(),
          "owner-strand retry must make dispatcher destruction safe");

    const auto death_command = quote_shell_argument(executable_path) +
        " --verify-dispatcher-destruction-fail-fast";
    check(std::system(death_command.c_str()) != 0,
          "last dispatcher owner must fail-fast while release ownership is pending");
}

void test_wrong_thread_retry_and_evaluator_close()
{
    bool ready{};
    std::size_t calls{};
    auto dispatcher = std::make_shared<runtime::HostReleaseDispatcher>(
        100, std::vector<runtime::HostReleaseAdapter>{{
            17, [&](const runtime::HostHandleValue&) { ++calls; return ready; }}});
    {
        runtime::Heap heap(limits());
        dispatcher->attach_context(heap);
        auto grant = dispatcher->adopt(dispatcher->reserve(
            runtime::HostHandleTypeId::Resource, 17, 21));
        (void)dispatcher->publish(heap, grant, runtime::HostHandleTypeId::Resource);
        check(!dispatcher->teardown(heap),
              "failed adapter must detach for owner-thread retry");
    }
    const auto before = calls;
    std::jthread wrong_thread([&] { dispatcher->retry_detached_releases(); });
    wrong_thread.join();
    check(calls == before && !dispatcher->stats().teardown_complete,
          "wrong-thread detached retry must not call adapter or mutate ownership");
    ready = true;
    dispatcher->retry_detached_releases();
    check(dispatcher->stats().teardown_complete,
          "owner strand must finish the detached retry");

    std::size_t consume_calls{};
    auto evaluator_dispatcher = std::make_shared<runtime::HostReleaseDispatcher>(
        101, std::vector<runtime::HostReleaseAdapter>{{
            12, [](const runtime::HostHandleValue&) { return true; }}});
    runtime::SynchronousEvaluator evaluator(
        {{"main", "import \"baas/handles\" as handles; let result = 1;\n"}},
        handle_options(evaluator_dispatcher, consume_calls));
    bool wrong_close{true};
    std::jthread closer([&] { wrong_close = evaluator.close(); });
    closer.join();
    check(!wrong_close, "wrong-thread evaluator close must fail closed");
    (void)evaluator.execute("main");
    check(evaluator.close(),
          "wrong-thread close must not prevent later owner-thread shutdown");

    runtime::SynchronousEvaluator* active{};
    bool reentrant_close{true};
    runtime::SynchronousHostOptions reentrant_options;
    reentrant_options.metadata = std::make_shared<const runtime::HostModuleRegistry>(
        std::vector<runtime::HostModuleDescriptor>{{
            "baas/reentrant", {1, 0},
            {{"try_close", "host.reentrant.close.v1", "reentrant.use"}}}});
    reentrant_options.bindings =
        std::make_shared<const runtime::SynchronousNativeBindingSet>(
            std::vector<runtime::SynchronousNativeBinding>{{
                "host.reentrant.close.v1",
                {{}, runtime::HostValueType::Boolean, "reentrant.close",
                 runtime::HostExecutionMode::ThreadSafe,
                 runtime::HostCancellationMode::Preflight},
                [&](const runtime::HostCallContext&, const runtime::HostArguments&) {
                    reentrant_close = active->close();
                    return runtime::HostResult::success(
                        runtime::HostValue(reentrant_close));
                }}});
    reentrant_options.permissions.declared_modules.push_back(
        {"baas/reentrant", 1, 0});
    for (auto* list : {&reentrant_options.permissions.declared_capabilities,
                       &reentrant_options.permissions.policy_capabilities,
                       &reentrant_options.permissions.platform_capabilities,
                       &reentrant_options.permissions.task_capabilities})
        list->push_back("reentrant.use");
    runtime::SynchronousEvaluator reentrant(
        {{"main",
          "import \"baas/reentrant\" as host; let result = host.try_close();\n"}},
        std::move(reentrant_options));
    active = &reentrant;
    (void)reentrant.execute("main");
    check(!reentrant_close && !reentrant.module_export("main", "result").as_boolean(),
          "callback-reentrant close must fail without touching the active Heap");
    check(reentrant.close(),
          "owner may close normally after callback reentry has returned");

    bool delete_ready{};
    std::size_t delete_releases{};
    std::size_t delete_consumes{};
    auto delete_dispatcher = std::make_shared<runtime::HostReleaseDispatcher>(
        102, std::vector<runtime::HostReleaseAdapter>{{
            12, [&](const runtime::HostHandleValue&) {
                ++delete_releases; return delete_ready;
            }}});
    auto deleted = std::make_unique<runtime::SynchronousEvaluator>(
        std::vector<runtime::SourceModule>{{
            "main", "import \"baas/handles\" as handles; let h = handles.make();\n"}},
        handle_options(delete_dispatcher, delete_consumes));
    (void)deleted->execute("main");
    std::jthread destroyer([owned = std::move(deleted)]() mutable { owned.reset(); });
    destroyer.join();
    check(delete_releases == 0 && delete_dispatcher->stats().pending_releases == 1,
          "wrong-thread destruction must detach ownership without Host I/O");
    delete_ready = true;
    std::atomic<bool> read_stats{true};
    std::jthread observer([&] {
        while (read_stats.load(std::memory_order_acquire))
            (void)delete_dispatcher->stats();
    });
    delete_dispatcher->retry_detached_releases();
    read_stats.store(false, std::memory_order_release);
    observer.join();
    check(delete_releases == 1 && delete_dispatcher->stats().teardown_complete,
          "original owner strand must retry after wrong-thread evaluator destruction");
}

void test_adopt_gates_empty_context_and_reentrant_release()
{
    runtime::Heap cross_heap(limits());
    runtime::HostReleaseDispatcher cross(
        103, {{18, [](const runtime::HostHandleValue&) { return true; }}});
    cross.attach_context(cross_heap);
    auto cross_reservation = cross.reserve(
        runtime::HostHandleTypeId::Resource, 18, 1);
    std::atomic<bool> cross_rejected{};
    std::jthread cross_thread(
        [&, reservation = std::move(cross_reservation)]() mutable {
            try { (void)cross.adopt(std::move(reservation)); }
            catch (const runtime::RuntimeError& error) {
                cross_rejected.store(
                    error.code() == runtime::RuntimeErrorCode::HeapTornDown,
                    std::memory_order_release);
            }
        });
    cross_thread.join();
    check(cross_rejected.load(std::memory_order_acquire),
          "cross-thread adopt must fail before consuming its reservation");

    runtime::Heap reserved_heap(limits());
    runtime::HostReleaseDispatcher reserved_dispatcher(
        112, {{27, [](const runtime::HostHandleValue&) { return true; }}});
    reserved_dispatcher.attach_context(reserved_heap);
    std::optional<runtime::HostHandleReservation> outstanding_reservation;
    outstanding_reservation.emplace(reserved_dispatcher.reserve(
        runtime::HostHandleTypeId::Resource, 27, 3));
    check(!reserved_dispatcher.teardown(reserved_heap) &&
              !reserved_dispatcher.destruction_safe(),
          "teardown must remain incomplete while a reservation owns a slot");
    outstanding_reservation.reset();
    reserved_dispatcher.retry_detached_releases();
    check(reserved_dispatcher.stats().teardown_complete &&
              reserved_dispatcher.destruction_safe(),
          "reservation reset must recycle its slot and complete detached teardown");

    auto empty_dispatcher = std::make_shared<runtime::HostReleaseDispatcher>(
        104, std::vector<runtime::HostReleaseAdapter>{{
            19, [](const runtime::HostHandleValue&) { return true; }}});
    runtime::SynchronousHostOptions empty_options;
    empty_options.metadata = std::make_shared<const runtime::HostModuleRegistry>(
        std::vector<runtime::HostModuleDescriptor>{});
    empty_options.bindings =
        std::make_shared<const runtime::SynchronousNativeBindingSet>(
            std::vector<runtime::SynchronousNativeBinding>{});
    empty_options.handles = empty_dispatcher;
    runtime::SynchronousEvaluator empty(
        {{"main", "let result = 1;\n"}}, std::move(empty_options));
    (void)empty.execute("main");
    check(empty.close() && empty_dispatcher->context_id() != 0,
          "handle dispatcher must attach even when the package imports no Host module");

    runtime::Heap heap(limits());
    runtime::HostReleaseDispatcher* dispatcher_ptr{};
    std::size_t release_calls{};
    bool nested_teardown{};
    runtime::HostReleaseDispatcher dispatcher(
        105, {{20, [&](const runtime::HostHandleValue&) {
            ++release_calls;
            nested_teardown = dispatcher_ptr->teardown(heap);
            return true;
        }}});
    dispatcher_ptr = &dispatcher;
    dispatcher.attach_context(heap);
    auto grant = dispatcher.adopt(dispatcher.reserve(
        runtime::HostHandleTypeId::Resource, 20, 9));
    const auto published = dispatcher.publish(
        heap, grant, runtime::HostHandleTypeId::Resource);
    (void)heap.close_host_handle(published.as_heap_ref());
    (void)dispatcher.dispatch_one(heap);
    dispatcher.retry_detached_releases();
    check(release_calls == 1 && !nested_teardown &&
              dispatcher.stats().teardown_complete,
          "release callback reentry must defer the in-progress record and never double release");

    runtime::Heap unpublished_heap(limits());
    runtime::HostReleaseDispatcher* unpublished_ptr{};
    std::size_t unpublished_calls{};
    bool unpublished_nested{};
    runtime::HostReleaseDispatcher unpublished_dispatcher(
        108, {{23, [&](const runtime::HostHandleValue&) {
            ++unpublished_calls;
            unpublished_nested = unpublished_ptr->teardown(unpublished_heap);
            return true;
        }}});
    unpublished_ptr = &unpublished_dispatcher;
    unpublished_dispatcher.attach_context(unpublished_heap);
    auto unpublished_grant = unpublished_dispatcher.adopt(
        unpublished_dispatcher.reserve(
            runtime::HostHandleTypeId::Resource, 23, 8));
    unpublished_dispatcher.abandon(unpublished_grant);
    unpublished_dispatcher.dispatch_all(unpublished_heap);
    unpublished_dispatcher.retry_detached_releases();
    check(unpublished_calls == 1 && !unpublished_nested &&
              unpublished_dispatcher.stats().teardown_complete &&
              !unpublished_grant.usable(),
          "unpublished teardown reentry must revoke the captured grant and release once");

    runtime::Heap reentrant_abandon_heap(limits());
    runtime::HostReleaseDispatcher* reentrant_abandon_ptr{};
    std::optional<runtime::HostHandleValue> original_grant;
    std::size_t reentrant_abandon_calls{};
    runtime::HostReleaseDispatcher reentrant_abandon(
        111, {{26, [&](const runtime::HostHandleValue&) {
            ++reentrant_abandon_calls;
            reentrant_abandon_ptr->abandon(*original_grant);
            return true;
        }}});
    reentrant_abandon_ptr = &reentrant_abandon;
    reentrant_abandon.attach_context(reentrant_abandon_heap);
    original_grant.emplace(reentrant_abandon.adopt(
        reentrant_abandon.reserve(
            runtime::HostHandleTypeId::Resource, 26, 7)));
    reentrant_abandon.abandon(*original_grant);
    reentrant_abandon.dispatch_all(reentrant_abandon_heap);
    reentrant_abandon.dispatch_all(reentrant_abandon_heap);
    check(reentrant_abandon_calls == 1 &&
              reentrant_abandon.destruction_safe() &&
              reentrant_abandon_heap.stats().external_bytes == 0,
          "abandon during unpublished ReleaseInProgress must not queue a second native release");

    runtime::Heap after_heap(limits());
    runtime::HostReleaseDispatcher after(
        106, {{21, [](const runtime::HostHandleValue&) { return true; }}});
    after.attach_context(after_heap);
    auto pending = after.reserve(runtime::HostHandleTypeId::Resource, 21, 2);
    check(after.detach_context_for_destruction(after_heap),
          "destruction detach should close the context without Host I/O");
    expect_runtime(runtime::RuntimeErrorCode::HeapTornDown, [&] {
        (void)after.adopt(std::move(pending));
    }, "adopt after teardown must fail before native publication");
}

void test_many_poisoned_records_do_not_starve_good_tail()
{
    constexpr std::size_t count = 128;
    std::size_t calls{};
    std::size_t good_calls{};
    bool release_all{};
    runtime::Heap heap(limits());
    runtime::HostReleaseDispatcher dispatcher(
        107, {{22, [&](const runtime::HostHandleValue& handle) {
            ++calls;
            const bool good = handle.handle_id() == count / 2;
            if (good) ++good_calls;
            return good || release_all;
        }}});
    dispatcher.attach_context(heap);
    std::vector<runtime::Value> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto grant = dispatcher.adopt(dispatcher.reserve(
            runtime::HostHandleTypeId::Resource, 22, 1));
        values.push_back(dispatcher.publish(
            heap, grant, runtime::HostHandleTypeId::Resource));
    }
    for (const auto value : values)
        (void)heap.close_host_handle(value.as_heap_ref());
    dispatcher.dispatch_all(heap);
    check(good_calls == 1 && calls <= count * 3 &&
              heap.stats().external_bytes == count - 1,
          "O(1) bad-head deferral must reach a good tail under bounded work");
    release_all = true;
    dispatcher.dispatch_all(heap);
    check(heap.stats().external_bytes == 0,
          "all deferred records must remain releasable after the bounded round");

    auto churn_limits = limits();
    churn_limits.max_pending_release_records = 4;
    runtime::Heap churn(churn_limits);
    const auto bad = churn.allocate_host_handle({1, 1, 0, false});
    (void)churn.close_host_handle(bad.as_heap_ref());
    for (std::uint64_t id = 2; id < 2002; ++id) {
        const auto fresh = churn.allocate_host_handle({id, 1, 0, false});
        (void)churn.close_host_handle(fresh.as_heap_ref());
        auto lease = churn.lease_host_release();
        check(lease.has_value(), "alternating churn must lease the bad cursor");
        if (lease) (void)churn.defer_host_release(lease->lease_id);
        lease = churn.lease_host_release();
        check(lease && lease->record.handle_id == id,
              "fair cursor must reach each newly appended good record");
        if (lease) (void)churn.acknowledge_host_release(lease->lease_id);
    }
    check(churn.lease_host_release()->record.handle_id == 1,
          "alternating ACK/close must retain the single deferred bad record");

    // Keep a large active set while consuming one record and appending one
    // replacement. The consumed prefix must be reused without moving the
    // remaining active records on every iteration.
    constexpr std::size_t active_count = 16'384;
    constexpr std::size_t churn_count = active_count * 2;
    auto large_limits = limits();
    large_limits.max_cells = active_count + churn_count + 16;
    large_limits.max_live_bytes = 64U * 1024U * 1024U;
    large_limits.soft_collect_threshold = large_limits.max_live_bytes;
    large_limits.max_pending_release_records = active_count + 1;
    runtime::Heap large_churn(large_limits);
    for (std::uint64_t id = 1; id <= active_count; ++id) {
        const auto value = large_churn.allocate_host_handle({id, 1, 0, false});
        (void)large_churn.close_host_handle(value.as_heap_ref());
    }
    for (std::uint64_t offset = 1; offset <= churn_count; ++offset) {
        auto lease = large_churn.lease_host_release();
        check(lease.has_value(), "large active churn must retain a leasable record");
        if (lease) (void)large_churn.acknowledge_host_release(lease->lease_id);
        const auto id = static_cast<std::uint64_t>(active_count) + offset;
        const auto value = large_churn.allocate_host_handle({id, 1, 0, false});
        (void)large_churn.close_host_handle(value.as_heap_ref());
    }
    std::size_t remaining{};
    while (const auto lease = large_churn.lease_host_release()) {
        ++remaining;
        (void)large_churn.acknowledge_host_release(lease->lease_id);
    }
    check(remaining == active_count && large_churn.stats().external_bytes == 0,
          "large alternating ACK/close churn must preserve active cardinality");

    // Exercise the publication preflight while a large release queue retains
    // an ACK-consumed prefix. Publication must reuse that prefix rather than
    // compacting all pending records before every replacement.
    constexpr std::size_t published_count = 1'024;
    constexpr std::size_t replacement_count = 4'096;
    auto publish_limits = limits();
    publish_limits.max_cells = published_count + replacement_count + 16;
    publish_limits.max_live_bytes = 64U * 1024U * 1024U;
    publish_limits.soft_collect_threshold = publish_limits.max_live_bytes;
    publish_limits.max_pending_release_records = published_count + 1;
    runtime::Heap publish_heap(publish_limits);
    std::size_t published_releases{};
    runtime::HostReleaseDispatcher publish_dispatcher(
        110, {{25, [&](const runtime::HostHandleValue&) {
            ++published_releases;
            return true;
        }}});
    publish_dispatcher.attach_context(publish_heap);
    std::vector<runtime::Value> published;
    published.reserve(published_count);
    for (std::size_t index = 0; index < published_count; ++index) {
        auto grant = publish_dispatcher.adopt(publish_dispatcher.reserve(
            runtime::HostHandleTypeId::Resource, 25, 1));
        published.push_back(publish_dispatcher.publish(
            publish_heap, grant, runtime::HostHandleTypeId::Resource));
    }
    for (const auto value : published)
        (void)publish_heap.close_host_handle(value.as_heap_ref());
    for (std::size_t index = 0; index < replacement_count; ++index) {
        check(publish_dispatcher.dispatch_one(publish_heap),
              "ACK/publish churn must release one pending native owner");
        auto grant = publish_dispatcher.adopt(publish_dispatcher.reserve(
            runtime::HostHandleTypeId::Resource, 25, 1));
        const auto replacement = publish_dispatcher.publish(
            publish_heap, grant, runtime::HostHandleTypeId::Resource);
        (void)publish_heap.close_host_handle(replacement.as_heap_ref());
    }
    publish_dispatcher.dispatch_all(publish_heap);
    check(published_releases == published_count + replacement_count &&
              publish_heap.stats().external_bytes == 0 &&
              publish_dispatcher.destruction_safe(),
          "large ACK/publish churn must preserve cardinality and release exactly once");
}

void test_handle_table_constant_work_indices()
{
    constexpr std::size_t count = 2'048;
    auto open_limits = limits();
    open_limits.max_cells = count + 16;
    open_limits.max_live_bytes = 64U * 1024U * 1024U;
    open_limits.soft_collect_threshold = open_limits.max_live_bytes;
    open_limits.max_pending_release_records = count + 1;
    runtime::Heap open_heap(open_limits);
    std::size_t open_releases{};
    runtime::HostReleaseDispatcher open_dispatcher(
        113, {{28, [&](const runtime::HostHandleValue&) {
            ++open_releases;
            return true;
        }}});
    open_dispatcher.attach_context(open_heap);
    std::vector<runtime::Value> open_values;
    open_values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto grant = open_dispatcher.adopt(open_dispatcher.reserve(
            runtime::HostHandleTypeId::Resource, 28, 0));
        open_values.push_back(open_dispatcher.publish(
            open_heap, grant, runtime::HostHandleTypeId::Resource));
    }

    // Open published handles are not release work. Repeated drain calls must
    // consult the Heap's pending count instead of walking the live handle set.
    for (std::size_t index = 0; index < count; ++index)
        open_dispatcher.dispatch_all(open_heap);
    check(open_releases == 0,
          "repeated drains with only Open handles must perform no adapter work");

    // Each callback scope creates one grant while thousands of unrelated live
    // handles remain. Scope finish must visit only the recorded scope index.
    for (std::size_t index = 0; index < count; ++index) {
        const auto scope = open_dispatcher.begin_callback_scope();
        auto grant = open_dispatcher.adopt(open_dispatcher.reserve(
            runtime::HostHandleTypeId::Resource, 28, 0));
        open_dispatcher.finish_callback_scope(scope, nullptr);
        open_dispatcher.dispatch_all(open_heap);
    }
    check(open_releases == count,
          "one-grant scopes must release exactly their recorded grant");

    // Reuse one released slot repeatedly inside one still-active scope so the
    // scope index count grows beyond the handle-table capacity. Adopt must
    // reserve this bookkeeping transactionally before installing each live
    // owner, and finish must drain the repeated indices without allocation.
    constexpr std::size_t reuse_in_one_scope = count * 3;
    const auto repeated_scope = open_dispatcher.begin_callback_scope();
    for (std::size_t index = 0; index < reuse_in_one_scope; ++index) {
        auto grant = open_dispatcher.adopt(open_dispatcher.reserve(
            runtime::HostHandleTypeId::Resource, 28, 0));
        open_dispatcher.abandon(grant);
        open_dispatcher.dispatch_all(open_heap);
    }
    open_dispatcher.finish_callback_scope(repeated_scope, nullptr);
    check(open_releases == count + reuse_in_one_scope,
          "one active scope must track repeated slot reuse without losing owners");
    for (const auto value : open_values)
        (void)open_heap.close_host_handle(value.as_heap_ref());
    open_dispatcher.dispatch_all(open_heap);
    const auto open_stats = open_dispatcher.stats();
    const auto expected_open_owners = count * 2 + reuse_in_one_scope;
    check(open_releases == expected_open_owners &&
              open_stats.issued == expected_open_owners &&
              open_stats.released == expected_open_owners &&
              open_dispatcher.destruction_safe(),
          "large Open-handle and scope workload must retire every indexed slot");

    bool release_all{};
    std::size_t unpublished_calls{};
    std::size_t good_calls{};
    runtime::Heap unpublished_heap(limits());
    runtime::HostReleaseDispatcher unpublished_dispatcher(
        114, {{29, [&](const runtime::HostHandleValue& value) {
            ++unpublished_calls;
            const bool good = value.handle_id() == count / 2;
            if (good) ++good_calls;
            return release_all || good;
        }}});
    unpublished_dispatcher.attach_context(unpublished_heap);
    for (std::size_t index = 0; index < count; ++index) {
        auto grant = unpublished_dispatcher.adopt(
            unpublished_dispatcher.reserve(
                runtime::HostHandleTypeId::Resource, 29, 0));
        unpublished_dispatcher.abandon(grant);
    }
    unpublished_dispatcher.dispatch_all(unpublished_heap);
    check(good_calls == 1 && unpublished_calls <= count * 3 &&
              unpublished_dispatcher.stats().pending_releases == count - 1,
          "intrusive unpublished FIFO must reach a middle success under bounded work");
    release_all = true;
    unpublished_dispatcher.dispatch_all(unpublished_heap);

    // A second full wave must reuse the intrusive free-slot list without a
    // table scan or a release-path allocation.
    for (std::size_t index = 0; index < count; ++index) {
        auto grant = unpublished_dispatcher.adopt(
            unpublished_dispatcher.reserve(
                runtime::HostHandleTypeId::Resource, 29, 0));
        unpublished_dispatcher.abandon(grant);
    }
    unpublished_dispatcher.dispatch_all(unpublished_heap);
    const auto unpublished_stats = unpublished_dispatcher.stats();
    check(unpublished_stats.issued == count * 2 &&
              unpublished_stats.released == count * 2 &&
              unpublished_stats.pending_releases == 0 &&
              unpublished_dispatcher.destruction_safe(),
          "two large unpublished waves must reuse and retire every slot exactly once");
}

int run_dispatcher_destruction_death_case()
{
    std::set_terminate([] { std::_Exit(86); });
    runtime::Heap heap(limits());
    runtime::HostReleaseDispatcher dispatcher(
        109, {{24, [](const runtime::HostHandleValue&) { return false; }}});
    dispatcher.attach_context(heap);
    auto grant = dispatcher.adopt(dispatcher.reserve(
        runtime::HostHandleTypeId::Resource, 24, 1));
    const auto value = dispatcher.publish(
        heap, grant, runtime::HostHandleTypeId::Resource);
    (void)heap.close_host_handle(value.as_heap_ref());
    dispatcher.dispatch_all(heap);
    return EXIT_SUCCESS;
}

} // namespace

int main(const int argc, char** const argv)
{
    if (argc == 2 && std::string_view(argv[1]) ==
            "--verify-dispatcher-destruction-fail-fast")
        return run_dispatcher_destruction_death_case();
    executable_path = argv[0];
    test_transactional_external_memory_and_exact_identity();
    test_callback_borrow_revoke_and_before_callback_validation();
    test_ack_tombstone_gc_json_and_error_details();
    test_producer_faults_round_robin_and_detached_retry();
    test_evaluator_vertical_and_close();
    test_external_reservation_can_outlive_heap_safely();
    test_forged_stale_and_multi_argument_rollback();
    test_limits_published_fairness_and_poisoned_completion();
    test_wrong_thread_retry_and_evaluator_close();
    test_adopt_gates_empty_context_and_reentrant_release();
    test_many_poisoned_records_do_not_starve_good_tail();
    test_handle_table_constant_work_indices();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "typed Host handle tests passed\n";
    return EXIT_SUCCESS;
}
