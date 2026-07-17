#include "script/host/ProcedureHost.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace host = baas::script::host;
namespace resources = baas::resources;
namespace runtime = baas::script::runtime;
using namespace std::chrono_literals;

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <class Function>
void expect_snapshot_error(
    const host::ProcedureSnapshotErrorCode code, Function&& function,
    const std::string_view message)
{
    try {
        std::forward<Function>(function)();
        check(false, message);
    } catch (const host::ProcedureSnapshotError& error) {
        check(error.code() == code, message);
    } catch (...) {
        check(false, message);
    }
}

std::shared_ptr<const std::vector<std::byte>> bytes(const std::string_view text)
{
    auto value = std::make_shared<std::vector<std::byte>>();
    for (const auto character : text)
        value->push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    return value;
}

resources::ResourcePayload resource(std::string id, const std::string_view content)
{
    const auto value = bytes(content);
    return {std::move(id), std::nullopt, std::nullopt, "application/octet-stream",
            value->size(), resources::sha256_hex(*value), value};
}

std::shared_ptr<const resources::ResourceSnapshot> resource_snapshot(
    const std::string_view marker = "v1")
{
    return resources::ResourceSnapshot::build(
        {"CN", std::nullopt},
        {resource("image/group/menu", marker), resource("json/group/rules", "{}")});
}

host::ProcedureDescriptorInput descriptor(
    std::string id = "group", std::vector<std::string> terminals = {"joined", "already_joined"},
    std::vector<host::ProcedureEffect> effects = {
        host::ProcedureEffect::Capture, host::ProcedureEffect::Vision,
        host::ProcedureEffect::Input, host::ProcedureEffect::Wait,
        host::ProcedureEffect::ForegroundCheck},
    std::vector<std::string> resource_ids = {"image/group/menu"})
{
    host::ProcedureDescriptorInput result{
        std::move(id), std::move(terminals), std::move(effects),
        std::move(resource_ids), {}};
    result.sha256 = host::procedure_descriptor_sha256(result);
    return result;
}

std::shared_ptr<const host::ProcedureSnapshot> procedure_snapshot(
    std::shared_ptr<const resources::ResourceSnapshot> resources = resource_snapshot())
{
    return host::ProcedureSnapshot::build({descriptor()}, std::move(resources));
}

class Probe final : public runtime::HostCancellationProbe {
public:
    bool cancelled() const noexcept override { return cancel.load(std::memory_order_acquire); }
    bool deadline_exceeded() const noexcept override
    {
        return deadline.load(std::memory_order_acquire);
    }
    std::atomic<bool> cancel{};
    std::atomic<bool> deadline{};
};

class LambdaExecutor final : public host::ProcedureExecutor {
public:
    using Function = std::function<host::ProcedureExecutorOutcome(
        const host::ProcedureExecutionRequest&)>;
    explicit LambdaExecutor(Function function) : function_(std::move(function)) {}
    host::ProcedureExecutorOutcome execute(
        const host::ProcedureExecutionRequest& request) override
    {
        return function_(request);
    }

private:
    Function function_;
};

runtime::HostCallContext context(std::shared_ptr<const Probe> probe = {})
{
    return {"baas/procedure", "run", "host.procedure.run.v1", {1, 0}, 0,
            std::move(probe)};
}

runtime::HostArguments arguments(
    std::string id = "group", std::optional<runtime::JsonObject> options = std::nullopt)
{
    runtime::HostArguments result;
    result.emplace_back(runtime::HostValue(std::move(id)));
    if (options)
        result.emplace_back(runtime::HostValue(runtime::JsonValue(std::move(*options))));
    else
        result.emplace_back(std::nullopt);
    return result;
}

runtime::HostResult invoke(
    const host::ProcedureHostRuntime& owner, const runtime::HostArguments& args,
    std::shared_ptr<const Probe> probe = {})
{
    const auto* binding = owner.bindings->find("host.procedure.run.v1");
    check(binding != nullptr, "procedure binding must exist");
    return runtime::invoke_host_callback(
        *binding, context(std::move(probe)), args, owner.bindings->limits(), nullptr);
}

std::optional<std::string> result_end(const runtime::HostResult& result)
{
    if (!result.ok() || result.value().type() != runtime::HostValueType::Json)
        return std::nullopt;
    const auto& json = std::get<runtime::JsonValue>(result.value().storage());
    const auto& object = std::get<runtime::JsonObject>(json.value());
    if (object.size() != 1 || object[0].first != "end" ||
        object[0].second.kind() != runtime::JsonKind::String) return std::nullopt;
    return std::get<std::string>(object[0].second.value());
}

std::optional<std::string> detail_string(
    const runtime::HostResult& result, const std::string_view key)
{
    if (!result.has_error() || !result.error().details ||
        result.error().details->kind() != runtime::JsonKind::Object) return std::nullopt;
    const auto& object = std::get<runtime::JsonObject>(result.error().details->value());
    const auto found = std::find_if(object.begin(), object.end(), [&](const auto& entry) {
        return entry.first == key && entry.second.kind() == runtime::JsonKind::String;
    });
    return found == object.end()
        ? std::nullopt
        : std::optional<std::string>(std::get<std::string>(found->second.value()));
}

bool wait_until(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout)
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (predicate()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

void test_snapshot_validation_identity_and_ownership()
{
    check(host::valid_procedure_id("activity/group") &&
              !host::valid_procedure_id("Activity/group") &&
              !host::valid_procedure_id("../group") &&
              !host::valid_procedure_id("activity//group") &&
              host::valid_procedure_terminal_id("already_joined") &&
              !host::valid_procedure_terminal_id("AlreadyJoined"),
          "procedure and terminal IDs must be exact canonical logical IDs");
    auto source = descriptor();
    auto second = descriptor("mail", {"claimed", "empty"},
                             {host::ProcedureEffect::Input, host::ProcedureEffect::Capture},
                             {"json/group/rules"});
    auto resources = resource_snapshot();
    const auto first = host::ProcedureSnapshot::build({source, second}, resources);
    const auto reordered = host::ProcedureSnapshot::build({second, source}, resources);
    check(first->snapshot_id() == reordered->snapshot_id() &&
              first->numeric_snapshot_id() == reordered->numeric_snapshot_id(),
          "snapshot identity must ignore descriptor and set input order");
    const auto original_identity = first->snapshot_id();
    source.procedure_id = "mutated";
    source.terminal_ids[0] = "mutated";
    source.resource_ids[0] = "json/group/rules";
    check(first->resolve("group") && !first->resolve("mutated") &&
              first->resolve("group")->terminal_ids()[0] == "joined" &&
              first->snapshot_id() == original_identity,
          "published procedure snapshots must own descriptor strings and vectors");
    const auto other_resources = resource_snapshot("v2");
    const auto other = host::ProcedureSnapshot::build({descriptor()}, other_resources);
    check(first->resource_snapshot().get() == resources.get() &&
              other->resource_snapshot().get() == other_resources.get() &&
              other->snapshot_id() != host::ProcedureSnapshot::build(
                  {descriptor()}, resources)->snapshot_id(),
          "procedure identity must bind the exact external resource snapshot");

    auto bad_digest = descriptor();
    bad_digest.sha256.assign(64, '0');
    expect_snapshot_error(host::ProcedureSnapshotErrorCode::DigestMismatch, [&] {
        (void)host::ProcedureSnapshot::build({bad_digest}, resources);
    }, "descriptor digest mismatch must reject the entire snapshot");
    auto duplicate_terminal = descriptor();
    duplicate_terminal.terminal_ids.push_back("joined");
    duplicate_terminal.sha256.clear();
    expect_snapshot_error(host::ProcedureSnapshotErrorCode::DuplicateTerminal, [&] {
        (void)host::procedure_descriptor_sha256(duplicate_terminal);
    }, "duplicate terminals must be rejected before digesting");
    auto duplicate_effect = descriptor();
    duplicate_effect.declared_effects.push_back(host::ProcedureEffect::Input);
    expect_snapshot_error(host::ProcedureSnapshotErrorCode::DuplicateEffect, [&] {
        (void)host::procedure_descriptor_sha256(duplicate_effect);
    }, "duplicate effects must be rejected as set aliases");
    auto duplicate_resource = descriptor();
    duplicate_resource.resource_ids.push_back("image/group/menu");
    expect_snapshot_error(host::ProcedureSnapshotErrorCode::DuplicateResource, [&] {
        (void)host::procedure_descriptor_sha256(duplicate_resource);
    }, "duplicate resources must be rejected as set aliases");
    auto missing_resource = descriptor();
    missing_resource.resource_ids = {"image/group/missing"};
    missing_resource.sha256 = host::procedure_descriptor_sha256(missing_resource);
    expect_snapshot_error(host::ProcedureSnapshotErrorCode::ResourceNotFound, [&] {
        (void)host::ProcedureSnapshot::build({missing_resource}, resources);
    }, "every declared resource must exist in the externally supplied snapshot");
    auto duplicate = descriptor();
    expect_snapshot_error(host::ProcedureSnapshotErrorCode::DuplicateProcedure, [&] {
        (void)host::ProcedureSnapshot::build({duplicate, duplicate}, resources);
    }, "duplicate procedure IDs must be rejected");
    auto case_alias = descriptor();
    case_alias.procedure_id = "Group";
    expect_snapshot_error(host::ProcedureSnapshotErrorCode::ProcedureIdCaseCollision, [&] {
        (void)host::ProcedureSnapshot::build({descriptor(), case_alias}, resources);
    }, "case-colliding procedure IDs must be rejected before lowercase validation");

    host::ProcedureSnapshotLimits limits;
    limits.max_procedures = 1;
    expect_snapshot_error(host::ProcedureSnapshotErrorCode::ProcedureLimitExceeded, [&] {
        (void)host::ProcedureSnapshot::build({descriptor(), second}, resources, limits);
    }, "procedure count must be bounded");
    limits = {};
    limits.max_validation_work = 1;
    expect_snapshot_error(host::ProcedureSnapshotErrorCode::WorkLimitExceeded, [&] {
        (void)host::ProcedureSnapshot::build({descriptor()}, resources, limits);
    }, "snapshot validation work must be bounded");
}

void test_metadata_success_options_and_lifetime()
{
    auto resources = resource_snapshot();
    auto snapshot = procedure_snapshot(resources);
    std::weak_ptr<const host::ProcedureSnapshot> weak_snapshot = snapshot;
    std::weak_ptr<const resources::ResourceSnapshot> weak_resources = resources;
    std::atomic<int> calls{};
    auto executor = std::make_shared<LambdaExecutor>(
        [expected_snapshot = snapshot, expected_resources = resources, &calls](
            const host::ProcedureExecutionRequest& request) {
            ++calls;
            check(request.snapshot().get() == expected_snapshot.get() &&
                      request.snapshot()->resource_snapshot().get() == expected_resources.get(),
                  "executor request must pin exact procedure/resource snapshot identity");
            check(request.device_id() == "emulator-5556",
                  "executor request must carry the frozen physical device id");
            if (calls != 2)
                check(request.options().empty(), "omitted options must default to an empty map");
            else
                check(request.options().size() == 2 && request.options()[0].first == "first" &&
                          request.options()[1].first == "second",
                      "options must preserve ordered-map insertion order");
            check(request.effects().report(
                      host::ProcedureEffect::ForegroundCheck,
                      host::ProcedureEffectStage::Committed),
                  "declared effect reporting must succeed");
            return host::ProcedureExecutorOutcome::success("joined");
        });
    auto coordinator = host::PhysicalDeviceCoordinator::create();
    auto owner = host::make_procedure_host_runtime(
        snapshot, "emulator-5556", executor, coordinator);
    const auto resolution = owner.metadata->resolve({
        {{"baas/procedure", 1, 0}}, {"procedure.execute"},
        {{"baas/procedure", {"run"}}}, {"procedure.execute"},
        {"procedure.execute"}, {"procedure.execute"}});
    check(resolution.modules.size() == 1 &&
              resolution.modules[0].bindings.size() == 1 &&
              resolution.modules[0].bindings[0].binding_id == "host.procedure.run.v1" &&
              resolution.modules[0].bindings[0].capability == "procedure.execute",
          "metadata must expose exact baas/procedure v1 capability binding");
    const auto* binding = owner.bindings->find("host.procedure.run.v1");
    check(binding && binding->contract.parameters.size() == 2 &&
              !binding->contract.parameters[1].required &&
              binding->contract.result == runtime::HostValueType::OrderedStringJsonMap &&
              binding->contract.budget_scope == "procedure_steps" &&
              binding->contract.cancellation == runtime::HostCancellationMode::Cooperative,
          "run ABI must have optional ordered-map options and exact result/budget contract");
    check(result_end(invoke(owner, arguments())) == "joined",
          "successful procedure must return exact {end:string}");
    check(result_end(invoke(owner, arguments("group", runtime::JsonObject{
              {"first", runtime::JsonValue(std::int64_t{1})},
              {"second", runtime::JsonValue("two")}}))) == "joined",
          "explicit options must execute successfully");

    snapshot.reset();
    resources.reset();
    executor.reset();
    coordinator.reset();
    auto bindings = owner.bindings;
    owner.host.reset();
    owner.metadata.reset();
    check(!weak_snapshot.expired() && !weak_resources.expired(),
          "binding callback must own snapshot and external resources for its lifetime");
    const auto* retained = bindings->find("host.procedure.run.v1");
    check(result_end(runtime::invoke_host_callback(
              *retained, context(), arguments(), bindings->limits(), nullptr)) == "joined",
          "retained binding must remain usable after runtime owner fields are released");
}

void test_validation_terminal_and_error_mapping()
{
    auto coordinator = host::PhysicalDeviceCoordinator::create();
    auto make_owner = [&](LambdaExecutor::Function function) {
        return host::make_procedure_host_runtime(
            procedure_snapshot(), "emulator-5556",
            std::make_shared<LambdaExecutor>(std::move(function)), coordinator);
    };
    auto success = make_owner([](const host::ProcedureExecutionRequest&) {
        return host::ProcedureExecutorOutcome::success("joined");
    });
    auto missing = invoke(success, arguments("unknown"));
    check(missing.has_error() && missing.error().code == runtime::HostErrorCode::ResourceNotFound,
          "unknown exact procedure ID must fail as snapshot resource-not-found");
    auto wrong_case = invoke(success, arguments("Group"));
    check(wrong_case.has_error() && wrong_case.error().code == runtime::HostErrorCode::InvalidArgument,
          "noncanonical procedure case must fail before lookup");
    auto malformed = invoke(success, {runtime::HostValue("group")});
    check(malformed.has_error() && malformed.error().code == runtime::HostErrorCode::InvalidArgument,
          "malformed argument arity must fail closed");
    auto duplicate_options = invoke(success, arguments("group", runtime::JsonObject{
        {"same", runtime::JsonValue(true)}, {"same", runtime::JsonValue(false)}}));
    check(duplicate_options.has_error() &&
              duplicate_options.error().code == runtime::HostErrorCode::InvalidArgument,
          "duplicate option keys must be rejected even on direct native invocation");

    auto bad_end = make_owner([](const host::ProcedureExecutionRequest&) {
        return host::ProcedureExecutorOutcome::success("not_declared");
    });
    auto end = invoke(bad_end, arguments());
    check(end.has_error() && end.error().code == runtime::HostErrorCode::Internal &&
              end.error().effect_state == runtime::HostEffectState::NotStarted,
          "undeclared executor terminal must fail closed with honest effect state");
    auto undeclared_effect = make_owner([](const host::ProcedureExecutionRequest& request) {
        (void)request.effects().report(
            static_cast<host::ProcedureEffect>(99), host::ProcedureEffectStage::Committed);
        return host::ProcedureExecutorOutcome::success("joined");
    });
    auto effect = invoke(undeclared_effect, arguments());
    check(effect.has_error() && effect.error().code == runtime::HostErrorCode::Internal,
          "undeclared effects must fail closed after allocation-free trace reporting");

    auto foreground = make_owner([](const host::ProcedureExecutionRequest& request) {
        (void)request.effects().report(
            host::ProcedureEffect::ForegroundCheck, host::ProcedureEffectStage::Began);
        return host::ProcedureExecutorOutcome::failure({
            host::ProcedureExecutorErrorCode::ForegroundPackageMismatch, {}, false,
            runtime::HostEffectState::NotStarted});
    });
    auto foreground_result = invoke(foreground, arguments());
    check(foreground_result.has_error() &&
              foreground_result.error().code == runtime::HostErrorCode::Unavailable &&
              foreground_result.error().effect_state == runtime::HostEffectState::Unknown &&
              detail_string(foreground_result, "unavailable_reason") ==
                  "foreground_package_mismatch",
          "foreground mismatch must map to HOST006 with stable discriminator/effect state");

    auto disconnected = make_owner([](const host::ProcedureExecutionRequest& request) {
        (void)request.effects().report(
            host::ProcedureEffect::Input, host::ProcedureEffectStage::Committed);
        return host::ProcedureExecutorOutcome::failure({
            host::ProcedureExecutorErrorCode::DeviceDisconnected, {}, true,
            runtime::HostEffectState::NotStarted});
    });
    auto disconnected_result = invoke(disconnected, arguments());
    check(disconnected_result.has_error() &&
              disconnected_result.error().code == runtime::HostErrorCode::DeviceDisconnected &&
              disconnected_result.error().effect_state == runtime::HostEffectState::Committed,
          "typed device error must preserve known committed effects");
}

void test_same_device_serialization_and_wait_cancellation()
{
    auto coordinator = host::PhysicalDeviceCoordinator::create(
        {.max_devices = 8, .max_waiters = 32, .max_device_id_bytes = 64,
         .poll_interval = 1ms});
    std::atomic<int> active{};
    std::atomic<int> maximum{};
    std::atomic<bool> release_first{};
    std::atomic<int> entered{};
    auto executor = std::make_shared<LambdaExecutor>([&](const host::ProcedureExecutionRequest&) {
        const auto current = active.fetch_add(1) + 1;
        maximum.store(std::max(maximum.load(), current));
        const auto order = entered.fetch_add(1);
        if (order == 0)
            while (!release_first.load(std::memory_order_acquire)) std::this_thread::yield();
        active.fetch_sub(1);
        return host::ProcedureExecutorOutcome::success("joined");
    });
    auto first = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556", executor, coordinator);
    auto second = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556", executor, coordinator);
    runtime::HostResult first_result = runtime::HostResult::success();
    runtime::HostResult second_result = runtime::HostResult::success();
    std::thread one([&] { first_result = invoke(first, arguments()); });
    check(wait_until([&] { return entered.load() == 1; }, 2s),
          "first same-device call must enter executor");
    std::thread two([&] { second_result = invoke(second, arguments()); });
    check(wait_until([&] { return coordinator->stats().waiters == 1; }, 2s),
          "second Host instance must wait on shared physical-device strand");
    release_first = true;
    one.join();
    two.join();
    check(first_result.ok() && second_result.ok() && maximum.load() == 1,
          "same physical device must serialize across distinct Host instances");

    // Hold the strand again and cancel another Host while it is waiting.
    release_first = false;
    entered = 0;
    std::thread holder([&] { (void)invoke(first, arguments()); });
    check(wait_until([&] { return entered.load() == 1; }, 2s),
          "holder must enter executor before cancellation test");
    auto probe = std::make_shared<Probe>();
    runtime::HostResult cancelled_result = runtime::HostResult::success();
    std::thread waiter([&] { cancelled_result = invoke(second, arguments(), probe); });
    check(wait_until([&] { return coordinator->stats().waiters == 1; }, 2s),
          "cancelled call must first be waiting on the strand");
    probe->cancel = true;
    waiter.join();
    check(cancelled_result.has_error() &&
              cancelled_result.error().code == runtime::HostErrorCode::Cancelled &&
              cancelled_result.error().effect_state == runtime::HostEffectState::NotStarted &&
              second.host->stats().cancelled_while_waiting == 1,
          "strand wait must cooperatively cancel without starting effects");
    release_first = true;
    holder.join();
}

void test_different_device_concurrency_reentry_and_shutdown()
{
    auto coordinator = host::PhysicalDeviceCoordinator::create(
        {.max_devices = 8, .max_waiters = 32, .max_device_id_bytes = 64,
         .poll_interval = 1ms});
    std::atomic<int> active{};
    std::atomic<int> maximum{};
    auto executor = std::make_shared<LambdaExecutor>([&](const host::ProcedureExecutionRequest&) {
        const auto current = active.fetch_add(1) + 1;
        maximum.store(std::max(maximum.load(), current));
        (void)wait_until([&] { return active.load() == 2; }, 1s);
        active.fetch_sub(1);
        return host::ProcedureExecutorOutcome::success("joined");
    });
    auto first = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556", executor, coordinator);
    auto second = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5558", executor, coordinator);
    std::thread one([&] { (void)invoke(first, arguments()); });
    std::thread two([&] { (void)invoke(second, arguments()); });
    one.join();
    two.join();
    check(maximum.load() == 2,
          "different physical devices must execute concurrently");

    runtime::HostResult inner_result = runtime::HostResult::success();
    std::shared_ptr<host::ProcedureHostRuntime> inner;
    auto outer_executor = std::make_shared<LambdaExecutor>(
        [&](const host::ProcedureExecutionRequest&) {
            inner_result = invoke(*inner, arguments());
            return host::ProcedureExecutorOutcome::success("joined");
        });
    auto outer = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556", outer_executor, coordinator);
    inner = std::make_shared<host::ProcedureHostRuntime>(
        host::make_procedure_host_runtime(
            procedure_snapshot(), "emulator-5556",
            std::make_shared<LambdaExecutor>([](const host::ProcedureExecutionRequest&) {
                return host::ProcedureExecutorOutcome::success("joined");
            }), coordinator));
    const auto outer_result = invoke(outer, arguments());
    check(outer_result.ok() && inner_result.has_error() &&
              inner_result.error().code == runtime::HostErrorCode::Unavailable &&
              detail_string(inner_result, "unavailable_reason") ==
                  "physical_device_strand_reentry",
          "same-thread same-device reentry must fail immediately instead of deadlocking");

    coordinator->shutdown();
    const auto stopped = invoke(first, arguments());
    check(stopped.has_error() && stopped.error().code == runtime::HostErrorCode::Unavailable &&
              detail_string(stopped, "unavailable_reason") ==
                  "physical_device_coordinator_shutdown",
          "coordinator shutdown must reject new work with a stable unavailable reason");
}

void test_cancellation_deadline_and_exception_safety()
{
    auto coordinator = host::PhysicalDeviceCoordinator::create(
        {.max_devices = 8, .max_waiters = 32, .max_device_id_bytes = 64,
         .poll_interval = 1ms});
    auto probe = std::make_shared<Probe>();
    std::atomic<bool> entered{};
    auto owner = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556",
        std::make_shared<LambdaExecutor>([&](const host::ProcedureExecutionRequest& request) {
            (void)request.effects().report(
                host::ProcedureEffect::Wait, host::ProcedureEffectStage::Began);
            entered = true;
            while (!request.cancelled() && !request.deadline_exceeded())
                std::this_thread::yield();
            return host::ProcedureExecutorOutcome::failure({
                request.deadline_exceeded()
                    ? host::ProcedureExecutorErrorCode::DeadlineExceeded
                    : host::ProcedureExecutorErrorCode::Cancelled,
                {}, false, runtime::HostEffectState::NotStarted});
        }), coordinator);
    runtime::HostResult result = runtime::HostResult::success();
    std::thread worker([&] { result = invoke(owner, arguments(), probe); });
    check(wait_until([&] { return entered.load(); }, 2s),
          "cooperative executor must begin before cancellation");
    probe->cancel = true;
    worker.join();
    check(result.has_error() && result.error().code == runtime::HostErrorCode::Cancelled &&
              result.error().effect_state == runtime::HostEffectState::Unknown,
          "cancellation during executor must preserve uncertain begun effect state");

    auto both = std::make_shared<Probe>();
    both->cancel = true;
    both->deadline = true;
    const auto precedence = invoke(owner, arguments(), both);
    check(precedence.has_error() &&
              precedence.error().code == runtime::HostErrorCode::DeadlineExceeded &&
              detail_string(precedence, "deadline_scope") == "call",
          "deadline must win whenever cancellation and deadline are both observable");

    auto throwing = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556",
        std::make_shared<LambdaExecutor>([](const host::ProcedureExecutionRequest& request)
            -> host::ProcedureExecutorOutcome {
            (void)request.effects().report(
                host::ProcedureEffect::Input, host::ProcedureEffectStage::Committed);
            throw std::runtime_error("unsafe adapter detail");
        }), coordinator);
    const auto thrown = invoke(throwing, arguments());
    check(thrown.has_error() && thrown.error().code == runtime::HostErrorCode::Internal &&
              thrown.error().effect_state == runtime::HostEffectState::Committed,
          "executor exceptions must not cross the Host ABI and must retain effect trace");
    auto allocating = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556",
        std::make_shared<LambdaExecutor>([](const host::ProcedureExecutionRequest& request)
            -> host::ProcedureExecutorOutcome {
            (void)request.effects().report(
                host::ProcedureEffect::Capture, host::ProcedureEffectStage::Began);
            throw std::bad_alloc{};
        }), coordinator);
    const auto allocation = invoke(allocating, arguments());
    check(allocation.has_error() &&
              allocation.error().code == runtime::HostErrorCode::BudgetExceeded &&
              allocation.error().effect_state == runtime::HostEffectState::Unknown &&
              detail_string(allocation, "budget_scope") == "external_memory",
          "executor allocation failure must map to bounded HOST005 without ABI unwind");
}

void test_stress_repeated_serialization()
{
    auto coordinator = host::PhysicalDeviceCoordinator::create(
        {.max_devices = 8, .max_waiters = 64, .max_device_id_bytes = 64,
         .poll_interval = 1ms});
    std::atomic<int> active{};
    std::atomic<int> maximum{};
    auto executor = std::make_shared<LambdaExecutor>([&](const host::ProcedureExecutionRequest&) {
        const auto current = active.fetch_add(1) + 1;
        maximum.store(std::max(maximum.load(), current));
        std::this_thread::yield();
        active.fetch_sub(1);
        return host::ProcedureExecutorOutcome::success("joined");
    });
    auto first = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556", executor, coordinator);
    auto second = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556", executor, coordinator);
    for (int repeat = 0; repeat < 64; ++repeat) {
        runtime::HostResult left = runtime::HostResult::success();
        runtime::HostResult right = runtime::HostResult::success();
        std::thread one([&] { left = invoke(first, arguments()); });
        std::thread two([&] { right = invoke(second, arguments()); });
        one.join();
        two.join();
        check(left.ok() && right.ok(), "stress calls must both complete");
    }
    check(maximum.load() == 1 && coordinator->stats().active_devices == 0 &&
              coordinator->stats().waiters == 0,
          "64 repeats must preserve serialization and release every lease/waiter");
}

}  // namespace

int main(const int argc, char** argv)
{
    if (argc == 2) {
        const std::string_view selected(argv[1]);
        if (selected == "snapshot") test_snapshot_validation_identity_and_ownership();
        else if (selected == "metadata") test_metadata_success_options_and_lifetime();
        else if (selected == "mapping") test_validation_terminal_and_error_mapping();
        else if (selected == "same") test_same_device_serialization_and_wait_cancellation();
        else if (selected == "different") test_different_device_concurrency_reentry_and_shutdown();
        else if (selected == "cancel") test_cancellation_deadline_and_exception_safety();
        else if (selected == "stress") test_stress_repeated_serialization();
        else return EXIT_FAILURE;
        return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    test_snapshot_validation_identity_and_ownership();
    test_metadata_success_options_and_lifetime();
    test_validation_terminal_and_error_mapping();
    test_same_device_serialization_and_wait_cancellation();
    test_different_device_concurrency_reentry_and_shutdown();
    test_cancellation_deadline_and_exception_safety();
    test_stress_repeated_serialization();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "procedure Host foundation tests passed\n";
    return EXIT_SUCCESS;
}
