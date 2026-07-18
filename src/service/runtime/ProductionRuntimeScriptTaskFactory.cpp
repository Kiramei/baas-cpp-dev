#include "service/runtime/ProductionRuntimeScriptTaskFactory.h"

#include "runtime/procedure/CoDetectSupportBundle.h"
#include "runtime/resources/RuntimeResourceSnapshotLoader.h"
#include "runtime/script/RuntimeScriptCatalog.h"
#include "runtime/script/RuntimeScriptExecutionPlan.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace baas::service::runtime {
namespace {

namespace repository = ::baas::runtime::repository;
namespace runtime_procedure = ::baas::runtime::procedure;
namespace runtime_resources = ::baas::runtime::resources;
namespace runtime_script = ::baas::runtime::script;
namespace script_host = ::baas::script::host;
namespace script_runtime = ::baas::script::runtime;

using Clock = std::chrono::steady_clock;

RuntimeTaskTerminal failure_terminal(const int exit_code) noexcept
{
    return {false, exit_code};
}

std::optional<RuntimeTaskTerminal> control_terminal(
    const RuntimeScriptTaskExecutionControl& control) noexcept
{
    if (control.deadline_exceeded())
        return failure_terminal(runtime_script_task_deadline_exit_code);
    if (control.stop_requested())
        return failure_terminal(runtime_script_task_cancelled_exit_code);
    return std::nullopt;
}

RuntimeTaskTerminal controlled_failure(
    const RuntimeScriptTaskExecutionControl& control) noexcept
{
    if (const auto boundary = control_terminal(control)) return *boundary;
    return failure_terminal(runtime_script_task_failure_exit_code);
}

bool bounded_text(
    const std::string_view value, const std::size_t maximum) noexcept
{
    return !value.empty() && value.size() <= maximum
        && value.find('\0') == std::string_view::npos;
}

bool lowercase_hex(const std::string_view value) noexcept
{
    return std::ranges::all_of(value, [](const char byte) {
        return (byte >= '0' && byte <= '9')
            || (byte >= 'a' && byte <= 'f');
    });
}

bool exact_generation(const std::string_view value) noexcept
{
    return value.size() == 64 && lowercase_hex(value);
}

bool exact_commit(const std::string_view value) noexcept
{
    return (value.size() == 40 || value.size() == 64)
        && lowercase_hex(value);
}

template <class Range>
bool valid_unique_text_range(
    const Range& values,
    const std::size_t maximum_count,
    const std::size_t maximum_bytes)
{
    if (values.size() > maximum_count) return false;
    std::set<std::string_view, std::less<>> seen;
    for (const auto& value : values) {
        if (!bounded_text(value, maximum_bytes) || !seen.insert(value).second)
            return false;
    }
    return true;
}

bool valid_limits(const ProductionRuntimeScriptTaskFactoryLimits& limits) noexcept
{
    return limits.max_tasks != 0 && limits.max_identity_bytes != 0
        && limits.max_capabilities != 0 && limits.max_secrets != 0
        && limits.max_procedure_support != 0;
}

class ExecutionCancellationProbe final
    : public script_runtime::HostCancellationProbe {
public:
    explicit ExecutionCancellationProbe(
        const RuntimeScriptTaskExecutionControl& control) noexcept
        : stop_(control.stop_token()), deadline_(control.deadline())
    {
    }

    [[nodiscard]] bool cancelled() const noexcept override
    {
        return stop_.stop_requested();
    }

    [[nodiscard]] bool deadline_exceeded() const noexcept override
    {
        return Clock::now() >= deadline_;
    }

private:
    std::stop_token stop_;
    Clock::time_point deadline_;
};

class ProcedureDispatcher final : public script_host::ProcedureExecutor {
public:
    using Entry = std::pair<
        std::string, std::shared_ptr<script_host::ProcedureExecutor>>;

    explicit ProcedureDispatcher(std::vector<Entry> entries)
        : entries_(std::move(entries))
    {
        std::ranges::sort(entries_, {}, &Entry::first);
    }

    [[nodiscard]] script_host::ProcedureExecutorOutcome execute(
        const script_host::ProcedureExecutionRequest& request) override
    {
        const auto& descriptor = request.procedure();
        if (!descriptor) {
            return script_host::ProcedureExecutorOutcome::failure(
                {script_host::ProcedureExecutorErrorCode::InvalidRequest,
                 false, script_runtime::HostEffectState::NotStarted});
        }
        const auto found = std::ranges::lower_bound(
            entries_, descriptor->procedure_id(), {}, &Entry::first);
        if (found == entries_.end()
            || found->first != descriptor->procedure_id() || !found->second) {
            return script_host::ProcedureExecutorOutcome::failure(
                {script_host::ProcedureExecutorErrorCode::InvalidRequest,
                 false, script_runtime::HostEffectState::NotStarted});
        }
        return found->second->execute(request);
    }

private:
    std::vector<Entry> entries_;
};

script_host::HostRuntimeContribution make_log_contribution(
    const std::shared_ptr<script_runtime::QueuedLogHost>& log_host)
{
    script_runtime::HostExportDescriptor emit{
        "emit", "host.log.emit.v1", "log.emit"};
    auto metadata = std::make_shared<const script_runtime::HostModuleRegistry>(
        std::vector<script_runtime::HostModuleDescriptor>{
            {"baas/log", {1, 0}, {std::move(emit)}}});
    auto bindings =
        std::make_shared<const script_runtime::SynchronousNativeBindingSet>(
            std::vector<script_runtime::SynchronousNativeBinding>{
                script_runtime::make_queued_log_binding(log_host)});
    return script_host::make_host_runtime_contribution(
        std::move(metadata), std::move(bindings), {}, {log_host});
}

std::vector<script_runtime::HostModuleRequirement> declared_modules(
    const runtime_script::RuntimeScriptTaskDescriptor& task)
{
    std::vector<script_runtime::HostModuleRequirement> result;
    result.reserve(task.host_modules.size());
    for (const auto& module : task.host_modules)
        result.push_back({module.canonical_id, module.major, module.min_minor});
    return result;
}

const ProductionRuntimeProcedureSupport* find_support(
    const ProductionRuntimeScriptConfigSnapshot& config,
    const std::string_view procedure_id) noexcept
{
    const auto found = std::ranges::lower_bound(
        config.procedure_support, procedure_id, {},
        &ProductionRuntimeProcedureSupport::procedure_id);
    return found != config.procedure_support.end()
            && found->procedure_id == procedure_id
        ? &*found : nullptr;
}

bool exact_device_identity(
    const ProductionRuntimeScriptConfigSnapshot& config,
    const std::shared_ptr<runtime_procedure::CoDetectProductionDevicePort>& port,
    const std::shared_ptr<const runtime_procedure::CoDetectProductionDeviceIdentity>&
        identity) noexcept
{
    if (!port || !identity || identity->device_id != config.device_id
        || identity->profile != config.profile)
        return false;
    const auto current = port->current_identity();
    return current && current == identity
        && current->device_id == config.device_id
        && current->profile == config.profile;
}

bool valid_inputs(
    const ProductionRuntimeScriptTaskInputs& inputs,
    const RuntimeTaskRequest& request,
    const ProductionRuntimeScriptTaskFactoryLimits& limits)
{
    if (!inputs.config || !inputs.repositories || !inputs.trust_evidence
        || !inputs.log_sink)
        return false;
    const auto& config = *inputs.config;
    const auto& bundle = *inputs.repositories;
    if (config.config_id != request.config_id
        || !bounded_text(config.config_id, limits.max_identity_bytes)
        || !bounded_text(config.snapshot_id, limits.max_identity_bytes)
        || !bounded_text(config.device_id, limits.max_identity_bytes)
        || !bounded_text(config.locale, limits.max_identity_bytes)
        || config.resource_selector.locale != config.locale
        || !exact_generation(bundle.generation())
        || bundle.resources().generation() != bundle.generation()
        || bundle.scripts().generation() != bundle.generation()
        || !exact_commit(bundle.resources().commit())
        || !exact_commit(bundle.scripts().commit())
        || bundle.resources().repository_id() != "resources"
        || bundle.scripts().repository_id() != "scripts"
        || !inputs.trust_evidence->covers(
            bundle.generation(), bundle.scripts().commit())
        || !valid_unique_text_range(
            config.policy_capabilities, limits.max_capabilities,
            limits.max_identity_bytes)
        || !valid_unique_text_range(
            config.platform_capabilities, limits.max_capabilities,
            limits.max_identity_bytes)
        || !valid_unique_text_range(
            config.log_secrets, limits.max_secrets,
            limits.log_host.max_secret_bytes))
        return false;

    if (config.procedure_support.size() > limits.max_procedure_support)
        return false;
    std::string_view previous;
    for (const auto& support : config.procedure_support) {
        if (!bounded_text(support.procedure_id, limits.max_identity_bytes)
            || !bounded_text(support.resource_id, limits.max_identity_bytes)
            || (!previous.empty() && support.procedure_id <= previous))
            return false;
        previous = support.procedure_id;
    }
    if (inputs.extensions) {
        const auto& extension = inputs.extensions->identity();
        if (extension.config_snapshot_id != config.snapshot_id
            || extension.generation != bundle.generation()
            || extension.scripts_commit != bundle.scripts().commit()
            || extension.resources_commit != bundle.resources().commit())
            return false;
    }
    return exact_device_identity(config, inputs.device, inputs.device_identity);
}

struct TaskExecution final {
    TaskExecution(
        runtime_script::RuntimeScriptExecutionPlan value_plan,
        std::shared_ptr<const runtime_procedure::RuntimeProcedureActivation>
            value_activation,
        std::vector<std::shared_ptr<const runtime_procedure::CoDetectSupportBundle>>
            value_support_bundles,
        std::shared_ptr<ProcedureDispatcher> value_procedure_dispatcher,
        script_host::ResourceHostRuntime value_resource_host,
        std::optional<script_host::ProcedureHostRuntime> value_procedure_host,
        std::shared_ptr<script_runtime::QueuedLogHost> value_log_host,
        script_host::ComposedHostRuntime value_hosts,
        std::shared_ptr<script_runtime::HostReleaseDispatcher>
            value_release_dispatcher,
        std::unique_ptr<script_runtime::SynchronousEvaluator> value_evaluator)
        : plan(std::move(value_plan)), activation(std::move(value_activation)),
          support_bundles(std::move(value_support_bundles)),
          procedure_dispatcher(std::move(value_procedure_dispatcher)),
          resource_host(std::move(value_resource_host)),
          procedure_host(std::move(value_procedure_host)),
          log_host(std::move(value_log_host)), hosts(std::move(value_hosts)),
          release_dispatcher(std::move(value_release_dispatcher)),
          evaluator(std::move(value_evaluator))
    {
    }

    TaskExecution(const TaskExecution&) = delete;
    TaskExecution& operator=(const TaskExecution&) = delete;
    TaskExecution(TaskExecution&&) noexcept = default;
    TaskExecution& operator=(TaskExecution&&) = delete;

    ~TaskExecution() { (void)finish(); }

    [[nodiscard]] bool finish() noexcept
    {
        if (finished) return true;
        bool closed = !evaluator || evaluator->close();
        if (!closed && release_dispatcher) {
            release_dispatcher->retry_detached_releases();
            closed = release_dispatcher->destruction_safe();
        }
        if (log_host) log_host->shutdown();
        finished = closed;
        return closed;
    }

    runtime_script::RuntimeScriptExecutionPlan plan;
    std::shared_ptr<const runtime_procedure::RuntimeProcedureActivation>
        activation;
    std::vector<std::shared_ptr<const runtime_procedure::CoDetectSupportBundle>>
        support_bundles;
    std::shared_ptr<ProcedureDispatcher> procedure_dispatcher;
    script_host::ResourceHostRuntime resource_host;
    std::optional<script_host::ProcedureHostRuntime> procedure_host;
    std::shared_ptr<script_runtime::QueuedLogHost> log_host;
    script_host::ComposedHostRuntime hosts;
    std::shared_ptr<script_runtime::HostReleaseDispatcher> release_dispatcher;
    std::unique_ptr<script_runtime::SynchronousEvaluator> evaluator;
    bool finished{};
};

class ProductionRuntime final : public RuntimeScriptTaskRuntime {
public:
    ProductionRuntime(
        RuntimeScriptTaskIdentity identity,
        ProductionRuntimeScriptTaskInputs inputs,
        std::shared_ptr<const runtime_resources::RuntimeResourceSnapshotActivation>
            resources,
        runtime_script::RuntimeScriptCatalog catalog,
        std::shared_ptr<const ExecutionCancellationProbe> cancellation,
        std::vector<TaskExecution> tasks)
        : identity_(std::move(identity)), inputs_(std::move(inputs)),
          resources_(std::move(resources)), catalog_(std::move(catalog)),
          cancellation_(std::move(cancellation)), tasks_(std::move(tasks))
    {
    }

    ~ProductionRuntime() override
    {
        for (auto& task : tasks_) (void)task.finish();
    }

    [[nodiscard]] const RuntimeScriptTaskIdentity& identity()
        const noexcept override
    {
        return identity_;
    }

    [[nodiscard]] RuntimeTaskTerminal execute(
        const RuntimeScriptTaskExecutionControl& control,
        const RuntimeTaskProgressReporter& report_progress) override
    {
        if (executed_.exchange(true)) return controlled_failure(control);
        for (std::size_t index{}; index < tasks_.size(); ++index) {
            if (const auto boundary = control_terminal(control)) return *boundary;
            if (!exact_device_identity(
                    *inputs_.config, inputs_.device, inputs_.device_identity))
                return controlled_failure(control);

            if (index != 0) {
                RuntimeTaskProgress progress;
                progress.is_flag_run = true;
                progress.current_task = identity_.requested_task_plan[index];
                progress.waiting_tasks.assign(
                    identity_.requested_task_plan.begin() + index + 1,
                    identity_.requested_task_plan.end());
                try {
                    (void)report_progress(std::move(progress));
                }
                catch (...) {
                    return controlled_failure(control);
                }
                if (const auto boundary = control_terminal(control)) return *boundary;
            }

            auto& task = tasks_[index];
            bool returned_true{};
            try {
                const auto result = task.evaluator->invoke_export(
                    task.plan.package().entry_module,
                    task.plan.task().entry_export);
                returned_true = result.value.inline_kind()
                        == script_runtime::ValueKind::Boolean
                    && result.value.as_boolean();
            }
            catch (...) {
                finish_task(task);
                return controlled_failure(control);
            }
            if (!finish_task(task)) return controlled_failure(control);
            if (!returned_true) return controlled_failure(control);
            if (const auto boundary = control_terminal(control)) return *boundary;
            if (!exact_device_identity(
                    *inputs_.config, inputs_.device, inputs_.device_identity))
                return controlled_failure(control);
        }
        return {false, 0};
    }

private:
    static bool finish_task(TaskExecution& task) noexcept
    {
        return task.finish();
    }

    RuntimeScriptTaskIdentity identity_;
    ProductionRuntimeScriptTaskInputs inputs_;
    std::shared_ptr<const runtime_resources::RuntimeResourceSnapshotActivation>
        resources_;
    runtime_script::RuntimeScriptCatalog catalog_;
    std::shared_ptr<const ExecutionCancellationProbe> cancellation_;
    std::vector<TaskExecution> tasks_;
    std::atomic<bool> executed_{};
};

class ProductionFactory final : public RuntimeScriptTaskRuntimeFactory {
public:
    ProductionFactory(
        std::shared_ptr<const ProductionRuntimeScriptTaskProvider> provider,
        ProductionRuntimeScriptTaskFactoryLimits limits)
        : provider_(std::move(provider)), limits_(std::move(limits)),
          coordinator_(script_host::PhysicalDeviceCoordinator::create(
              limits_.device_coordinator))
    {
    }

    [[nodiscard]] std::unique_ptr<RuntimeScriptTaskRuntime> create(
        const RuntimeTaskRequest& request,
        const std::span<const std::string> requested_task_plan,
        const RuntimeScriptTaskExecutionControl& control) const override
    {
        if (requested_task_plan.empty()
            || requested_task_plan.size() > limits_.max_tasks
            || control_terminal(control))
            return {};

        auto pinned = provider_->pin(request, requested_task_plan, control);
        if (!pinned || !valid_inputs(*pinned, request, limits_)
            || control_terminal(control))
            return {};

        const auto& config = *pinned->config;
        const auto& repositories = *pinned->repositories;
        const auto resources = runtime_resources::load_runtime_resource_snapshot(
            repositories.resources(), config.resource_selector,
            limits_.resources, control.stop_token());
        if (!resources || resources.activation->generation()
                != repositories.generation()
            || resources.activation->commit()
                != repositories.resources().commit()
            || control_terminal(control))
            return {};

        const auto catalog_result = runtime_script::load_runtime_script_catalog(
            repositories.scripts(),
            {repositories.generation(), repositories.scripts().commit()},
            limits_.catalog, control.stop_token());
        if (!catalog_result || control_terminal(control)) return {};
        auto catalog = *catalog_result.catalog;

        auto cancellation = std::make_shared<const ExecutionCancellationProbe>(
            control);
        RuntimeScriptTaskIdentity identity;
        identity.config_id = config.config_id;
        identity.config_snapshot_id = config.snapshot_id;
        identity.profile = std::string{
            runtime_procedure::co_detect_profile_name(config.profile)};
        identity.device_id = config.device_id;
        identity.runtime_generation = repositories.generation();
        identity.scripts_commit = repositories.scripts().commit();
        identity.resources_commit = repositories.resources().commit();
        identity.run_mode = request.run_mode;
        identity.requested_task_plan.assign(
            requested_task_plan.begin(), requested_task_plan.end());
        identity.canonical_task_plan.reserve(requested_task_plan.size());

        std::vector<TaskExecution> tasks;
        tasks.reserve(requested_task_plan.size());
        for (const auto& requested_task : requested_task_plan) {
            if (control_terminal(control)) return {};
            const auto resolution = catalog.resolve(
                request.run_mode, requested_task);
            if (!resolution) return {};
            const auto plan_result =
                runtime_script::build_runtime_script_execution_plan(
                    repositories.scripts(), *resolution,
                    pinned->trust_evidence.get(), limits_.execution_plan,
                    control.stop_token());
            if (!plan_result || control_terminal(control)) return {};
            auto plan = *plan_result.plan;
            identity.canonical_task_plan.push_back(plan.task().canonical_task);
            auto task = build_task(
                plan, *pinned, resources.activation, cancellation, control);
            if (!task) return {};
            tasks.push_back(std::move(*task));
        }

        if (!exact_device_identity(
                config, pinned->device, pinned->device_identity)
            || control_terminal(control))
            return {};
        return std::make_unique<ProductionRuntime>(
            std::move(identity), std::move(*pinned), resources.activation,
            std::move(catalog), std::move(cancellation), std::move(tasks));
    }

private:
    [[nodiscard]] std::optional<TaskExecution> build_task(
        runtime_script::RuntimeScriptExecutionPlan plan,
        const ProductionRuntimeScriptTaskInputs& inputs,
        std::shared_ptr<const runtime_resources::RuntimeResourceSnapshotActivation>
            resources,
        const std::shared_ptr<const ExecutionCancellationProbe>& cancellation,
        const RuntimeScriptTaskExecutionControl& control) const
    {
        const auto& config = *inputs.config;
        const auto& repositories = *inputs.repositories;
        auto resource_host = script_host::make_resource_host_runtime(
            resources->snapshot(), limits_.resource_host);
        std::vector<script_host::HostRuntimeContribution> contributions;
        contributions.reserve(3);
        contributions.push_back(script_host::make_host_runtime_contribution(
            resource_host.metadata, resource_host.bindings,
            resource_host.handles, {resource_host.host}));

        std::shared_ptr<const runtime_procedure::RuntimeProcedureActivation>
            activation;
        std::vector<std::shared_ptr<const runtime_procedure::CoDetectSupportBundle>>
            support_bundles;
        std::shared_ptr<ProcedureDispatcher> dispatcher;
        std::optional<script_host::ProcedureHostRuntime> procedure_host;
        if (!plan.procedure_ids().empty()) {
            const auto activated =
                runtime_procedure::load_runtime_procedure_activation(
                    repositories.scripts(), plan, resources,
                    limits_.procedures, control.stop_token());
            if (!activated || activated.activation->generation()
                    != repositories.generation()
                || activated.activation->scripts_commit()
                    != repositories.scripts().commit()
                || activated.activation->resources_commit()
                    != repositories.resources().commit())
                return std::nullopt;
            activation = activated.activation;

            std::vector<ProcedureDispatcher::Entry> executors;
            executors.reserve(plan.procedure_ids().size());
            support_bundles.reserve(plan.procedure_ids().size());
            for (const auto& procedure_id : plan.procedure_ids()) {
                if (control_terminal(control)) return std::nullopt;
                const auto definition = activation->resolve_definition(procedure_id);
                if (!definition) return std::nullopt;
                std::shared_ptr<script_host::ProcedureExecutor> executor;
                if (definition->engine()
                    == runtime_procedure::co_detect_python_compat_engine) {
                    const auto support = find_support(config, procedure_id);
                    if (!support) return std::nullopt;
                    const auto loaded =
                        runtime_procedure::load_co_detect_support_bundle(
                            resources, repositories.generation(),
                            support->resource_id, config.locale, config.profile,
                            limits_.support_bundle, control.stop_token());
                    if (!loaded || loaded.bundle->generation()
                            != repositories.generation()
                        || loaded.bundle->commit()
                            != repositories.resources().commit())
                        return std::nullopt;
                    executor = runtime_procedure::
                        make_activated_co_detect_production_executor(
                            activation, procedure_id, inputs.device,
                            inputs.device_identity, loaded.bundle,
                            limits_.device_adapter);
                    support_bundles.push_back(loaded.bundle);
                }
                else if (inputs.extensions) {
                    executor = inputs.extensions->make_procedure_executor(
                        activation, procedure_id, control);
                }
                if (!executor) return std::nullopt;
                executors.emplace_back(procedure_id, std::move(executor));
            }
            dispatcher = std::make_shared<ProcedureDispatcher>(
                std::move(executors));
            procedure_host.emplace(script_host::make_procedure_host_runtime(
                activation->snapshot(), config.device_id, dispatcher,
                coordinator_, limits_.procedure_host));
            contributions.push_back(script_host::make_host_runtime_contribution(
                procedure_host->metadata, procedure_host->bindings, {},
                {procedure_host->host, dispatcher}));
        }

        auto log_host = std::make_shared<script_runtime::QueuedLogHost>(
            inputs.log_sink,
            script_runtime::LogHostIdentity{
                plan.task().canonical_task, config.snapshot_id,
                config.config_id},
            config.log_secrets, limits_.log_host);
        contributions.push_back(make_log_contribution(log_host));
        if (inputs.extensions) {
            auto extras = inputs.extensions->make_host_contributions(
                plan, cancellation);
            for (auto& extra : extras)
                contributions.push_back(std::move(extra));
        }
        auto hosts = script_host::compose_host_runtime(
            std::move(contributions), limits_.host_composition);
        auto host_options = hosts.options();
        host_options.permissions.declared_modules = declared_modules(plan.task());
        const auto capabilities = plan.capabilities();
        host_options.permissions.declared_capabilities.assign(
            capabilities.begin(), capabilities.end());
        host_options.permissions.policy_capabilities = config.policy_capabilities;
        host_options.permissions.platform_capabilities =
            config.platform_capabilities;
        host_options.permissions.task_capabilities.assign(
            capabilities.begin(), capabilities.end());
        host_options.cancellation = cancellation;
        auto release_dispatcher = host_options.handles;
        const auto& package_modules = plan.package().modules;
        std::vector<script_runtime::SourceModule> modules{
            package_modules.begin(), package_modules.end()};
        auto evaluator = std::make_unique<script_runtime::SynchronousEvaluator>(
            std::move(modules), std::move(host_options), limits_.evaluator,
            limits_.heap);
        return TaskExecution{
            std::move(plan), std::move(activation),
            std::move(support_bundles), std::move(dispatcher),
            std::move(resource_host), std::move(procedure_host),
            std::move(log_host), std::move(hosts),
            std::move(release_dispatcher), std::move(evaluator)};
    }

    std::shared_ptr<const ProductionRuntimeScriptTaskProvider> provider_;
    ProductionRuntimeScriptTaskFactoryLimits limits_;
    std::shared_ptr<script_host::PhysicalDeviceCoordinator> coordinator_;
};

} // namespace

std::shared_ptr<const RuntimeScriptTaskRuntimeFactory>
make_production_runtime_script_task_factory(
    std::shared_ptr<const ProductionRuntimeScriptTaskProvider> provider,
    ProductionRuntimeScriptTaskFactoryLimits limits)
{
    if (!provider)
        throw std::invalid_argument{"production runtime provider is absent"};
    if (!valid_limits(limits))
        throw std::invalid_argument{"production runtime factory limits are invalid"};
    return std::make_shared<const ProductionFactory>(
        std::move(provider), std::move(limits));
}

} // namespace baas::service::runtime
