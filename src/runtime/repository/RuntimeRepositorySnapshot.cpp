#include "runtime/repository/RuntimeRepositorySnapshot.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <span>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace baas::runtime::repository {
namespace {

using Digest = std::array<std::byte, 32>;

constexpr std::array<std::uint32_t, 64> sha256_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

class Sha256 final {
public:
    void update(const std::span<const std::byte> input) noexcept
    {
        for (const auto value : input) {
            block_[block_size_++] = value;
            ++total_bytes_;
            if (block_size_ == block_.size()) { transform(); block_size_ = 0; }
        }
    }

    [[nodiscard]] Digest finish() noexcept
    {
        const auto bits = total_bytes_ * 8U;
        block_[block_size_++] = std::byte{0x80};
        if (block_size_ > 56) {
            while (block_size_ < block_.size()) block_[block_size_++] = std::byte{0};
            transform(); block_size_ = 0;
        }
        while (block_size_ < 56) block_[block_size_++] = std::byte{0};
        for (std::size_t index = 0; index < 8; ++index)
            block_[63 - index] = static_cast<std::byte>(bits >> (index * 8U));
        transform();
        Digest result{};
        for (std::size_t word = 0; word < state_.size(); ++word)
            for (std::size_t byte = 0; byte < 4; ++byte)
                result[word * 4 + byte] = static_cast<std::byte>(
                    state_[word] >> ((3U - byte) * 8U));
        return result;
    }

private:
    void transform() noexcept
    {
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t index = 0; index < 16; ++index) {
            const auto offset = index * 4;
            schedule[index] =
                (std::to_integer<std::uint32_t>(block_[offset]) << 24U) |
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
        auto a = state_[0]; auto b = state_[1]; auto c = state_[2]; auto d = state_[3];
        auto e = state_[4]; auto f = state_[5]; auto g = state_[6]; auto h = state_[7];
        for (std::size_t index = 0; index < schedule.size(); ++index) {
            const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto t1 = h + s1 + ((e & f) ^ (~e & g)) +
                sha256_constants[index] + schedule[index];
            const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::byte, 64> block_{};
    std::size_t block_size_{};
    std::uint64_t total_bytes_{};
};

void add_field(Sha256& hash, const std::string_view value) noexcept
{
    std::array<std::byte, 8> length{};
    const auto size = static_cast<std::uint64_t>(value.size());
    for (std::size_t index = 0; index < length.size(); ++index)
        length[7 - index] = static_cast<std::byte>(size >> (index * 8U));
    hash.update(length);
    hash.update(std::as_bytes(std::span(value.data(), value.size())));
}

[[nodiscard]] std::string hex(const Digest& digest)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t index = 0; index < digest.size(); ++index) {
        const auto value = std::to_integer<unsigned int>(digest[index]);
        result[index * 2] = digits[value >> 4U];
        result[index * 2 + 1] = digits[value & 0x0fU];
    }
    return result;
}

struct Json final {
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;
    std::variant<std::string, Array, Object> value;
};

class JsonParser final {
public:
    JsonParser(const std::string_view input, const RuntimeRepositoryLimits limits)
        : input_(input), limits_(limits) {}

    [[nodiscard]] Json parse()
    {
        auto result = value(1);
        whitespace();
        if (offset_ != input_.size()) fail("trailing JSON data");
        return result;
    }

private:
    [[noreturn]] void fail(const char* message) const
    {
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::InvalidJson, message);
    }

    void whitespace() noexcept
    {
        while (offset_ < input_.size() &&
               (input_[offset_] == ' ' || input_[offset_] == '\t' ||
                input_[offset_] == '\r' || input_[offset_] == '\n')) ++offset_;
    }

    [[nodiscard]] char take()
    {
        if (offset_ == input_.size()) fail("unexpected end of JSON");
        return input_[offset_++];
    }

    [[nodiscard]] static int hex_digit(const char character) noexcept
    {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    }

    [[nodiscard]] std::string string()
    {
        if (take() != '"') fail("JSON string expected");
        std::string result;
        for (;;) {
            const auto character = take();
            if (character == '"') return result;
            if (static_cast<unsigned char>(character) < 0x20U) fail("control byte in JSON string");
            char decoded = character;
            if (character == '\\') {
                const auto escape = take();
                switch (escape) {
                    case '"': decoded = '"'; break;
                    case '\\': decoded = '\\'; break;
                    case '/': decoded = '/'; break;
                    case 'b': decoded = '\b'; break;
                    case 'f': decoded = '\f'; break;
                    case 'n': decoded = '\n'; break;
                    case 'r': decoded = '\r'; break;
                    case 't': decoded = '\t'; break;
                    case 'u': {
                        unsigned int code{};
                        for (int index = 0; index < 4; ++index) {
                            const auto digit = hex_digit(take());
                            if (digit < 0) fail("invalid JSON unicode escape");
                            code = (code << 4U) | static_cast<unsigned int>(digit);
                        }
                        if (code > 0x7fU) fail("repository JSON strings must be ASCII");
                        decoded = static_cast<char>(code);
                        break;
                    }
                    default: fail("invalid JSON escape");
                }
            } else if (static_cast<unsigned char>(character) > 0x7fU) {
                fail("repository JSON strings must be ASCII");
            }
            if (result.size() == limits_.max_json_string_bytes)
                fail("JSON string limit exceeded");
            result.push_back(decoded);
        }
    }

    [[nodiscard]] Json value(const std::size_t depth)
    {
        whitespace();
        if (depth > limits_.max_json_depth || ++nodes_ > limits_.max_json_nodes)
            fail("JSON structure limit exceeded");
        if (offset_ == input_.size()) fail("JSON value expected");
        if (input_[offset_] == '"') return Json{string()};
        if (input_[offset_] == '[') return Json{array(depth)};
        if (input_[offset_] == '{') return Json{object(depth)};
        fail("repository JSON permits only strings, arrays, and objects");
    }

    [[nodiscard]] Json::Array array(const std::size_t depth)
    {
        static_cast<void>(take());
        Json::Array result;
        whitespace();
        if (offset_ < input_.size() && input_[offset_] == ']') { ++offset_; return result; }
        for (;;) {
            result.push_back(value(depth + 1));
            whitespace();
            const auto separator = take();
            if (separator == ']') return result;
            if (separator != ',') fail("array separator expected");
        }
    }

    [[nodiscard]] Json::Object object(const std::size_t depth)
    {
        static_cast<void>(take());
        Json::Object result;
        whitespace();
        if (offset_ < input_.size() && input_[offset_] == '}') { ++offset_; return result; }
        for (;;) {
            whitespace();
            if (offset_ == input_.size() || input_[offset_] != '"') fail("object key expected");
            auto key = string();
            if (std::ranges::any_of(result, [&](const auto& item) { return item.first == key; }))
                fail("duplicate object key");
            whitespace();
            if (take() != ':') fail("object colon expected");
            result.emplace_back(std::move(key), value(depth + 1));
            whitespace();
            const auto separator = take();
            if (separator == '}') return result;
            if (separator != ',') fail("object separator expected");
        }
    }

    std::string_view input_;
    RuntimeRepositoryLimits limits_;
    std::size_t offset_{};
    std::size_t nodes_{};
};

[[nodiscard]] const Json::Object& object(const Json& value)
{
    const auto* result = std::get_if<Json::Object>(&value.value);
    if (!result) throw RuntimeRepositoryError(
        RuntimeRepositoryErrorCode::InvalidFieldSet, "JSON object expected");
    return *result;
}

[[nodiscard]] const Json::Array& array(const Json& value)
{
    const auto* result = std::get_if<Json::Array>(&value.value);
    if (!result) throw RuntimeRepositoryError(
        RuntimeRepositoryErrorCode::InvalidFieldSet, "JSON array expected");
    return *result;
}

[[nodiscard]] const std::string& member(
    const Json::Object& value, const std::string_view name)
{
    const auto found = std::ranges::find_if(value, [&](const auto& item) {
        return item.first == name;
    });
    if (found == value.end()) throw RuntimeRepositoryError(
        RuntimeRepositoryErrorCode::InvalidFieldSet, "required JSON field is absent");
    const auto* result = std::get_if<std::string>(&found->second.value);
    if (!result) throw RuntimeRepositoryError(
        RuntimeRepositoryErrorCode::InvalidFieldSet, "JSON field must be a string");
    return *result;
}

[[nodiscard]] const Json& any_member(
    const Json::Object& value, const std::string_view name)
{
    const auto found = std::ranges::find_if(value, [&](const auto& item) {
        return item.first == name;
    });
    if (found == value.end()) throw RuntimeRepositoryError(
        RuntimeRepositoryErrorCode::InvalidFieldSet, "required JSON field is absent");
    return found->second;
}

void exact_fields(const Json::Object& value, const std::span<const std::string_view> expected)
{
    if (value.size() != expected.size() ||
        !std::ranges::all_of(value, [&](const auto& item) {
            return std::ranges::find(expected, std::string_view(item.first)) != expected.end();
        }))
        throw RuntimeRepositoryError(
            RuntimeRepositoryErrorCode::InvalidFieldSet, "unknown or missing JSON field");
}

[[nodiscard]] bool lower_hex(const std::string_view value, const std::size_t size) noexcept
{
    return value.size() == size && std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');
    });
}

[[nodiscard]] bool manifest_name(const std::string_view value) noexcept
{
    return !value.empty() && value != "." && value != ".." &&
        std::ranges::all_of(value, [](const char character) {
            return (character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') || character == '_' ||
                character == '.' || character == '-';
        });
}

[[nodiscard]] bool within(
    const std::filesystem::path& root, const std::filesystem::path& candidate) noexcept
{
    const auto equal_component = [](const std::filesystem::path& left,
                                    const std::filesystem::path& right) noexcept {
#ifdef _WIN32
        return CompareStringOrdinal(
            left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
        return left == right;
#endif
    };
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end(); ++root_it, ++candidate_it)
        if (candidate_it == candidate.end() ||
            !equal_component(*candidate_it, *root_it)) return false;
    return true;
}

void reject_symlink(const std::filesystem::path& path)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || status.type() == std::filesystem::file_type::not_found)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::Io, "repository state path is absent");
    if (std::filesystem::is_symlink(status))
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::PathViolation, "repository state symlink is forbidden");
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::Io, "repository state attributes are unavailable");
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::PathViolation, "repository state reparse point is forbidden");
#endif
}

[[nodiscard]] std::filesystem::path checked_file(
    const std::filesystem::path& root, const std::filesystem::path& relative)
{
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.lexically_normal() != relative)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::PathViolation, "repository state path is not canonical");
    const auto candidate = root / relative;
    auto current = root;
    reject_symlink(current);
    for (const auto& component : relative) {
        current /= component;
        reject_symlink(current);
    }
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error) throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::Io, "repository root cannot be canonicalized");
    const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, error);
    if (error || !within(canonical_root, canonical_candidate))
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::PathViolation, "repository state path escaped its root");
    if (!std::filesystem::is_regular_file(candidate, error) || error)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::Io, "repository state file is not regular");
    return candidate;
}

[[nodiscard]] std::string read_bounded(
    const std::filesystem::path& path, const std::size_t maximum)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::Io, "repository state file cannot be opened");
    std::string result;
    std::array<char, 4096> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(input.gcount());
        if (count > maximum - std::min(maximum, result.size()))
            throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::FileLimitExceeded, "repository state file limit exceeded");
        result.append(buffer.data(), count);
    }
    if (!input.eof()) throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::Io, "repository state file read failed");
    return result;
}

[[nodiscard]] RuntimeRepository parse_repository(const Json& value)
{
    const auto& record = object(value);
    constexpr std::array fields{
        std::string_view{"id"}, std::string_view{"commit"}, std::string_view{"root"},
        std::string_view{"manifest"}, std::string_view{"manifest_sha256"}};
    exact_fields(record, fields);
    RuntimeRepository result{
        member(record, "id"), member(record, "commit"), member(record, "root"),
        member(record, "manifest"), member(record, "manifest_sha256")};
    if ((result.id != "resources" && result.id != "scripts") ||
        (!lower_hex(result.commit, 40) && !lower_hex(result.commit, 64)) ||
        result.root != "objects/" + result.id + "/" + result.commit ||
        !manifest_name(result.manifest) || !lower_hex(result.manifest_sha256, 64))
        throw RuntimeRepositoryError(
            RuntimeRepositoryErrorCode::InvalidRepository, "repository descriptor is invalid");
    return result;
}

}  // namespace

struct RuntimeRepositorySnapshot::Impl {
    std::string generation;
    std::array<RuntimeRepository, 2> repositories;
};

RuntimeRepositoryError::RuntimeRepositoryError(
    const RuntimeRepositoryErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

RuntimeRepositoryErrorCode RuntimeRepositoryError::code() const noexcept { return code_; }

std::string runtime_repository_generation(
    const std::array<RuntimeRepository, 2>& repositories)
{
    if (repositories[0].id != "resources" || repositories[1].id != "scripts")
        throw RuntimeRepositoryError(
            RuntimeRepositoryErrorCode::InvalidRepository, "repositories are not canonically ordered");
    Sha256 hash;
    add_field(hash, snapshot_schema);
    for (const auto& item : repositories) {
        add_field(hash, item.id);
        add_field(hash, item.commit);
        add_field(hash, item.root);
        add_field(hash, item.manifest);
        add_field(hash, item.manifest_sha256);
    }
    return hex(hash.finish());
}

std::shared_ptr<const RuntimeRepositorySnapshot> RuntimeRepositorySnapshot::activate(
    const std::filesystem::path& state_root, const RuntimeRepositoryLimits limits)
{
    if (limits.max_current_bytes == 0 || limits.max_snapshot_bytes == 0 ||
        limits.max_json_string_bytes == 0 || limits.max_json_nodes < 4 ||
        limits.max_json_depth < 3)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::FileLimitExceeded, "repository limits are invalid");

    // Windows replacement can briefly invalidate a path-based open between
    // metadata validation and opening the file. Retry that narrow I/O race;
    // malformed content and every policy failure remain fail-closed.
    const auto load_current = [&] {
        for (std::size_t attempt = 0; ; ++attempt) {
            try {
                const auto current_path = checked_file(state_root, "current.json");
                return JsonParser(
                    read_bounded(current_path, limits.max_current_bytes), limits).parse();
            } catch (const RuntimeRepositoryError& error) {
                if (error.code() != RuntimeRepositoryErrorCode::Io || attempt == 63) throw;
                std::this_thread::yield();
            }
        }
    };
    const auto current_document = load_current();
    const auto& current = object(current_document);
    constexpr std::array current_fields{
        std::string_view{"schema"}, std::string_view{"generation"},
        std::string_view{"snapshot"}};
    exact_fields(current, current_fields);
    if (member(current, "schema") != current_schema)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::InvalidSchema, "current schema is invalid");
    const auto generation = member(current, "generation");
    if (!lower_hex(generation, 64))
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::InvalidGeneration, "current generation is invalid");
    const auto expected_snapshot = "snapshots/" + generation + ".json";
    if (member(current, "snapshot") != expected_snapshot)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::PathViolation, "current snapshot path is invalid");

    const auto snapshot_path = checked_file(state_root, std::filesystem::path(expected_snapshot));
    const auto snapshot_document = JsonParser(
        read_bounded(snapshot_path, limits.max_snapshot_bytes), limits).parse();
    const auto& snapshot = object(snapshot_document);
    constexpr std::array snapshot_fields{
        std::string_view{"schema"}, std::string_view{"generation"},
        std::string_view{"repositories"}};
    exact_fields(snapshot, snapshot_fields);
    if (member(snapshot, "schema") != snapshot_schema)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::InvalidSchema, "snapshot schema is invalid");
    if (member(snapshot, "generation") != generation)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::IdentityMismatch, "snapshot generation differs from current");
    const auto& records = array(any_member(snapshot, "repositories"));
    if (records.size() != 2)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::InvalidRepository, "snapshot must contain exactly two repositories");
    std::array repositories{parse_repository(records[0]), parse_repository(records[1])};
    if (repositories[0].id != "resources" || repositories[1].id != "scripts")
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::InvalidRepository, "repositories are not canonically ordered");
    if (runtime_repository_generation(repositories) != generation)
        throw RuntimeRepositoryError(RuntimeRepositoryErrorCode::IdentityMismatch, "snapshot identity digest does not match");

    auto impl = std::make_unique<Impl>();
    impl->generation = generation;
    impl->repositories = std::move(repositories);
    return std::shared_ptr<const RuntimeRepositorySnapshot>(
        new RuntimeRepositorySnapshot(std::move(impl)));
}

RuntimeRepositorySnapshot::RuntimeRepositorySnapshot(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
RuntimeRepositorySnapshot::~RuntimeRepositorySnapshot() = default;
const std::string& RuntimeRepositorySnapshot::generation() const noexcept { return impl_->generation; }
const std::array<RuntimeRepository, 2>& RuntimeRepositorySnapshot::repositories() const noexcept
{
    return impl_->repositories;
}
const RuntimeRepository& RuntimeRepositorySnapshot::resources() const noexcept
{
    return impl_->repositories[0];
}
const RuntimeRepository& RuntimeRepositorySnapshot::scripts() const noexcept
{
    return impl_->repositories[1];
}

}  // namespace baas::runtime::repository
