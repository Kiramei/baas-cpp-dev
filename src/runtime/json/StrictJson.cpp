#include "runtime/json/StrictJson.h"

#include <new>
#include <set>
#include <string>
#include <vector>

namespace baas::runtime::json {
namespace {

using Json = nlohmann::json;

struct ParseFailure final {
    StrictJsonError error;
};

[[noreturn]] void fail(const StrictJsonError error) {
    throw ParseFailure{error};
}

[[nodiscard]] bool valid_utf8(const std::string_view text) noexcept {
    std::size_t offset{};
    while (offset < text.size()) {
        const auto lead = static_cast<unsigned char>(text[offset]);
        std::uint32_t code{};
        std::size_t length{};
        if (lead <= 0x7fU) {
            code = lead;
            length = 1;
        } else if (lead >= 0xc2U && lead <= 0xdfU) {
            code = lead & 0x1fU;
            length = 2;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            code = lead & 0x0fU;
            length = 3;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            code = lead & 0x07U;
            length = 4;
        } else {
            return false;
        }
        if (length > text.size() - offset) return false;
        for (std::size_t index = 1; index < length; ++index) {
            const auto continuation = static_cast<unsigned char>(text[offset + index]);
            if ((continuation & 0xc0U) != 0x80U) return false;
            code = (code << 6U) | (continuation & 0x3fU);
        }
        if ((length == 2 && code < 0x80U) ||
            (length == 3 && code < 0x800U) ||
            (length == 4 && code < 0x10000U) || code > 0x10ffffU ||
            (code >= 0xd800U && code <= 0xdfffU)) return false;
        offset += length;
    }
    return true;
}

}  // namespace

StrictJsonResult parse_strict_json(const std::string_view text,
                                   const StrictJsonLimits limits) noexcept {
    if (limits.max_depth == 0 || limits.max_nodes == 0 ||
        limits.max_string_bytes == 0) {
        return {{}, StrictJsonError::invalid_limits};
    }
    if (!valid_utf8(text)) return {{}, StrictJsonError::invalid_utf8};

    try {
        std::size_t nodes{};
        bool duplicate{};
        std::vector<std::set<std::string, std::less<>>> objects;
        auto callback = [&](const int depth, const Json::parse_event_t event,
                            Json& parsed) {
            if (depth < 0 || static_cast<std::size_t>(depth) > limits.max_depth)
                fail(StrictJsonError::depth_limit_exceeded);
            if ((event == Json::parse_event_t::key ||
                 event == Json::parse_event_t::value) && parsed.is_string() &&
                parsed.get_ref<const std::string&>().size() > limits.max_string_bytes)
                fail(StrictJsonError::string_limit_exceeded);
            if (event == Json::parse_event_t::object_start ||
                event == Json::parse_event_t::array_start ||
                event == Json::parse_event_t::value) {
                if (++nodes > limits.max_nodes)
                    fail(StrictJsonError::node_limit_exceeded);
            }
            if (event == Json::parse_event_t::object_start) {
                objects.emplace_back();
            } else if (event == Json::parse_event_t::key) {
                if (objects.empty() ||
                    !objects.back().insert(parsed.get<std::string>()).second)
                    duplicate = true;
            } else if (event == Json::parse_event_t::object_end) {
                if (objects.empty()) fail(StrictJsonError::internal_failure);
                objects.pop_back();
            }
            return true;
        };
        auto result = Json::parse(text.begin(), text.end(), callback, true, false);
        if (duplicate) return {{}, StrictJsonError::duplicate_key};
        if (!objects.empty()) return {{}, StrictJsonError::internal_failure};
        return {std::move(result), StrictJsonError::none};
    } catch (const ParseFailure& error) {
        return {{}, error.error};
    } catch (const Json::exception&) {
        return {{}, StrictJsonError::invalid_syntax};
    } catch (const std::bad_alloc&) {
        return {{}, StrictJsonError::resource_exhausted};
    } catch (...) {
        return {{}, StrictJsonError::internal_failure};
    }
}

std::string_view strict_json_error_name(const StrictJsonError error) noexcept {
    using enum StrictJsonError;
    switch (error) {
    case none: return "SJ000_NONE";
    case invalid_limits: return "SJ001_INVALID_LIMITS";
    case invalid_utf8: return "SJ002_INVALID_UTF8";
    case invalid_syntax: return "SJ003_INVALID_SYNTAX";
    case duplicate_key: return "SJ004_DUPLICATE_KEY";
    case depth_limit_exceeded: return "SJ005_DEPTH_LIMIT_EXCEEDED";
    case node_limit_exceeded: return "SJ006_NODE_LIMIT_EXCEEDED";
    case string_limit_exceeded: return "SJ007_STRING_LIMIT_EXCEEDED";
    case resource_exhausted: return "SJ008_RESOURCE_EXHAUSTED";
    case internal_failure: return "SJ009_INTERNAL_FAILURE";
    }
    return "SJ009_INTERNAL_FAILURE";
}

}  // namespace baas::runtime::json
