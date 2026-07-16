#include "RuntimeRepositoryTreeFormat.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <variant>

namespace baas::runtime::repository::detail {
namespace {

constexpr std::string_view tree_manifest_schema =
    "baas.runtime-repository.tree-manifest/v1";
constexpr std::array<std::uint32_t, 64> sha256_constants{
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,
    0x923f82a4U,0xab1c5ed5U,0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,
    0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,0xe49b69c1U,0xefbe4786U,
    0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,
    0x06ca6351U,0x14292967U,0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,
    0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,0xa2bfe8a1U,0xa81a664bU,
    0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,
    0x5b9cca4fU,0x682e6ff3U,0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,
    0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};

[[noreturn]] void invalid(const std::string_view message) {
    throw TreeFormatError(std::string(message));
}

struct Json final {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    std::variant<std::string, Array, Object> value;
};

class JsonParser final {
public:
    JsonParser(const std::string_view input, const TreeFormatLimits& limits)
        : input_(input), string_limit_(std::max<std::size_t>(
              limits.max_relative_path_bytes, 256U)) {
        constexpr std::size_t nodes_per_entry = 6;
        constexpr std::size_t fixed_nodes = 16;
        if (limits.max_entries >
            (std::numeric_limits<std::size_t>::max() - fixed_nodes) / nodes_per_entry)
            invalid("tree manifest entry limit cannot be represented");
        node_limit_ = fixed_nodes + limits.max_entries * nodes_per_entry;
    }
    [[nodiscard]] Json parse() {
        auto result = value(0);
        whitespace();
        if (offset_ != input_.size()) invalid("trailing JSON data");
        return result;
    }

private:
    void whitespace() noexcept {
        while (offset_ < input_.size() &&
               (input_[offset_] == ' ' || input_[offset_] == '\n' ||
                input_[offset_] == '\r' || input_[offset_] == '\t')) ++offset_;
    }
    [[nodiscard]] char take() {
        if (offset_ == input_.size()) invalid("truncated JSON");
        return input_[offset_++];
    }
    [[nodiscard]] static unsigned hex_digit(const char value) {
        if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<unsigned>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F') return static_cast<unsigned>(value - 'A' + 10);
        invalid("invalid JSON Unicode escape");
    }
    [[nodiscard]] std::uint32_t unicode_unit() {
        std::uint32_t result{};
        for (int index = 0; index < 4; ++index) result = (result << 4U) | hex_digit(take());
        return result;
    }
    static void append_utf8(std::string& result, const std::uint32_t codepoint) {
        if (codepoint <= 0x7fU) result.push_back(static_cast<char>(codepoint));
        else if (codepoint <= 0x7ffU) {
            result.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else if (codepoint <= 0xffffU) {
            result.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else {
            result.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        }
    }
    void check_string(const std::string& result) {
        if (result.size() > string_limit_)
            invalid("JSON string byte limit exceeded");
    }
    void account_string(const std::string& result) {
        if (result.size() > input_.size() - decoded_string_bytes_)
            invalid("JSON decoded string budget exceeded");
        decoded_string_bytes_ += result.size();
    }
    void append_unicode(std::string& result) {
        const auto first = unicode_unit();
        if (first >= 0xdc00U && first <= 0xdfffU) invalid("isolated low surrogate");
        if (first < 0xd800U || first > 0xdbffU) {
            append_utf8(result, first);
            check_string(result);
            return;
        }
        if (take() != '\\' || take() != 'u')
            invalid("high surrogate is missing a low surrogate");
        const auto second = unicode_unit();
        if (second < 0xdc00U || second > 0xdfffU)
            invalid("high surrogate is followed by an invalid low surrogate");
        append_utf8(result, 0x10000U + ((first - 0xd800U) << 10U) + second - 0xdc00U);
        check_string(result);
    }
    [[nodiscard]] std::string string() {
        if (take() != '"') invalid("JSON string expected");
        std::string result;
        for (;;) {
            const auto value = take();
            if (value == '"') {
                account_string(result);
                return result;
            }
            if (static_cast<unsigned char>(value) < 0x20U) invalid("control byte in JSON");
            if (value != '\\') {
                result.push_back(value);
                check_string(result);
                continue;
            }
            const auto escaped = take();
            if (escaped == '"' || escaped == '\\' || escaped == '/') result.push_back(escaped);
            else if (escaped == 'b') result.push_back('\b');
            else if (escaped == 'f') result.push_back('\f');
            else if (escaped == 'n') result.push_back('\n');
            else if (escaped == 'r') result.push_back('\r');
            else if (escaped == 't') result.push_back('\t');
            else if (escaped == 'u') append_unicode(result);
            else invalid("invalid JSON escape");
            check_string(result);
        }
    }
    [[nodiscard]] Json value(const std::size_t depth) {
        if (depth > 6) invalid("JSON nesting limit exceeded");
        if (++nodes_ > node_limit_) invalid("JSON node limit exceeded");
        whitespace();
        if (offset_ == input_.size()) invalid("missing JSON value");
        if (input_[offset_] == '"') return Json{string()};
        if (input_[offset_] == '{') return Json{object(depth + 1)};
        if (input_[offset_] == '[') return Json{array(depth + 1)};
        invalid("unsupported JSON value");
    }
    [[nodiscard]] Json::Object object(const std::size_t depth) {
        static_cast<void>(take());
        Json::Object result;
        whitespace();
        if (offset_ < input_.size() && input_[offset_] == '}') { ++offset_; return result; }
        for (;;) {
            whitespace();
            auto key = string();
            whitespace();
            if (take() != ':') invalid("JSON member separator expected");
            if (!result.emplace(std::move(key), value(depth)).second) invalid("duplicate JSON member");
            whitespace();
            const auto separator = take();
            if (separator == '}') return result;
            if (separator != ',') invalid("JSON object delimiter expected");
        }
    }
    [[nodiscard]] Json::Array array(const std::size_t depth) {
        static_cast<void>(take());
        Json::Array result;
        whitespace();
        if (offset_ < input_.size() && input_[offset_] == ']') { ++offset_; return result; }
        for (;;) {
            result.push_back(value(depth));
            whitespace();
            const auto separator = take();
            if (separator == ']') return result;
            if (separator != ',') invalid("JSON array delimiter expected");
        }
    }
    std::string_view input_;
    std::size_t offset_{};
    std::size_t nodes_{};
    std::size_t node_limit_{};
    std::size_t string_limit_{};
    std::size_t decoded_string_bytes_{};
};

[[nodiscard]] const Json::Object& object(const Json& value) {
    if (const auto* result = std::get_if<Json::Object>(&value.value)) return *result;
    invalid("JSON object expected");
}
[[nodiscard]] const Json::Array& array(const Json& value) {
    if (const auto* result = std::get_if<Json::Array>(&value.value)) return *result;
    invalid("JSON array expected");
}
[[nodiscard]] const Json& member(const Json::Object& value, const std::string_view key) {
    const auto found = value.find(key);
    if (found == value.end()) invalid("required JSON member absent");
    return found->second;
}
[[nodiscard]] const std::string& string_member(
    const Json::Object& value, const std::string_view key) {
    if (const auto* result = std::get_if<std::string>(&member(value, key).value)) return *result;
    invalid("JSON string expected");
}
template <std::size_t Size>
void exact_members(const Json::Object& value, const std::array<std::string_view, Size>& names) {
    if (value.size() != names.size() ||
        !std::ranges::all_of(names, [&](const auto name) { return value.contains(name); }))
        invalid("unknown or missing JSON member");
}
[[nodiscard]] std::uintmax_t decimal_size(const std::string_view value) {
    if (value.empty() || (value.size() > 1 && value.front() == '0'))
        invalid("manifest entry size is not canonical");
    std::uintmax_t result{};
    for (const auto character : value) {
        if (character < '0' || character > '9' ||
            result > (std::numeric_limits<std::uintmax_t>::max() -
                      static_cast<unsigned>(character - '0')) / 10U)
            invalid("manifest entry size is invalid");
        result = result * 10U + static_cast<unsigned>(character - '0');
    }
    return result;
}

}  // namespace

void Sha256::update(const std::span<const std::byte> input) noexcept {
    for (const auto value : input) {
        block_[block_size_++] = value;
        ++total_bytes_;
        if (block_size_ == block_.size()) { transform(); block_size_ = 0; }
    }
}
void Sha256::update(const std::string_view input) noexcept {
    update(std::as_bytes(std::span(input.data(), input.size())));
}
Sha256Digest Sha256::finish() noexcept {
    const auto bits = total_bytes_ * 8U;
    block_[block_size_++] = std::byte{0x80};
    if (block_size_ > 56) {
        while (block_size_ < block_.size()) block_[block_size_++] = std::byte{};
        transform(); block_size_ = 0;
    }
    while (block_size_ < 56) block_[block_size_++] = std::byte{};
    for (std::size_t index = 0; index < 8; ++index)
        block_[63 - index] = static_cast<std::byte>(bits >> (index * 8U));
    transform();
    Sha256Digest result{};
    for (std::size_t word = 0; word < state_.size(); ++word)
        for (std::size_t byte = 0; byte < 4; ++byte)
            result[word * 4 + byte] =
                static_cast<std::byte>(state_[word] >> ((3U - byte) * 8U));
    return result;
}
void Sha256::transform() noexcept {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t index = 0; index < 16; ++index) {
        const auto offset = index * 4;
        schedule[index] = (std::to_integer<std::uint32_t>(block_[offset]) << 24U) |
            (std::to_integer<std::uint32_t>(block_[offset + 1]) << 16U) |
            (std::to_integer<std::uint32_t>(block_[offset + 2]) << 8U) |
            std::to_integer<std::uint32_t>(block_[offset + 3]);
    }
    for (std::size_t index = 16; index < schedule.size(); ++index) {
        const auto s0 = std::rotr(schedule[index - 15], 7) ^
            std::rotr(schedule[index - 15], 18) ^ (schedule[index - 15] >> 3U);
        const auto s1 = std::rotr(schedule[index - 2], 17) ^
            std::rotr(schedule[index - 2], 19) ^ (schedule[index - 2] >> 10U);
        schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
    }
    auto a=state_[0],b=state_[1],c=state_[2],d=state_[3];
    auto e=state_[4],f=state_[5],g=state_[6],h=state_[7];
    for (std::size_t index = 0; index < schedule.size(); ++index) {
        const auto s1 = std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25);
        const auto t1 = h+s1+((e&f)^(~e&g))+sha256_constants[index]+schedule[index];
        const auto s0 = std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22);
        const auto t2 = s0+((a&b)^(a&c)^(b&c));
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
    state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
}

std::string sha256_hex(const Sha256Digest& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t index = 0; index < digest.size(); ++index) {
        const auto value = std::to_integer<unsigned>(digest[index]);
        result[index * 2] = digits[value >> 4U];
        result[index * 2 + 1] = digits[value & 0x0fU];
    }
    return result;
}
std::string sha256_hex(const std::span<const std::byte> bytes) {
    Sha256 digest; digest.update(bytes); return sha256_hex(digest.finish());
}
bool lower_hex(const std::string_view value, const std::size_t length) noexcept {
    return value.size() == length && std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}
std::filesystem::path path_from_utf8(const std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

std::string portable_path_key(
    const std::string_view value, const TreeFormatLimits& limits) {
    if (value.empty() || value.size() > limits.max_relative_path_bytes ||
        value.front() == '/' || value.back() == '/' ||
        value.find('\\') != std::string_view::npos || value.find(':') != std::string_view::npos)
        invalid("manifest entry path is not portable");
    std::string key;
    key.reserve(value.size());
    std::size_t begin{};
    std::size_t depth{};
    while (begin < value.size()) {
        if (++depth > limits.max_relative_path_depth) invalid("manifest entry path is too deep");
        const auto end = value.find('/', begin);
        const auto component = value.substr(begin,
            end == std::string_view::npos ? value.size() - begin : end - begin);
        if (component.empty() || component == "." || component == ".." ||
            component.front() == ' ' || component.back() == '.' || component.back() == ' ')
            invalid("manifest entry path is not canonical");
        std::string folded;
        for (std::size_t offset = 0; offset < component.size();) {
            const auto first = static_cast<unsigned char>(component[offset]);
            std::uint32_t codepoint{};
            std::size_t width{};
            if (first < 0x80U) { codepoint = first; width = 1; }
            else if (first >= 0xc2U && first <= 0xdfU) { codepoint = first & 0x1fU; width = 2; }
            else if (first >= 0xe0U && first <= 0xefU) { codepoint = first & 0x0fU; width = 3; }
            else if (first >= 0xf0U && first <= 0xf4U) { codepoint = first & 0x07U; width = 4; }
            else invalid("manifest entry path is not valid UTF-8");
            if (offset + width > component.size()) invalid("manifest entry path is not valid UTF-8");
            for (std::size_t index = 1; index < width; ++index) {
                const auto continuation = static_cast<unsigned char>(component[offset + index]);
                if ((continuation & 0xc0U) != 0x80U) invalid("manifest entry path is not valid UTF-8");
                codepoint = (codepoint << 6U) | (continuation & 0x3fU);
            }
            const bool overlong = (width==2 && codepoint<0x80U) ||
                (width==3 && codepoint<0x800U) || (width==4 && codepoint<0x10000U);
            const bool combining = (codepoint>=0x0300U&&codepoint<=0x036fU) ||
                (codepoint>=0x1ab0U&&codepoint<=0x1affU) ||
                (codepoint>=0x1dc0U&&codepoint<=0x1dffU) ||
                (codepoint>=0x20d0U&&codepoint<=0x20ffU) ||
                (codepoint>=0xfe20U&&codepoint<=0xfe2fU) ||
                codepoint==0x3099U || codepoint==0x309aU ||
                (codepoint>=0x1100U&&codepoint<=0x11ffU) ||
                (codepoint>=0xa960U&&codepoint<=0xa97fU) ||
                (codepoint>=0xd7b0U&&codepoint<=0xd7ffU);
            if (overlong || codepoint>0x10ffffU || (codepoint>=0xd800U&&codepoint<=0xdfffU) ||
                codepoint==0x85U || codepoint==0x2028U || codepoint==0x2029U ||
                (codepoint>=0x7fU&&codepoint<=0x9fU) ||
                (codepoint>=0xfdd0U&&codepoint<=0xfdefU) ||
                (codepoint&0xffffU)==0xfffeU || (codepoint&0xffffU)==0xffffU || combining)
                invalid("manifest entry path is not portable normalized UTF-8");
            if (width == 1) {
                const auto character = static_cast<char>(first);
                if (first < 0x20U || character=='<' || character=='>' || character=='"' ||
                    character=='|' || character=='?' || character=='*')
                    invalid("manifest entry path contains a non-portable character");
                folded.push_back(character>='A'&&character<='Z'
                    ? static_cast<char>(character + ('a'-'A')) : character);
            } else folded.append(component.substr(offset, width));
            offset += width;
        }
        const auto dot = folded.find('.');
        const auto stem = folded.substr(0, dot);
        const bool reserved = stem=="con" || stem=="prn" || stem=="aux" || stem=="nul" ||
            (stem.size()==4 && (stem.starts_with("com")||stem.starts_with("lpt")) &&
             stem.back()>='1'&&stem.back()<='9') ||
            (stem.size()==5 && (stem.starts_with("com")||stem.starts_with("lpt")) &&
             (stem.ends_with("\xc2\xb9")||stem.ends_with("\xc2\xb2")||stem.ends_with("\xc2\xb3")));
        if (reserved) invalid("manifest entry path uses a reserved portable name");
        if (!key.empty()) key.push_back('/');
        key += folded;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return key;
}

std::vector<TreeManifestEntry> parse_tree_manifest(
    const std::string_view bytes, const std::string_view manifest_name,
    const TreeFormatLimits& limits) {
    const auto document = JsonParser(bytes, limits).parse();
    const auto& root = object(document);
    constexpr std::array root_names{std::string_view{"schema"}, std::string_view{"entries"}};
    exact_members(root, root_names);
    if (string_member(root, "schema") != tree_manifest_schema) invalid("invalid tree manifest schema");
    std::vector<TreeManifestEntry> result;
    std::map<std::string, std::string, std::less<>> portable_paths;
    portable_paths.emplace(portable_path_key(manifest_name, limits), manifest_name);
    std::uintmax_t total{};
    for (const auto& item : array(member(root, "entries"))) {
        if (result.size() == limits.max_entries) invalid("tree manifest entry limit exceeded");
        const auto& entry = object(item);
        constexpr std::array names{std::string_view{"path"},std::string_view{"size"},
            std::string_view{"sha256"},std::string_view{"mode"}};
        exact_members(entry, names);
        TreeManifestEntry parsed{string_member(entry,"path"),
            decimal_size(string_member(entry,"size")), string_member(entry,"sha256")};
        if (string_member(entry,"mode") != "file" || !lower_hex(parsed.sha256,64) ||
            parsed.path == manifest_name || parsed.size > limits.max_file_bytes ||
            parsed.size > limits.max_total_bytes - total)
            invalid("tree manifest entry is invalid or exceeds limits");
        total += parsed.size;
        const auto key = portable_path_key(parsed.path, limits);
        if (!portable_paths.emplace(key, parsed.path).second)
            invalid("tree manifest contains a portable path alias");
        result.push_back(std::move(parsed));
    }
    std::ranges::sort(result, {}, &TreeManifestEntry::path);
    if (std::adjacent_find(result.begin(), result.end(),
        [](const auto& left, const auto& right){return left.path==right.path;}) != result.end())
        invalid("tree manifest contains duplicate entries");
    return result;
}

}  // namespace baas::runtime::repository::detail
