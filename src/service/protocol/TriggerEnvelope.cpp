#include "service/protocol/TriggerEnvelope.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace baas::service::protocol::trigger {
namespace {

constexpr std::size_t hard_depth_limit = 256;

enum class JsonKind : std::uint8_t { null, boolean, number, string, array, object };

struct JsonValue {
    JsonKind kind{JsonKind::null};
    bool boolean{};
    std::string text;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;
};

[[nodiscard]] bool valid_limits_impl(const TriggerEnvelopeLimits& limits) noexcept
{
    return limits.max_input_json_bytes != 0 && limits.max_output_json_bytes != 0
        && limits.max_binary_bytes != 0 && limits.max_command_bytes != 0
        && limits.max_config_id_bytes != 0 && limits.max_depth != 0
        && limits.max_depth <= hard_depth_limit && limits.max_nodes != 0
        && limits.max_string_bytes != 0 && limits.max_work != 0;
}

[[nodiscard]] bool is_continuation(const unsigned char byte) noexcept
{
    return (byte & 0xC0U) == 0x80U;
}

[[nodiscard]] bool is_valid_utf8(const std::string_view value) noexcept
{
    std::size_t index = 0;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU) {
            if (index + 1 >= value.size()
                || !is_continuation(static_cast<unsigned char>(value[index + 1]))) {
                return false;
            }
            index += 2;
            continue;
        }
        if (first >= 0xE0U && first <= 0xEFU) {
            if (index + 2 >= value.size()) return false;
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            if (!is_continuation(second) || !is_continuation(third)) return false;
            if ((first == 0xE0U && second < 0xA0U)
                || (first == 0xEDU && second >= 0xA0U)) {
                return false;
            }
            index += 3;
            continue;
        }
        if (first >= 0xF0U && first <= 0xF4U) {
            if (index + 3 >= value.size()) return false;
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            const auto fourth = static_cast<unsigned char>(value[index + 3]);
            if (!is_continuation(second) || !is_continuation(third)
                || !is_continuation(fourth)) {
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

[[nodiscard]] bool is_command_name(const std::string_view value) noexcept
{
    if (value.empty() || value.front() < 'a' || value.front() > 'z') return false;
    return std::all_of(value.begin() + 1, value.end(), [](const char character) {
        return (character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9') || character == '_';
    });
}

class Parser final {
public:
    Parser(const std::string_view input, const TriggerEnvelopeLimits& limits)
        : input_(input), limits_(limits)
    {}

    [[nodiscard]] std::optional<JsonValue> parse()
    {
        skip_whitespace();
        auto value = parse_value(1);
        if (!value) return std::nullopt;
        skip_whitespace();
        if (!failed_ && index_ != input_.size()) fail(EnvelopeError::invalid_json);
        if (failed_) return std::nullopt;
        return value;
    }

    [[nodiscard]] EnvelopeError error() const noexcept { return error_; }
    [[nodiscard]] std::size_t error_offset() const noexcept { return error_offset_; }

private:
    void fail(const EnvelopeError error)
    {
        if (failed_) return;
        failed_ = true;
        error_ = error;
        error_offset_ = index_;
    }

    [[nodiscard]] bool advance()
    {
        if (index_ >= input_.size()) {
            fail(EnvelopeError::invalid_json);
            return false;
        }
        if (work_ >= limits_.max_work) {
            fail(EnvelopeError::work_limit);
            return false;
        }
        ++index_;
        ++work_;
        return true;
    }

    [[nodiscard]] bool take(const char expected)
    {
        if (index_ >= input_.size() || input_[index_] != expected) {
            fail(EnvelopeError::invalid_json);
            return false;
        }
        return advance();
    }

    [[nodiscard]] bool literal(const std::string_view expected)
    {
        for (const char character : expected) {
            if (index_ >= input_.size() || input_[index_] != character || !advance()) {
                if (!failed_) fail(EnvelopeError::invalid_json);
                return false;
            }
        }
        return true;
    }

    void skip_whitespace()
    {
        while (!failed_ && index_ < input_.size()) {
            const char character = input_[index_];
            if (character != ' ' && character != '\t' && character != '\r'
                && character != '\n') {
                break;
            }
            static_cast<void>(advance());
        }
    }

    [[nodiscard]] bool can_append_string(
        const std::size_t current, const std::size_t additional)
    {
        const auto remaining = limits_.max_string_bytes
            - std::min(string_bytes_, limits_.max_string_bytes);
        if (current > remaining || additional > remaining - current) {
            fail(EnvelopeError::string_limit);
            return false;
        }
        return true;
    }

    [[nodiscard]] static int hex_value(const char character) noexcept
    {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    }

    [[nodiscard]] std::optional<std::uint32_t> unicode_escape()
    {
        std::uint32_t value = 0;
        for (int digit = 0; digit < 4; ++digit) {
            if (index_ >= input_.size()) {
                fail(EnvelopeError::invalid_json);
                return std::nullopt;
            }
            const int decoded = hex_value(input_[index_]);
            if (decoded < 0) {
                fail(EnvelopeError::invalid_json);
                return std::nullopt;
            }
            value = (value << 4U) | static_cast<std::uint32_t>(decoded);
            if (!advance()) return std::nullopt;
        }
        return value;
    }

    static void append_code_point(std::string& output, const std::uint32_t value)
    {
        if (value <= 0x7FU) {
            output.push_back(static_cast<char>(value));
        } else if (value <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else if (value <= 0xFFFFU) {
            output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else {
            output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
    }

    [[nodiscard]] std::optional<std::string> parse_string()
    {
        if (!take('"')) return std::nullopt;
        std::string output;
        while (!failed_ && index_ < input_.size()) {
            const auto byte = static_cast<unsigned char>(input_[index_]);
            if (byte == '"') {
                static_cast<void>(advance());
                string_bytes_ += output.size();
                return output;
            }
            if (byte < 0x20U) {
                fail(EnvelopeError::invalid_json);
                return std::nullopt;
            }
            if (byte != '\\') {
                if (!can_append_string(output.size(), 1)) return std::nullopt;
                output.push_back(static_cast<char>(byte));
                if (!advance()) return std::nullopt;
                continue;
            }

            static_cast<void>(advance());
            if (failed_ || index_ >= input_.size()) {
                fail(EnvelopeError::invalid_json);
                return std::nullopt;
            }
            const char escaped = input_[index_];
            if (!advance()) return std::nullopt;
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                case 'b':
                case 'f':
                case 'n':
                case 'r':
                case 't': {
                    if (!can_append_string(output.size(), 1)) return std::nullopt;
                    switch (escaped) {
                        case '"': output.push_back('"'); break;
                        case '\\': output.push_back('\\'); break;
                        case '/': output.push_back('/'); break;
                        case 'b': output.push_back('\b'); break;
                        case 'f': output.push_back('\f'); break;
                        case 'n': output.push_back('\n'); break;
                        case 'r': output.push_back('\r'); break;
                        case 't': output.push_back('\t'); break;
                        default: break;
                    }
                    break;
                }
                case 'u': {
                    auto first = unicode_escape();
                    if (!first) return std::nullopt;
                    std::uint32_t code_point = *first;
                    if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
                        if (index_ + 2 > input_.size() || input_[index_] != '\\'
                            || input_[index_ + 1] != 'u') {
                            fail(EnvelopeError::invalid_json);
                            return std::nullopt;
                        }
                        if (!advance() || !advance()) return std::nullopt;
                        const auto second = unicode_escape();
                        if (!second || *second < 0xDC00U || *second > 0xDFFFU) {
                            fail(EnvelopeError::invalid_json);
                            return std::nullopt;
                        }
                        code_point = 0x10000U
                            + ((code_point - 0xD800U) << 10U) + (*second - 0xDC00U);
                    } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
                        fail(EnvelopeError::invalid_json);
                        return std::nullopt;
                    }
                    const std::size_t encoded_bytes = code_point <= 0x7FU ? 1
                        : code_point <= 0x7FFU ? 2 : code_point <= 0xFFFFU ? 3 : 4;
                    if (!can_append_string(output.size(), encoded_bytes))
                        return std::nullopt;
                    append_code_point(output, code_point);
                    break;
                }
                default:
                    fail(EnvelopeError::invalid_json);
                    return std::nullopt;
            }
        }
        fail(EnvelopeError::invalid_json);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> parse_number()
    {
        const auto start = index_;
        if (index_ < input_.size() && input_[index_] == '-' && !advance())
            return std::nullopt;
        if (index_ >= input_.size()) {
            fail(EnvelopeError::invalid_json);
            return std::nullopt;
        }
        if (input_[index_] == '0') {
            if (!advance()) return std::nullopt;
            if (index_ < input_.size() && input_[index_] >= '0' && input_[index_] <= '9') {
                fail(EnvelopeError::invalid_json);
                return std::nullopt;
            }
        } else if (input_[index_] >= '1' && input_[index_] <= '9') {
            do {
                if (!advance()) return std::nullopt;
            } while (index_ < input_.size() && input_[index_] >= '0'
                     && input_[index_] <= '9');
        } else {
            fail(EnvelopeError::invalid_json);
            return std::nullopt;
        }
        if (index_ < input_.size() && input_[index_] == '.') {
            if (!advance() || index_ >= input_.size() || input_[index_] < '0'
                || input_[index_] > '9') {
                fail(EnvelopeError::invalid_json);
                return std::nullopt;
            }
            do {
                if (!advance()) return std::nullopt;
            } while (index_ < input_.size() && input_[index_] >= '0'
                     && input_[index_] <= '9');
        }
        if (index_ < input_.size() && (input_[index_] == 'e' || input_[index_] == 'E')) {
            if (!advance()) return std::nullopt;
            if (index_ < input_.size() && (input_[index_] == '+' || input_[index_] == '-')) {
                if (!advance()) return std::nullopt;
            }
            if (index_ >= input_.size() || input_[index_] < '0' || input_[index_] > '9') {
                fail(EnvelopeError::invalid_json);
                return std::nullopt;
            }
            do {
                if (!advance()) return std::nullopt;
            } while (index_ < input_.size() && input_[index_] >= '0'
                     && input_[index_] <= '9');
        }
        return std::string{input_.substr(start, index_ - start)};
    }

    [[nodiscard]] std::optional<JsonValue> parse_array(const std::size_t depth)
    {
        JsonValue result;
        result.kind = JsonKind::array;
        if (!take('[')) return std::nullopt;
        skip_whitespace();
        if (!failed_ && index_ < input_.size() && input_[index_] == ']') {
            static_cast<void>(advance());
            return result;
        }
        while (!failed_) {
            auto value = parse_value(depth + 1);
            if (!value) return std::nullopt;
            result.array.push_back(std::move(*value));
            skip_whitespace();
            if (index_ < input_.size() && input_[index_] == ']') {
                static_cast<void>(advance());
                return result;
            }
            if (!take(',')) return std::nullopt;
            skip_whitespace();
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<JsonValue> parse_object(const std::size_t depth)
    {
        JsonValue result;
        result.kind = JsonKind::object;
        std::unordered_set<std::string> keys;
        if (!take('{')) return std::nullopt;
        skip_whitespace();
        if (!failed_ && index_ < input_.size() && input_[index_] == '}') {
            static_cast<void>(advance());
            return result;
        }
        while (!failed_) {
            if (index_ >= input_.size() || input_[index_] != '"') {
                fail(EnvelopeError::invalid_json);
                return std::nullopt;
            }
            auto key = parse_string();
            if (!key) return std::nullopt;
            if (!keys.insert(*key).second) {
                fail(EnvelopeError::duplicate_key);
                return std::nullopt;
            }
            skip_whitespace();
            if (!take(':')) return std::nullopt;
            skip_whitespace();
            auto value = parse_value(depth + 1);
            if (!value) return std::nullopt;
            result.object.emplace_back(std::move(*key), std::move(*value));
            skip_whitespace();
            if (index_ < input_.size() && input_[index_] == '}') {
                static_cast<void>(advance());
                return result;
            }
            if (!take(',')) return std::nullopt;
            skip_whitespace();
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<JsonValue> parse_value(const std::size_t depth)
    {
        if (depth > limits_.max_depth) {
            fail(EnvelopeError::depth_limit);
            return std::nullopt;
        }
        if (nodes_ >= limits_.max_nodes) {
            fail(EnvelopeError::node_limit);
            return std::nullopt;
        }
        ++nodes_;
        if (index_ >= input_.size()) {
            fail(EnvelopeError::invalid_json);
            return std::nullopt;
        }
        switch (input_[index_]) {
            case 'n':
                if (!literal("null")) return std::nullopt;
                return JsonValue{};
            case 't': {
                if (!literal("true")) return std::nullopt;
                JsonValue value;
                value.kind = JsonKind::boolean;
                value.boolean = true;
                return value;
            }
            case 'f': {
                if (!literal("false")) return std::nullopt;
                JsonValue value;
                value.kind = JsonKind::boolean;
                return value;
            }
            case '"': {
                auto string = parse_string();
                if (!string) return std::nullopt;
                JsonValue value;
                value.kind = JsonKind::string;
                value.text = std::move(*string);
                return value;
            }
            case '[': return parse_array(depth);
            case '{': return parse_object(depth);
            default: {
                auto number = parse_number();
                if (!number) return std::nullopt;
                JsonValue value;
                value.kind = JsonKind::number;
                value.text = std::move(*number);
                return value;
            }
        }
    }

    std::string_view input_;
    const TriggerEnvelopeLimits& limits_;
    std::size_t index_{};
    std::size_t nodes_{};
    std::size_t string_bytes_{};
    std::size_t work_{};
    EnvelopeError error_{EnvelopeError::invalid_json};
    std::size_t error_offset_{};
    bool failed_{};
};

class BoundedWriter final {
public:
    explicit BoundedWriter(const std::size_t limit) : limit_(limit)
    {
        output_.reserve(std::min(limit, std::size_t{4'096}));
    }

    [[nodiscard]] bool append(const std::string_view value)
    {
        if (exceeded_) return false;
        if (value.size() > limit_ - output_.size()) {
            exceeded_ = true;
            return false;
        }
        output_.append(value);
        return true;
    }

    [[nodiscard]] bool append(const char value)
    {
        return append(std::string_view{&value, 1});
    }

    [[nodiscard]] bool exceeded() const noexcept { return exceeded_; }
    [[nodiscard]] std::string take() { return std::move(output_); }

private:
    std::size_t limit_;
    std::string output_;
    bool exceeded_{};
};

[[nodiscard]] char hex_digit(const unsigned int value) noexcept
{
    return value < 10U ? static_cast<char>('0' + value)
                       : static_cast<char>('A' + value - 10U);
}

[[nodiscard]] bool append_json_string(BoundedWriter& output, const std::string_view value)
{
    if (!output.append('"')) return false;
    for (const unsigned char byte : value) {
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
                    const char escaped[] = {
                        '\\', 'u', '0', '0', hex_digit((byte >> 4U) & 0x0FU),
                        hex_digit(byte & 0x0FU)};
                    if (!output.append(std::string_view{escaped, sizeof(escaped)}))
                        return false;
                } else if (!output.append(static_cast<char>(byte))) {
                    return false;
                }
                break;
        }
    }
    return output.append('"');
}

[[nodiscard]] bool append_json(BoundedWriter& output, const JsonValue& value);

[[nodiscard]] bool append_object(BoundedWriter& output, const JsonValue& value)
{
    if (!output.append('{')) return false;
    for (std::size_t index = 0; index < value.object.size(); ++index) {
        if ((index != 0 && !output.append(','))
            || !append_json_string(output, value.object[index].first)
            || !output.append(':') || !append_json(output, value.object[index].second)) {
            return false;
        }
    }
    return output.append('}');
}

[[nodiscard]] bool append_json(BoundedWriter& output, const JsonValue& value)
{
    switch (value.kind) {
        case JsonKind::null: return output.append("null");
        case JsonKind::boolean: return output.append(value.boolean ? "true" : "false");
        case JsonKind::number: return output.append(value.text);
        case JsonKind::string: return append_json_string(output, value.text);
        case JsonKind::array:
            if (!output.append('[')) return false;
            for (std::size_t index = 0; index < value.array.size(); ++index) {
                if ((index != 0 && !output.append(','))
                    || !append_json(output, value.array[index])) {
                    return false;
                }
            }
            return output.append(']');
        case JsonKind::object: return append_object(output, value);
    }
    return false;
}

[[nodiscard]] const JsonValue* member(
    const JsonValue& object, const std::string_view name) noexcept
{
    const auto iterator = std::find_if(
        object.object.begin(), object.object.end(), [name](const auto& entry) {
            return entry.first == name;
        });
    return iterator == object.object.end() ? nullptr : &iterator->second;
}

[[nodiscard]] bool has_member(const JsonValue& object, const std::string_view name) noexcept
{
    return member(object, name) != nullptr;
}

[[nodiscard]] std::optional<JsonValue> parse_json(
    const std::string_view input,
    const TriggerEnvelopeLimits& limits,
    const std::size_t byte_limit,
    EnvelopeError& error,
    std::size_t& error_offset)
{
    if (input.size() > byte_limit) {
        error = EnvelopeError::input_too_large;
        error_offset = byte_limit;
        return std::nullopt;
    }
    if (!is_valid_utf8(input)) {
        error = EnvelopeError::invalid_utf8;
        error_offset = 0;
        return std::nullopt;
    }
    Parser parser{input, limits};
    auto result = parser.parse();
    if (!result) {
        error = parser.error();
        error_offset = parser.error_offset();
    }
    return result;
}

[[nodiscard]] std::optional<Timestamp> decode_timestamp(const JsonValue& value) noexcept
{
    if (value.kind != JsonKind::number || value.text.empty()
        || value.text.front() == '-' || value.text.find_first_of(".eE") != std::string::npos) {
        return std::nullopt;
    }
    Timestamp result{};
    const auto [end, error] = std::from_chars(
        value.text.data(), value.text.data() + value.text.size(), result);
    if (error != std::errc{} || end != value.text.data() + value.text.size()
        || result > maximum_safe_timestamp) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<std::string> serialize_json(
    const JsonValue& value, const std::size_t limit)
{
    BoundedWriter output{limit};
    if (!append_json(output, value) || output.exceeded()) return std::nullopt;
    return output.take();
}

[[nodiscard]] EncodeResponseResult encode_failure(
    const EnvelopeError error, const std::size_t offset = 0)
{
    EncodeResponseResult result;
    result.error = error;
    result.error_offset = offset;
    return result;
}

}  // namespace

bool valid_trigger_envelope_limits(const TriggerEnvelopeLimits& limits) noexcept
{
    return valid_limits_impl(limits);
}

std::string_view envelope_error_name(const EnvelopeError error) noexcept
{
    using enum EnvelopeError;
    switch (error) {
        case none: return "none";
        case invalid_limits: return "invalid_limits";
        case input_too_large: return "input_too_large";
        case invalid_utf8: return "invalid_utf8";
        case invalid_json: return "invalid_json";
        case depth_limit: return "depth_limit";
        case node_limit: return "node_limit";
        case string_limit: return "string_limit";
        case work_limit: return "work_limit";
        case duplicate_key: return "duplicate_key";
        case root_not_object: return "root_not_object";
        case missing_type: return "missing_type";
        case invalid_type: return "invalid_type";
        case missing_command: return "missing_command";
        case invalid_command: return "invalid_command";
        case missing_timestamp: return "missing_timestamp";
        case invalid_timestamp: return "invalid_timestamp";
        case invalid_config_id: return "invalid_config_id";
        case invalid_payload: return "invalid_payload";
        case binary_presence_mismatch: return "binary_presence_mismatch";
        case invalid_data: return "invalid_data";
        case invalid_error: return "invalid_error";
        case invalid_terminal: return "invalid_terminal";
        case reserved_binary_field: return "reserved_binary_field";
        case binary_too_large: return "binary_too_large";
        case output_too_large: return "output_too_large";
    }
    return "unknown";
}

DecodeCommandResult decode_command_envelope(
    const std::string_view json, const TriggerEnvelopeLimits limits)
{
    DecodeCommandResult result;
    if (!valid_trigger_envelope_limits(limits)) {
        result.error = EnvelopeError::invalid_limits;
        return result;
    }

    EnvelopeError parse_error{EnvelopeError::none};
    auto root = parse_json(
        json, limits, limits.max_input_json_bytes, parse_error, result.error_offset);
    if (!root) {
        result.error = parse_error;
        return result;
    }
    if (root->kind != JsonKind::object) {
        result.error = EnvelopeError::root_not_object;
        return result;
    }

    const auto* type = member(*root, "type");
    if (!type) {
        result.error = EnvelopeError::missing_type;
        return result;
    }
    if (type->kind != JsonKind::string || type->text != "command") {
        result.error = EnvelopeError::invalid_type;
        return result;
    }

    const auto* command = member(*root, "command");
    if (!command) {
        result.error = EnvelopeError::missing_command;
        return result;
    }
    if (command->kind != JsonKind::string
        || command->text.size() > limits.max_command_bytes
        || !is_command_name(command->text)) {
        result.error = EnvelopeError::invalid_command;
        return result;
    }

    const auto* timestamp = member(*root, "timestamp");
    if (!timestamp) {
        result.error = EnvelopeError::missing_timestamp;
        return result;
    }
    const auto decoded_timestamp = decode_timestamp(*timestamp);
    if (!decoded_timestamp) {
        result.error = EnvelopeError::invalid_timestamp;
        return result;
    }

    const auto* config_id = member(*root, "config_id");
    if (config_id && config_id->kind != JsonKind::null) {
        if (config_id->kind != JsonKind::string
            || config_id->text.size() > limits.max_config_id_bytes) {
            result.error = EnvelopeError::invalid_config_id;
            return result;
        }
        result.envelope.config_id = config_id->text;
    }

    const JsonValue empty_payload{JsonKind::object};
    const auto* payload = member(*root, "payload");
    if (!payload) payload = &empty_payload;
    if (payload->kind != JsonKind::object) {
        result.error = EnvelopeError::invalid_payload;
        return result;
    }
    auto encoded_payload = serialize_json(*payload, limits.max_input_json_bytes);
    if (!encoded_payload) {
        result.error = EnvelopeError::input_too_large;
        return result;
    }

    result.envelope.command = command->text;
    result.envelope.timestamp = *decoded_timestamp;
    result.envelope.payload_json = std::move(*encoded_payload);
    if (const auto* binary = member(*payload, "binary")) {
        result.envelope.declares_binary =
            binary->kind == JsonKind::boolean && binary->boolean;
    }
    return result;
}

BuildAdmissionResult make_admission(
    const CommandEnvelope& envelope,
    const std::optional<std::size_t> binary_frame_bytes,
    const ResponseMode mode)
{
    if (binary_frame_bytes.has_value() != envelope.declares_binary) {
        return {{}, EnvelopeError::binary_presence_mismatch};
    }
    return {{
        envelope.command,
        envelope.timestamp,
        envelope.config_id,
        envelope.payload_json.size(),
        binary_frame_bytes.value_or(0),
        mode,
    }, EnvelopeError::none};
}

EncodeResponseResult encode_command_response(
    CommandResponse response, const TriggerEnvelopeLimits limits)
{
    if (!valid_trigger_envelope_limits(limits)) {
        return encode_failure(EnvelopeError::invalid_limits);
    }
    if (response.command.size() > limits.max_command_bytes
        || !is_command_name(response.command)) {
        return encode_failure(EnvelopeError::invalid_command);
    }
    if (response.timestamp > maximum_safe_timestamp)
        return encode_failure(EnvelopeError::invalid_timestamp);
    if (!response.error.empty() && (!is_valid_utf8(response.error)
        || response.error.size() > limits.max_string_bytes)) {
        return encode_failure(EnvelopeError::invalid_error);
    }
    if (response.status == ResponseStatus::ok && !response.error.empty())
        return encode_failure(EnvelopeError::invalid_error);
    if (response.status != ResponseStatus::ok && response.error.empty())
        return encode_failure(EnvelopeError::invalid_error);
    if ((response.response_mode == ResponseMode::single && !response.terminal)
        || (response.status != ResponseStatus::ok && !response.terminal)) {
        return encode_failure(EnvelopeError::invalid_terminal);
    }
    if (response.binary && response.binary->size() > limits.max_binary_bytes)
        return encode_failure(EnvelopeError::binary_too_large);

    std::optional<JsonValue> data;
    if (response.data_json) {
        EnvelopeError parse_error{EnvelopeError::none};
        std::size_t offset{};
        data = parse_json(
            *response.data_json, limits, limits.max_output_json_bytes,
            parse_error, offset);
        if (!data) {
            return encode_failure(
                parse_error == EnvelopeError::input_too_large
                    ? EnvelopeError::output_too_large : parse_error,
                offset);
        }
    }
    if (response.response_mode == ResponseMode::stream
        && response.status == ResponseStatus::ok) {
        if (data && data->kind == JsonKind::object && has_member(*data, "done"))
            return encode_failure(EnvelopeError::invalid_terminal);
        if (response.terminal) {
            if (!data) {
                data = JsonValue{};
                data->kind = JsonKind::object;
            }
            if (data->kind != JsonKind::object)
                return encode_failure(EnvelopeError::invalid_terminal);
            JsonValue done;
            done.kind = JsonKind::boolean;
            done.boolean = true;
            data->object.emplace_back("done", std::move(done));
        }
    }
    if (response.binary) {
        if (!data) {
            data = JsonValue{};
            data->kind = JsonKind::object;
        }
        if (data->kind != JsonKind::object)
            return encode_failure(EnvelopeError::invalid_data);
        if (has_member(*data, "binary"))
            return encode_failure(EnvelopeError::reserved_binary_field);

        JsonValue size;
        size.kind = JsonKind::number;
        size.text = std::to_string(response.binary->size());
        JsonValue declaration;
        declaration.kind = JsonKind::object;
        declaration.object.emplace_back("size", std::move(size));
        data->object.emplace_back("binary", std::move(declaration));
    } else if (data && data->kind == JsonKind::object && has_member(*data, "binary")) {
        // Tauri treats any data.binary value as a promise that the next frame is
        // bytes. Only the codec may produce that reserved declaration.
        return encode_failure(EnvelopeError::reserved_binary_field);
    }

    BoundedWriter output{limits.max_output_json_bytes};
    if (!output.append(R"({"type":"command_response","command":)")
        || !append_json_string(output, response.command)
        || !output.append(R"(,"status":)")
        || !append_json_string(
            output, response.status == ResponseStatus::ok ? "ok" : "error")) {
        return encode_failure(EnvelopeError::output_too_large);
    }
    if (response.status != ResponseStatus::ok
        && (!output.append(R"(,"error":)")
            || !append_json_string(output, response.error))) {
        return encode_failure(EnvelopeError::output_too_large);
    }
    if (data && (!output.append(R"(,"data":)") || !append_json(output, *data)))
        return encode_failure(EnvelopeError::output_too_large);
    if (!output.append(R"(,"timestamp":)")
        || !output.append(std::to_string(response.timestamp)) || !output.append('}')
        || output.exceeded()) {
        return encode_failure(EnvelopeError::output_too_large);
    }

    EncodeResponseResult result;
    const bool has_binary = response.binary.has_value();
    std::vector<std::byte> binary;
    if (response.binary) binary = std::move(*response.binary);
    result.batch = OutboundBatch{
        std::move(response.command), response.timestamp, response.status,
        response.response_mode, response.terminal, output.take(), has_binary,
        std::move(binary)};
    return result;
}

}  // namespace baas::service::protocol::trigger
