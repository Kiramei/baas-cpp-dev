#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace baas::service::auth {

inline constexpr std::int64_t maximum_safe_json_integer = 9'007'199'254'740'991LL;
inline constexpr std::int64_t minimum_safe_json_integer = -maximum_safe_json_integer;

struct CanonicalJsonLimits {
    std::size_t max_input_bytes{1U * 1'024U * 1'024U};
    std::size_t max_output_bytes{1U * 1'024U * 1'024U};
    std::size_t max_depth{64};
    std::size_t max_values{65'536};
};

enum class CanonicalJsonError : std::uint8_t {
    none,
    input_too_large,
    output_too_large,
    invalid_utf8,
    invalid_json,
    duplicate_key,
    unsupported_number,
    unsafe_integer,
    depth_exceeded,
    value_limit_exceeded,
    resource_exhausted,
};

[[nodiscard]] std::string_view canonical_json_error_name(
    CanonicalJsonError error) noexcept;

class CanonicalJsonValue final {
public:
    using Array = std::vector<CanonicalJsonValue>;
    using Object = std::vector<std::pair<std::string, CanonicalJsonValue>>;
    using Storage = std::variant<std::monostate, bool, std::int64_t, std::string,
                                 Array, Object>;

    CanonicalJsonValue() noexcept = default;
    explicit CanonicalJsonValue(bool value) : storage_(value) {}
    explicit CanonicalJsonValue(std::int64_t value) : storage_(value) {}
    explicit CanonicalJsonValue(std::string value) : storage_(std::move(value)) {}
    explicit CanonicalJsonValue(Array value) : storage_(std::move(value)) {}
    explicit CanonicalJsonValue(Object value) : storage_(std::move(value)) {}

    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] const bool* as_boolean() const noexcept;
    [[nodiscard]] const std::int64_t* as_integer() const noexcept;
    [[nodiscard]] const std::string* as_string() const noexcept;
    [[nodiscard]] const Array* as_array() const noexcept;
    [[nodiscard]] const Object* as_object() const noexcept;
    [[nodiscard]] const CanonicalJsonValue* find(std::string_view key) const noexcept;
    [[nodiscard]] const Storage& storage() const noexcept { return storage_; }

private:
    Storage storage_;
};

struct CanonicalJsonParseResult {
    std::optional<CanonicalJsonValue> value;
    CanonicalJsonError error{CanonicalJsonError::none};
    std::size_t error_offset{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == CanonicalJsonError::none && value.has_value();
    }
};

struct CanonicalJsonEncodeResult {
    std::string text;
    CanonicalJsonError error{CanonicalJsonError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == CanonicalJsonError::none;
    }
};

// The accepted value domain is deliberately narrower than general JSON:
// null, boolean, UTF-8 string, arrays, objects, and exact integers in the
// JavaScript safe range. Floating point and exponent spellings are rejected.
// Object keys are decoded before duplicate detection and canonical sorting.
[[nodiscard]] CanonicalJsonParseResult parse_canonical_json_value(
    std::string_view input,
    CanonicalJsonLimits limits = {});

[[nodiscard]] CanonicalJsonEncodeResult encode_canonical_json_value(
    const CanonicalJsonValue& value,
    CanonicalJsonLimits limits = {});

[[nodiscard]] bool is_valid_utf8(std::string_view input) noexcept;

}  // namespace baas::service::auth
