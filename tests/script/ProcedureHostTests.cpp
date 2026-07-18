#include "script/host/ProcedureHost.h"
#include "script/runtime/SynchronousEvaluator.h"

#include <algorithm>
#include <array>
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
        std::move(resource_ids), resources::sha256_hex(*bytes("legacy.test-implementation/v1")), {}};
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

runtime::HostCallContext context(
    std::shared_ptr<const Probe> probe = {},
    std::shared_ptr<const runtime::HostAdmissionToken> admission = {})
{
    return {"baas/procedure", "run", "host.procedure.run.v1", {1, 0}, 0,
            std::move(probe), std::move(admission)};
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
    std::shared_ptr<const Probe> probe = {},
    std::shared_ptr<const runtime::HostAdmissionToken> admission = {})
{
    const auto* binding = owner.bindings->find("host.procedure.run.v1");
    check(binding != nullptr, "procedure binding must exist");
    return runtime::invoke_host_callback(
        *binding, context(std::move(probe), std::move(admission)), args,
        owner.bindings->limits(), nullptr);
}

runtime::SynchronousHostOptions procedure_log_options(
    const host::ProcedureHostRuntime& procedure,
    const std::shared_ptr<runtime::InMemoryLogHost>& log)
{
    const auto* run = procedure.bindings->find("host.procedure.run.v1");
    check(run != nullptr, "combined evaluator fixture requires procedure binding");
    runtime::SynchronousHostOptions options;
    options.metadata = std::make_shared<const runtime::HostModuleRegistry>(
        std::vector<runtime::HostModuleDescriptor>{
            {"baas/procedure", {1, 0},
             {{"run", "host.procedure.run.v1", "procedure.execute"}}},
            {"baas/log", {1, 0},
             {{"emit", "host.log.emit.v1", "log.emit"}}},
        });
    options.bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            *run, runtime::make_in_memory_log_binding(log)});
    options.permissions.declared_modules = {
        {"baas/procedure", 1, 0}, {"baas/log", 1, 0}};
    options.permissions.declared_capabilities = {"procedure.execute", "log.emit"};
    options.permissions.policy_capabilities = {"procedure.execute", "log.emit"};
    options.permissions.platform_capabilities = {"procedure.execute", "log.emit"};
    options.permissions.task_capabilities = {"procedure.execute", "log.emit"};
    return options;
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

    auto changed_implementation = descriptor();
    changed_implementation.implementation_sha256 =
        resources::sha256_hex(*bytes("legacy.test-implementation/v2"));
    changed_implementation.sha256 =
        host::procedure_descriptor_sha256(changed_implementation);
    check(host::ProcedureSnapshot::build({descriptor()}, resources)->snapshot_id() !=
              host::ProcedureSnapshot::build({changed_implementation}, resources)->snapshot_id(),
          "procedure identity must bind the executable implementation digest");

    auto invalid_implementation = descriptor();
    invalid_implementation.implementation_sha256 = "not-a-digest";
    expect_snapshot_error(host::ProcedureSnapshotErrorCode::InvalidDigest, [&] {
        (void)host::procedure_descriptor_sha256(invalid_implementation);
    }, "implementation identity must be a lowercase SHA-256 digest");

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
    duplicate_effect.declared_effects.pop_back();
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
    check(host::procedure_host_error_codes ==
              std::array{
                  runtime::HostErrorCode::CapabilityDenied,
                  runtime::HostErrorCode::InvalidArgument,
                  runtime::HostErrorCode::Cancelled,
                  runtime::HostErrorCode::DeadlineExceeded,
                  runtime::HostErrorCode::BudgetExceeded,
                  runtime::HostErrorCode::Unavailable,
                  runtime::HostErrorCode::DeviceDisconnected,
                  runtime::HostErrorCode::ResourceNotFound,
                  runtime::HostErrorCode::Internal,
                  runtime::HostErrorCode::Backpressure},
          "runtime error metadata must exactly match the machine catalog order");
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
                      host::ProcedureEffectStage::Began) &&
                      request.effects().report(
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
    try {
        (void)owner.metadata->resolve({
            {{"baas/procedure", 1, 0}}, {"procedure.execute"},
            {{"baas/procedure", {"run"}}}, {},
            {"procedure.execute"}, {"procedure.execute"}});
        check(false, "policy denial must reject procedure capability resolution");
    } catch (const runtime::HostRegistryError& error) {
        check(error.code() == runtime::HostRegistryErrorCode::CapabilityDenied &&
                  error.capability() == "procedure.execute",
              "procedure capability denial must remain the registry's HOST001 boundary");
    } catch (...) {
        check(false, "procedure capability denial must be a typed registry error");
    }
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

    const auto invalid_sequence = [&](const auto& report_sequence, const std::string_view message) {
        auto owner = make_owner([&](const host::ProcedureExecutionRequest& request) {
            report_sequence(request.effects());
            return host::ProcedureExecutorOutcome::success("joined");
        });
        const auto result = invoke(owner, arguments());
        check(result.has_error() && result.error().code == runtime::HostErrorCode::Internal,
              message);
    };
    invalid_sequence([](host::ProcedureEffectReporter& reporter) {
        check(reporter.report(host::ProcedureEffect::Capture,
                  host::ProcedureEffectStage::Began), "first began must be accepted");
        check(!reporter.report(host::ProcedureEffect::Capture,
                   host::ProcedureEffectStage::Began), "duplicate began must be rejected");
    }, "duplicate began must invalidate the complete effect trace");
    invalid_sequence([](host::ProcedureEffectReporter& reporter) {
        check(!reporter.report(host::ProcedureEffect::Capture,
                   host::ProcedureEffectStage::Committed),
              "committed without began must be rejected");
    }, "committed without began must invalidate the complete effect trace");
    invalid_sequence([](host::ProcedureEffectReporter& reporter) {
        check(!reporter.report(host::ProcedureEffect::Capture,
                   host::ProcedureEffectStage::Unknown),
              "unknown without began must be rejected");
    }, "unknown without began must invalidate the complete effect trace");
    invalid_sequence([](host::ProcedureEffectReporter& reporter) {
        check(reporter.report(host::ProcedureEffect::Capture,
                  host::ProcedureEffectStage::Began) &&
                  reporter.report(host::ProcedureEffect::Capture,
                      host::ProcedureEffectStage::Committed),
              "valid terminal sequence must be accepted");
        check(!reporter.report(host::ProcedureEffect::Capture,
                   host::ProcedureEffectStage::Unknown),
              "report after terminal must be rejected");
    }, "a terminal report without a new began must invalidate the complete effect trace");

    auto repeated_effect = make_owner([](const host::ProcedureExecutionRequest& request) {
        check(request.effects().report(
                  host::ProcedureEffect::Capture, host::ProcedureEffectStage::Began) &&
                  request.effects().report(
                      host::ProcedureEffect::Capture,
                      host::ProcedureEffectStage::Committed) &&
                  request.effects().report(
                      host::ProcedureEffect::Capture,
                      host::ProcedureEffectStage::Began) &&
                  request.effects().report(
                      host::ProcedureEffect::Capture,
                      host::ProcedureEffectStage::Committed),
              "two complete operations of one declared effect must be accepted");
        return host::ProcedureExecutorOutcome::success("joined");
    });
    check(invoke(repeated_effect, arguments()).ok(),
          "committed aggregate state must allow a subsequent paired operation");
    invalid_sequence([](host::ProcedureEffectReporter& reporter) {
        check(reporter.report(host::ProcedureEffect::Capture,
                  host::ProcedureEffectStage::Began) &&
                  reporter.report(host::ProcedureEffect::Capture,
                      host::ProcedureEffectStage::Unknown),
              "began-to-unknown must be accepted once");
        check(!reporter.report(host::ProcedureEffect::Capture,
                   host::ProcedureEffectStage::Began),
              "unknown aggregate state must permanently reject another operation");
    }, "unknown completion must permanently invalidate later reports");

    auto foreground = make_owner([](const host::ProcedureExecutionRequest& request) {
        for (const auto effect : {host::ProcedureEffect::Capture,
                 host::ProcedureEffect::Vision, host::ProcedureEffect::Wait,
                 host::ProcedureEffect::ForegroundCheck}) {
            (void)request.effects().report(effect, host::ProcedureEffectStage::Began);
            (void)request.effects().report(effect, host::ProcedureEffectStage::Committed);
        }
        return host::ProcedureExecutorOutcome::failure({
            host::ProcedureExecutorErrorCode::ForegroundPackageMismatch, false,
            runtime::HostEffectState::Committed});
    });
    auto foreground_result = invoke(foreground, arguments());
    check(foreground_result.has_error() &&
              foreground_result.error().code == runtime::HostErrorCode::Unavailable &&
              foreground_result.error().retryable &&
              foreground_result.error().effect_state == runtime::HostEffectState::NotStarted &&
              detail_string(foreground_result, "unavailable_reason") ==
                  "foreground_package_mismatch",
          "non-input effects and supplied state must not forge foreground input effect state");
    auto foreground_not_started = make_owner([](const host::ProcedureExecutionRequest&) {
        return host::ProcedureExecutorOutcome::failure({
            host::ProcedureExecutorErrorCode::ForegroundPackageMismatch, false,
            runtime::HostEffectState::NotStarted});
    });
    auto not_started_result = invoke(foreground_not_started, arguments());
    check(not_started_result.has_error() && not_started_result.error().retryable &&
              not_started_result.error().effect_state ==
                  runtime::HostEffectState::NotStarted &&
              detail_string(not_started_result, "unavailable_reason") ==
                  "foreground_package_mismatch",
          "foreground mismatch before effects must remain retryable/not_started");
    auto foreground_committed = make_owner([](const host::ProcedureExecutionRequest& request) {
        (void)request.effects().report(
            host::ProcedureEffect::Input, host::ProcedureEffectStage::Began);
        (void)request.effects().report(
            host::ProcedureEffect::Input, host::ProcedureEffectStage::Committed);
        return host::ProcedureExecutorOutcome::failure({
            host::ProcedureExecutorErrorCode::ForegroundPackageMismatch, false,
            runtime::HostEffectState::NotStarted});
    });
    auto committed_result = invoke(foreground_committed, arguments());
    check(committed_result.has_error() && committed_result.error().retryable &&
              committed_result.error().effect_state ==
                  runtime::HostEffectState::Committed &&
              detail_string(committed_result, "unavailable_reason") ==
                  "foreground_package_mismatch",
          "foreground mismatch after a confirmed input must remain retryable/committed");

    for (const auto stage : {
             host::ProcedureEffectStage::Began,
             host::ProcedureEffectStage::Unknown}) {
        auto foreground_unknown = make_owner([stage](
            const host::ProcedureExecutionRequest& request) {
            (void)request.effects().report(
                host::ProcedureEffect::Input, host::ProcedureEffectStage::Began);
            if (stage == host::ProcedureEffectStage::Unknown)
                (void)request.effects().report(host::ProcedureEffect::Input, stage);
            return host::ProcedureExecutorOutcome::failure({
                host::ProcedureExecutorErrorCode::ForegroundPackageMismatch, false,
                runtime::HostEffectState::NotStarted});
        });
        auto unknown_result = invoke(foreground_unknown, arguments());
        check(unknown_result.has_error() && unknown_result.error().retryable &&
                  unknown_result.error().effect_state ==
                      runtime::HostEffectState::Unknown &&
                  detail_string(unknown_result, "unavailable_reason") ==
                      "foreground_package_mismatch",
              "begun or indeterminate input must make foreground mismatch effect unknown");
    }

    auto disconnected = make_owner([](const host::ProcedureExecutionRequest& request) {
        (void)request.effects().report(
            host::ProcedureEffect::Input, host::ProcedureEffectStage::Began);
        (void)request.effects().report(
            host::ProcedureEffect::Input, host::ProcedureEffectStage::Committed);
        return host::ProcedureExecutorOutcome::failure({
            host::ProcedureExecutorErrorCode::DeviceDisconnected, true,
            runtime::HostEffectState::NotStarted});
    });
    auto disconnected_result = invoke(disconnected, arguments());
    check(disconnected_result.has_error() &&
              disconnected_result.error().code == runtime::HostErrorCode::DeviceDisconnected &&
              disconnected_result.error().effect_state == runtime::HostEffectState::Committed,
          "typed device error must preserve known committed effects");

    struct TypedErrorCase {
        host::ProcedureExecutorErrorCode supplied;
        runtime::HostErrorCode expected;
        bool expected_retryable;
    };
    const std::array typed_errors{
        TypedErrorCase{host::ProcedureExecutorErrorCode::InvalidRequest,
                       runtime::HostErrorCode::InvalidArgument, false},
        TypedErrorCase{host::ProcedureExecutorErrorCode::Cancelled,
                       runtime::HostErrorCode::Cancelled, false},
        TypedErrorCase{host::ProcedureExecutorErrorCode::DeadlineExceeded,
                       runtime::HostErrorCode::DeadlineExceeded, false},
        TypedErrorCase{host::ProcedureExecutorErrorCode::BudgetExceeded,
                       runtime::HostErrorCode::BudgetExceeded, true},
        TypedErrorCase{host::ProcedureExecutorErrorCode::ResourceExhausted,
                       runtime::HostErrorCode::BudgetExceeded, false},
        TypedErrorCase{host::ProcedureExecutorErrorCode::Unavailable,
                       runtime::HostErrorCode::Unavailable, true},
        TypedErrorCase{host::ProcedureExecutorErrorCode::ResourceNotFound,
                       runtime::HostErrorCode::ResourceNotFound, true},
        TypedErrorCase{host::ProcedureExecutorErrorCode::Internal,
                       runtime::HostErrorCode::Internal, false},
    };
    for (const auto& typed : typed_errors) {
        auto typed_owner = make_owner([typed](const host::ProcedureExecutionRequest&) {
            return host::ProcedureExecutorOutcome::failure({
                typed.supplied, true, runtime::HostEffectState::NotStarted});
        });
        const auto mapped = invoke(typed_owner, arguments());
        check(mapped.has_error() && mapped.error().code == typed.expected &&
                  mapped.error().effect_state == runtime::HostEffectState::NotStarted &&
                  mapped.error().retryable == typed.expected_retryable,
              "every typed executor error must map to its exact public Host code");
    }
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

    release_first = false;
    entered = 0;
    std::thread deadline_holder([&] { (void)invoke(first, arguments()); });
    check(wait_until([&] { return entered.load() == 1; }, 2s),
          "holder must enter executor before deadline wait test");
    auto deadline_probe = std::make_shared<Probe>();
    runtime::HostResult deadline_result = runtime::HostResult::success();
    std::thread deadline_waiter(
        [&] { deadline_result = invoke(second, arguments(), deadline_probe); });
    check(wait_until([&] { return coordinator->stats().waiters == 1; }, 2s),
          "deadline call must first wait on the shared strand");
    deadline_probe->deadline = true;
    deadline_waiter.join();
    check(deadline_result.has_error() &&
              deadline_result.error().code == runtime::HostErrorCode::DeadlineExceeded &&
              deadline_result.error().effect_state == runtime::HostEffectState::NotStarted &&
              !deadline_result.error().retryable,
          "strand wait must cooperatively observe deadline without starting effects");
    release_first = true;
    deadline_holder.join();

    release_first = false;
    entered = 0;
    std::thread shutdown_holder([&] { (void)invoke(first, arguments()); });
    check(wait_until([&] { return entered.load() == 1; }, 2s),
          "holder must enter executor before coordinator shutdown test");
    runtime::HostResult shutdown_result = runtime::HostResult::success();
    std::thread shutdown_waiter([&] { shutdown_result = invoke(second, arguments()); });
    check(wait_until([&] { return coordinator->stats().waiters == 1; }, 2s),
          "shutdown test must have a bounded strand waiter");
    coordinator->shutdown();
    shutdown_waiter.join();
    check(shutdown_result.has_error() &&
              shutdown_result.error().code == runtime::HostErrorCode::Unavailable &&
              shutdown_result.error().effect_state == runtime::HostEffectState::NotStarted,
          "coordinator shutdown must wake and fail a pending strand waiter");
    release_first = true;
    shutdown_holder.join();
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
              !inner_result.error().details,
          "same-thread same-device reentry must fail immediately instead of deadlocking");

    coordinator->shutdown();
    const auto stopped = invoke(first, arguments());
    check(stopped.has_error() && stopped.error().code == runtime::HostErrorCode::Unavailable &&
              !stopped.error().details,
          "coordinator shutdown must reject new work without inventing a public discriminator");
}

void test_cross_thread_logical_reentry_is_bounded()
{
    auto coordinator = host::PhysicalDeviceCoordinator::create(
        {.max_devices = 8, .max_waiters = 32, .max_device_id_bytes = 64,
         .poll_interval = 1ms});
    auto inner = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556",
        std::make_shared<LambdaExecutor>([](const host::ProcedureExecutionRequest&) {
            return host::ProcedureExecutorOutcome::success("joined");
        }), coordinator);
    auto nested_probe = std::make_shared<Probe>();
    runtime::HostResult nested_result = runtime::HostResult::success();
    std::atomic<bool> nested_returned{};
    bool returned_before_cancel{};
    std::shared_ptr<const runtime::HostAdmissionToken> retained_admission;
    auto outer = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556",
        std::make_shared<LambdaExecutor>([&](const host::ProcedureExecutionRequest& request) {
            retained_admission = request.admission_token();
            std::thread helper([&, admission = request.admission_token()] {
                nested_result = invoke(
                    inner, arguments(), nested_probe, std::move(admission));
                nested_returned.store(true, std::memory_order_release);
            });
            returned_before_cancel = wait_until(
                [&] { return nested_returned.load(std::memory_order_acquire); }, 500ms);
            if (!returned_before_cancel)
                nested_probe->cancel.store(true, std::memory_order_release);
            helper.join();
            return host::ProcedureExecutorOutcome::success("joined");
        }), coordinator);

    const auto outer_result = invoke(outer, arguments());
    check(outer_result.ok() && returned_before_cancel && nested_result.has_error() &&
              nested_result.error().code == runtime::HostErrorCode::Unavailable &&
              nested_result.error().effect_state ==
                  runtime::HostEffectState::NotStarted &&
              coordinator->stats().waiters == 0,
          "propagated logical admission must reject cross-thread same-device reentry without queuing");

    const auto after_release = invoke(
        inner, arguments(), {}, std::move(retained_admission));
    check(after_release.ok(),
          "a retained admission token must become inert when its owning lease is released");

    auto bounded = host::PhysicalDeviceCoordinator::create(
        {.max_devices = 8, .max_waiters = 8, .max_device_id_bytes = 64,
         .poll_interval = 1ms, .max_admission_depth = 1});
    auto root = bounded->acquire("emulator-5556", {});
    const auto too_deep = bounded->acquire(
        "emulator-5558", {}, root.admission);
    check(root.code == host::PhysicalDeviceAcquireCode::Acquired &&
              too_deep.code == host::PhysicalDeviceAcquireCode::Backpressure &&
              !too_deep.lease && !too_deep.admission,
          "logical admission lineage must fail closed at its configured depth bound");
}

void test_same_device_multi_waiter_fifo_order()
{
    auto coordinator = host::PhysicalDeviceCoordinator::create(
        {.max_devices = 8, .max_waiters = 32, .max_device_id_bytes = 64,
         .poll_interval = 1ms});
    std::atomic<bool> holder_entered{};
    std::atomic<bool> release_holder{};
    std::mutex order_mutex;
    std::vector<int> order;
    const auto record = [&](const int value) {
        const std::scoped_lock lock(order_mutex);
        order.push_back(value);
    };
    auto holder = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556",
        std::make_shared<LambdaExecutor>([&](const host::ProcedureExecutionRequest&) {
            record(0);
            holder_entered.store(true, std::memory_order_release);
            while (!release_holder.load(std::memory_order_acquire))
                std::this_thread::yield();
            return host::ProcedureExecutorOutcome::success("joined");
        }), coordinator);

    std::vector<host::ProcedureHostRuntime> waiters;
    waiters.reserve(4);
    for (int id = 1; id <= 4; ++id) {
        waiters.push_back(host::make_procedure_host_runtime(
            procedure_snapshot(), "emulator-5556",
            std::make_shared<LambdaExecutor>([&, id](const host::ProcedureExecutionRequest&) {
                record(id);
                return host::ProcedureExecutorOutcome::success("joined");
            }), coordinator));
    }

    std::thread holder_thread([&] { (void)invoke(holder, arguments()); });
    check(wait_until(
              [&] { return holder_entered.load(std::memory_order_acquire); }, 2s),
          "FIFO holder must own the device before waiters start");
    std::array<int, 4> waiter_ok{};
    std::vector<std::thread> threads;
    threads.reserve(waiters.size());
    for (std::size_t index = 0; index < waiters.size(); ++index) {
        threads.emplace_back([&, index] {
            waiter_ok[index] = invoke(waiters[index], arguments()).ok() ? 1 : -1;
        });
        check(wait_until(
                  [&] { return coordinator->stats().waiters == index + 1; }, 2s),
              "each FIFO waiter must be admitted to the queue in launch order");
    }
    release_holder.store(true, std::memory_order_release);
    holder_thread.join();
    for (auto& thread : threads) thread.join();
    check(order == std::vector<int>({0, 1, 2, 3, 4}) &&
              std::all_of(waiter_ok.begin(), waiter_ok.end(),
                          [](const int value) { return value == 1; }) &&
              coordinator->stats().waiters == 0,
          "same-device multi-waiter admission must preserve exact FIFO order");
}

void test_queued_acquisition_allocation_failure_recovers()
{
    for (std::size_t checkpoint = 1;
         checkpoint <= host::testing::queued_acquisition_allocation_checkpoints;
         ++checkpoint) {
        auto coordinator = host::PhysicalDeviceCoordinator::create(
            {.max_devices = 8, .max_waiters = 8, .max_device_id_bytes = 64,
             .poll_interval = 1ms});
        std::atomic<bool> holder_entered{};
        std::atomic<bool> release_holder{};
        auto holder = host::make_procedure_host_runtime(
            procedure_snapshot(), "emulator-5556",
            std::make_shared<LambdaExecutor>([&](const host::ProcedureExecutionRequest&) {
                holder_entered.store(true, std::memory_order_release);
                while (!release_holder.load(std::memory_order_acquire))
                    std::this_thread::yield();
                return host::ProcedureExecutorOutcome::success("joined");
            }), coordinator);
        auto waiter = host::make_procedure_host_runtime(
            procedure_snapshot(), "emulator-5556",
            std::make_shared<LambdaExecutor>([](const host::ProcedureExecutionRequest&) {
                return host::ProcedureExecutorOutcome::success("joined");
            }), coordinator);
        auto successor = host::make_procedure_host_runtime(
            procedure_snapshot(), "emulator-5556",
            std::make_shared<LambdaExecutor>([](const host::ProcedureExecutionRequest&) {
                return host::ProcedureExecutorOutcome::success("joined");
            }), coordinator);
        runtime::HostResult failed = runtime::HostResult::success();
        runtime::HostResult succeeded = runtime::HostResult::success();
        std::thread holder_thread([&] { (void)invoke(holder, arguments()); });
        check(wait_until(
                  [&] { return holder_entered.load(std::memory_order_acquire); }, 2s),
              "allocation-failure holder must own the strand");
        std::thread waiter_thread([&] { failed = invoke(waiter, arguments()); });
        check(wait_until([&] { return coordinator->stats().waiters == 1; }, 2s),
              "allocation-failure call must be queued before injection");
        std::thread successor_thread(
            [&] { succeeded = invoke(successor, arguments()); });
        check(wait_until([&] { return coordinator->stats().waiters == 2; }, 2s),
              "allocation-failure successor must queue behind the injected front");
        host::testing::fail_queued_acquisition_at_allocation(checkpoint);
        release_holder.store(true, std::memory_order_release);
        holder_thread.join();
        waiter_thread.join();
        successor_thread.join();
        host::testing::fail_queued_acquisition_at_allocation(0);

        const auto stats = coordinator->stats();
        check(failed.has_error() &&
                  failed.error().code == runtime::HostErrorCode::BudgetExceeded &&
                  failed.error().effect_state == runtime::HostEffectState::NotStarted &&
                  succeeded.ok() && stats.waiters == 0 && stats.active_devices == 0,
              "queued admission allocation failure must erase its front ticket and stats");
        const auto recovered = invoke(waiter, arguments());
        check(recovered.ok() && coordinator->stats().waiters == 0,
              "same-device admission must recover after queued allocation failure");
    }
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
                false, runtime::HostEffectState::NotStarted});
        }), coordinator);
    auto cancelled_before = std::make_shared<Probe>();
    cancelled_before->cancel = true;
    const auto preflight_cancel = invoke(owner, arguments(), cancelled_before);
    check(preflight_cancel.has_error() &&
              preflight_cancel.error().code == runtime::HostErrorCode::Cancelled &&
              preflight_cancel.error().effect_state == runtime::HostEffectState::NotStarted &&
              !entered.load(),
          "cancellation before strand admission must not enter the executor");
    runtime::HostResult result = runtime::HostResult::success();
    std::thread worker([&] { result = invoke(owner, arguments(), probe); });
    check(wait_until([&] { return entered.load(); }, 2s),
          "cooperative executor must begin before cancellation");
    probe->cancel = true;
    worker.join();
    check(result.has_error() && result.error().code == runtime::HostErrorCode::Cancelled &&
              result.error().effect_state == runtime::HostEffectState::Unknown,
          "cancellation during executor must preserve uncertain begun effect state");

    entered = false;
    auto executing_deadline = std::make_shared<Probe>();
    runtime::HostResult execution_deadline_result = runtime::HostResult::success();
    std::thread deadline_worker(
        [&] { execution_deadline_result = invoke(owner, arguments(), executing_deadline); });
    check(wait_until([&] { return entered.load(); }, 2s),
          "cooperative executor must begin before execution deadline");
    executing_deadline->deadline = true;
    deadline_worker.join();
    check(execution_deadline_result.has_error() &&
              execution_deadline_result.error().code ==
                  runtime::HostErrorCode::DeadlineExceeded &&
              execution_deadline_result.error().effect_state ==
                  runtime::HostEffectState::Unknown &&
              !execution_deadline_result.error().retryable,
          "deadline during executor must preserve uncertain begun effect state");

    auto both = std::make_shared<Probe>();
    both->cancel = true;
    both->deadline = true;
    const auto precedence = invoke(owner, arguments(), both);
    check(precedence.has_error() &&
              precedence.error().code == runtime::HostErrorCode::DeadlineExceeded &&
              !precedence.error().retryable &&
              detail_string(precedence, "deadline_scope") == "call",
          "deadline must win whenever cancellation and deadline are both observable");

    auto throwing = host::make_procedure_host_runtime(
        procedure_snapshot(), "emulator-5556",
        std::make_shared<LambdaExecutor>([](const host::ProcedureExecutionRequest& request)
            -> host::ProcedureExecutorOutcome {
            (void)request.effects().report(
                host::ProcedureEffect::Input, host::ProcedureEffectStage::Began);
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
              !allocation.error().retryable &&
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

void test_real_evaluator_mock_procedure_log_golden_runner()
{
    auto resources = resource_snapshot();
    auto group = descriptor(
        "group",
        {"group_sign-up-reward", "group_menu", "group_join-club"},
        {host::ProcedureEffect::Capture, host::ProcedureEffect::Vision,
         host::ProcedureEffect::Input, host::ProcedureEffect::Wait,
         host::ProcedureEffect::ForegroundCheck},
        {"image/group/menu", "json/group/rules"});
    auto snapshot = host::ProcedureSnapshot::build({group}, resources);
    std::atomic<int> executions{};
    auto executor = std::make_shared<LambdaExecutor>(
        [snapshot, resources, &executions](const host::ProcedureExecutionRequest& request) {
            ++executions;
            check(request.snapshot().get() == snapshot.get() &&
                      request.snapshot()->resource_snapshot().get() == resources.get(),
                  "golden runner must install the exact mock snapshot/resources");
            check(request.procedure()->procedure_id() == "group" &&
                      request.options().size() == 1 &&
                      request.options()[0].first == "scenario" &&
                      request.options()[0].second == runtime::JsonValue("reward"),
                  "real evaluator must pass exact logical procedure/options to mock executor");
            (void)request.effects().report(
                host::ProcedureEffect::Capture, host::ProcedureEffectStage::Began);
            (void)request.effects().report(
                host::ProcedureEffect::Capture, host::ProcedureEffectStage::Committed);
            return host::ProcedureExecutorOutcome::success("group_sign-up-reward");
        });
    auto procedure = host::make_procedure_host_runtime(
        snapshot, "emulator-5556", executor,
        host::PhysicalDeviceCoordinator::create());
    auto log = std::make_shared<runtime::InMemoryLogHost>();
    runtime::SynchronousEvaluator evaluator(
        {{"main",
          "import \"baas/procedure\" as procedure;\n"
          "import \"baas/log\" as log;\n"
          "let result = procedure.run(\"group\", {\"scenario\": \"reward\"});\n"
          "log.emit(\"info\", result.end, {\"end\": result.end});\n"
          "let end = result.end;\n"}},
        procedure_log_options(procedure, log));
    static_cast<void>(evaluator.execute("main"));
    const auto end = evaluator.heap().string_copy(
        evaluator.module_export("main", "end").as_heap_ref());
    const auto events = log->events();
    check(end == "group_sign-up-reward" && executions.load() == 1 &&
              events.size() == 1 && events[0].level == "info" &&
              events[0].message == "group_sign-up-reward" && events[0].fields &&
              *events[0].fields == runtime::JsonObject{{
                  "end", runtime::JsonValue("group_sign-up-reward")}},
          "real evaluator + mock Procedure/LogHost must produce deterministic group golden trace");

    auto mismatch_executor = std::make_shared<LambdaExecutor>(
        [](const host::ProcedureExecutionRequest&) {
            return host::ProcedureExecutorOutcome::failure({
                host::ProcedureExecutorErrorCode::ForegroundPackageMismatch,
                false, runtime::HostEffectState::NotStarted});
        });
    auto mismatch_procedure = host::make_procedure_host_runtime(
        snapshot, "emulator-5556", mismatch_executor,
        host::PhysicalDeviceCoordinator::create());
    auto mismatch_log = std::make_shared<runtime::InMemoryLogHost>();
    runtime::SynchronousEvaluator mismatch(
        {{"main",
          "import \"baas/procedure\" as procedure;\n"
          "let code = \"\"; let host_code = \"\"; let reason = \"\";\n"
          "let retryable = false; let effect = \"\";\n"
          "try { procedure.run(\"group\"); } catch (error) {\n"
          "  code = error.code; host_code = error.details.host_code;\n"
          "  reason = error.details.host_details.unavailable_reason;\n"
          "  retryable = error.details.retryable;\n"
          "  effect = error.details.effect_state;\n"
          "}\n"}},
        procedure_log_options(mismatch_procedure, mismatch_log));
    static_cast<void>(mismatch.execute("main"));
    const auto mismatch_string = [&](const std::string_view name) {
        return mismatch.heap().string_copy(
            mismatch.module_export("main", name).as_heap_ref());
    };
    check(mismatch_string("code") == "HostUnavailable" &&
              mismatch_string("host_code") == "HOST006_UNAVAILABLE" &&
              mismatch_string("reason") == "foreground_package_mismatch" &&
              mismatch.module_export("main", "retryable").as_boolean() &&
              mismatch_string("effect") == "not_started",
          "actual ProcedureHost foreground mismatch must survive ERR-016 into host_details");
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
        else if (selected == "cross-thread") test_cross_thread_logical_reentry_is_bounded();
        else if (selected == "fifo") test_same_device_multi_waiter_fifo_order();
        else if (selected == "alloc-queue") test_queued_acquisition_allocation_failure_recovers();
        else if (selected == "cancel") test_cancellation_deadline_and_exception_safety();
        else if (selected == "stress") test_stress_repeated_serialization();
        else if (selected == "golden") test_real_evaluator_mock_procedure_log_golden_runner();
        else return EXIT_FAILURE;
        return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    test_snapshot_validation_identity_and_ownership();
    test_metadata_success_options_and_lifetime();
    test_validation_terminal_and_error_mapping();
    test_same_device_serialization_and_wait_cancellation();
    test_different_device_concurrency_reentry_and_shutdown();
    test_cross_thread_logical_reentry_is_bounded();
    test_same_device_multi_waiter_fifo_order();
    test_queued_acquisition_allocation_failure_recovers();
    test_cancellation_deadline_and_exception_safety();
    test_stress_repeated_serialization();
    test_real_evaluator_mock_procedure_log_golden_runner();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "procedure Host foundation tests passed\n";
    return EXIT_SUCCESS;
}
