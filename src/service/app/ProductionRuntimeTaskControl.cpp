#include "service/app/ProductionRuntimeTaskControl.h"

#include <nlohmann/json.hpp>

#include <array>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace baas::service::app {
namespace {

using OrderedJson = nlohmann::ordered_json;

class NoopPreparedOperation final : public RuntimeTaskPreparedOperation {
public:
    void commit() noexcept override {}
};

class StartPreparedOperation final : public RuntimeTaskPreparedOperation {
public:
    explicit StartPreparedOperation(runtime::RuntimeTaskStartReservation value)
        : value_(std::move(value))
    {
    }

    void commit() noexcept override { value_.commit(); }

private:
    runtime::RuntimeTaskStartReservation value_;
};

class StopPreparedOperation final : public RuntimeTaskPreparedOperation {
public:
    explicit StopPreparedOperation(runtime::RuntimeTaskStopReservation value)
        : value_(std::move(value))
    {
    }

    void commit() noexcept override { value_.commit(); }

private:
    runtime::RuntimeTaskStopReservation value_;
};

class StopAllPreparedOperation final : public RuntimeTaskPreparedOperation {
public:
    explicit StopAllPreparedOperation(
        runtime::RuntimeTaskStopAllReservation value)
        : value_(std::move(value))
    {
    }

    void commit() noexcept override { value_.commit(); }

private:
    runtime::RuntimeTaskStopAllReservation value_;
};

[[nodiscard]] RuntimeTaskPrepareResult failure(
    const RuntimeTaskControlError error) noexcept
{
    RuntimeTaskPrepareResult result;
    result.error = error;
    return result;
}

[[nodiscard]] RuntimeTaskPrepareResult prepared_noop(OrderedJson data)
{
    return {data.dump(), std::make_unique<NoopPreparedOperation>()};
}

[[nodiscard]] bool bounded_nonempty(
    const std::string_view value) noexcept
{
    return !value.empty() && value.find('\0') == std::string_view::npos;
}

[[nodiscard]] std::string normalize_task(const std::string_view task)
{
    static constexpr std::array aliases{
        std::pair{"start_hard_task", "explore_hard_task"},
        std::pair{"start_normal_task", "explore_normal_task"},
        std::pair{"start_fhx", "de_clothes"},
        std::pair{"start_main_story", "main_story"},
        std::pair{"start_group_story", "group_story"},
        std::pair{"start_mini_story", "mini_story"},
        std::pair{"start_explore_activity_story", "explore_activity_story"},
        std::pair{"start_explore_activity_mission", "explore_activity_mission"},
        std::pair{"start_explore_activity_challenge", "explore_activity_challenge"},
    };
    for (const auto& [legacy, canonical] : aliases) {
        if (task == legacy) return canonical;
    }
    return std::string{task};
}

[[nodiscard]] RuntimeTaskControlError start_error(
    const runtime::RuntimeTaskStartDecision decision) noexcept
{
    using enum runtime::RuntimeTaskStartDecision;
    switch (decision) {
        case capacity_exceeded: return RuntimeTaskControlError::capacity;
        case invalid_request: return RuntimeTaskControlError::invalid_task;
        case reservation_conflict: return RuntimeTaskControlError::conflict;
        case owner_stopped: return RuntimeTaskControlError::unavailable;
        case thread_start_failed: return RuntimeTaskControlError::internal_error;
        case preparation_failed: return RuntimeTaskControlError::internal_error;
        case started:
        case already_running:
            return RuntimeTaskControlError::none;
        case stopping:
            return RuntimeTaskControlError::conflict;
    }
    return RuntimeTaskControlError::internal_error;
}

[[nodiscard]] RuntimeTaskControlError prepare_error(
    const runtime::RuntimeScriptTaskPrepareError error) noexcept
{
    using enum runtime::RuntimeScriptTaskPrepareError;
    switch (error) {
        case none: return RuntimeTaskControlError::none;
        case cancelled: return RuntimeTaskControlError::cancelled;
        case deadline: return RuntimeTaskControlError::deadline;
        case invalid_request:
        case invalid_identity: return RuntimeTaskControlError::invalid_task;
        case repository_mismatch:
            return RuntimeTaskControlError::repository_mismatch;
        case capacity: return RuntimeTaskControlError::capacity;
        case unavailable: return RuntimeTaskControlError::unavailable;
        case internal_error: return RuntimeTaskControlError::internal_error;
    }
    return RuntimeTaskControlError::internal_error;
}

[[nodiscard]] RuntimeTaskControlError stop_error(
    const runtime::RuntimeTaskStopDecision decision) noexcept
{
    using enum runtime::RuntimeTaskStopDecision;
    switch (decision) {
        case capacity_exceeded: return RuntimeTaskControlError::capacity;
        case reservation_conflict: return RuntimeTaskControlError::conflict;
        case owner_stopped: return RuntimeTaskControlError::unavailable;
        case stop_requested:
        case already_stopping:
        case already_stopped:
        case unknown_config:
            return RuntimeTaskControlError::none;
    }
    return RuntimeTaskControlError::internal_error;
}

[[nodiscard]] RuntimeTaskControlError stop_all_error(
    const runtime::RuntimeTaskStopAllDecision decision) noexcept
{
    using enum runtime::RuntimeTaskStopAllDecision;
    switch (decision) {
        case reservation_conflict: return RuntimeTaskControlError::conflict;
        case owner_stopped: return RuntimeTaskControlError::unavailable;
        case stop_requested:
        case nothing_to_stop:
            return RuntimeTaskControlError::none;
    }
    return RuntimeTaskControlError::internal_error;
}

}  // namespace

ProductionRuntimeTaskControl::ProductionRuntimeTaskControl(
    std::shared_ptr<runtime::RuntimeTaskOwner> owner,
    std::shared_ptr<const runtime::RuntimeScriptTaskRuntimeFactory> factory,
    runtime::RuntimeScriptTaskRepositoryBinding repository,
    const runtime::RuntimeScriptTaskBackendOptions options)
    : owner_(std::move(owner)), factory_(std::move(factory)),
      repository_(std::move(repository)), options_(options)
{
    if (!owner_ || !factory_ || repository_.generation.empty()
        || repository_.scripts_commit.empty()
        || repository_.resources_commit.empty()) {
        throw std::invalid_argument{
            "production runtime task control requires an owner, factory and repository"};
    }
}

ProductionRuntimeTaskControl::~ProductionRuntimeTaskControl() = default;

RuntimeTaskPrepareResult
ProductionRuntimeTaskControl::prepare_start_scheduler(
    const std::string_view config_id)
{
    if (!bounded_nonempty(config_id)) {
        return failure(RuntimeTaskControlError::invalid_config_id);
    }
    // No scheduler task plan is present in the production v2 script catalog.
    // Keep the descriptor registered for wire compatibility while refusing to
    // claim that one-shot procedures are a scheduler.
    return failure(RuntimeTaskControlError::unavailable);
}

RuntimeTaskPrepareResult
ProductionRuntimeTaskControl::prepare_stop_scheduler(
    const std::string_view config_id)
{
    if (!bounded_nonempty(config_id)) {
        return failure(RuntimeTaskControlError::invalid_config_id);
    }
    try {
        auto stopped = owner_->prepare_stop(config_id);
        if (const auto error = stop_error(stopped.decision);
            error != RuntimeTaskControlError::none) {
            return failure(error);
        }
        const bool unknown = stopped.decision
            == runtime::RuntimeTaskStopDecision::unknown_config;
        OrderedJson data = OrderedJson::object();
        data["status"] = unknown ? "unknown-config" : "stopped";
        data["config_id"] = config_id;
        return {
            data.dump(),
            std::make_unique<StopPreparedOperation>(
                std::move(stopped.reservation))};
    } catch (const std::bad_alloc&) {
        return failure(RuntimeTaskControlError::capacity);
    } catch (...) {
        return failure(RuntimeTaskControlError::internal_error);
    }
}

RuntimeTaskPrepareResult
ProductionRuntimeTaskControl::prepare_start_task(
    const std::string_view config_id,
    const std::string_view requested_task,
    const std::stop_token stop_token)
{
    if (!bounded_nonempty(config_id)) {
        return failure(RuntimeTaskControlError::invalid_config_id);
    }
    if (!bounded_nonempty(requested_task)) {
        return failure(RuntimeTaskControlError::invalid_task);
    }
    try {
        if (const auto existing = owner_->snapshot(config_id); existing) {
            if (existing->stopping) {
                return failure(RuntimeTaskControlError::conflict);
            }
            if (existing->running) {
                OrderedJson data = OrderedJson::object();
                data["status"] = "already-running";
                data["config_id"] = config_id;
                return prepared_noop(std::move(data));
            }
        }
        auto task = normalize_task(requested_task);
        runtime::RuntimeTaskRequest request;
        request.config_id = std::string{config_id};
        request.run_mode = "solve";
        request.current_task = task;
        auto prepared_backend = runtime::prepare_runtime_script_task_backend(
            factory_, request, repository_, options_, stop_token);
        if (!prepared_backend) {
            return failure(prepare_error(prepared_backend.error));
        }
        auto preparation_status = prepared_backend.preparation_status;
        request.prepared_backend = std::move(prepared_backend.backend);
        auto started = owner_->prepare_start(std::move(request));
        if (started.decision
            == runtime::RuntimeTaskStartDecision::preparation_failed) {
            const auto error = preparation_status
                ? preparation_status->load(std::memory_order_acquire)
                : runtime::RuntimeScriptTaskPrepareError::internal_error;
            return failure(prepare_error(error));
        }
        if (const auto error = start_error(started.decision);
            error != RuntimeTaskControlError::none) {
            return failure(error);
        }
        if (started.decision == runtime::RuntimeTaskStartDecision::already_running) {
            OrderedJson data = OrderedJson::object();
            data["status"] = "already-running";
            data["config_id"] = config_id;
            return prepared_noop(std::move(data));
        }
        if (!started) {
            return failure(RuntimeTaskControlError::internal_error);
        }
        OrderedJson data = OrderedJson::object();
        data["status"] = "ok";
        data["task"] = task;
        data["result"] = 0;
        return {
            data.dump(),
            std::make_unique<StartPreparedOperation>(
                std::move(started.reservation))};
    } catch (const std::bad_alloc&) {
        return failure(RuntimeTaskControlError::capacity);
    } catch (...) {
        return failure(RuntimeTaskControlError::internal_error);
    }
}

RuntimeTaskPrepareResult
ProductionRuntimeTaskControl::prepare_stop_all_tasks()
{
    try {
        auto stopped = owner_->prepare_stop_all();
        if (const auto error = stop_all_error(stopped.decision);
            error != RuntimeTaskControlError::none) {
            return failure(error);
        }
        OrderedJson results = OrderedJson::array();
        for (const auto& snapshot : stopped.snapshots) {
            OrderedJson item = OrderedJson::object();
            item["status"] = "stopped";
            item["config_id"] = snapshot.config_id;
            results.push_back(std::move(item));
        }
        OrderedJson data = OrderedJson::object();
        data["status"] = "stopped";
        data["results"] = std::move(results);
        return {
            data.dump(),
            std::make_unique<StopAllPreparedOperation>(
                std::move(stopped.reservation))};
    } catch (const std::bad_alloc&) {
        return failure(RuntimeTaskControlError::capacity);
    } catch (...) {
        return failure(RuntimeTaskControlError::internal_error);
    }
}

class ProductionRuntimeTaskCompositionFactory final
    : public ServiceRuntimeTaskCompositionFactory {
public:
    ProductionRuntimeTaskCompositionFactory(
        std::shared_ptr<const runtime::ProductionRuntimeScriptTaskProvider> provider,
        const runtime::RuntimeScriptTaskBackendOptions backend_options,
        const runtime::RuntimeTaskLimits owner_limits)
        : provider_(std::move(provider)), backend_options_(backend_options),
          owner_limits_(owner_limits)
    {
        if (!provider_) {
            throw std::invalid_argument{"production runtime provider is required"};
        }
    }

    ServiceRuntimeTaskComposition compose(
        std::shared_ptr<const
            ::baas::runtime::repository::RuntimeRepositoryReadBundle> bundle) override
    {
        if (!bundle) return {};
        auto factory = runtime::make_production_runtime_script_task_factory(provider_);
        runtime::RuntimeTaskBackend fallback = [](
            const runtime::RuntimeTaskRequest&, std::stop_token,
            const runtime::RuntimeTaskProgressReporter&) noexcept {
            return runtime::RuntimeTaskTerminal{
                false, runtime::runtime_script_task_failure_exit_code};
        };
        auto owner = std::make_shared<runtime::RuntimeTaskOwner>(
            std::move(fallback), owner_limits_);
        runtime::RuntimeScriptTaskRepositoryBinding repository{
            bundle->generation(), bundle->scripts().commit(),
            bundle->resources().commit()};
        auto control = std::make_shared<ProductionRuntimeTaskControl>(
            owner, std::move(factory), std::move(repository), backend_options_);
        return {std::move(owner), std::move(control)};
    }

private:
    std::shared_ptr<const runtime::ProductionRuntimeScriptTaskProvider> provider_;
    runtime::RuntimeScriptTaskBackendOptions backend_options_;
    runtime::RuntimeTaskLimits owner_limits_;
};

std::shared_ptr<ServiceRuntimeTaskCompositionFactory>
make_production_runtime_task_composition_factory(
    std::shared_ptr<const runtime::ProductionRuntimeScriptTaskProvider> provider,
    const runtime::RuntimeScriptTaskBackendOptions backend_options,
    const runtime::RuntimeTaskLimits owner_limits)
{
    return std::make_shared<ProductionRuntimeTaskCompositionFactory>(
        std::move(provider), backend_options, owner_limits);
}

}  // namespace baas::service::app
