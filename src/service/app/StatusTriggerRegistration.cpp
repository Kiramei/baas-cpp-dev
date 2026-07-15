#include "service/app/StatusTriggerRegistration.h"

#include "service/protocol/TriggerEnvelope.h"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace baas::service::app {
namespace {

constexpr std::string_view cancelled_error = "cancelled";
constexpr std::string_view source_capacity_error = "status_source_capacity";
constexpr std::string_view source_unavailable_error = "status_source_unavailable";
constexpr std::string_view source_exception_error = "status_source_exception";
constexpr std::string_view json_capacity_error = "status_json_capacity";
constexpr std::string_view invalid_json_error = "status_invalid_json";
constexpr std::string_view response_rejected_error = "status_response_rejected";

enum class StatusValidation : std::uint8_t { valid, invalid, capacity };

[[nodiscard]] bool valid_limits(const StatusTriggerLimits& limits) noexcept
{
    return limits.max_json_bytes >= 2
        && limits.max_json_bytes <= status_trigger_hard_max_json_bytes
        && limits.max_json_depth != 0
        && limits.max_json_depth <= status_trigger_hard_max_json_depth
        && limits.max_json_nodes != 0
        && limits.max_json_nodes <= status_trigger_hard_max_json_nodes;
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

[[nodiscard]] bool root_starts_with_object(const std::string_view value) noexcept
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](const char byte) {
        return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
    });
    return first != value.end() && *first == '{';
}

[[nodiscard]] StatusValidation validate_status_json(
    const std::string_view value, const StatusTriggerLimits& limits)
{
    if (value.size() > limits.max_json_bytes) return StatusValidation::capacity;
    if (!root_starts_with_object(value)) return StatusValidation::invalid;

    protocol::trigger::TriggerEnvelopeLimits envelope_limits;
    envelope_limits.max_output_json_bytes =
        saturating_add(limits.max_json_bytes, 256);
    envelope_limits.max_depth = limits.max_json_depth;
    envelope_limits.max_nodes = limits.max_json_nodes;
    envelope_limits.max_string_bytes = limits.max_json_bytes;
    envelope_limits.max_work = saturating_add(
        saturating_multiply(limits.max_json_bytes, 4), 1'024);

    protocol::trigger::CommandResponse probe;
    probe.command = "status";
    probe.data_json = std::string{value};
    const auto encoded = protocol::trigger::encode_command_response(
        std::move(probe), envelope_limits);
    if (encoded) return StatusValidation::valid;
    using enum protocol::trigger::EnvelopeError;
    switch (encoded.error) {
        case input_too_large:
        case output_too_large:
        case depth_limit:
        case node_limit:
        case string_limit:
        case work_limit:
            return StatusValidation::capacity;
        default:
            return StatusValidation::invalid;
    }
}

void stage_error(trigger::TriggerResponseSink& sink, const std::string_view message)
{
    static_cast<void>(sink.error(std::string{message}));
}

void stage_cancelled(trigger::TriggerResponseSink& sink)
{
    static_cast<void>(sink.cancelled(std::string{cancelled_error}));
}

class CallbackStatusSource final : public StatusSource {
public:
    explicit CallbackStatusSource(StatusSourceCallback callback)
        : callback_(std::move(callback))
    {}

    [[nodiscard]] StatusSourceResult current_status(
        const std::stop_token stop) override
    {
        return callback_(stop);
    }

private:
    StatusSourceCallback callback_;
};

[[nodiscard]] trigger::TriggerHandler make_status_handler(
    std::shared_ptr<StatusSource> source, const StatusTriggerLimits limits)
{
    return [source = std::move(source), limits](
               const trigger::AdmittedTriggerRequest&,
               trigger::TriggerResponseSink& sink,
               const std::stop_token stop) {
        if (stop.stop_requested()) {
            stage_cancelled(sink);
            return;
        }

        StatusSourceResult snapshot;
        try {
            snapshot = source->current_status(stop);
        } catch (...) {
            stage_error(sink, source_exception_error);
            return;
        }

        if (stop.stop_requested() || snapshot.error == StatusSourceError::cancelled) {
            stage_cancelled(sink);
            return;
        }
        switch (snapshot.error) {
            case StatusSourceError::none: break;
            case StatusSourceError::capacity:
                stage_error(sink, source_capacity_error);
                return;
            case StatusSourceError::unavailable:
                stage_error(sink, source_unavailable_error);
                return;
            case StatusSourceError::cancelled:
                stage_cancelled(sink);
                return;
            default:
                stage_error(sink, source_unavailable_error);
                return;
        }

        StatusValidation validation{StatusValidation::invalid};
        try {
            validation = validate_status_json(snapshot.data_json, limits);
        } catch (const std::bad_alloc&) {
            validation = StatusValidation::capacity;
        } catch (...) {
            validation = StatusValidation::invalid;
        }
        if (stop.stop_requested()) {
            stage_cancelled(sink);
            return;
        }
        if (validation == StatusValidation::capacity) {
            stage_error(sink, json_capacity_error);
            return;
        }
        if (validation != StatusValidation::valid) {
            stage_error(sink, invalid_json_error);
            return;
        }

        const auto staged = sink.success(std::move(snapshot.data_json));
        if (!staged) stage_error(sink, response_rejected_error);
    };
}

}  // namespace

std::string_view status_source_error_name(const StatusSourceError error) noexcept
{
    using enum StatusSourceError;
    switch (error) {
        case none: return "none";
        case cancelled: return "cancelled";
        case capacity: return "capacity";
        case unavailable: return "unavailable";
    }
    return "unknown";
}

std::string_view status_trigger_registration_error_name(
    const StatusTriggerRegistrationError error) noexcept
{
    using enum StatusTriggerRegistrationError;
    switch (error) {
        case none: return "none";
        case missing_source: return "missing_source";
        case empty_callback: return "empty_callback";
        case invalid_limits: return "invalid_limits";
        case resource_exhausted: return "resource_exhausted";
    }
    return "unknown";
}

StatusTriggerRegistrationResult make_status_trigger_registration(
    std::shared_ptr<StatusSource> source, const StatusTriggerLimits limits) noexcept
{
    if (!source) return {std::nullopt, StatusTriggerRegistrationError::missing_source};
    if (!valid_limits(limits)) {
        return {std::nullopt, StatusTriggerRegistrationError::invalid_limits};
    }
    try {
        trigger::TriggerHandlerRegistration registration;
        registration.descriptor_name = "status";
        registration.handler = make_status_handler(std::move(source), limits);
        return {std::move(registration), StatusTriggerRegistrationError::none};
    } catch (...) {
        return {std::nullopt, StatusTriggerRegistrationError::resource_exhausted};
    }
}

StatusTriggerRegistrationResult make_status_trigger_registration(
    StatusSourceCallback callback, const StatusTriggerLimits limits) noexcept
{
    if (!callback) {
        return {std::nullopt, StatusTriggerRegistrationError::empty_callback};
    }
    try {
        return make_status_trigger_registration(
            std::make_shared<CallbackStatusSource>(std::move(callback)), limits);
    } catch (...) {
        return {std::nullopt, StatusTriggerRegistrationError::resource_exhausted};
    }
}

}  // namespace baas::service::app
