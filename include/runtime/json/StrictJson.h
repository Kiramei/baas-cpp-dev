#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

namespace baas::runtime::json {

enum class StrictJsonError : std::uint8_t {
    none,
    invalid_limits,
    invalid_utf8,
    invalid_syntax,
    duplicate_key,
    depth_limit_exceeded,
    node_limit_exceeded,
    string_limit_exceeded,
    resource_exhausted,
    internal_failure,
};

struct StrictJsonLimits final {
    std::size_t max_depth{32};
    std::size_t max_nodes{65'536};
    std::size_t max_string_bytes{4'096};
};

struct StrictJsonResult final {
    std::optional<nlohmann::json> document;
    StrictJsonError error{StrictJsonError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return document.has_value() && error == StrictJsonError::none;
    }
};

// Parses one complete RFC 8259 value. UTF-8, duplicate names, comments,
// non-finite numbers, trailing input and caller-supplied structural limits are
// checked before a document is returned.
[[nodiscard]] StrictJsonResult parse_strict_json(
    std::string_view text, StrictJsonLimits limits = {}) noexcept;

[[nodiscard]] std::string_view strict_json_error_name(StrictJsonError error) noexcept;

}  // namespace baas::runtime::json
