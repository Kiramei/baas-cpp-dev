#include "service/app/ConfigurationTriggerRegistration.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_set>
#include <utility>

namespace baas::service::app {
namespace {

using Json = nlohmann::json;

enum class PayloadError { none, capacity, invalid, missing_id };

struct ParsedId {
    std::string id;
    PayloadError error{PayloadError::none};
};

[[nodiscard]] bool valid_limits(const ConfigurationTriggerLimits& limits) noexcept
{
    return limits.max_payload_bytes >= 2
        && limits.max_payload_bytes <= 1U * 1'024U * 1'024U
        && limits.max_payload_depth != 0 && limits.max_payload_depth <= 64
        && limits.max_payload_nodes != 0 && limits.max_payload_nodes <= 65'536
        && limits.max_id_bytes != 0 && limits.max_id_bytes <= 1'024;
}

[[nodiscard]] ParsedId parse_id(
    const std::string_view payload, const ConfigurationTriggerLimits& limits)
{
    if (payload.size() > limits.max_payload_bytes) {
        return {{}, PayloadError::capacity};
    }
    bool duplicate{};
    bool capacity{};
    std::size_t nodes{};
    std::vector<std::unordered_set<std::string>> keys;
    try {
        const auto callback = [&](const int depth, const Json::parse_event_t event,
                                  Json& parsed) {
            if (depth < 0 || static_cast<std::size_t>(depth) > limits.max_payload_depth) {
                capacity = true;
                return false;
            }
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            if (event == Json::parse_event_t::key && !keys.empty()) {
                const auto key = parsed.get<std::string>();
                if (!keys.back().insert(key).second) duplicate = true;
            }
            if (event == Json::parse_event_t::object_end && !keys.empty()) {
                keys.pop_back();
            }
            if (event == Json::parse_event_t::value
                || event == Json::parse_event_t::object_start
                || event == Json::parse_event_t::array_start) {
                if (++nodes > limits.max_payload_nodes) capacity = true;
            }
            return !duplicate && !capacity;
        };
        auto root = Json::parse(payload, callback, false);
        if (capacity) return {{}, PayloadError::capacity};
        if (root.is_discarded() || duplicate || !root.is_object()) {
            return {{}, PayloadError::invalid};
        }
        const auto found = root.find("id");
        if (found == root.end() || !found->is_string()) {
            return {{}, PayloadError::missing_id};
        }
        auto id = found->get<std::string>();
        if (id.empty()) return {{}, PayloadError::missing_id};
        if (id.size() > limits.max_id_bytes) return {{}, PayloadError::capacity};
        return {std::move(id), PayloadError::none};
    } catch (const std::bad_alloc&) {
        return {{}, PayloadError::capacity};
    } catch (...) {
        return {{}, PayloadError::invalid};
    }
}

void stage_error(trigger::TriggerResponseSink& sink, const std::string_view error)
{
    static_cast<void>(sink.error(std::string{error}));
}

void stage_cancelled(trigger::TriggerResponseSink& sink)
{
    static_cast<void>(sink.cancelled("cancelled"));
}

[[nodiscard]] std::string_view command_error(
    const adapters::ConfigCommandError error) noexcept
{
    using enum adapters::ConfigCommandError;
    switch (error) {
        case none: return "none";
        case cancelled: return "cancelled";
        case invalid_id: return "invalid_config_id";
        case not_found: return "config_not_found";
        case invalid_data: return "config_invalid_data";
        case capacity: return "config_capacity";
        case conflict: return "config_conflict";
        case internal_error: return "config_internal_error";
    }
    return "config_internal_error";
}

void validate_or_error(
    const ParsedId& parsed, trigger::TriggerResponseSink& sink)
{
    switch (parsed.error) {
        case PayloadError::none: break;
        case PayloadError::capacity: stage_error(sink, "config_payload_capacity"); break;
        case PayloadError::invalid: stage_error(sink, "invalid_config_payload"); break;
        case PayloadError::missing_id: stage_error(sink, "id is required"); break;
    }
}

}  // namespace

std::string_view configuration_trigger_registration_error_name(
    const ConfigurationTriggerRegistrationError error) noexcept
{
    using enum ConfigurationTriggerRegistrationError;
    switch (error) {
        case none: return "none";
        case missing_store: return "missing_store";
        case invalid_limits: return "invalid_limits";
        case resource_exhausted: return "resource_exhausted";
    }
    return "unknown";
}

ConfigurationTriggerRegistrationResult make_configuration_trigger_registrations(
    std::shared_ptr<adapters::FileResourceStore> store,
    const ConfigurationTriggerLimits limits) noexcept
{
    if (!store) return {{}, ConfigurationTriggerRegistrationError::missing_store};
    if (!valid_limits(limits)) {
        return {{}, ConfigurationTriggerRegistrationError::invalid_limits};
    }
    try {
        std::vector<trigger::TriggerHandlerRegistration> registrations;
        registrations.reserve(2);
        registrations.push_back({
            "copy_config",
            [store, limits](const trigger::AdmittedTriggerRequest& request,
                            trigger::TriggerResponseSink& sink,
                            const std::stop_token stop) {
                if (stop.stop_requested()) return stage_cancelled(sink);
                const auto parsed = parse_id(request.payload_json(), limits);
                if (parsed.error != PayloadError::none) {
                    validate_or_error(parsed, sink);
                    return;
                }
                bool claim_attempted{};
                const auto result = store->copy_config(
                    parsed.id, stop,
                    [&](const std::string_view serial,
                        const std::string_view name) {
                        claim_attempted = true;
                        std::string data;
                        try {
                            data = "{\"serial\":" + Json(serial).dump()
                                + ",\"name\":" + Json(name).dump() + "}";
                        } catch (...) {
                            stage_error(sink, "config_response_capacity");
                            return false;
                        }
                        const auto prepared =
                            sink.irrevocable_success(std::move(data));
                        if (!prepared && !sink.irrevocable_terminal_claimed()) {
                            stage_error(sink, "config_response_rejected");
                        }
                        return sink.irrevocable_terminal_claimed();
                    });
                if (claim_attempted) {
                    if (!result && sink.irrevocable_terminal_claimed()) {
                        static_cast<void>(sink.irrevocable_error(
                            std::string{command_error(result.error)}));
                    }
                    return;
                }
                if (stop.stop_requested()
                    || result.error == adapters::ConfigCommandError::cancelled) {
                    return stage_cancelled(sink);
                }
                if (!result) return stage_error(sink, command_error(result.error));
                stage_error(sink, "config_internal_error");
            },
        });
        registrations.push_back({
            "remove_config*",
            [store, limits](const trigger::AdmittedTriggerRequest& request,
                            trigger::TriggerResponseSink& sink,
                            const std::stop_token stop) {
                if (stop.stop_requested()) return stage_cancelled(sink);
                const auto parsed = parse_id(request.payload_json(), limits);
                if (parsed.error != PayloadError::none) {
                    validate_or_error(parsed, sink);
                    return;
                }
                bool claim_attempted{};
                const auto result = store->remove_config(parsed.id, stop, [&] {
                    claim_attempted = true;
                    const auto prepared = sink.irrevocable_success("{}");
                    if (!prepared && !sink.irrevocable_terminal_claimed()) {
                        stage_error(sink, "config_response_rejected");
                    }
                    return sink.irrevocable_terminal_claimed();
                });
                if (claim_attempted) {
                    if (!result && sink.irrevocable_terminal_claimed()) {
                        static_cast<void>(sink.irrevocable_error(
                            std::string{command_error(result.error)}));
                    }
                    return;
                }
                if (stop.stop_requested()
                    || result.error == adapters::ConfigCommandError::cancelled) {
                    return stage_cancelled(sink);
                }
                if (!result) return stage_error(sink, command_error(result.error));
                if (!sink.success("{}")) {
                    stage_error(sink, "config_response_rejected");
                }
            },
        });
        return {std::move(registrations),
                ConfigurationTriggerRegistrationError::none};
    } catch (...) {
        return {{}, ConfigurationTriggerRegistrationError::resource_exhausted};
    }
}

}  // namespace baas::service::app
