#pragma once

#include "service/adapters/FileResourceStore.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace baas::service::test {

inline std::shared_ptr<const adapters::ConfigurationDefaults>
synthetic_configuration_defaults()
{
    static const auto defaults = [] {
        using Json = nlohmann::json;

        Json user{
            {"name", "Synthetic"},
            {"server", "官服"},
            {"create_item_holding_quantity", Json::object()},
            {"new_event_enable_state", "default"},
            {"purchase_arena_ticket_times", "0"},
            {"autostart", false},
            {"then", "none"},
        };
        for (std::size_t index = user.size(); index < 97; ++index) {
            user["synthetic_user_" + std::to_string(index)] = index;
        }

        Json events = Json::array();
        for (std::size_t index = 0; index < 26; ++index) {
            Json resets = Json::array({Json::array({20, 0, 0})});
            if (index >= 1 && index <= 3) {
                resets.push_back(Json::array({20, 0, 0}));
            }
            events.push_back(Json{
                {"enabled", true},
                {"priority", static_cast<int>(index)},
                {"interval", 0},
                {"daily_reset", std::move(resets)},
                {"next_tick", 0},
                {"event_name", "synthetic-event-" + std::to_string(index)},
                {"func_name", index == 0
                    ? std::string{"restart"}
                    : "synthetic_event_" + std::to_string(index)},
                {"disabled_time_range", Json::array()},
                {"pre_task", Json::array()},
                {"post_task", Json::array()},
            });
        }

        Json switches = Json::array();
        for (std::size_t index = 0; index < 11; ++index) {
            switches.push_back(Json{
                {"config", index == 0
                    ? std::string{"cafeInvite"}
                    : "synthetic_switch_" + std::to_string(index)},
            });
        }

        const auto item_vector = [](const std::string& prefix,
                                    const std::size_t count) {
            Json values = Json::array();
            for (std::size_t index = 0; index < count; ++index) {
                values.push_back(prefix + std::to_string(index));
            }
            return values;
        };
        Json static_data{
            {"create_item_order",
             Json{
                 {"CN", Json{{"basic", Json{{"Synthetic", item_vector("cn-", 154)}}}}},
                 {"Global", Json{{"basic", Json{{"Synthetic", item_vector("global-", 157)}}}}},
                 {"JP", Json{{"basic", Json{{"Synthetic", item_vector("jp-", 157)}}}}},
             }},
        };
        for (std::size_t index = static_data.size(); index < 25; ++index) {
            static_data["synthetic_static_" + std::to_string(index)] = index;
        }

        return std::make_shared<const adapters::ConfigurationDefaults>(
            adapters::ConfigurationDefaults{
                user.dump(), events.dump(), switches.dump(), static_data.dump()});
    }();
    return defaults;
}

inline adapters::FileResourceStoreDependencies with_synthetic_defaults(
    adapters::FileResourceStoreDependencies dependencies = {})
{
    dependencies.configuration_defaults = synthetic_configuration_defaults();
    return dependencies;
}

}  // namespace baas::service::test
