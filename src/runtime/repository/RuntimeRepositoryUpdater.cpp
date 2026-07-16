#include "runtime/repository/RuntimeRepositoryUpdater.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <cstdio>
#endif
#include <unistd.h>
#endif

namespace baas::runtime::repository {

std::string_view runtime_repository_update_error_message(
    const RuntimeRepositoryUpdateErrorCode code) noexcept {
    switch (code) {
    case RuntimeRepositoryUpdateErrorCode::Busy:
        return "runtime repository update is already in progress";
    case RuntimeRepositoryUpdateErrorCode::Cancelled:
        return "runtime repository update was cancelled";
    case RuntimeRepositoryUpdateErrorCode::InvalidPlan:
        return "runtime repository update plan is invalid";
    case RuntimeRepositoryUpdateErrorCode::FetchFailed:
        return "runtime repository fetch failed";
    case RuntimeRepositoryUpdateErrorCode::CommitMismatch:
        return "runtime repository commit verification failed";
    case RuntimeRepositoryUpdateErrorCode::ValidationFailed:
        return "runtime repository validation failed";
    case RuntimeRepositoryUpdateErrorCode::CurrentConflict:
        return "runtime repository current generation conflict";
    case RuntimeRepositoryUpdateErrorCode::NoPrevious:
        return "previous runtime repository generation is unavailable";
    case RuntimeRepositoryUpdateErrorCode::Io:
        return "runtime repository storage operation failed";
    case RuntimeRepositoryUpdateErrorCode::RecoveryFailed:
        return "runtime repository recovery failed";
    }
    return "runtime repository update failed";
}

namespace {

constexpr std::string_view journal_schema = "baas.runtime-repositories.publish-journal/v1";
constexpr std::string_view tree_manifest_schema = "baas.runtime-repository.tree-manifest/v1";
constexpr std::size_t max_state_json_bytes = 64U * 1024U;
using Digest = std::array<std::byte, 32>;

#ifdef BAAS_RUNTIME_REPOSITORY_UPDATER_TESTING
std::atomic<RuntimeRepositoryValidationReadHook> validation_read_hook{};
#endif

class UpdateFailure final : public std::runtime_error {
  public:
    UpdateFailure(const RuntimeRepositoryUpdateErrorCode code, std::string message,
                  const PublishDisposition disposition = PublishDisposition::NotCommitted)
        : std::runtime_error(std::move(message)), code_(code), disposition_(disposition) {}
    [[nodiscard]] RuntimeRepositoryUpdateErrorCode code() const noexcept { return code_; }
    [[nodiscard]] PublishDisposition disposition() const noexcept { return disposition_; }

  private:
    RuntimeRepositoryUpdateErrorCode code_;
    PublishDisposition disposition_;
};

[[noreturn]] void fail(const RuntimeRepositoryUpdateErrorCode code, std::string message) {
    throw UpdateFailure(code, std::move(message));
}

constexpr std::array<std::uint32_t, 64> sha256_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U,
};

class Sha256 final {
  public:
    void update(const std::span<const std::byte> input) noexcept {
        for (const auto value : input) {
            block_[block_size_++] = value;
            ++total_bytes_;
            if (block_size_ == block_.size()) {
                transform();
                block_size_ = 0;
            }
        }
    }

    void update(const std::string_view input) noexcept {
        update(std::as_bytes(std::span(input.data(), input.size())));
    }

    [[nodiscard]] Digest finish() noexcept {
        const auto bits = total_bytes_ * 8U;
        block_[block_size_++] = std::byte{0x80};
        if (block_size_ > 56) {
            while (block_size_ < block_.size())
                block_[block_size_++] = std::byte{0};
            transform();
            block_size_ = 0;
        }
        while (block_size_ < 56)
            block_[block_size_++] = std::byte{0};
        for (std::size_t index = 0; index < 8; ++index)
            block_[63 - index] = static_cast<std::byte>(bits >> (index * 8U));
        transform();
        Digest result{};
        for (std::size_t word = 0; word < state_.size(); ++word)
            for (std::size_t byte = 0; byte < 4; ++byte)
                result[word * 4 + byte] =
                    static_cast<std::byte>(state_[word] >> ((3U - byte) * 8U));
        return result;
    }

  private:
    void transform() noexcept {
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
        auto a = state_[0];
        auto b = state_[1];
        auto c = state_[2];
        auto d = state_[3];
        auto e = state_[4];
        auto f = state_[5];
        auto g = state_[6];
        auto h = state_[7];
        for (std::size_t index = 0; index < schedule.size(); ++index) {
            const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto t1 =
                h + s1 + ((e & f) ^ (~e & g)) + sha256_constants[index] + schedule[index];
            const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::byte, 64> block_{};
    std::size_t block_size_{};
    std::uint64_t total_bytes_{};
};

[[nodiscard]] std::string hex(const Digest& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t index = 0; index < digest.size(); ++index) {
        const auto value = std::to_integer<unsigned int>(digest[index]);
        result[index * 2] = digits[value >> 4U];
        result[index * 2 + 1] = digits[value & 0x0fU];
    }
    return result;
}

void add_length_prefixed(Sha256& digest, const std::string_view value) noexcept {
    std::array<std::byte, 8> length{};
    const auto size = static_cast<std::uint64_t>(value.size());
    for (std::size_t index = 0; index < length.size(); ++index)
        length[7 - index] = static_cast<std::byte>(size >> (index * 8U));
    digest.update(length);
    digest.update(value);
}

[[nodiscard]] bool lower_hex(const std::string_view value, const std::size_t length) noexcept {
    return value.size() == length && std::ranges::all_of(value, [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] bool valid_commit(const std::string_view value) noexcept {
    return lower_hex(value, 40) || lower_hex(value, 64);
}

[[nodiscard]] bool valid_manifest(const std::string_view value) noexcept {
    if (value.empty() || value == "." || value == "..")
        return false;
    return std::ranges::all_of(value, [](const char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '_' || character == '.' || character == '-';
    });
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

[[nodiscard]] std::string id_name(const RuntimeRepositoryId id) {
    return id == RuntimeRepositoryId::Resources ? "resources" : "scripts";
}

struct Json final {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    std::variant<std::nullptr_t, std::string, Array, Object> value;
};

class JsonParser final {
  public:
    explicit JsonParser(const std::string_view input) : input_(input) {}

    [[nodiscard]] Json parse() {
        auto result = value(0);
        whitespace();
        if (offset_ != input_.size())
            invalid("trailing JSON data");
        return result;
    }

  private:
    [[noreturn]] static void invalid(const char* message) {
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, message);
    }

    void whitespace() {
        while (offset_ < input_.size() && (input_[offset_] == ' ' || input_[offset_] == '\n' ||
                                           input_[offset_] == '\r' || input_[offset_] == '\t'))
            ++offset_;
    }

    [[nodiscard]] char take() {
        if (offset_ == input_.size())
            invalid("truncated JSON");
        return input_[offset_++];
    }

    [[nodiscard]] Json value(const std::size_t depth) {
        if (depth > 12)
            invalid("JSON nesting limit exceeded");
        whitespace();
        if (offset_ == input_.size())
            invalid("missing JSON value");
        if (input_[offset_] == '"')
            return Json{string()};
        if (input_[offset_] == '{')
            return Json{object(depth + 1)};
        if (input_[offset_] == '[')
            return Json{array(depth + 1)};
        if (input_.substr(offset_, 4) == "null") {
            offset_ += 4;
            return Json{nullptr};
        }
        invalid("unsupported JSON value");
    }

    [[nodiscard]] std::string string() {
        if (take() != '"')
            invalid("JSON string expected");
        std::string result;
        while (offset_ < input_.size()) {
            const auto character = take();
            if (character == '"')
                return result;
            if (static_cast<unsigned char>(character) < 0x20U)
                invalid("JSON strings must not contain control bytes");
            if (character != '\\') {
                result.push_back(character);
                continue;
            }
            const auto escaped = take();
            if (escaped == '"' || escaped == '\\' || escaped == '/')
                result.push_back(escaped);
            else if (escaped == 'b')
                result.push_back('\b');
            else if (escaped == 'f')
                result.push_back('\f');
            else if (escaped == 'n')
                result.push_back('\n');
            else if (escaped == 'r')
                result.push_back('\r');
            else if (escaped == 't')
                result.push_back('\t');
            else if (escaped == 'u')
                append_unicode_escape(result);
            else
                invalid("unsupported JSON escape");
        }
        invalid("unterminated JSON string");
    }

    [[nodiscard]] static unsigned int hex_digit(const char character) {
        if (character >= '0' && character <= '9')
            return static_cast<unsigned int>(character - '0');
        if (character >= 'a' && character <= 'f')
            return static_cast<unsigned int>(character - 'a' + 10);
        if (character >= 'A' && character <= 'F')
            return static_cast<unsigned int>(character - 'A' + 10);
        invalid("invalid JSON Unicode escape");
    }

    [[nodiscard]] std::uint32_t unicode_code_unit() {
        std::uint32_t result{};
        for (std::size_t index = 0; index < 4; ++index)
            result = (result << 4U) | hex_digit(take());
        return result;
    }

    static void append_utf8(std::string& result, const std::uint32_t codepoint) {
        if (codepoint <= 0x7fU) {
            result.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffU) {
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

    void append_unicode_escape(std::string& result) {
        const auto first = unicode_code_unit();
        if (first >= 0xdc00U && first <= 0xdfffU)
            invalid("isolated low surrogate in JSON Unicode escape");
        if (first < 0xd800U || first > 0xdbffU) {
            append_utf8(result, first);
            return;
        }
        if (take() != '\\' || take() != 'u')
            invalid("high surrogate is not followed by a low surrogate");
        const auto second = unicode_code_unit();
        if (second < 0xdc00U || second > 0xdfffU)
            invalid("high surrogate is not followed by a low surrogate");
        append_utf8(result,
                    0x10000U + ((first - 0xd800U) << 10U) + (second - 0xdc00U));
    }

    [[nodiscard]] Json::Object object(const std::size_t depth) {
        if (take() != '{')
            invalid("JSON object expected");
        Json::Object result;
        whitespace();
        if (offset_ < input_.size() && input_[offset_] == '}') {
            ++offset_;
            return result;
        }
        for (;;) {
            whitespace();
            auto key = string();
            whitespace();
            if (take() != ':')
                invalid("JSON object separator expected");
            auto item = value(depth);
            if (!result.emplace(std::move(key), std::move(item)).second)
                invalid("duplicate JSON member");
            whitespace();
            const auto separator = take();
            if (separator == '}')
                return result;
            if (separator != ',')
                invalid("JSON object delimiter expected");
        }
    }

    [[nodiscard]] Json::Array array(const std::size_t depth) {
        if (take() != '[')
            invalid("JSON array expected");
        Json::Array result;
        whitespace();
        if (offset_ < input_.size() && input_[offset_] == ']') {
            ++offset_;
            return result;
        }
        for (;;) {
            result.push_back(value(depth));
            whitespace();
            const auto separator = take();
            if (separator == ']')
                return result;
            if (separator != ',')
                invalid("JSON array delimiter expected");
        }
    }

    std::string_view input_;
    std::size_t offset_{};
};

[[nodiscard]] const Json::Object& object(const Json& json) {
    const auto result = std::get_if<Json::Object>(&json.value);
    if (!result)
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "JSON object expected");
    return *result;
}

[[nodiscard]] const Json::Array& array(const Json& json) {
    const auto result = std::get_if<Json::Array>(&json.value);
    if (!result)
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "JSON array expected");
    return *result;
}

[[nodiscard]] const Json& member(const Json::Object& value, const std::string_view key) {
    const auto found = value.find(key);
    if (found == value.end())
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "required JSON member absent");
    return found->second;
}

[[nodiscard]] const std::string& string_member(const Json::Object& value,
                                               const std::string_view key) {
    const auto result = std::get_if<std::string>(&member(value, key).value);
    if (!result)
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "JSON string expected");
    return *result;
}

template <std::size_t Size>
void exact_members(const Json::Object& value, const std::array<std::string_view, Size>& names) {
    if (value.size() != names.size() ||
        !std::ranges::all_of(names, [&](const auto name) { return value.contains(name); }))
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "unknown or missing JSON member");
}

[[nodiscard]] std::uintmax_t parse_decimal_size(const std::string_view value) {
    if (value.empty() || (value.size() > 1 && value.front() == '0'))
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "manifest entry size is not canonical");
    std::uintmax_t result{};
    for (const auto character : value) {
        if (character < '0' || character > '9' ||
            result > (std::numeric_limits<std::uintmax_t>::max() -
                      static_cast<unsigned int>(character - '0')) /
                         10U)
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "manifest entry size is invalid");
        result = result * 10U + static_cast<unsigned int>(character - '0');
    }
    return result;
}

[[nodiscard]] std::string portable_path_key(const std::string_view value) {
    if (value.empty() || value.size() > 1'024 || value.front() == '/' || value.back() == '/' ||
        value.find('\\') != std::string_view::npos || value.find(':') != std::string_view::npos)
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "manifest entry path is not portable");
    std::string key;
    key.reserve(value.size());
    std::size_t begin{};
    while (begin < value.size()) {
        const auto end = value.find('/', begin);
        const auto component =
            value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (component.empty() || component == "." || component == ".." ||
            component.front() == ' ' || component.back() == '.' || component.back() == ' ')
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "manifest entry path is not canonical");
        std::string folded;
        folded.reserve(component.size());
        for (std::size_t offset = 0; offset < component.size();) {
            const auto first = static_cast<unsigned char>(component[offset]);
            std::uint32_t codepoint{};
            std::size_t width{};
            if (first < 0x80U) {
                codepoint = first;
                width = 1;
            } else if (first >= 0xc2U && first <= 0xdfU) {
                codepoint = first & 0x1fU;
                width = 2;
            } else if (first >= 0xe0U && first <= 0xefU) {
                codepoint = first & 0x0fU;
                width = 3;
            } else if (first >= 0xf0U && first <= 0xf4U) {
                codepoint = first & 0x07U;
                width = 4;
            } else {
                fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                     "manifest entry path is not valid UTF-8");
            }
            if (offset + width > component.size())
                fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                     "manifest entry path is not valid UTF-8");
            for (std::size_t index = 1; index < width; ++index) {
                const auto continuation = static_cast<unsigned char>(component[offset + index]);
                if ((continuation & 0xc0U) != 0x80U)
                    fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                         "manifest entry path is not valid UTF-8");
                codepoint = (codepoint << 6U) | (continuation & 0x3fU);
            }
            const auto overlong = (width == 2 && codepoint < 0x80U) ||
                                  (width == 3 && codepoint < 0x800U) ||
                                  (width == 4 && codepoint < 0x10000U);
            const auto combining = (codepoint >= 0x0300U && codepoint <= 0x036fU) ||
                                   (codepoint >= 0x1ab0U && codepoint <= 0x1affU) ||
                                   (codepoint >= 0x1dc0U && codepoint <= 0x1dffU) ||
                                   (codepoint >= 0x20d0U && codepoint <= 0x20ffU) ||
                                   (codepoint >= 0xfe20U && codepoint <= 0xfe2fU) ||
                                   codepoint == 0x3099U || codepoint == 0x309aU ||
                                   (codepoint >= 0x1100U && codepoint <= 0x11ffU) ||
                                   (codepoint >= 0xa960U && codepoint <= 0xa97fU) ||
                                   (codepoint >= 0xd7b0U && codepoint <= 0xd7ffU);
            if (overlong || codepoint > 0x10ffffU ||
                (codepoint >= 0xd800U && codepoint <= 0xdfffU) || codepoint == 0x85U ||
                codepoint == 0x2028U || codepoint == 0x2029U ||
                (codepoint >= 0x7fU && codepoint <= 0x9fU) ||
                (codepoint >= 0xfdd0U && codepoint <= 0xfdefU) ||
                (codepoint & 0xffffU) == 0xfffeU || (codepoint & 0xffffU) == 0xffffU || combining)
                fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                     "manifest entry path is not portable normalized UTF-8");
            if (width == 1) {
                const auto character = static_cast<char>(first);
                if (first < 0x20U || character == '<' || character == '>' || character == '"' ||
                    character == '|' || character == '?' || character == '*')
                    fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                         "manifest entry path contains a non-portable character");
                folded.push_back(character >= 'A' && character <= 'Z'
                                     ? static_cast<char>(character + ('a' - 'A'))
                                     : character);
            } else {
                folded.append(component.substr(offset, width));
            }
            offset += width;
        }
        const auto dot = folded.find('.');
        const auto stem = folded.substr(0, dot);
        const auto reserved =
            stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
             stem.back() >= '1' && stem.back() <= '9') ||
            (stem.size() == 5 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
             (stem.ends_with("\xc2\xb9") || stem.ends_with("\xc2\xb2") ||
              stem.ends_with("\xc2\xb3")));
        if (reserved)
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "manifest entry path uses a reserved portable name");
        if (!key.empty())
            key.push_back('/');
        key += folded;
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    return key;
}

struct TreeManifestEntry final {
    std::string path;
    std::uintmax_t size{};
    std::string sha256;
};

[[nodiscard]] std::vector<TreeManifestEntry>
parse_tree_manifest(const std::string_view bytes, const std::string_view manifest_name,
                    const std::size_t maximum_entries) {
    try {
        const auto document = JsonParser(bytes).parse();
        const auto& root = object(document);
        constexpr std::array root_names{std::string_view{"schema"}, std::string_view{"entries"}};
        exact_members(root, root_names);
        if (string_member(root, "schema") != tree_manifest_schema)
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree manifest schema is invalid");
        std::vector<TreeManifestEntry> result;
        std::map<std::string, std::string, std::less<>> portable_paths;
        for (const auto& item : array(member(root, "entries"))) {
            if (result.size() == maximum_entries)
                fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                     "repository tree manifest entry limit exceeded");
            const auto& entry = object(item);
            constexpr std::array entry_names{std::string_view{"path"}, std::string_view{"size"},
                                             std::string_view{"sha256"}, std::string_view{"mode"}};
            exact_members(entry, entry_names);
            TreeManifestEntry parsed{string_member(entry, "path"),
                                     parse_decimal_size(string_member(entry, "size")),
                                     string_member(entry, "sha256")};
            if (string_member(entry, "mode") != "file" || !lower_hex(parsed.sha256, 64) ||
                parsed.path == manifest_name)
                fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                     "repository tree manifest entry is invalid");
            const auto key = portable_path_key(parsed.path);
            if (!portable_paths.emplace(key, parsed.path).second)
                fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                     "repository tree manifest contains a duplicate portable path");
            result.push_back(std::move(parsed));
        }
        std::ranges::sort(result, {}, &TreeManifestEntry::path);
        if (std::adjacent_find(result.begin(), result.end(),
                               [](const auto& left, const auto& right) {
                                   return left.path == right.path;
                               }) != result.end())
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree manifest contains duplicate entries");
        return result;
    } catch (const UpdateFailure& error) {
        if (error.code() != RuntimeRepositoryUpdateErrorCode::RecoveryFailed)
            throw;
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             std::string("repository tree manifest is invalid: ") + error.what());
    }
}

[[nodiscard]] std::string quote_json(const std::string_view value) {
    std::string result{"\""};
    for (const char character : value) {
        if (character == '"' || character == '\\')
            result.push_back('\\');
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

struct Pointer final {
    std::string generation;
    std::string snapshot;
    [[nodiscard]] bool operator==(const Pointer&) const = default;
};

[[nodiscard]] std::string pointer_json(const Pointer& pointer) {
    return "{\n  \"schema\": " + quote_json(current_schema) +
           ",\n  \"generation\": " + quote_json(pointer.generation) +
           ",\n  \"snapshot\": " + quote_json(pointer.snapshot) + "\n}\n";
}

void validate_pointer(const Pointer& pointer) {
    if (!lower_hex(pointer.generation, 64) ||
        pointer.snapshot != "snapshots/" + pointer.generation + ".json")
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "invalid repository pointer");
}

[[nodiscard]] Pointer parse_pointer(const Json& json) {
    const auto& value = object(json);
    constexpr std::array names{std::string_view{"schema"}, std::string_view{"generation"},
                               std::string_view{"snapshot"}};
    exact_members(value, names);
    if (string_member(value, "schema") != current_schema)
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "invalid pointer schema");
    Pointer result{string_member(value, "generation"), string_member(value, "snapshot")};
    validate_pointer(result);
    return result;
}

[[nodiscard]] std::string repository_json(const RuntimeRepository& value) {
    return "{\"id\":" + quote_json(value.id) + ",\"commit\":" + quote_json(value.commit) +
           ",\"root\":" + quote_json(value.root) + ",\"manifest\":" + quote_json(value.manifest) +
           ",\"manifest_sha256\":" + quote_json(value.manifest_sha256) + "}";
}

[[nodiscard]] std::string snapshot_json(const std::string_view generation,
                                        const std::array<RuntimeRepository, 2>& repositories) {
    return "{\n  \"schema\": " + quote_json(snapshot_schema) +
           ",\n  \"generation\": " + quote_json(generation) + ",\n  \"repositories\": [\n    " +
           repository_json(repositories[0]) + ",\n    " + repository_json(repositories[1]) +
           "\n  ]\n}\n";
}

[[nodiscard]] RuntimeRepository parse_repository(const Json& json) {
    const auto& value = object(json);
    constexpr std::array names{std::string_view{"id"}, std::string_view{"commit"},
                               std::string_view{"root"}, std::string_view{"manifest"},
                               std::string_view{"manifest_sha256"}};
    exact_members(value, names);
    RuntimeRepository result{string_member(value, "id"), string_member(value, "commit"),
                             string_member(value, "root"), string_member(value, "manifest"),
                             string_member(value, "manifest_sha256")};
    if ((result.id != "resources" && result.id != "scripts") || !valid_commit(result.commit) ||
        !valid_manifest(result.manifest) || !lower_hex(result.manifest_sha256, 64) ||
        result.root != "objects/" + result.id + "/" + result.commit)
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "invalid repository descriptor");
    return result;
}

[[nodiscard]] bool reparse_or_link(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "managed path status failed");
    if (std::filesystem::is_symlink(status))
        return true;
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "managed path attributes failed");
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}

[[nodiscard]] bool path_present(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return false;
    if (error)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "managed path status failed");
    return status.type() != std::filesystem::file_type::not_found;
}

void require_plain_directory(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (!std::filesystem::create_directory(path, error) && error)
            fail(RuntimeRepositoryUpdateErrorCode::Io, "managed directory creation failed");
    }
    if (error || reparse_or_link(path) || !std::filesystem::is_directory(path))
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed, "managed directory is not plain");
}

void validate_anchored_relative(const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.lexically_normal() != relative ||
        std::ranges::any_of(relative, [](const auto& component) { return component == ".."; }))
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "managed file path is not canonical");
}

#ifdef _WIN32

class AnchoredFile final {
  public:
    AnchoredFile() noexcept = default;
    explicit AnchoredFile(const HANDLE handle, const std::uintmax_t size) noexcept
        : handle_(handle), size_(size) {}
    ~AnchoredFile() {
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }
    AnchoredFile(const AnchoredFile&) = delete;
    AnchoredFile& operator=(const AnchoredFile&) = delete;
    AnchoredFile(AnchoredFile&& other) noexcept
        : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)), size_(other.size_) {}
    AnchoredFile& operator=(AnchoredFile&& other) noexcept {
        if (this != &other) {
            if (handle_ != INVALID_HANDLE_VALUE)
                CloseHandle(handle_);
            handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
            size_ = other.size_;
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] std::uintmax_t size() const noexcept { return size_; }

  private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
    std::uintmax_t size_{};
};

[[nodiscard]] std::filesystem::path final_handle_path(const HANDLE handle) {
    constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    const auto required = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
    if (required == 0)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "managed handle final path is unavailable");
    std::wstring buffer(required, L'\0');
    const auto written =
        GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
    if (written == 0 || written >= buffer.size())
        fail(RuntimeRepositoryUpdateErrorCode::Io, "managed handle final path is unavailable");
    buffer.resize(written);
    return std::filesystem::path(std::move(buffer));
}

[[nodiscard]] bool within_handle_root(const std::filesystem::path& root,
                                      const std::filesystem::path& candidate) noexcept {
    auto root_iterator = root.begin();
    auto candidate_iterator = candidate.begin();
    for (; root_iterator != root.end(); ++root_iterator, ++candidate_iterator) {
        if (candidate_iterator == candidate.end() ||
            CompareStringOrdinal(root_iterator->c_str(), -1, candidate_iterator->c_str(), -1,
                                 TRUE) != CSTR_EQUAL)
            return false;
    }
    return true;
}

[[nodiscard]] AnchoredFile open_anchored_file(const std::filesystem::path& root,
                                              const std::filesystem::path& relative,
                                              const bool writable) {
    validate_anchored_relative(relative);
    const auto sharing_directory = FILE_SHARE_READ | FILE_SHARE_WRITE;
    const auto sharing_file = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    const auto root_handle = CreateFileW(
        root.c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY, sharing_directory, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (root_handle == INVALID_HANDLE_VALUE)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "managed root handle cannot be opened");
    std::vector<HANDLE> directories{root_handle};
    const auto close_directories = [&directories] {
        for (const auto handle : directories)
            CloseHandle(handle);
        directories.clear();
    };
    FILE_ATTRIBUTE_TAG_INFO root_attributes{};
    if (!GetFileInformationByHandleEx(root_handle, FileAttributeTagInfo, &root_attributes,
                                      sizeof(root_attributes)) ||
        (root_attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (root_attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        close_directories();
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "managed root is not a plain directory");
    }
    std::filesystem::path canonical_root;
    try {
        canonical_root = final_handle_path(root_handle);
    } catch (...) {
        close_directories();
        throw;
    }
    auto current = root;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::size_t component_index{};
    const auto component_count =
        static_cast<std::size_t>(std::distance(relative.begin(), relative.end()));
    try {
        for (const auto& component : relative) {
            current /= component;
            const auto final_component = ++component_index == component_count;
            const auto access = final_component ? (GENERIC_READ | (writable ? GENERIC_WRITE : 0U))
                                                : (FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY);
            const auto handle = CreateFileW(
                current.c_str(), access, final_component ? sharing_file : sharing_directory,
                nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT |
                    (final_component ? FILE_FLAG_SEQUENTIAL_SCAN : 0U),
                nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                fail(RuntimeRepositoryUpdateErrorCode::Io,
                     "managed path component cannot be opened");
            FILE_ATTRIBUTE_TAG_INFO attributes{};
            bool contained{};
            try {
                contained = within_handle_root(canonical_root, final_handle_path(handle));
            } catch (...) {
                CloseHandle(handle);
                throw;
            }
            if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes,
                                              sizeof(attributes)) ||
                (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                (((attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) ==
                 final_component) ||
                !contained) {
                CloseHandle(handle);
                fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                     "managed path escaped through a reparse point");
            }
            if (final_component)
                file = handle;
            else
                directories.push_back(handle);
        }
        FILE_STANDARD_INFO information{};
        if (file == INVALID_HANDLE_VALUE ||
            !GetFileInformationByHandleEx(file, FileStandardInfo, &information,
                                           sizeof(information)) ||
            information.Directory || information.EndOfFile.QuadPart < 0 ||
            information.NumberOfLinks != 1) {
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "managed file handle is invalid");
        }
        close_directories();
        return AnchoredFile(file, static_cast<std::uintmax_t>(information.EndOfFile.QuadPart));
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        close_directories();
        throw;
    }
}

#else

class AnchoredFile final {
  public:
    AnchoredFile() noexcept = default;
    explicit AnchoredFile(const int descriptor, const std::uintmax_t size) noexcept
        : descriptor_(descriptor), size_(size) {}
    ~AnchoredFile() {
        if (descriptor_ >= 0)
            close(descriptor_);
    }
    AnchoredFile(const AnchoredFile&) = delete;
    AnchoredFile& operator=(const AnchoredFile&) = delete;
    AnchoredFile(AnchoredFile&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)), size_(other.size_) {}
    AnchoredFile& operator=(AnchoredFile&& other) noexcept {
        if (this != &other) {
            if (descriptor_ >= 0)
                close(descriptor_);
            descriptor_ = std::exchange(other.descriptor_, -1);
            size_ = other.size_;
        }
        return *this;
    }
    [[nodiscard]] int get() const noexcept { return descriptor_; }
    [[nodiscard]] std::uintmax_t size() const noexcept { return size_; }

  private:
    int descriptor_{-1};
    std::uintmax_t size_{};
};

[[nodiscard]] AnchoredFile open_anchored_file(const std::filesystem::path& root,
                                              const std::filesystem::path& relative,
                                              const bool writable) {
    validate_anchored_relative(relative);
    auto parent = open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (parent < 0)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "managed root handle cannot be opened");
    std::size_t component_index{};
    const auto component_count =
        static_cast<std::size_t>(std::distance(relative.begin(), relative.end()));
    for (const auto& component : relative) {
        const auto final_component = ++component_index == component_count;
        const auto flags = O_CLOEXEC | O_NOFOLLOW |
                           (final_component ? (writable ? O_RDWR : O_RDONLY) | O_NONBLOCK
                                            : O_RDONLY | O_DIRECTORY);
        const auto opened = openat(parent, component.c_str(), flags);
        const auto saved_error = errno;
        close(parent);
        if (opened < 0) {
            errno = saved_error;
            fail(RuntimeRepositoryUpdateErrorCode::Io, "managed path component cannot be opened");
        }
        parent = opened;
    }
    struct stat information{};
    if (fstat(parent, &information) != 0 || !S_ISREG(information.st_mode) ||
        information.st_size < 0 || information.st_nlink != 1) {
        close(parent);
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed, "managed file handle is invalid");
    }
    return AnchoredFile(parent, static_cast<std::uintmax_t>(information.st_size));
}

#endif

[[nodiscard]] std::string read_anchored_file(const std::filesystem::path& root,
                                             const std::filesystem::path& relative,
                                             const std::size_t maximum = max_state_json_bytes) {
    auto input = open_anchored_file(root, relative, false);
    if (input.size() > maximum)
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed, "file limit exceeded");
    std::string result;
    result.reserve(static_cast<std::size_t>(input.size()));
    std::array<char, 4096> buffer{};
    for (;;) {
#ifdef _WIN32
        DWORD count{};
        if (!ReadFile(input.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count,
                      nullptr))
            fail(RuntimeRepositoryUpdateErrorCode::Io, "managed file read failed");
#else
        const auto raw_count = read(input.get(), buffer.data(), buffer.size());
        if (raw_count < 0) {
            if (errno == EINTR)
                continue;
            fail(RuntimeRepositoryUpdateErrorCode::Io, "managed file read failed");
        }
        const auto count = static_cast<std::size_t>(raw_count);
#endif
        if (count == 0)
            break;
        if (count > maximum - std::min(maximum, result.size()))
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "file grew beyond bounded read");
        result.append(buffer.data(), static_cast<std::size_t>(count));
    }
    return result;
}

void sync_directory(const std::filesystem::path& path) {
#ifndef _WIN32
    const auto descriptor = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "directory sync open failed");
    const auto result = fsync(descriptor);
    close(descriptor);
    if (result != 0)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "directory sync failed");
#else
    (void)path;
#endif
}

void sync_anchored_directory(const std::filesystem::path& root,
                             const std::filesystem::path& relative) {
#ifndef _WIN32
    auto descriptor = open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "repository root sync open failed");
    for (const auto& component : relative) {
        const auto child =
            openat(descriptor, component.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        const auto saved_error = errno;
        close(descriptor);
        if (child < 0) {
            errno = saved_error;
            fail(RuntimeRepositoryUpdateErrorCode::Io, "repository directory sync open failed");
        }
        descriptor = child;
    }
    const auto result = fsync(descriptor);
    close(descriptor);
    if (result != 0)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "repository directory sync failed");
#else
    (void)root;
    (void)relative;
#endif
}

void sync_plain_tree(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> directories;
    for (std::filesystem::recursive_directory_iterator iterator(root), end; iterator != end;
         ++iterator) {
        const auto& path = iterator->path();
        if (reparse_or_link(path))
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree changed before synchronization");
        if (iterator->is_directory()) {
            directories.push_back(path.lexically_relative(root));
            continue;
        }
        if (!iterator->is_regular_file())
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree contains a special file");
        const auto relative = path.lexically_relative(root);
        auto file = open_anchored_file(root, relative, true);
#ifdef _WIN32
        if (!FlushFileBuffers(file.get()))
#else
        if (fsync(file.get()) != 0)
#endif
            fail(RuntimeRepositoryUpdateErrorCode::Io, "repository payload sync failed");
    }
    for (auto iterator = directories.rbegin(); iterator != directories.rend(); ++iterator)
        sync_anchored_directory(root, *iterator);
    sync_directory(root);
}

std::atomic<std::uint64_t> temporary_counter{static_cast<std::uint64_t>(
    std::chrono::high_resolution_clock::now().time_since_epoch().count())};

[[nodiscard]] std::filesystem::path temporary_path(const std::filesystem::path& parent,
                                                   const std::string_view label) {
    return parent /
           ("." + std::string(label) + "." +
#ifdef _WIN32
            std::to_string(GetCurrentProcessId()) + "." +
#else
            std::to_string(getpid()) + "." +
#endif
            std::to_string(temporary_counter.fetch_add(1, std::memory_order_relaxed)) + ".tmp");
}

void write_new_file(const std::filesystem::path& path, const std::string_view bytes) {
    if (bytes.size() > max_state_json_bytes)
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed, "state JSON limit exceeded");
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "temporary file creation failed");
    DWORD written{};
    const auto write_ok =
        bytes.empty() ||
        WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    const auto sync_ok = write_ok && written == bytes.size() && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!sync_ok) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        fail(RuntimeRepositoryUpdateErrorCode::Io, "temporary file write failed");
    }
#else
    const auto descriptor =
        open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "temporary file creation failed");
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto count = write(descriptor, bytes.data() + offset, bytes.size() - offset);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            close(descriptor);
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            fail(RuntimeRepositoryUpdateErrorCode::Io, "temporary file write failed");
        }
        offset += static_cast<std::size_t>(count);
    }
    const auto sync_result = fsync(descriptor);
    close(descriptor);
    if (sync_result != 0) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        fail(RuntimeRepositoryUpdateErrorCode::Io, "temporary file sync failed");
    }
#endif
}

void atomic_replace(const std::filesystem::path& source, const std::filesystem::path& target,
                    bool* const replaced) {
#ifdef _WIN32
    for (std::size_t attempt = 0; attempt < 2'000; ++attempt) {
        if (MoveFileExW(source.c_str(), target.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            if (replaced)
                *replaced = true;
            return;
        }
        const auto error = GetLastError();
        if (attempt + 1 < 2'000 &&
            (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        fail(RuntimeRepositoryUpdateErrorCode::Io, "atomic replace failed");
    }
#else
    if (rename(source.c_str(), target.c_str()) != 0)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "atomic replace failed");
    if (replaced)
        *replaced = true;
    sync_directory(target.parent_path());
#endif
}

void write_atomically(const std::filesystem::path& target, const std::string_view bytes,
                      bool* const replaced = nullptr) {
    const auto temporary = temporary_path(target.parent_path(), target.filename().string());
    bool temporary_created = false;
    try {
        write_new_file(temporary, bytes);
        temporary_created = true;
        atomic_replace(temporary, target, replaced);
    } catch (...) {
        if (temporary_created) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        throw;
    }
}

enum class InstallResult { Installed, Existing };

struct DirectoryIdentity final {
#ifdef _WIN32
    DWORD volume{};
    std::uint64_t file_index{};
#else
    dev_t device{};
    ino_t inode{};
#endif
    [[nodiscard]] bool operator==(const DirectoryIdentity&) const = default;
};

[[nodiscard]] DirectoryIdentity directory_identity(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto handle = CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        fail(RuntimeRepositoryUpdateErrorCode::Io, "immutable directory identity open failed");
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    BY_HANDLE_FILE_INFORMATION information{};
    const auto valid = GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes,
                                                    sizeof(attributes)) &&
                       GetFileInformationByHandle(handle, &information) &&
                       (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                       (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    CloseHandle(handle);
    if (!valid)
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "immutable directory identity is invalid");
    return {information.dwVolumeSerialNumber,
            (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
                information.nFileIndexLow};
#else
    struct stat information{};
    if (lstat(path.c_str(), &information) != 0 || !S_ISDIR(information.st_mode))
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "immutable directory identity is invalid");
    return {information.st_dev, information.st_ino};
#endif
}

[[nodiscard]] InstallResult install_file_no_replace(const std::filesystem::path& source,
                                                    const std::filesystem::path& target) {
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        return InstallResult::Installed;
    const auto error = GetLastError();
    if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
        return InstallResult::Existing;
    fail(RuntimeRepositoryUpdateErrorCode::Io, "immutable file install failed");
#else
    if (link(source.c_str(), target.c_str()) == 0)
        return InstallResult::Installed;
    if (errno == EEXIST)
        return InstallResult::Existing;
    fail(RuntimeRepositoryUpdateErrorCode::Io, "immutable file install failed");
#endif
}

[[nodiscard]] InstallResult install_directory_no_replace(const std::filesystem::path& source,
                                                         const std::filesystem::path& target) {
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        return InstallResult::Installed;
    const auto error = GetLastError();
    if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS || error == ERROR_DIR_NOT_EMPTY)
        return InstallResult::Existing;
    fail(RuntimeRepositoryUpdateErrorCode::Io, "immutable object install failed");
#else
#if defined(__linux__) && defined(SYS_renameat2)
    constexpr unsigned int rename_no_replace = 1U;
    if (syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, target.c_str(),
                rename_no_replace) == 0) {
        sync_directory(target.parent_path());
        return InstallResult::Installed;
    }
    if (errno == EEXIST || errno == ENOTEMPTY)
        return InstallResult::Existing;
    fail(RuntimeRepositoryUpdateErrorCode::Io, "immutable object no-replace install failed");
#elif defined(__APPLE__)
    if (renamex_np(source.c_str(), target.c_str(), RENAME_EXCL) == 0) {
        sync_directory(target.parent_path());
        return InstallResult::Installed;
    }
    if (errno == EEXIST || errno == ENOTEMPTY)
        return InstallResult::Existing;
    fail(RuntimeRepositoryUpdateErrorCode::Io, "immutable object no-replace install failed");
#else
    fail(RuntimeRepositoryUpdateErrorCode::Io,
         "platform lacks atomic no-replace directory install");
#endif
#endif
}

void discard_installed_object_if_unchanged(const std::filesystem::path& object,
                                           const DirectoryIdentity& installed_identity) noexcept {
    try {
        if (directory_identity(object) != installed_identity)
            return;
        const auto quarantine =
            temporary_path(object.parent_path(), "invalid-" + object.filename().string());
        if (install_directory_no_replace(object, quarantine) != InstallResult::Installed)
            return;
        std::error_code ignored;
        std::filesystem::remove_all(quarantine, ignored);
    } catch (...) {
    }
}

void install_immutable_json(const std::filesystem::path& state_root,
                            const std::filesystem::path& target, const std::string_view bytes) {
    const auto relative = target.lexically_relative(state_root);
    if (path_present(target)) {
        if (read_anchored_file(state_root, relative) != bytes)
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "immutable snapshot differs from existing content");
        return;
    }
    const auto temporary = temporary_path(target.parent_path(), target.filename().string());
    bool temporary_created = false;
    try {
        write_new_file(temporary, bytes);
        temporary_created = true;
        const auto result = install_file_no_replace(temporary, target);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        if (result == InstallResult::Existing && read_anchored_file(state_root, relative) != bytes)
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "immutable snapshot collision");
        sync_directory(target.parent_path());
    } catch (...) {
        if (temporary_created) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        throw;
    }
}

[[nodiscard]] std::optional<Pointer> read_pointer(const std::filesystem::path& path) {
    if (!path_present(path)) {
        return std::nullopt;
    }
    return parse_pointer(
        JsonParser(read_anchored_file(path.parent_path(), path.filename())).parse());
}

void write_optional_pointer(const std::filesystem::path& path,
                            const std::optional<Pointer>& pointer) {
    if (pointer)
        write_atomically(path, pointer_json(*pointer));
    else {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error)
            fail(RuntimeRepositoryUpdateErrorCode::Io, "pointer removal failed");
        sync_directory(path.parent_path());
    }
}

struct Journal final {
    std::string operation;
    std::string phase;
    std::optional<Pointer> old_previous;
    std::optional<Pointer> old_current;
    std::optional<Pointer> new_previous;
    Pointer new_current;
};

[[nodiscard]] std::string optional_pointer_json(const std::optional<Pointer>& pointer) {
    if (!pointer)
        return "null";
    auto result = pointer_json(*pointer);
    if (!result.empty() && result.back() == '\n')
        result.pop_back();
    return result;
}

[[nodiscard]] std::string journal_json(const Journal& journal) {
    return "{\n  \"schema\": " + quote_json(journal_schema) +
           ",\n  \"operation\": " + quote_json(journal.operation) +
           ",\n  \"phase\": " + quote_json(journal.phase) +
           ",\n  \"old_previous\": " + optional_pointer_json(journal.old_previous) +
           ",\n  \"old_current\": " + optional_pointer_json(journal.old_current) +
           ",\n  \"new_previous\": " + optional_pointer_json(journal.new_previous) +
           ",\n  \"new_current\": " + optional_pointer_json(journal.new_current) + "\n}\n";
}

[[nodiscard]] std::optional<Pointer> parse_optional_pointer(const Json& json) {
    if (std::holds_alternative<std::nullptr_t>(json.value))
        return std::nullopt;
    return parse_pointer(json);
}

[[nodiscard]] Journal parse_journal(const Json& json) {
    const auto& value = object(json);
    constexpr std::array names{std::string_view{"schema"},      std::string_view{"operation"},
                               std::string_view{"phase"},       std::string_view{"old_previous"},
                               std::string_view{"old_current"}, std::string_view{"new_previous"},
                               std::string_view{"new_current"}};
    exact_members(value, names);
    if (string_member(value, "schema") != journal_schema)
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "invalid journal schema");
    Journal result{string_member(value, "operation"),
                   string_member(value, "phase"),
                   parse_optional_pointer(member(value, "old_previous")),
                   parse_optional_pointer(member(value, "old_current")),
                   parse_optional_pointer(member(value, "new_previous")),
                   parse_pointer(member(value, "new_current"))};
    if ((result.operation != "publish" && result.operation != "rollback") ||
        (result.phase != "prepared" && result.phase != "previous_replaced" &&
         result.phase != "current_replaced") ||
        result.new_previous != result.old_current ||
        (result.operation == "rollback" &&
         (!result.old_previous || *result.old_previous != result.new_current)))
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "invalid journal relationship");
    return result;
}

[[nodiscard]] std::array<RuntimeRepository, 2>
read_snapshot(const std::filesystem::path& state_root, const Pointer& pointer) {
    validate_pointer(pointer);
    const auto document = JsonParser(read_anchored_file(state_root, pointer.snapshot)).parse();
    const auto& value = object(document);
    constexpr std::array names{std::string_view{"schema"}, std::string_view{"generation"},
                               std::string_view{"repositories"}};
    exact_members(value, names);
    if (string_member(value, "schema") != snapshot_schema ||
        string_member(value, "generation") != pointer.generation)
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "snapshot identity mismatch");
    const auto& entries = array(member(value, "repositories"));
    if (entries.size() != 2)
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "snapshot repository count invalid");
    std::array repositories{parse_repository(entries[0]), parse_repository(entries[1])};
    if (repositories[0].id != "resources" || repositories[1].id != "scripts" ||
        runtime_repository_generation(repositories) != pointer.generation)
        fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, "snapshot generation invalid");
    return repositories;
}

void check_cancelled(const std::stop_token stop_token) {
    if (stop_token.stop_requested())
        fail(RuntimeRepositoryUpdateErrorCode::Cancelled, "repository update cancelled");
}

[[nodiscard]] Digest hash_plain_file(const std::filesystem::path& root,
                                     const std::filesystem::path& relative,
                                     const std::uintmax_t expected_size,
                                     const std::stop_token stop_token) {
    Sha256 digest;
    std::uintmax_t total{};
    std::array<char, 64U * 1024U> buffer{};
#ifdef BAAS_RUNTIME_REPOSITORY_UPDATER_TESTING
    if (const auto hook = validation_read_hook.load(std::memory_order_acquire))
        hook(root, relative);
#endif
    auto file = open_anchored_file(root, relative, false);
    if (file.size() != expected_size)
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "repository payload changed before sealing");
#ifdef _WIN32
    for (;;) {
        check_cancelled(stop_token);
        DWORD count{};
        if (!ReadFile(file.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count,
                      nullptr))
            fail(RuntimeRepositoryUpdateErrorCode::Io, "repository payload read failed");
        if (count == 0)
            break;
        total += count;
        if (total > expected_size) {
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository payload grew while sealing");
        }
        digest.update(std::as_bytes(std::span(buffer.data(), static_cast<std::size_t>(count))));
    }
#else
    for (;;) {
        check_cancelled(stop_token);
        const auto count = read(file.get(), buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR)
                continue;
            fail(RuntimeRepositoryUpdateErrorCode::Io, "repository payload read failed");
        }
        if (count == 0)
            break;
        total += static_cast<std::uintmax_t>(count);
        if (total > expected_size) {
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository payload grew while sealing");
        }
        digest.update(std::as_bytes(std::span(buffer.data(), static_cast<std::size_t>(count))));
    }
#endif
    if (total != expected_size)
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "repository payload changed while sealing");
    return digest.finish();
}

class NativeWriterLock final {
  public:
    explicit NativeWriterLock(const std::filesystem::path& path) {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            fail(RuntimeRepositoryUpdateErrorCode::Io, "writer lock open failed");
        FILE_ATTRIBUTE_TAG_INFO attributes{};
        if (!GetFileInformationByHandleEx(handle_, FileAttributeTagInfo, &attributes,
                                          sizeof(attributes)) ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "writer lock is not a plain file");
        }
        OVERLAPPED overlapped{};
        if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                        &overlapped)) {
            const auto error = GetLastError();
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING)
                fail(RuntimeRepositoryUpdateErrorCode::Busy, "repository writer is busy");
            fail(RuntimeRepositoryUpdateErrorCode::Io, "writer lock failed");
        }
        BY_HANDLE_FILE_INFORMATION locked_information{};
        if (!GetFileInformationByHandle(handle_, &locked_information)) {
            OVERLAPPED unlock{};
            UnlockFileEx(handle_, 0, 1, 0, &unlock);
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            fail(RuntimeRepositoryUpdateErrorCode::Io, "writer lock identity failed");
        }
        const auto reopened = CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION path_information{};
        const auto same_identity =
            reopened != INVALID_HANDLE_VALUE &&
            GetFileInformationByHandle(reopened, &path_information) &&
            locked_information.dwVolumeSerialNumber == path_information.dwVolumeSerialNumber &&
            locked_information.nFileIndexHigh == path_information.nFileIndexHigh &&
            locked_information.nFileIndexLow == path_information.nFileIndexLow;
        if (reopened != INVALID_HANDLE_VALUE)
            CloseHandle(reopened);
        if (!same_identity) {
            OVERLAPPED unlock{};
            UnlockFileEx(handle_, 0, 1, 0, &unlock);
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "writer lock path changed while locked");
        }
#else
        descriptor_ = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (descriptor_ < 0)
            fail(RuntimeRepositoryUpdateErrorCode::Io, "writer lock open failed");
        if (flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            const auto error = errno;
            close(descriptor_);
            descriptor_ = -1;
            if (error == EWOULDBLOCK || error == EAGAIN)
                fail(RuntimeRepositoryUpdateErrorCode::Busy, "repository writer is busy");
            fail(RuntimeRepositoryUpdateErrorCode::Io, "writer lock failed");
        }
        struct stat locked_information{};
        struct stat path_information{};
        if (fstat(descriptor_, &locked_information) != 0 ||
            lstat(path.c_str(), &path_information) != 0 || !S_ISREG(locked_information.st_mode) ||
            !S_ISREG(path_information.st_mode) ||
            locked_information.st_dev != path_information.st_dev ||
            locked_information.st_ino != path_information.st_ino) {
            flock(descriptor_, LOCK_UN);
            close(descriptor_);
            descriptor_ = -1;
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "writer lock path changed while locked");
        }
#endif
    }

    ~NativeWriterLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            OVERLAPPED overlapped{};
            UnlockFileEx(handle_, 0, 1, 0, &overlapped);
            CloseHandle(handle_);
        }
#else
        if (descriptor_ >= 0) {
            flock(descriptor_, LOCK_UN);
            close(descriptor_);
        }
#endif
    }

    NativeWriterLock(const NativeWriterLock&) = delete;
    NativeWriterLock& operator=(const NativeWriterLock&) = delete;

  private:
#ifdef _WIN32
    HANDLE handle_{INVALID_HANDLE_VALUE};
#else
    int descriptor_{-1};
#endif
};

class NoopHooks final : public RuntimeRepositoryUpdaterHooks {
  public:
    void checkpoint(RuntimeRepositoryUpdaterCheckpoint) override {}
};

[[nodiscard]] RuntimeRepositoryUpdateResult
error_result(const UpdateFailure& error, const PublishDisposition disposition,
             std::shared_ptr<const RuntimeRepositorySnapshot> pin,
             RuntimeRepositoryUpdaterHooks& hooks) {
    hooks.diagnostic(error.code(), error.what());
    return RuntimeRepositoryUpdateResult{
        RuntimeRepositoryUpdateError{
            error.code(), std::string(runtime_repository_update_error_message(error.code()))},
        disposition, pin ? pin->generation() : std::string{}, std::move(pin)};
}

[[nodiscard]] RuntimeRepositoryUpdateResult
unknown_error_result(const std::exception& error, const PublishDisposition disposition,
                     std::shared_ptr<const RuntimeRepositorySnapshot> pin,
                     RuntimeRepositoryUpdaterHooks& hooks) {
    hooks.diagnostic(RuntimeRepositoryUpdateErrorCode::Io, error.what());
    return RuntimeRepositoryUpdateResult{
        RuntimeRepositoryUpdateError{
            RuntimeRepositoryUpdateErrorCode::Io,
            std::string(runtime_repository_update_error_message(
                RuntimeRepositoryUpdateErrorCode::Io))},
        disposition, pin ? pin->generation() : std::string{}, std::move(pin)};
}

void validate_plan(const RuntimeRepositoryUpdatePlan& plan) {
    if (plan.repositories[0].id != RuntimeRepositoryId::Resources ||
        plan.repositories[1].id != RuntimeRepositoryId::Scripts)
        fail(RuntimeRepositoryUpdateErrorCode::InvalidPlan,
             "repository plan must be ordered resources then scripts");
    for (const auto& spec : plan.repositories) {
        if (spec.remote_url.empty() || spec.remote_url.size() > 8'192 ||
            spec.advertised_reference.empty() || spec.advertised_reference.size() > 1'024 ||
            !valid_commit(spec.exact_commit) || !valid_manifest(spec.manifest) ||
            !lower_hex(spec.expected_manifest_sha256, 64))
            fail(RuntimeRepositoryUpdateErrorCode::InvalidPlan, "repository plan is invalid");
    }
}

void validate_seal(const RepositoryTreeSeal& seal, const RepositoryFetchSpec& spec) {
    if (seal.file_count == 0 || seal.manifest_sha256 != spec.expected_manifest_sha256 ||
        !lower_hex(seal.payload_sha256, 64))
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "repository validator returned an incomplete seal");
}

void validate_private_staging(const std::filesystem::path& state_root,
                              const std::filesystem::path& allocated_directory) {
    const auto staging_root = std::filesystem::canonical(state_root / "staging");
    if (!std::filesystem::exists(allocated_directory) || reparse_or_link(allocated_directory) ||
        !std::filesystem::is_directory(allocated_directory))
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "fetch backend replaced its updater-owned staging directory");
    const auto canonical = std::filesystem::canonical(allocated_directory);
    if (canonical != allocated_directory.lexically_normal() ||
        canonical.parent_path() != staging_root)
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "fetch backend staging directory escaped the state root");
}

void ensure_same_seal(const RepositoryTreeSeal& left, const RepositoryTreeSeal& right) {
    if (left != right)
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "existing immutable object differs from staged candidate");
}

[[nodiscard]] std::filesystem::path object_path(const std::filesystem::path& state_root,
                                                const RepositoryFetchSpec& spec) {
    return state_root / "objects" / id_name(spec.id) / spec.exact_commit;
}

[[nodiscard]] RepositoryTreeSeal validate_tree(const RuntimeRepositoryTreeValidator& validator,
                                               const RepositoryFetchSpec& spec,
                                               const std::filesystem::path& repository_root,
                                               const std::stop_token stop_token) {
    try {
        return validator.validate_and_seal(spec, repository_root, stop_token);
    } catch (const UpdateFailure&) {
        throw;
    } catch (const std::exception& error) {
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed, error.what());
    }
}

void validate_pointer_objects(const std::filesystem::path& state_root, const Pointer& pointer,
                              const RuntimeRepositoryTreeValidator& validator,
                              const std::stop_token stop_token) {
    const auto repositories = read_snapshot(state_root, pointer);
    for (std::size_t index = 0; index < repositories.size(); ++index) {
        check_cancelled(stop_token);
        const auto id = index == 0 ? RuntimeRepositoryId::Resources : RuntimeRepositoryId::Scripts;
        const auto& value = repositories[index];
        RepositoryFetchSpec spec{id, {}, {}, value.commit, value.manifest, value.manifest_sha256};
        const auto seal = validate_tree(validator, spec, state_root / value.root, stop_token);
        validate_seal(seal, spec);
    }
}

} // namespace

#ifdef BAAS_RUNTIME_REPOSITORY_UPDATER_TESTING
void set_runtime_repository_validation_read_hook_for_testing(
    const RuntimeRepositoryValidationReadHook hook) noexcept {
    validation_read_hook.store(hook, std::memory_order_release);
}
#endif

ExpectedCurrent ExpectedCurrent::any() { return {}; }
ExpectedCurrent ExpectedCurrent::absent() { return {ExpectedCurrentKind::Absent, {}}; }
ExpectedCurrent ExpectedCurrent::exact(std::string generation) {
    return {ExpectedCurrentKind::Exact, std::move(generation)};
}

StrictRuntimeRepositoryTreeValidator::StrictRuntimeRepositoryTreeValidator(
    const RepositoryValidationLimits limits)
    : limits_(limits) {
    if (limits_.max_files == 0 || limits_.max_entries < limits_.max_files ||
        limits_.max_total_bytes == 0 || limits_.max_file_bytes == 0 ||
        limits_.max_manifest_bytes == 0 || limits_.max_relative_path_bytes == 0 ||
        limits_.max_relative_path_depth == 0)
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "repository validation limits are invalid");
}

RepositoryTreeSeal StrictRuntimeRepositoryTreeValidator::validate_and_seal(
    const RepositoryFetchSpec& spec, const std::filesystem::path& repository_root,
    const std::stop_token stop_token) const {
    check_cancelled(stop_token);
    if (!std::filesystem::exists(repository_root) || reparse_or_link(repository_root) ||
        !std::filesystem::is_directory(repository_root))
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "repository root is not a plain directory");
    const auto canonical_root = std::filesystem::canonical(repository_root);
    if (canonical_root != repository_root.lexically_normal())
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "repository root is not canonical");

    struct FileRecord final {
        std::string relative;
        std::uintmax_t size{};
        Digest digest{};
    };
    std::vector<FileRecord> files;
    std::vector<std::string> directories;
    for (std::filesystem::recursive_directory_iterator iterator(repository_root), end;
         iterator != end; ++iterator) {
        check_cancelled(stop_token);
        const auto& path = iterator->path();
        if (reparse_or_link(path))
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree contains a link or reparse point");
        const auto relative_path = path.lexically_relative(repository_root);
        const auto encoded_relative = relative_path.generic_u8string();
        const std::string relative(reinterpret_cast<const char*>(encoded_relative.data()),
                                   encoded_relative.size());
        const auto depth =
            static_cast<std::size_t>(std::distance(relative_path.begin(), relative_path.end()));
        if (relative.empty() || relative == "." || relative_path.is_absolute() ||
            relative_path.lexically_normal() != relative_path ||
            relative.size() > limits_.max_relative_path_bytes ||
            depth > limits_.max_relative_path_depth)
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree path escapes its root");
        if (files.size() + directories.size() == limits_.max_entries)
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree entry limit exceeded");
        if (iterator->is_directory()) {
            directories.push_back(relative);
            continue;
        }
        if (!iterator->is_regular_file())
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree contains a special file");
        const auto size = iterator->file_size();
        const auto maximum_size = relative == spec.manifest
                                      ? std::min<std::uintmax_t>(limits_.max_file_bytes,
                                                                limits_.max_manifest_bytes)
                                      : limits_.max_file_bytes;
        if (size > maximum_size || files.size() == limits_.max_files)
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree limit exceeded");
        files.push_back({relative, size, {}});
    }
    std::ranges::sort(files, {}, &FileRecord::relative);

    Sha256 payload;
    RepositoryTreeSeal result;
    for (auto& file : files) {
        check_cancelled(stop_token);
        if (file.size > limits_.max_total_bytes - result.total_bytes)
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree byte limit exceeded");
        file.digest =
            hash_plain_file(canonical_root, path_from_utf8(file.relative), file.size, stop_token);
        add_length_prefixed(payload, file.relative);
        std::array<std::byte, 8> encoded_size{};
        for (std::size_t index = 0; index < encoded_size.size(); ++index)
            encoded_size[7 - index] = static_cast<std::byte>(file.size >> (index * 8U));
        payload.update(encoded_size);
        payload.update(file.digest);
        if (file.relative == spec.manifest)
            result.manifest_sha256 = hex(file.digest);
        ++result.file_count;
        result.total_bytes += file.size;
    }
    if (result.manifest_sha256.empty() || result.manifest_sha256 != spec.expected_manifest_sha256)
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "repository manifest digest mismatch: expected " + spec.expected_manifest_sha256 +
                 ", got " + result.manifest_sha256);
    const auto manifest = parse_tree_manifest(
        read_anchored_file(canonical_root, spec.manifest, limits_.max_manifest_bytes),
        spec.manifest, limits_.max_files - 1);
    std::vector<const FileRecord*> payload_files;
    payload_files.reserve(files.size());
    for (const auto& file : files)
        if (file.relative != spec.manifest)
            payload_files.push_back(&file);
    if (manifest.size() != payload_files.size())
        fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
             "repository tree differs from its manifest");
    for (std::size_t index = 0; index < manifest.size(); ++index) {
        const auto& expected = manifest[index];
        const auto& actual = *payload_files[index];
        if (expected.path != actual.relative || expected.size != actual.size ||
            expected.sha256 != hex(actual.digest))
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree entry differs from its manifest");
    }
    std::set<std::string, std::less<>> populated_directories;
    for (const auto& file : files)
        for (auto separator = file.relative.find('/'); separator != std::string::npos;
             separator = file.relative.find('/', separator + 1))
            populated_directories.emplace(file.relative.substr(0, separator));
    for (const auto& directory : directories)
        if (!populated_directories.contains(directory))
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                 "repository tree contains an unmanifested empty directory");
    result.payload_sha256 = hex(payload.finish());
    return result;
}

struct RuntimeRepositoryUpdater::Impl final {
    explicit Impl(std::filesystem::path requested_root,
                  std::shared_ptr<RuntimeRepositoryUpdaterHooks> requested_hooks)
        : hooks(requested_hooks ? std::move(requested_hooks) : std::make_shared<NoopHooks>()) {
        if (requested_root.empty())
            fail(RuntimeRepositoryUpdateErrorCode::InvalidPlan, "state root is empty");
        std::filesystem::create_directories(requested_root);
        if (reparse_or_link(requested_root) || !std::filesystem::is_directory(requested_root))
            fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed, "state root is not plain");
        root = std::filesystem::canonical(requested_root);
        for (const auto name : {"staging", "objects", "snapshots"})
            require_plain_directory(root / name);
        for (const auto name : {"resources", "scripts"})
            require_plain_directory(root / "objects" / name);
    }

    [[nodiscard]] std::shared_ptr<const RuntimeRepositorySnapshot> current_pin() const {
        std::scoped_lock lock(pin_mutex);
        return active;
    }

    void set_pin(std::shared_ptr<const RuntimeRepositorySnapshot> value) {
        std::scoped_lock lock(pin_mutex);
        active = std::move(value);
    }

    [[nodiscard]] std::shared_ptr<const RuntimeRepositorySnapshot> load_pin() {
        if (!path_present(root / "current.json"))
            return {};
        return RuntimeRepositorySnapshot::activate(root);
    }

    void checkpoint(const RuntimeRepositoryUpdaterCheckpoint value) { hooks->checkpoint(value); }

    void check_expectation(const std::optional<Pointer>& current,
                           const ExpectedCurrent& expected) const {
        if (expected.kind == ExpectedCurrentKind::Exact && !lower_hex(expected.generation, 64))
            fail(RuntimeRepositoryUpdateErrorCode::InvalidPlan,
                 "expected current generation is invalid");
        const bool matches = expected.kind == ExpectedCurrentKind::Any ||
                             (expected.kind == ExpectedCurrentKind::Absent && !current) ||
                             (expected.kind == ExpectedCurrentKind::Exact && current &&
                              current->generation == expected.generation);
        if (!matches)
            fail(RuntimeRepositoryUpdateErrorCode::CurrentConflict,
                 "runtime repository current generation conflict");
    }

    void recover_pending(const RuntimeRepositoryTreeValidator& validator) {
        const auto path = root / ".publish-journal.json";
        if (!path_present(path))
            return;
        bool current_replaced = false;
        try {
            const auto journal =
                parse_journal(JsonParser(read_anchored_file(root, path.filename())).parse());
            validate_pointer_objects(root, journal.new_current, validator, {});
            if (journal.new_previous)
                validate_pointer_objects(root, *journal.new_previous, validator, {});
            hooks->before_file_operation(RuntimeRepositoryFileOperation::PreviousPointerReplace);
            write_optional_pointer(root / "previous.json", journal.new_previous);
            hooks->before_file_operation(RuntimeRepositoryFileOperation::CurrentPointerReplace);
            write_atomically(root / "current.json", pointer_json(journal.new_current),
                             &current_replaced);
            hooks->before_file_operation(RuntimeRepositoryFileOperation::JournalRemove);
            std::error_code error;
            std::filesystem::remove(path, error);
            if (error)
                fail(RuntimeRepositoryUpdateErrorCode::RecoveryFailed,
                     "journal cleanup failed after recovered commit");
            sync_directory(root);
            set_pin(load_pin());
        } catch (const UpdateFailure& error) {
            if (!current_replaced)
                throw;
            try {
                set_pin(load_pin());
            } catch (...) {
            }
            throw UpdateFailure(error.code(), error.what(),
                                PublishDisposition::CommittedDurabilityUncertain);
        } catch (const std::exception& error) {
            if (!current_replaced)
                throw;
            try {
                set_pin(load_pin());
            } catch (...) {
            }
            throw UpdateFailure(RuntimeRepositoryUpdateErrorCode::RecoveryFailed, error.what(),
                                PublishDisposition::CommittedDurabilityUncertain);
        }
    }

    [[nodiscard]] std::filesystem::path allocate_staging(const RuntimeRepositoryId id) {
        const auto staging = root / "staging";
        for (std::size_t attempt = 0; attempt < 64; ++attempt) {
            const auto path =
                staging /
                (id_name(id) + "-" +
                 std::to_string(temporary_counter.fetch_add(1, std::memory_order_relaxed)));
            std::error_code error;
            if (std::filesystem::create_directory(path, error))
                return path;
            if (error && error != std::errc::file_exists)
                fail(RuntimeRepositoryUpdateErrorCode::Io, "staging directory creation failed");
        }
        fail(RuntimeRepositoryUpdateErrorCode::Io, "staging directory allocation exhausted");
    }

    void cleanup_staging(const std::vector<std::filesystem::path>& paths) noexcept {
        for (const auto& path : paths) {
            std::error_code ignored;
            if (!path.empty())
                std::filesystem::remove_all(path, ignored);
        }
    }

    [[nodiscard]] RuntimeRepository
    commit_candidate(const RepositoryFetchSpec& spec, const std::filesystem::path& staging,
                     const RepositoryTreeSeal& staged_seal,
                     const RuntimeRepositoryTreeValidator& validator,
                     const std::stop_token stop_token) {
        const auto object = object_path(root, spec);
        if (path_present(object)) {
            const auto existing = validate_tree(validator, spec, object, stop_token);
            validate_seal(existing, spec);
            ensure_same_seal(staged_seal, existing);
            std::filesystem::remove_all(staging);
        } else {
            const auto staged_identity = directory_identity(staging);
            const auto install_result = install_directory_no_replace(staging, object);
            if (install_result == InstallResult::Existing) {
                const auto existing = validate_tree(validator, spec, object, stop_token);
                validate_seal(existing, spec);
                ensure_same_seal(staged_seal, existing);
                std::filesystem::remove_all(staging);
            }
            try {
                const auto installed = validate_tree(validator, spec, object, stop_token);
                validate_seal(installed, spec);
                ensure_same_seal(staged_seal, installed);
            } catch (...) {
                if (install_result == InstallResult::Installed)
                    discard_installed_object_if_unchanged(object, staged_identity);
                throw;
            }
            sync_directory(object.parent_path());
        }
        return RuntimeRepository{id_name(spec.id), spec.exact_commit,
                                 "objects/" + id_name(spec.id) + "/" + spec.exact_commit,
                                 spec.manifest, spec.expected_manifest_sha256};
    }

    [[nodiscard]] RuntimeRepositoryUpdateResult
    transaction(Journal journal, std::shared_ptr<const RuntimeRepositorySnapshot> old_pin,
                const std::stop_token stop_token) {
        bool current_replaced = false;
        try {
            hooks->before_file_operation(RuntimeRepositoryFileOperation::PreparedJournalReplace);
            write_atomically(root / ".publish-journal.json", journal_json(journal));
            checkpoint(RuntimeRepositoryUpdaterCheckpoint::JournalPrepared);
            hooks->before_file_operation(RuntimeRepositoryFileOperation::PreviousPointerReplace);
            write_optional_pointer(root / "previous.json", journal.new_previous);
            journal.phase = "previous_replaced";
            hooks->before_file_operation(
                RuntimeRepositoryFileOperation::PreviousPhaseJournalReplace);
            write_atomically(root / ".publish-journal.json", journal_json(journal));
            checkpoint(RuntimeRepositoryUpdaterCheckpoint::PreviousReplaced);
            check_cancelled(stop_token);
            checkpoint(RuntimeRepositoryUpdaterCheckpoint::BeforeCurrentReplace);
            hooks->before_file_operation(RuntimeRepositoryFileOperation::CurrentPointerReplace);
            write_atomically(root / "current.json", pointer_json(journal.new_current),
                             &current_replaced);
            hooks->committed(RuntimeRepositoryUpdaterCheckpoint::CurrentReplaced);

            journal.phase = "current_replaced";
            try {
                hooks->before_file_operation(
                    RuntimeRepositoryFileOperation::CurrentPhaseJournalReplace);
                write_atomically(root / ".publish-journal.json", journal_json(journal));
                hooks->before_file_operation(RuntimeRepositoryFileOperation::JournalRemove);
                std::error_code error;
                std::filesystem::remove(root / ".publish-journal.json", error);
                if (error)
                    fail(RuntimeRepositoryUpdateErrorCode::Io, "journal removal failed");
                sync_directory(root);
                auto pin = load_pin();
                set_pin(pin);
                hooks->committed(RuntimeRepositoryUpdaterCheckpoint::JournalRemoved);
                return RuntimeRepositoryUpdateResult{std::nullopt, PublishDisposition::Committed,
                                                     pin->generation(), std::move(pin)};
            } catch (const std::exception& error) {
                auto pin = old_pin;
                try {
                    pin = load_pin();
                    set_pin(pin);
                } catch (...) {
                }
                return unknown_error_result(error, PublishDisposition::CommittedDurabilityUncertain,
                                            std::move(pin), *hooks);
            }
        } catch (const UpdateFailure& error) {
            if (!current_replaced) {
                bool restored = true;
                try {
                    write_optional_pointer(root / "previous.json", journal.old_previous);
                    write_optional_pointer(root / "current.json", journal.old_current);
                    std::error_code cleanup_error;
                    std::filesystem::remove(root / ".publish-journal.json", cleanup_error);
                    if (cleanup_error)
                        restored = false;
                    sync_directory(root);
                } catch (...) {
                    restored = false;
                }
                return error_result(error,
                                     restored ? PublishDisposition::NotCommitted
                                              : PublishDisposition::CommittedDurabilityUncertain,
                                     std::move(old_pin), *hooks);
            }
            return error_result(error, PublishDisposition::CommittedDurabilityUncertain,
                                std::move(old_pin), *hooks);
        } catch (const std::exception& error) {
            bool restored = !current_replaced;
            if (!current_replaced) {
                try {
                    write_optional_pointer(root / "previous.json", journal.old_previous);
                    write_optional_pointer(root / "current.json", journal.old_current);
                    std::error_code cleanup_error;
                    std::filesystem::remove(root / ".publish-journal.json", cleanup_error);
                    if (cleanup_error)
                        restored = false;
                    sync_directory(root);
                } catch (...) {
                    restored = false;
                }
            }
            return unknown_error_result(error,
                                        current_replaced || !restored
                                             ? PublishDisposition::CommittedDurabilityUncertain
                                             : PublishDisposition::NotCommitted,
                                         std::move(old_pin), *hooks);
        }
    }

    std::filesystem::path root;
    std::shared_ptr<RuntimeRepositoryUpdaterHooks> hooks;
    mutable std::mutex pin_mutex;
    std::shared_ptr<const RuntimeRepositorySnapshot> active;
    std::mutex writer;
};

RuntimeRepositoryUpdater::RuntimeRepositoryUpdater(
    std::filesystem::path state_root, std::shared_ptr<RuntimeRepositoryUpdaterHooks> hooks)
    : impl_(std::make_unique<Impl>(std::move(state_root), std::move(hooks))) {
    impl_->set_pin(impl_->load_pin());
}

RuntimeRepositoryUpdater::~RuntimeRepositoryUpdater() = default;

RuntimeRepositoryUpdateResult RuntimeRepositoryUpdater::update(
    const RuntimeRepositoryUpdatePlanProvider& plan_provider,
    RuntimeRepositoryFetchBackend& fetch_backend, const RuntimeRepositoryTreeValidator& validator,
    ExpectedCurrent expected_current, const std::stop_token stop_token) {
    auto old_pin = impl_->current_pin();
    std::unique_lock process_lock(impl_->writer, std::try_to_lock);
    if (!process_lock.owns_lock()) {
        const UpdateFailure error(RuntimeRepositoryUpdateErrorCode::Busy,
                                  "repository writer is busy");
        return error_result(error, PublishDisposition::NotCommitted, std::move(old_pin),
                            *impl_->hooks);
    }
    std::vector<std::filesystem::path> staging;
    try {
        NativeWriterLock file_lock(impl_->root / ".writer.lock");
        impl_->recover_pending(validator);
        impl_->set_pin(impl_->load_pin());
        old_pin = impl_->current_pin();
        check_cancelled(stop_token);
        RuntimeRepositoryUpdatePlan plan;
        try {
            plan = plan_provider.trusted_plan();
        } catch (const UpdateFailure&) {
            throw;
        } catch (const std::exception& error) {
            fail(RuntimeRepositoryUpdateErrorCode::InvalidPlan, error.what());
        }
        validate_plan(plan);
        impl_->checkpoint(RuntimeRepositoryUpdaterCheckpoint::PlanValidated);
        const auto old_current = read_pointer(impl_->root / "current.json");
        impl_->check_expectation(old_current, expected_current);
        if (old_current)
            validate_pointer_objects(impl_->root, *old_current, validator, stop_token);

        std::array<RepositoryTreeSeal, 2> seals;
        for (std::size_t index = 0; index < plan.repositories.size(); ++index) {
            check_cancelled(stop_token);
            const auto& spec = plan.repositories[index];
            const auto directory = impl_->allocate_staging(spec.id);
            staging.push_back(directory);
            RepositoryStageResult stage;
            try {
                stage = fetch_backend.stage_exact(spec, directory, stop_token);
            } catch (const RuntimeRepositoryFetchCancelled&) {
                fail(RuntimeRepositoryUpdateErrorCode::Cancelled,
                     "repository fetch observed cancellation");
            } catch (const UpdateFailure&) {
                throw;
            } catch (const std::exception& error) {
                fail(RuntimeRepositoryUpdateErrorCode::FetchFailed, error.what());
            }
            check_cancelled(stop_token);
            validate_private_staging(impl_->root, directory);
            if (stage.resolved_commit != spec.exact_commit)
                fail(RuntimeRepositoryUpdateErrorCode::CommitMismatch,
                     "fetch backend resolved a different commit");
            impl_->checkpoint(index == 0 ? RuntimeRepositoryUpdaterCheckpoint::ResourcesStaged
                                         : RuntimeRepositoryUpdaterCheckpoint::ScriptsStaged);
            try {
                seals[index] = validator.validate_and_seal(spec, directory, stop_token);
            } catch (const UpdateFailure&) {
                throw;
            } catch (const std::exception& error) {
                fail(RuntimeRepositoryUpdateErrorCode::ValidationFailed, error.what());
            }
            validate_seal(seals[index], spec);
            sync_plain_tree(directory);
        }
        impl_->checkpoint(RuntimeRepositoryUpdaterCheckpoint::CandidatesSealed);
        check_cancelled(stop_token);

        std::array<RuntimeRepository, 2> repositories;
        for (std::size_t index = 0; index < repositories.size(); ++index)
            repositories[index] = impl_->commit_candidate(plan.repositories[index], staging[index],
                                                          seals[index], validator, stop_token);
        impl_->checkpoint(RuntimeRepositoryUpdaterCheckpoint::ObjectsCommitted);
        staging.clear();

        const auto generation = runtime_repository_generation(repositories);
        if (old_current && old_current->generation == generation) {
            auto pin = impl_->load_pin();
            impl_->set_pin(pin);
            return RuntimeRepositoryUpdateResult{std::nullopt, PublishDisposition::Committed,
                                                 pin->generation(), std::move(pin)};
        }
        const Pointer pointer{generation, "snapshots/" + generation + ".json"};
        install_immutable_json(impl_->root, impl_->root / pointer.snapshot,
                               snapshot_json(generation, repositories));
        impl_->checkpoint(RuntimeRepositoryUpdaterCheckpoint::SnapshotInstalled);
        check_cancelled(stop_token);
        const auto old_previous = read_pointer(impl_->root / "previous.json");
        Journal journal{"publish", "prepared", old_previous, old_current, old_current, pointer};
        return impl_->transaction(std::move(journal), std::move(old_pin), stop_token);
    } catch (const UpdateFailure& error) {
        impl_->cleanup_staging(staging);
        if (error.disposition() != PublishDisposition::NotCommitted)
            old_pin = impl_->current_pin();
        return error_result(error, error.disposition(), std::move(old_pin), *impl_->hooks);
    } catch (const std::exception& error) {
        impl_->cleanup_staging(staging);
        return unknown_error_result(error, PublishDisposition::NotCommitted, std::move(old_pin),
                                    *impl_->hooks);
    }
}

RuntimeRepositoryUpdateResult RuntimeRepositoryUpdater::rollback(ExpectedCurrent expected_current,
                                                                 const std::stop_token stop_token) {
    auto old_pin = impl_->current_pin();
    std::unique_lock process_lock(impl_->writer, std::try_to_lock);
    if (!process_lock.owns_lock()) {
        const UpdateFailure error(RuntimeRepositoryUpdateErrorCode::Busy,
                                  "repository writer is busy");
        return error_result(error, PublishDisposition::NotCommitted, std::move(old_pin),
                            *impl_->hooks);
    }
    try {
        NativeWriterLock file_lock(impl_->root / ".writer.lock");
        StrictRuntimeRepositoryTreeValidator validator;
        impl_->recover_pending(validator);
        impl_->set_pin(impl_->load_pin());
        old_pin = impl_->current_pin();
        check_cancelled(stop_token);
        const auto current = read_pointer(impl_->root / "current.json");
        impl_->check_expectation(current, expected_current);
        if (!current)
            fail(RuntimeRepositoryUpdateErrorCode::CurrentConflict, "current generation is absent");
        const auto previous = read_pointer(impl_->root / "previous.json");
        if (!previous)
            fail(RuntimeRepositoryUpdateErrorCode::NoPrevious,
                 "previous generation is unavailable");
        validate_pointer_objects(impl_->root, *current, validator, stop_token);
        validate_pointer_objects(impl_->root, *previous, validator, stop_token);
        Journal journal{"rollback", "prepared", previous, current, current, *previous};
        return impl_->transaction(std::move(journal), std::move(old_pin), stop_token);
    } catch (const UpdateFailure& error) {
        if (error.disposition() != PublishDisposition::NotCommitted)
            old_pin = impl_->current_pin();
        return error_result(error, error.disposition(), std::move(old_pin), *impl_->hooks);
    } catch (const std::exception& error) {
        return unknown_error_result(error, PublishDisposition::NotCommitted, std::move(old_pin),
                                    *impl_->hooks);
    }
}

std::shared_ptr<const RuntimeRepositorySnapshot> RuntimeRepositoryUpdater::pin_current() const {
    return impl_->current_pin();
}

const std::filesystem::path& RuntimeRepositoryUpdater::state_root() const noexcept {
    return impl_->root;
}

} // namespace baas::runtime::repository
