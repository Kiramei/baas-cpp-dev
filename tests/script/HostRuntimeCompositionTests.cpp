#include "script/host/HostRuntimeComposition.h"
#include "script/runtime/SynchronousEvaluator.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace host = baas::script::host;
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

template <class Function>
void expect_composition_error(
    const host::HostRuntimeCompositionErrorCode code, Function&& function,
    const std::string_view message)
{
    try {
        std::forward<Function>(function)();
        check(false, message);
    } catch (const host::HostRuntimeCompositionError& error) {
        check(error.code() == code, message);
    } catch (...) {
        check(false, message);
    }
}

std::shared_ptr<const runtime::HostModuleRegistry> metadata(
    std::string module, std::string export_name, std::string binding,
    std::string capability)
{
    return std::make_shared<const runtime::HostModuleRegistry>(
        std::vector<runtime::HostModuleDescriptor>{
            {std::move(module), {1, 0},
             {{std::move(export_name), std::move(binding),
               std::move(capability)}}},
        });
}

runtime::SynchronousNativeBinding null_binding(std::string binding_id)
{
    return {
        std::move(binding_id),
        {{}, runtime::HostValueType::Null, "adapter_calls",
         runtime::HostExecutionMode::ThreadSafe,
         runtime::HostCancellationMode::Preflight},
        [](const runtime::HostCallContext&, const runtime::HostArguments&) {
            return runtime::HostResult::success();
        }};
}

std::shared_ptr<const runtime::SynchronousNativeBindingSet> bindings(
    std::string binding_id)
{
    return std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            null_binding(std::move(binding_id))});
}

void test_empty_and_snapshot_composition()
{
    const auto empty = host::compose_host_runtime({});
    check(empty.module_version_count() == 0,
          "empty composition must own an empty metadata registry");
    check(empty.binding_count() == 0,
          "empty composition must own an empty binding set");

    auto first_metadata = metadata(
        "baas/alpha", "run", "host.alpha.run.v1", "alpha.run");
    auto first_bindings = bindings("host.alpha.run.v1");
    auto second_metadata = metadata(
        "baas/beta", "run", "host.beta.run.v1", "beta.run");
    auto second_bindings = bindings("host.beta.run.v1");
    auto composed = host::compose_host_runtime({
        host::make_host_runtime_contribution(first_metadata, first_bindings),
        host::make_host_runtime_contribution(second_metadata, second_bindings),
    });
    first_metadata.reset();
    first_bindings.reset();
    second_metadata.reset();
    second_bindings.reset();

    runtime::SynchronousHostOptions options = composed.options();
    options.permissions.declared_modules = {
        {"baas/alpha", 1, 0}, {"baas/beta", 1, 0}};
    options.permissions.declared_capabilities = {"alpha.run", "beta.run"};
    options.permissions.policy_capabilities = {"alpha.run", "beta.run"};
    options.permissions.platform_capabilities = {"alpha.run", "beta.run"};
    options.permissions.task_capabilities = {"alpha.run", "beta.run"};
    runtime::SynchronousEvaluator evaluator(
        {{"main",
          "import \"baas/alpha\" as alpha;\n"
          "import \"baas/beta\" as beta;\n"
          "alpha.run();\n"
          "beta.run();\n"
          "let result = 2;\n"}},
        std::move(options));
    static_cast<void>(evaluator.execute("main"));
    check(evaluator.module_export("main", "result").as_integer() == 2,
          "one evaluator must invoke bindings from two composed adapters");
    check(evaluator.stats().host_calls == 2,
          "composed bindings must retain independent callback identities");
}

void test_validation_and_duplicate_rejection()
{
    auto valid_metadata = metadata(
        "baas/alpha", "run", "host.alpha.run.v1", "alpha.run");
    auto valid_bindings = bindings("host.alpha.run.v1");
    expect_composition_error(
        host::HostRuntimeCompositionErrorCode::MissingMetadata,
        [&] {
            static_cast<void>(host::make_host_runtime_contribution(
                {}, valid_bindings));
        },
        "missing metadata must fail with a stable composition code");
    expect_composition_error(
        host::HostRuntimeCompositionErrorCode::MissingBindings,
        [&] {
            static_cast<void>(host::make_host_runtime_contribution(
                valid_metadata, {}));
        },
        "missing bindings must fail with a stable composition code");

    const auto contribution = host::make_host_runtime_contribution(
        valid_metadata, valid_bindings);
    try {
        static_cast<void>(host::compose_host_runtime(
            {contribution, contribution}));
        check(false, "duplicate module versions must be rejected after composition");
    } catch (const runtime::HostRegistryError& error) {
        check(error.code() == runtime::HostRegistryErrorCode::DuplicateModuleVersion,
              "duplicate metadata must preserve registry diagnostics");
    } catch (...) {
        check(false, "duplicate metadata must preserve registry diagnostics");
    }

    host::HostRuntimeContribution unbound;
    unbound.metadata = valid_metadata->descriptors();
    expect_composition_error(
        host::HostRuntimeCompositionErrorCode::UnboundExport,
        [&] {
            static_cast<void>(host::compose_host_runtime({unbound}));
        },
        "an export cannot borrow a native binding from another adapter");

    host::HostRuntimeContribution orphan;
    orphan.bindings = valid_bindings->bindings();
    expect_composition_error(
        host::HostRuntimeCompositionErrorCode::OrphanBinding,
        [&] {
            static_cast<void>(host::compose_host_runtime({orphan}));
        },
        "a native binding without local export metadata must fail closed");

    host::HostRuntimeContribution cross_metadata;
    cross_metadata.metadata = valid_metadata->descriptors();
    host::HostRuntimeContribution cross_binding;
    cross_binding.bindings = valid_bindings->bindings();
    expect_composition_error(
        host::HostRuntimeCompositionErrorCode::UnboundExport,
        [&] {
            static_cast<void>(host::compose_host_runtime(
                {cross_metadata, cross_binding}));
        },
        "separate contributions cannot silently satisfy each other's links");

    host::HostRuntimeCompositionLimits limits;
    limits.max_contributions = 1;
    expect_composition_error(
        host::HostRuntimeCompositionErrorCode::ContributionLimitExceeded,
        [&] {
            static_cast<void>(host::compose_host_runtime(
                {contribution, contribution}, limits));
        },
        "contribution count must be bounded before concatenation");
}

void test_dispatcher_and_lifetime_ownership()
{
    auto dispatcher = std::make_shared<runtime::HostReleaseDispatcher>(
        7, std::vector<runtime::HostReleaseAdapter>{});
    auto owner = std::make_shared<int>(9);
    std::weak_ptr<int> retained = owner;
    host::HostRuntimeContribution first;
    first.lifetime_owners.push_back(owner);
    first.handles = dispatcher;
    host::HostRuntimeContribution second;
    second.handles = dispatcher;
    {
        auto composed = host::compose_host_runtime({first, second});
        owner.reset();
        check(!retained.expired(),
              "composed runtime must retain explicit adapter lifetime owners");
        check(composed.options().handles == dispatcher,
              "repeated identical dispatchers must collapse to one owner");
    }
    first.lifetime_owners.clear();
    check(retained.expired(),
          "adapter owner must release after every composition owner is gone");

    auto other = std::make_shared<runtime::HostReleaseDispatcher>(
        8, std::vector<runtime::HostReleaseAdapter>{});
    host::HostRuntimeContribution third;
    third.handles = std::move(other);
    expect_composition_error(
        host::HostRuntimeCompositionErrorCode::MultipleReleaseDispatchers,
        [&] {
            static_cast<void>(host::compose_host_runtime({first, third}));
        },
        "one evaluator must reject distinct typed-handle dispatchers");

    host::HostRuntimeCompositionLimits limits;
    limits.max_lifetime_owners = 1;
    host::HostRuntimeContribution too_many;
    too_many.lifetime_owners = {
        std::make_shared<int>(1), std::make_shared<int>(2)};
    expect_composition_error(
        host::HostRuntimeCompositionErrorCode::LifetimeOwnerLimitExceeded,
        [&] {
            static_cast<void>(host::compose_host_runtime({too_many}, limits));
        },
        "explicit lifetime ownership must have a bounded total");

    auto evaluator_owner = std::make_shared<int>(3);
    std::weak_ptr<int> evaluator_retained = evaluator_owner;
    runtime::SynchronousHostOptions retained_options;
    {
        host::HostRuntimeContribution owned;
        owned.lifetime_owners.push_back(evaluator_owner);
        auto composed = host::compose_host_runtime({std::move(owned)});
        retained_options = composed.options();
    }
    evaluator_owner.reset();
    check(!evaluator_retained.expired(),
          "host options must retain composition owners after the result dies");
    retained_options = {};
    check(evaluator_retained.expired(),
          "composition owners must release after evaluator options are gone");

    host::HostRuntimeCompositionLimits invalid_limits;
    invalid_limits.bindings.max_bindings = 0;
    expect_composition_error(
        host::HostRuntimeCompositionErrorCode::InvalidLimits,
        [&] {
            static_cast<void>(host::compose_host_runtime({}, invalid_limits));
        },
        "invalid nested limits must always use the composition error family");
}

}  // namespace

int main()
{
    try {
        test_empty_and_snapshot_composition();
        test_validation_and_duplicate_rejection();
        test_dispatcher_and_lifetime_ownership();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    if (failures != 0) {
        std::cerr << failures << " Host runtime composition test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Host runtime composition tests passed\n";
    return EXIT_SUCCESS;
}
