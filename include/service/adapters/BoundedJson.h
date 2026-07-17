#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace baas::service::adapters::bounded_json {

using Json = nlohmann::json;

struct JsonBounds {
    std::size_t bytes;
    std::size_t depth;
    std::size_t nodes;
};

class ShapeSax final : public nlohmann::json_sax<Json> {
public:
    ShapeSax(const std::size_t maximum_depth,
             const std::size_t maximum_nodes) noexcept
        : maximum_depth_(maximum_depth), maximum_nodes_(maximum_nodes)
    {}

    bool null() override { return scalar(); }
    bool boolean(bool) override { return scalar(); }
    bool number_integer(Json::number_integer_t) override { return scalar(); }
    bool number_unsigned(Json::number_unsigned_t) override { return scalar(); }
    bool number_float(Json::number_float_t, const Json::string_t&) override
    {
        return scalar();
    }
    bool string(Json::string_t&) override { return scalar(); }
    bool binary(Json::binary_t&) override { return scalar(); }
    bool start_object(std::size_t) override { return container_start(); }
    bool key(Json::string_t&) override { return true; }
    bool end_object() override { return container_end(); }
    bool start_array(std::size_t) override { return container_start(); }
    bool end_array() override { return container_end(); }
    bool parse_error(std::size_t, const std::string&,
                     const nlohmann::detail::exception&) override
    {
        return false;
    }

private:
    [[nodiscard]] bool node() noexcept
    {
        return depth_ <= maximum_depth_ && ++nodes_ <= maximum_nodes_;
    }

    [[nodiscard]] bool scalar() noexcept { return node(); }

    [[nodiscard]] bool container_start() noexcept
    {
        if (!node()) return false;
        ++depth_;
        return true;
    }

    [[nodiscard]] bool container_end() noexcept
    {
        if (depth_ == 0) return false;
        --depth_;
        return true;
    }

    std::size_t maximum_depth_{};
    std::size_t maximum_nodes_{};
    std::size_t depth_{};
    std::size_t nodes_{};
};

[[nodiscard]] inline bool is_valid_utf8(const std::string_view input) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(input.data());
    std::size_t index = 0;
    while (index < input.size()) {
        const auto lead = bytes[index++];
        if (lead <= 0x7fU) continue;

        std::size_t trailing{};
        std::uint32_t code_point{};
        std::uint32_t minimum{};
        if (lead >= 0xc2U && lead <= 0xdfU) {
            trailing = 1;
            code_point = lead & 0x1fU;
            minimum = 0x80U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            trailing = 2;
            code_point = lead & 0x0fU;
            minimum = 0x800U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            trailing = 3;
            code_point = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (trailing > input.size() - index) return false;
        for (std::size_t offset = 0; offset < trailing; ++offset) {
            const auto byte = bytes[index++];
            if ((byte & 0xc0U) != 0x80U) return false;
            code_point = (code_point << 6U) | (byte & 0x3fU);
        }
        if (code_point < minimum || code_point > 0x10ffffU
            || (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool bounded_tree(
    const Json& value, const std::size_t maximum_depth,
    const std::size_t maximum_nodes, const std::size_t depth,
    std::size_t& nodes)
{
    if (++nodes > maximum_nodes || depth > maximum_depth) return false;
    if (value.is_array()) {
        for (const auto& child : value) {
            if (!bounded_tree(child, maximum_depth, maximum_nodes, depth + 1, nodes)) {
                return false;
            }
        }
    } else if (value.is_object()) {
        for (const auto& [key, child] : value.items()) {
            static_cast<void>(key);
            if (!bounded_tree(child, maximum_depth, maximum_nodes, depth + 1, nodes)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] inline std::optional<Json> parse_json(
    const std::string_view text, const JsonBounds bounds)
{
    if (text.size() > bounds.bytes || !is_valid_utf8(text)) return std::nullopt;
    try {
        // Reject excessive nesting and node counts before constructing a DOM.
        // This parser is reachable with unauthenticated input, so a post-parse
        // tree walk alone is not an allocation or recursion boundary.
        ShapeSax shape{bounds.depth, bounds.nodes};
        if (!Json::sax_parse(text, &shape)) return std::nullopt;

        bool duplicate{};
        std::vector<std::unordered_set<std::string>> object_keys;
        const auto callback = [&duplicate, &object_keys](
                                  int, const Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start) {
                object_keys.emplace_back();
            } else if (event == Json::parse_event_t::key && !object_keys.empty()) {
                if (!object_keys.back().insert(parsed.get<std::string>()).second) {
                    duplicate = true;
                }
            } else if (event == Json::parse_event_t::object_end && !object_keys.empty()) {
                object_keys.pop_back();
            }
            return !duplicate;
        };
        Json value = Json::parse(text, callback, false);
        if (duplicate || value.is_discarded()) return std::nullopt;
        std::size_t nodes{};
        if (!bounded_tree(value, bounds.depth, bounds.nodes, 0, nodes)) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace baas::service::adapters::bounded_json
