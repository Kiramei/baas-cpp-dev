#include "service/auth/CanonicalJson.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <new>

namespace baas::service::auth {
namespace {

[[nodiscard]] bool continuation(const unsigned char value) noexcept
{
    return (value & 0xC0U) == 0x80U;
}

[[nodiscard]] bool valid_utf8_impl(const std::string_view input) noexcept
{
    std::size_t index = 0;
    while (index < input.size()) {
        const auto first = static_cast<unsigned char>(input[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU) {
            if (index + 1 >= input.size()
                || !continuation(static_cast<unsigned char>(input[index + 1]))) {
                return false;
            }
            index += 2;
            continue;
        }
        if (first >= 0xE0U && first <= 0xEFU) {
            if (index + 2 >= input.size()) return false;
            const auto second = static_cast<unsigned char>(input[index + 1]);
            const auto third = static_cast<unsigned char>(input[index + 2]);
            if (!continuation(second) || !continuation(third)) return false;
            if ((first == 0xE0U && second < 0xA0U)
                || (first == 0xEDU && second >= 0xA0U)) {
                return false;
            }
            index += 3;
            continue;
        }
        if (first >= 0xF0U && first <= 0xF4U) {
            if (index + 3 >= input.size()) return false;
            const auto second = static_cast<unsigned char>(input[index + 1]);
            if (!continuation(second)
                || !continuation(static_cast<unsigned char>(input[index + 2]))
                || !continuation(static_cast<unsigned char>(input[index + 3]))) {
                return false;
            }
            if ((first == 0xF0U && second < 0x90U)
                || (first == 0xF4U && second > 0x8FU)) {
                return false;
            }
            index += 4;
            continue;
        }
        return false;
    }
    return true;
}

void append_utf8(std::string& output, const std::uint32_t code_point)
{
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

[[nodiscard]] int hex_digit(const char value) noexcept
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

class Parser final {
public:
    Parser(const std::string_view input, const CanonicalJsonLimits limits)
        : input_(input), limits_(limits)
    {}

    [[nodiscard]] CanonicalJsonParseResult parse()
    {
        skip_whitespace();
        auto value = parse_value(1);
        skip_whitespace();
        if (error_ == CanonicalJsonError::none && index_ != input_.size()) {
            fail(CanonicalJsonError::invalid_json);
        }
        if (error_ != CanonicalJsonError::none) {
            return {std::nullopt, error_, error_offset_};
        }
        return {std::move(value), CanonicalJsonError::none, 0};
    }

private:
    void fail(const CanonicalJsonError error) noexcept
    {
        if (error_ != CanonicalJsonError::none) return;
        error_ = error;
        error_offset_ = index_;
    }

    void skip_whitespace() noexcept
    {
        while (index_ < input_.size()) {
            const auto value = input_[index_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') break;
            ++index_;
        }
    }

    [[nodiscard]] bool consume(const std::string_view expected) noexcept
    {
        if (input_.substr(index_, expected.size()) != expected) {
            fail(CanonicalJsonError::invalid_json);
            return false;
        }
        index_ += expected.size();
        return true;
    }

    [[nodiscard]] std::optional<std::uint16_t> parse_hex_quad()
    {
        if (index_ + 4 > input_.size()) {
            fail(CanonicalJsonError::invalid_json);
            return std::nullopt;
        }
        std::uint16_t value = 0;
        for (std::size_t offset = 0; offset < 4; ++offset) {
            const auto digit = hex_digit(input_[index_ + offset]);
            if (digit < 0) {
                index_ += offset;
                fail(CanonicalJsonError::invalid_json);
                return std::nullopt;
            }
            value = static_cast<std::uint16_t>((value << 4U) | digit);
        }
        index_ += 4;
        return value;
    }

    [[nodiscard]] std::optional<std::string> parse_string()
    {
        if (index_ >= input_.size() || input_[index_] != '"') {
            fail(CanonicalJsonError::invalid_json);
            return std::nullopt;
        }
        ++index_;
        std::string output;
        while (index_ < input_.size()) {
            const auto value = static_cast<unsigned char>(input_[index_++]);
            if (value == '"') return output;
            if (value < 0x20U) {
                fail(CanonicalJsonError::invalid_json);
                return std::nullopt;
            }
            if (value != '\\') {
                output.push_back(static_cast<char>(value));
                continue;
            }
            if (index_ >= input_.size()) {
                fail(CanonicalJsonError::invalid_json);
                return std::nullopt;
            }
            const auto escape = input_[index_++];
            switch (escape) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    const auto first = parse_hex_quad();
                    if (!first) return std::nullopt;
                    std::uint32_t code_point = *first;
                    if (*first >= 0xD800U && *first <= 0xDBFFU) {
                        if (index_ + 2 > input_.size() || input_[index_] != '\\'
                            || input_[index_ + 1] != 'u') {
                            fail(CanonicalJsonError::invalid_json);
                            return std::nullopt;
                        }
                        index_ += 2;
                        const auto second = parse_hex_quad();
                        if (!second || *second < 0xDC00U || *second > 0xDFFFU) {
                            if (error_ == CanonicalJsonError::none)
                                fail(CanonicalJsonError::invalid_json);
                            return std::nullopt;
                        }
                        code_point = 0x10000U
                            + ((static_cast<std::uint32_t>(*first) - 0xD800U) << 10U)
                            + (static_cast<std::uint32_t>(*second) - 0xDC00U);
                    } else if (*first >= 0xDC00U && *first <= 0xDFFFU) {
                        fail(CanonicalJsonError::invalid_json);
                        return std::nullopt;
                    }
                    append_utf8(output, code_point);
                    break;
                }
                default:
                    fail(CanonicalJsonError::invalid_json);
                    return std::nullopt;
            }
        }
        fail(CanonicalJsonError::invalid_json);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<CanonicalJsonValue> parse_number()
    {
        const auto begin = index_;
        bool negative = false;
        if (index_ < input_.size() && input_[index_] == '-') {
            negative = true;
            ++index_;
        }
        if (index_ >= input_.size() || input_[index_] < '0' || input_[index_] > '9') {
            fail(CanonicalJsonError::invalid_json);
            return std::nullopt;
        }
        if (input_[index_] == '0') {
            ++index_;
            if (index_ < input_.size() && input_[index_] >= '0' && input_[index_] <= '9') {
                fail(CanonicalJsonError::invalid_json);
                return std::nullopt;
            }
        } else {
            while (index_ < input_.size() && input_[index_] >= '0'
                   && input_[index_] <= '9') {
                ++index_;
            }
        }
        if (index_ < input_.size()
            && (input_[index_] == '.' || input_[index_] == 'e' || input_[index_] == 'E')) {
            fail(CanonicalJsonError::unsupported_number);
            return std::nullopt;
        }
        std::uint64_t magnitude = 0;
        const auto digits_begin = begin + (negative ? 1U : 0U);
        const auto conversion = std::from_chars(
            input_.data() + digits_begin, input_.data() + index_, magnitude);
        if (conversion.ec != std::errc{} || magnitude > static_cast<std::uint64_t>(maximum_safe_json_integer)) {
            fail(CanonicalJsonError::unsafe_integer);
            return std::nullopt;
        }
        const auto signed_value = negative
            ? -static_cast<std::int64_t>(magnitude)
            : static_cast<std::int64_t>(magnitude);
        return CanonicalJsonValue{signed_value};
    }

    [[nodiscard]] std::optional<CanonicalJsonValue> parse_array(const std::size_t depth)
    {
        ++index_;
        CanonicalJsonValue::Array values;
        skip_whitespace();
        if (index_ < input_.size() && input_[index_] == ']') {
            ++index_;
            return CanonicalJsonValue{std::move(values)};
        }
        for (;;) {
            skip_whitespace();
            auto value = parse_value(depth + 1);
            if (!value) return std::nullopt;
            values.push_back(std::move(*value));
            skip_whitespace();
            if (index_ >= input_.size()) {
                fail(CanonicalJsonError::invalid_json);
                return std::nullopt;
            }
            if (input_[index_] == ']') {
                ++index_;
                return CanonicalJsonValue{std::move(values)};
            }
            if (input_[index_++] != ',') {
                fail(CanonicalJsonError::invalid_json);
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] std::optional<CanonicalJsonValue> parse_object(const std::size_t depth)
    {
        ++index_;
        CanonicalJsonValue::Object values;
        skip_whitespace();
        if (index_ < input_.size() && input_[index_] == '}') {
            ++index_;
            return CanonicalJsonValue{std::move(values)};
        }
        for (;;) {
            skip_whitespace();
            auto key = parse_string();
            if (!key) return std::nullopt;
            skip_whitespace();
            if (index_ >= input_.size() || input_[index_++] != ':') {
                fail(CanonicalJsonError::invalid_json);
                return std::nullopt;
            }
            skip_whitespace();
            auto value = parse_value(depth + 1);
            if (!value) return std::nullopt;
            values.emplace_back(std::move(*key), std::move(*value));
            skip_whitespace();
            if (index_ >= input_.size()) {
                fail(CanonicalJsonError::invalid_json);
                return std::nullopt;
            }
            if (input_[index_] == '}') {
                ++index_;
                break;
            }
            if (input_[index_++] != ',') {
                fail(CanonicalJsonError::invalid_json);
                return std::nullopt;
            }
        }
        std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        for (std::size_t index = 1; index < values.size(); ++index) {
            if (values[index - 1].first == values[index].first) {
                fail(CanonicalJsonError::duplicate_key);
                return std::nullopt;
            }
        }
        return CanonicalJsonValue{std::move(values)};
    }

    [[nodiscard]] std::optional<CanonicalJsonValue> parse_value(const std::size_t depth)
    {
        if (depth > limits_.max_depth) {
            fail(CanonicalJsonError::depth_exceeded);
            return std::nullopt;
        }
        if (++value_count_ > limits_.max_values) {
            fail(CanonicalJsonError::value_limit_exceeded);
            return std::nullopt;
        }
        if (index_ >= input_.size()) {
            fail(CanonicalJsonError::invalid_json);
            return std::nullopt;
        }
        switch (input_[index_]) {
            case 'n':
                if (!consume("null")) return std::nullopt;
                return CanonicalJsonValue{};
            case 't':
                if (!consume("true")) return std::nullopt;
                return CanonicalJsonValue{true};
            case 'f':
                if (!consume("false")) return std::nullopt;
                return CanonicalJsonValue{false};
            case '"': {
                auto value = parse_string();
                if (!value) return std::nullopt;
                return CanonicalJsonValue{std::move(*value)};
            }
            case '[': return parse_array(depth);
            case '{': return parse_object(depth);
            default:
                if (input_[index_] == '-' || (input_[index_] >= '0' && input_[index_] <= '9'))
                    return parse_number();
                fail(CanonicalJsonError::invalid_json);
                return std::nullopt;
        }
    }

    std::string_view input_;
    CanonicalJsonLimits limits_;
    std::size_t index_{};
    std::size_t value_count_{};
    CanonicalJsonError error_{CanonicalJsonError::none};
    std::size_t error_offset_{};
};

class Writer final {
public:
    explicit Writer(const std::size_t limit) : limit_(limit) {}

    [[nodiscard]] bool append(const char value)
    {
        if (output_.size() >= limit_) return false;
        output_.push_back(value);
        return true;
    }

    [[nodiscard]] bool append(const std::string_view value)
    {
        if (value.size() > limit_ - output_.size()) return false;
        output_.append(value);
        return true;
    }

    [[nodiscard]] std::string take() { return std::move(output_); }

private:
    std::size_t limit_;
    std::string output_;
};

[[nodiscard]] bool append_json_string(Writer& output, const std::string_view value)
{
    if (!output.append('"')) return false;
    constexpr std::array<char, 16> hex{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (const auto raw : value) {
        const auto byte = static_cast<unsigned char>(raw);
        switch (byte) {
            case '"': if (!output.append("\\\"")) return false; break;
            case '\\': if (!output.append("\\\\")) return false; break;
            case '\b': if (!output.append("\\b")) return false; break;
            case '\f': if (!output.append("\\f")) return false; break;
            case '\n': if (!output.append("\\n")) return false; break;
            case '\r': if (!output.append("\\r")) return false; break;
            case '\t': if (!output.append("\\t")) return false; break;
            default:
                if (byte < 0x20U) {
                    const std::array<char, 6> escaped{
                        '\\', 'u', '0', '0', hex[byte >> 4U], hex[byte & 0x0FU]};
                    if (!output.append(std::string_view{escaped.data(), escaped.size()})) return false;
                } else if (!output.append(static_cast<char>(byte))) {
                    return false;
                }
        }
    }
    return output.append('"');
}

[[nodiscard]] CanonicalJsonError append_value(
    Writer& output,
    const CanonicalJsonValue& value,
    const CanonicalJsonLimits& limits,
    const std::size_t depth,
    std::size_t& value_count)
{
    if (depth > limits.max_depth) return CanonicalJsonError::depth_exceeded;
    if (++value_count > limits.max_values) return CanonicalJsonError::value_limit_exceeded;
    if (value.is_null()) return output.append("null")
        ? CanonicalJsonError::none : CanonicalJsonError::output_too_large;
    if (const auto* boolean = value.as_boolean()) return output.append(*boolean ? "true" : "false")
        ? CanonicalJsonError::none : CanonicalJsonError::output_too_large;
    if (const auto* integer = value.as_integer()) {
        if (*integer < minimum_safe_json_integer || *integer > maximum_safe_json_integer)
            return CanonicalJsonError::unsafe_integer;
        std::array<char, 32> buffer{};
        const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), *integer);
        if (result.ec != std::errc{}) return CanonicalJsonError::invalid_json;
        return output.append(std::string_view{buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())})
            ? CanonicalJsonError::none : CanonicalJsonError::output_too_large;
    }
    if (const auto* string = value.as_string()) {
        if (!valid_utf8_impl(*string)) return CanonicalJsonError::invalid_utf8;
        return append_json_string(output, *string)
            ? CanonicalJsonError::none : CanonicalJsonError::output_too_large;
    }
    if (const auto* array = value.as_array()) {
        if (!output.append('[')) return CanonicalJsonError::output_too_large;
        for (std::size_t index = 0; index < array->size(); ++index) {
            if (index != 0 && !output.append(',')) return CanonicalJsonError::output_too_large;
            const auto error = append_value(output, (*array)[index], limits, depth + 1, value_count);
            if (error != CanonicalJsonError::none) return error;
        }
        return output.append(']') ? CanonicalJsonError::none : CanonicalJsonError::output_too_large;
    }
    const auto* object = value.as_object();
    if (object == nullptr) return CanonicalJsonError::invalid_json;
    std::vector<const std::pair<std::string, CanonicalJsonValue>*> sorted;
    sorted.reserve(object->size());
    for (const auto& item : *object) {
        if (!valid_utf8_impl(item.first)) return CanonicalJsonError::invalid_utf8;
        sorted.push_back(&item);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto* left, const auto* right) {
        return left->first < right->first;
    });
    for (std::size_t index = 1; index < sorted.size(); ++index) {
        if (sorted[index - 1]->first == sorted[index]->first)
            return CanonicalJsonError::duplicate_key;
    }
    if (!output.append('{')) return CanonicalJsonError::output_too_large;
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (index != 0 && !output.append(',')) return CanonicalJsonError::output_too_large;
        if (!append_json_string(output, sorted[index]->first) || !output.append(':'))
            return CanonicalJsonError::output_too_large;
        const auto error = append_value(
            output, sorted[index]->second, limits, depth + 1, value_count);
        if (error != CanonicalJsonError::none) return error;
    }
    return output.append('}') ? CanonicalJsonError::none : CanonicalJsonError::output_too_large;
}

}  // namespace

bool CanonicalJsonValue::is_null() const noexcept
{
    return std::holds_alternative<std::monostate>(storage_);
}

const bool* CanonicalJsonValue::as_boolean() const noexcept
{
    return std::get_if<bool>(&storage_);
}

const std::int64_t* CanonicalJsonValue::as_integer() const noexcept
{
    return std::get_if<std::int64_t>(&storage_);
}

const std::string* CanonicalJsonValue::as_string() const noexcept
{
    return std::get_if<std::string>(&storage_);
}

const CanonicalJsonValue::Array* CanonicalJsonValue::as_array() const noexcept
{
    return std::get_if<Array>(&storage_);
}

const CanonicalJsonValue::Object* CanonicalJsonValue::as_object() const noexcept
{
    return std::get_if<Object>(&storage_);
}

const CanonicalJsonValue* CanonicalJsonValue::find(const std::string_view key) const noexcept
{
    const auto* object = as_object();
    if (object == nullptr) return nullptr;
    for (const auto& [candidate, value] : *object) {
        if (candidate == key) return &value;
    }
    return nullptr;
}

std::string_view canonical_json_error_name(const CanonicalJsonError error) noexcept
{
    using enum CanonicalJsonError;
    switch (error) {
        case none: return "none";
        case input_too_large: return "input_too_large";
        case output_too_large: return "output_too_large";
        case invalid_utf8: return "invalid_utf8";
        case invalid_json: return "invalid_json";
        case duplicate_key: return "duplicate_key";
        case unsupported_number: return "unsupported_number";
        case unsafe_integer: return "unsafe_integer";
        case depth_exceeded: return "depth_exceeded";
        case value_limit_exceeded: return "value_limit_exceeded";
        case resource_exhausted: return "resource_exhausted";
    }
    return "unknown";
}

CanonicalJsonParseResult parse_canonical_json_value(
    const std::string_view input, const CanonicalJsonLimits limits)
{
    if (limits.max_input_bytes == 0 || limits.max_output_bytes == 0
        || limits.max_depth == 0 || limits.max_values == 0) {
        return {std::nullopt, CanonicalJsonError::invalid_json, 0};
    }
    if (input.size() > limits.max_input_bytes)
        return {std::nullopt, CanonicalJsonError::input_too_large, 0};
    if (!valid_utf8_impl(input))
        return {std::nullopt, CanonicalJsonError::invalid_utf8, 0};
    try {
        return Parser{input, limits}.parse();
    } catch (const std::bad_alloc&) {
        return {std::nullopt, CanonicalJsonError::resource_exhausted, 0};
    }
}

CanonicalJsonEncodeResult encode_canonical_json_value(
    const CanonicalJsonValue& value, const CanonicalJsonLimits limits)
{
    if (limits.max_input_bytes == 0 || limits.max_output_bytes == 0
        || limits.max_depth == 0 || limits.max_values == 0) {
        return {{}, CanonicalJsonError::invalid_json};
    }
    try {
        Writer output{limits.max_output_bytes};
        std::size_t value_count = 0;
        const auto error = append_value(output, value, limits, 1, value_count);
        if (error != CanonicalJsonError::none) return {{}, error};
        return {output.take(), CanonicalJsonError::none};
    } catch (const std::bad_alloc&) {
        return {{}, CanonicalJsonError::resource_exhausted};
    }
}

bool is_valid_utf8(const std::string_view input) noexcept
{
    return valid_utf8_impl(input);
}

}  // namespace baas::service::auth
