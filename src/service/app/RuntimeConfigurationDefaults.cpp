#include "service/app/RuntimeConfigurationDefaults.h"

#include "runtime/repository/RuntimeRepositoryReadView.h"
#include "service/adapters/BoundedJson.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace baas::service::app {
namespace {

using Json = nlohmann::json;

[[nodiscard]] std::string read_text(
    const ::baas::runtime::repository::RuntimeRepositoryReadView& resources,
    const std::string_view path,
    const std::uintmax_t limit)
{
    const auto bytes = resources.read(path, limit);
    std::string text(bytes.size(), '\0');
    if (!bytes.empty()) {
        std::memcpy(text.data(), bytes.data(), bytes.size());
    }
    return text;
}

[[nodiscard]] Json parse_document(
    const std::string& bytes, const std::string_view name,
    const channels::ResourceStoreLimits& limits)
{
    auto parsed = adapters::bounded_json::parse_json(
        bytes,
        {limits.max_json_bytes, limits.max_json_depth, limits.max_json_nodes});
    if (!parsed) {
        throw std::invalid_argument(
            "runtime configuration default is invalid or exceeds limits: "
            + std::string{name});
    }
    return std::move(*parsed);
}

void validate_user(const Json& value)
{
    if (!value.is_object()
        || !value.contains("name") || !value.at("name").is_string()
        || !value.contains("server") || !value.at("server").is_string()
        || !value.contains("create_item_holding_quantity")
        || !value.at("create_item_holding_quantity").is_object()) {
        throw std::invalid_argument(
            "runtime user configuration default has an invalid shape");
    }
}

void validate_event(const Json& value)
{
    if (!value.is_array()) {
        throw std::invalid_argument(
            "runtime event configuration default has an invalid shape");
    }
    for (const auto& item : value) {
        if (!item.is_object() || !item.contains("func_name")
            || !item.at("func_name").is_string()
            || !item.contains("daily_reset")
            || !item.at("daily_reset").is_array()) {
            throw std::invalid_argument(
                "runtime event configuration entry has an invalid shape");
        }
        for (const auto& reset : item.at("daily_reset")) {
            if (!reset.is_array() || reset.size() != 3
                || !reset[0].is_number_integer()
                || !reset[1].is_number_integer()
                || !reset[2].is_number_integer()) {
                throw std::invalid_argument(
                    "runtime event reset has an invalid shape");
            }
            try {
                for (const auto& component : reset) {
                    const auto number = component.get<std::int64_t>();
                    if (number < std::numeric_limits<int>::min()
                        || number > std::numeric_limits<int>::max()) {
                        throw std::invalid_argument(
                            "runtime event reset exceeds integer range");
                    }
                }
                if (reset[0].get<std::int64_t>()
                    == std::numeric_limits<int>::min()) {
                    throw std::invalid_argument(
                        "runtime event reset cannot be shifted safely");
                }
            } catch (const Json::exception&) {
                throw std::invalid_argument(
                    "runtime event reset exceeds integer range");
            }
        }
    }
}

void validate_switches(const Json& value)
{
    if (!value.is_array()) {
        throw std::invalid_argument(
            "runtime switch configuration default has an invalid shape");
    }
    for (const auto& item : value) {
        if (!item.is_object() || !item.contains("config")
            || !item.at("config").is_string()) {
            throw std::invalid_argument(
                "runtime switch configuration entry has an invalid shape");
        }
    }
}

void require_persistable(
    const Json& value, const std::string_view name,
    const channels::ResourceStoreLimits& limits)
{
    std::size_t nodes{};
    if (!adapters::bounded_json::bounded_tree(
            value, limits.max_json_depth, limits.max_json_nodes, 0, nodes)
        || value.dump(2).size() > limits.max_json_bytes) {
        throw std::invalid_argument(
            "runtime configuration default cannot be persisted within limits: "
            + std::string{name});
    }
}

void validate_persisted_capacity(
    const Json& user, const Json& event, const Json& switches,
    const Json& static_data, const channels::ResourceStoreLimits& limits)
{
    require_persistable(switches, "switch", limits);
    require_persistable(event, "event/CN", limits);

    auto shifted_event = event;
    for (auto& item : shifted_event) {
        for (auto& reset : item["daily_reset"]) {
            reset[0] = reset[0].get<int>() - 1;
        }
    }
    require_persistable(shifted_event, "event/non-CN", limits);

    const auto& create_order = static_data.at("create_item_order");
    const auto utf8 = [](const std::u8string_view value) {
        return std::string{
            reinterpret_cast<const char*>(value.data()), value.size()};
    };
    for (const std::string_view server : {"CN", "Global", "JP"}) {
        auto candidate = user;
        candidate["name"] = "x";
        candidate["server"] = server == "CN"
            ? utf8(u8"官服")
            : server == "Global" ? utf8(u8"国际服青少年")
                                 : utf8(u8"日服PC端");
        auto& quantities = candidate.at("create_item_holding_quantity");
        std::unordered_set<std::string> valid_items;
        for (const auto& category : create_order.at(server).at("basic")) {
            for (const auto& item : category) {
                valid_items.insert(item.get<std::string>());
            }
        }
        for (auto iterator = quantities.begin(); iterator != quantities.end();) {
            if (!valid_items.contains(iterator.key())) {
                iterator = quantities.erase(iterator);
            } else {
                ++iterator;
            }
        }
        for (const auto& item : valid_items) {
            if (!quantities.contains(item)) quantities[item] = -1;
        }
        require_persistable(
            candidate, "user/" + std::string{server}, limits);
    }
}

void validate_static(const Json& value)
{
    if (!value.is_object() || !value.contains("create_item_order")) {
        throw std::invalid_argument(
            "runtime static configuration default has an invalid shape");
    }
    const auto& order = value.at("create_item_order");
    for (const std::string_view server : {"CN", "Global", "JP"}) {
        const auto found = order.find(server);
        if (!order.is_object() || found == order.end() || !found->is_object()
            || !found->contains("basic") || !found->at("basic").is_object()) {
            throw std::invalid_argument(
                "runtime create-item order has an invalid server shape");
        }
        for (const auto& category : found->at("basic")) {
            if (!category.is_array()) {
                throw std::invalid_argument(
                    "runtime create-item category has an invalid shape");
            }
            for (const auto& item : category) {
                if (!item.is_string()) {
                    throw std::invalid_argument(
                        "runtime create-item entry has an invalid shape");
                }
            }
        }
    }
}

}  // namespace

std::shared_ptr<const adapters::ConfigurationDefaults>
load_runtime_configuration_defaults(
    const ::baas::runtime::repository::RuntimeRepositoryReadView& resources,
    const channels::ResourceStoreLimits& consumer_limits)
{
    if (consumer_limits.max_json_bytes == 0
        || consumer_limits.max_json_depth == 0
        || consumer_limits.max_json_nodes == 0) {
        throw std::invalid_argument(
            "runtime configuration consumer limits are invalid");
    }
    auto defaults = std::make_shared<adapters::ConfigurationDefaults>();
    defaults->user_json = read_text(
        resources, "service/configuration/defaults/user.json",
        consumer_limits.max_json_bytes);
    defaults->event_json = read_text(
        resources, "service/configuration/defaults/event.json",
        consumer_limits.max_json_bytes);
    defaults->switch_json = read_text(
        resources, "service/configuration/defaults/switch.json",
        consumer_limits.max_json_bytes);
    defaults->static_json = read_text(
        resources, "service/configuration/defaults/static.json",
        consumer_limits.max_json_bytes);

    const auto user = parse_document(
        defaults->user_json, "config", consumer_limits);
    const auto event = parse_document(
        defaults->event_json, "event", consumer_limits);
    const auto switches = parse_document(
        defaults->switch_json, "switch", consumer_limits);
    const auto static_data = parse_document(
        defaults->static_json, "static", consumer_limits);
    validate_user(user);
    validate_event(event);
    validate_switches(switches);
    validate_static(static_data);
    validate_persisted_capacity(
        user, event, switches, static_data, consumer_limits);
    return defaults;
}

}  // namespace baas::service::app
