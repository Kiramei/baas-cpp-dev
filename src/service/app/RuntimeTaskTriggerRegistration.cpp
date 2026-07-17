#include "service/app/RuntimeTaskTriggerRegistration.h"

#include "service/adapters/BoundedJson.h"
#include "service/protocol/TriggerEnvelope.h"

#include <algorithm>
#include <limits>
#include <new>
#include <optional>
#include <utility>

namespace baas::service::app {
namespace {

constexpr std::string_view cancelled_error = "cancelled";
constexpr std::string_view missing_config_error = "config_id is required";
constexpr std::string_view missing_task_error =
    "task is required for solve command";
constexpr std::string_view invalid_payload_error =
    "invalid_runtime_task_payload";
constexpr std::string_view payload_capacity_error =
    "runtime_task_payload_capacity";
constexpr std::string_view control_exception_error =
    "runtime_task_control_exception";
constexpr std::string_view invalid_result_error =
    "runtime_task_result_invalid_json";
constexpr std::string_view result_capacity_error =
    "runtime_task_result_capacity";
constexpr std::string_view response_rejected_error =
    "runtime_task_response_rejected";

enum class JsonValidation : std::uint8_t { valid, invalid, capacity };

[[nodiscard]] bool valid_limits(const RuntimeTaskTriggerLimits& limits) noexcept
{
    return limits.max_payload_bytes >= 2
        && limits.max_payload_bytes <= runtime_task_trigger_hard_max_json_bytes
        && limits.max_payload_depth != 0
        && limits.max_payload_depth <= runtime_task_trigger_hard_max_json_depth
        && limits.max_payload_nodes != 0
        && limits.max_payload_nodes <= runtime_task_trigger_hard_max_json_nodes
        && limits.max_task_bytes != 0
        && limits.max_task_bytes <= runtime_task_trigger_hard_max_json_bytes
        && limits.max_result_json_bytes >= 2
        && limits.max_result_json_bytes
            <= runtime_task_trigger_hard_max_json_bytes
        && limits.max_result_json_depth != 0
        && limits.max_result_json_depth
            <= runtime_task_trigger_hard_max_json_depth
        && limits.max_result_json_nodes != 0
        && limits.max_result_json_nodes
            <= runtime_task_trigger_hard_max_json_nodes;
}

[[nodiscard]] std::size_t saturating_add(
    const std::size_t left, const std::size_t right) noexcept
{
    return right > std::numeric_limits<std::size_t>::max() - left
        ? std::numeric_limits<std::size_t>::max() : left + right;
}

[[nodiscard]] std::size_t saturating_multiply(
    const std::size_t value, const std::size_t factor) noexcept
{
    return value != 0 && factor > std::numeric_limits<std::size_t>::max() / value
        ? std::numeric_limits<std::size_t>::max() : value * factor;
}

[[nodiscard]] bool starts_with_object(const std::string_view value) noexcept
{
    const auto first = std::find_if_not(
        value.begin(), value.end(), [](const char byte) {
            return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
        });
    return first != value.end() && *first == '{';
}

[[nodiscard]] JsonValidation validate_result_json(
    const std::string_view value, const RuntimeTaskTriggerLimits& limits)
{
    if (value.size() > limits.max_result_json_bytes)
        return JsonValidation::capacity;
    if (!starts_with_object(value)) return JsonValidation::invalid;

    protocol::trigger::TriggerEnvelopeLimits envelope_limits;
    envelope_limits.max_input_json_bytes = limits.max_result_json_bytes;
    envelope_limits.max_output_json_bytes =
        saturating_add(limits.max_result_json_bytes, 512);
    envelope_limits.max_depth = limits.max_result_json_depth;
    envelope_limits.max_nodes = limits.max_result_json_nodes;
    envelope_limits.max_string_bytes = limits.max_result_json_bytes;
    envelope_limits.max_work = saturating_add(
        saturating_multiply(limits.max_result_json_bytes, 4), 1'024);

    protocol::trigger::CommandResponse probe;
    probe.command = "solve";
    probe.data_json = std::string{value};
    const auto encoded = protocol::trigger::encode_command_response(
        std::move(probe), envelope_limits);
    if (encoded) return JsonValidation::valid;

    using enum protocol::trigger::EnvelopeError;
    switch (encoded.error) {
        case input_too_large:
        case output_too_large:
        case depth_limit:
        case node_limit:
        case string_limit:
        case work_limit:
            return JsonValidation::capacity;
        default: return JsonValidation::invalid;
    }
}

struct ParsedTask {
    std::string task;
    JsonValidation validation{JsonValidation::invalid};
    bool missing{};
};

[[nodiscard]] ParsedTask parse_solve_task(
    const std::string_view payload, const RuntimeTaskTriggerLimits& limits)
{
    if (payload.size() > limits.max_payload_bytes)
        return {{}, JsonValidation::capacity, false};
    try {
        const auto root = adapters::bounded_json::parse_json(
            payload,
            {limits.max_payload_bytes,
             limits.max_payload_depth,
             limits.max_payload_nodes});
        if (!root || !root->is_object()) return {};
        const auto found = root->find("task");
        if (found == root->end() || !found->is_string() || found->empty()) {
            return {{}, JsonValidation::invalid, true};
        }
        auto task = found->get<std::string>();
        if (task.size() > limits.max_task_bytes)
            return {{}, JsonValidation::capacity, false};
        return {std::move(task), JsonValidation::valid, false};
    } catch (const std::bad_alloc&) {
        return {{}, JsonValidation::capacity, false};
    } catch (...) {
        return {};
    }
}

void stage_error(trigger::TriggerResponseSink& sink, const std::string_view error)
{
    static_cast<void>(sink.error(std::string{error}));
}

void stage_cancelled(trigger::TriggerResponseSink& sink)
{
    static_cast<void>(sink.cancelled(std::string{cancelled_error}));
}

[[nodiscard]] std::string_view wire_error(
    const RuntimeTaskControlError error) noexcept
{
    using enum RuntimeTaskControlError;
    switch (error) {
        case none: return "none";
        case invalid_config_id: return "runtime_task_invalid_config_id";
        case invalid_task: return "runtime_task_invalid_task";
        case conflict: return "runtime_task_conflict";
        case capacity: return "runtime_task_control_capacity";
        case unavailable: return "runtime_task_control_unavailable";
        case internal_error: return "runtime_task_internal_error";
    }
    return "runtime_task_internal_error";
}

template <typename Invoke>
void invoke_control(
    trigger::TriggerResponseSink& sink,
    const RuntimeTaskTriggerLimits& limits,
    Invoke&& invoke)
{
    RuntimeTaskControlResult result;
    try {
        result = std::forward<Invoke>(invoke)();
    } catch (...) {
        stage_error(sink, control_exception_error);
        return;
    }
    if (!result) {
        stage_error(sink, wire_error(result.error));
        return;
    }

    JsonValidation validation{JsonValidation::invalid};
    try {
        validation = validate_result_json(result.data_json, limits);
    } catch (const std::bad_alloc&) {
        validation = JsonValidation::capacity;
    } catch (...) {
        validation = JsonValidation::invalid;
    }
    if (validation == JsonValidation::capacity) {
        stage_error(sink, result_capacity_error);
        return;
    }
    if (validation != JsonValidation::valid) {
        stage_error(sink, invalid_result_error);
        return;
    }
    if (!sink.success(std::move(result.data_json))) {
        stage_error(sink, response_rejected_error);
    }
}

[[nodiscard]] std::optional<std::string_view> required_config_id(
    const trigger::AdmittedTriggerRequest& request,
    trigger::TriggerResponseSink& sink)
{
    // TriggerIngress enforces the catalog requirement. Keep this second check
    // at the business boundary so future transports cannot bypass it.
    if (!request.config_id() || request.config_id()->empty()) {
        stage_error(sink, missing_config_error);
        return std::nullopt;
    }
    return *request.config_id();
}

}  // namespace

std::string_view runtime_task_control_error_name(
    const RuntimeTaskControlError error) noexcept
{
    using enum RuntimeTaskControlError;
    switch (error) {
        case none: return "none";
        case invalid_config_id: return "invalid_config_id";
        case invalid_task: return "invalid_task";
        case conflict: return "conflict";
        case capacity: return "capacity";
        case unavailable: return "unavailable";
        case internal_error: return "internal_error";
    }
    return "unknown";
}

std::string_view runtime_task_trigger_registration_error_name(
    const RuntimeTaskTriggerRegistrationError error) noexcept
{
    using enum RuntimeTaskTriggerRegistrationError;
    switch (error) {
        case none: return "none";
        case missing_control: return "missing_control";
        case invalid_limits: return "invalid_limits";
        case resource_exhausted: return "resource_exhausted";
    }
    return "unknown";
}

RuntimeTaskTriggerRegistrationResult make_runtime_task_trigger_registrations(
    std::shared_ptr<RuntimeTaskControl> control,
    const RuntimeTaskTriggerLimits limits) noexcept
{
    if (!control) {
        return {{}, RuntimeTaskTriggerRegistrationError::missing_control};
    }
    if (!valid_limits(limits)) {
        return {{}, RuntimeTaskTriggerRegistrationError::invalid_limits};
    }
    try {
        std::vector<trigger::TriggerHandlerRegistration> registrations;
        registrations.reserve(5);
        registrations.push_back({
            "start_scheduler",
            [control, limits](const trigger::AdmittedTriggerRequest& request,
                              trigger::TriggerResponseSink& sink,
                              const std::stop_token stop) {
                if (stop.stop_requested()) return stage_cancelled(sink);
                const auto config_id = required_config_id(request, sink);
                if (!config_id) return;
                invoke_control(sink, limits, [&] {
                    return control->start_scheduler(*config_id);
                });
            },
        });
        registrations.push_back({
            "stop_scheduler",
            [control, limits](const trigger::AdmittedTriggerRequest& request,
                              trigger::TriggerResponseSink& sink,
                              const std::stop_token stop) {
                if (stop.stop_requested()) return stage_cancelled(sink);
                const auto config_id = required_config_id(request, sink);
                if (!config_id) return;
                invoke_control(sink, limits, [&] {
                    return control->stop_scheduler(*config_id);
                });
            },
        });
        registrations.push_back({
            "solve",
            [control, limits](const trigger::AdmittedTriggerRequest& request,
                              trigger::TriggerResponseSink& sink,
                              const std::stop_token stop) {
                if (stop.stop_requested()) return stage_cancelled(sink);
                const auto config_id = required_config_id(request, sink);
                if (!config_id) return;
                const auto parsed = parse_solve_task(request.payload_json(), limits);
                if (parsed.validation == JsonValidation::capacity) {
                    return stage_error(sink, payload_capacity_error);
                }
                if (parsed.missing) return stage_error(sink, missing_task_error);
                if (parsed.validation != JsonValidation::valid) {
                    return stage_error(sink, invalid_payload_error);
                }
                invoke_control(sink, limits, [&] {
                    return control->start_task(*config_id, parsed.task);
                });
            },
        });
        registrations.push_back({
            "start_*",
            [control, limits](const trigger::AdmittedTriggerRequest& request,
                              trigger::TriggerResponseSink& sink,
                              const std::stop_token stop) {
                if (stop.stop_requested()) return stage_cancelled(sink);
                const auto config_id = required_config_id(request, sink);
                if (!config_id) return;
                if (request.command().size() > limits.max_task_bytes) {
                    return stage_error(sink, payload_capacity_error);
                }
                // Preserve the original legacy alias. RuntimeTaskControl owns
                // normalization because it also owns the runtime task catalog.
                invoke_control(sink, limits, [&] {
                    return control->start_task(*config_id, request.command());
                });
            },
        });
        registrations.push_back({
            "stop_all_tasks",
            [control, limits](const trigger::AdmittedTriggerRequest&,
                              trigger::TriggerResponseSink& sink,
                              const std::stop_token stop) {
                if (stop.stop_requested()) return stage_cancelled(sink);
                invoke_control(sink, limits, [&] {
                    return control->stop_all_tasks();
                });
            },
        });
        return {std::move(registrations),
                RuntimeTaskTriggerRegistrationError::none};
    } catch (...) {
        return {{}, RuntimeTaskTriggerRegistrationError::resource_exhausted};
    }
}

}  // namespace baas::service::app
