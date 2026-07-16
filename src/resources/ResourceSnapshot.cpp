#include "resources/ResourceSnapshot.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

namespace baas::resources {
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
            if (block_size_ == block_.size()) {
                transform();
                block_size_ = 0;
            }
        }
    }

    [[nodiscard]] Digest finish() noexcept
    {
        const auto bit_count = total_bytes_ * 8U;
        block_[block_size_++] = std::byte{0x80};
        if (block_size_ > 56) {
            while (block_size_ < block_.size()) block_[block_size_++] = std::byte{0};
            transform();
            block_size_ = 0;
        }
        while (block_size_ < 56) block_[block_size_++] = std::byte{0};
        for (std::size_t index = 0; index < 8; ++index) {
            block_[63 - index] = static_cast<std::byte>(bit_count >> (index * 8U));
        }
        transform();
        Digest result{};
        for (std::size_t word = 0; word < state_.size(); ++word) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                result[word * 4 + byte] = static_cast<std::byte>(
                    state_[word] >> ((3U - byte) * 8U));
            }
        }
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
            const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto choice = (e & f) ^ (~e & g);
            const auto temporary1 = h + sum1 + choice + sha256_constants[index] + schedule[index];
            const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g; g = f; f = e; e = d + temporary1;
            d = c; c = b; b = a; a = temporary1 + temporary2;
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

[[nodiscard]] Digest sha256(const std::span<const std::byte> input) noexcept
{
    Sha256 hash;
    hash.update(input);
    return hash.finish();
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

[[nodiscard]] bool valid_token(
    const std::string_view value, const std::size_t max_bytes) noexcept
{
    if (value.empty() || value.size() > max_bytes) return false;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!(byte >= 'A' && byte <= 'Z') && !(byte >= 'a' && byte <= 'z') &&
            !(byte >= '0' && byte <= '9') && character != '-' && character != '_')
            return false;
    }
    return value != "." && value != "..";
}

[[nodiscard]] bool valid_media_type(
    const std::string_view value, const std::size_t max_bytes) noexcept
{
    if (value.empty() || value.size() > max_bytes) return false;
    const auto slash = value.find('/');
    if (slash == 0 || slash == std::string_view::npos || slash + 1 == value.size() ||
        value.find('/', slash + 1) != std::string_view::npos) return false;
    for (const auto character : value) {
        if (character == '/') continue;
        if (!(character >= 'a' && character <= 'z') &&
            !(character >= '0' && character <= '9') && character != '-' &&
            character != '+' && character != '.') return false;
    }
    return true;
}

[[nodiscard]] bool valid_digest(const std::string_view value) noexcept
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

void hash_length_prefixed(Sha256& hash, const std::string_view value)
{
    std::array<std::byte, 8> length{};
    const auto size = static_cast<std::uint64_t>(value.size());
    for (std::size_t index = 0; index < length.size(); ++index)
        length[7 - index] = static_cast<std::byte>(size >> (index * 8U));
    hash.update(length);
    hash.update(std::as_bytes(std::span(value.data(), value.size())));
}

using VariantKey = std::tuple<std::string, std::string, std::string>;

[[nodiscard]] VariantKey key_for(const ResourcePayload& payload)
{
    return {payload.resource_id, payload.locale.value_or(""), payload.activity.value_or("")};
}

}  // namespace

struct ResourceSnapshot::Impl {
    ResourceSelector selector;
    std::string snapshot_id;
    std::uint64_t numeric_snapshot_id{};
    std::size_t retained_bytes{};
    std::map<VariantKey, std::shared_ptr<const ResourceEntry>, std::less<>> entries;
};

std::string_view resource_error_code_name(const ResourceErrorCode code) noexcept
{
    using enum ResourceErrorCode;
    switch (code) {
        case InvalidLimits: return "RES001_INVALID_LIMITS";
        case EntryLimitExceeded: return "RES002_ENTRY_LIMIT_EXCEEDED";
        case ByteLimitExceeded: return "RES003_BYTE_LIMIT_EXCEEDED";
        case InvalidResourceId: return "RES004_INVALID_RESOURCE_ID";
        case InvalidLocale: return "RES005_INVALID_LOCALE";
        case InvalidActivity: return "RES006_INVALID_ACTIVITY";
        case InvalidMediaType: return "RES007_INVALID_MEDIA_TYPE";
        case InvalidDigest: return "RES008_INVALID_DIGEST";
        case SizeMismatch: return "RES009_SIZE_MISMATCH";
        case DigestMismatch: return "RES010_DIGEST_MISMATCH";
        case DuplicateVariant: return "RES011_DUPLICATE_VARIANT";
    }
    return "RES000_UNKNOWN";
}

ResourceError::ResourceError(const ResourceErrorCode code, std::string message)
    : std::runtime_error(std::string(resource_error_code_name(code)) + ": " + message),
      code_(code)
{
}

bool valid_resource_id(const std::string_view value, const std::size_t max_bytes) noexcept
{
    if (value.empty() || value.size() > max_bytes || value.front() == '/' ||
        value.back() == '/' || value.find('\\') != std::string_view::npos ||
        value.find(':') != std::string_view::npos || value.find('\0') != std::string_view::npos)
        return false;
    std::size_t begin{};
    while (begin < value.size()) {
        const auto end = value.find('/', begin);
        const auto segment = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (segment.empty() || segment == "." || segment == ".." ||
            segment.back() == '.' || segment.back() == ' ') return false;
        for (const auto character : segment) {
            if (!(character >= 'a' && character <= 'z') &&
                !(character >= '0' && character <= '9') && character != '-' &&
                character != '_' && character != '.') return false;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return true;
}

bool valid_resource_locale(const std::string_view value, const std::size_t max_bytes) noexcept
{
    return valid_token(value, max_bytes);
}

bool valid_resource_activity(const std::string_view value, const std::size_t max_bytes) noexcept
{
    return valid_token(value, max_bytes);
}

std::string sha256_hex(const std::span<const std::byte> bytes)
{
    return hex(sha256(bytes));
}

ResourceEntry::ResourceEntry(ResourcePayload payload) : payload_(std::move(payload)) {}
const std::string& ResourceEntry::resource_id() const noexcept { return payload_.resource_id; }
const std::optional<std::string>& ResourceEntry::locale() const noexcept { return payload_.locale; }
const std::optional<std::string>& ResourceEntry::activity() const noexcept { return payload_.activity; }
const std::string& ResourceEntry::media_type() const noexcept { return payload_.media_type; }
const std::string& ResourceEntry::sha256() const noexcept { return payload_.sha256; }
std::span<const std::byte> ResourceEntry::bytes() const noexcept { return *payload_.bytes; }
std::size_t ResourceEntry::retained_bytes() const noexcept { return payload_.bytes->size(); }

ResourceSnapshot::ResourceSnapshot(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
ResourceSnapshot::~ResourceSnapshot() = default;

std::shared_ptr<const ResourceSnapshot> ResourceSnapshot::build(
    ResourceSelector selector,
    std::vector<ResourcePayload> payloads,
    const ResourceSnapshotLimits limits)
{
    if (limits.max_entries == 0 || limits.max_total_bytes == 0 ||
        limits.max_entry_bytes == 0 || limits.max_resource_id_bytes == 0 ||
        limits.max_selector_bytes == 0 || limits.max_media_type_bytes == 0 ||
        limits.max_entry_bytes > limits.max_total_bytes)
        throw ResourceError(ResourceErrorCode::InvalidLimits, "resource limits must be non-zero and ordered");
    if (!valid_resource_locale(selector.locale, limits.max_selector_bytes))
        throw ResourceError(ResourceErrorCode::InvalidLocale, "snapshot locale is not canonical");
    if (selector.current_activity &&
        !valid_resource_activity(*selector.current_activity, limits.max_selector_bytes))
        throw ResourceError(ResourceErrorCode::InvalidActivity, "snapshot activity is not canonical");
    if (payloads.size() > limits.max_entries)
        throw ResourceError(ResourceErrorCode::EntryLimitExceeded, "resource entry limit exceeded");

    auto impl = std::make_unique<Impl>();
    impl->selector = std::move(selector);
    std::sort(payloads.begin(), payloads.end(), [](const auto& left, const auto& right) {
        return key_for(left) < key_for(right);
    });
    Sha256 identity;
    hash_length_prefixed(identity, "baas.resource.snapshot/v1");
    hash_length_prefixed(identity, impl->selector.locale);
    hash_length_prefixed(identity, impl->selector.current_activity.value_or(""));

    std::optional<VariantKey> previous;
    std::size_t total_bytes{};
    for (auto& payload : payloads) {
        const auto key = key_for(payload);
        if (previous && *previous == key)
            throw ResourceError(ResourceErrorCode::DuplicateVariant, "duplicate resource variant");
        previous = key;
        if (!valid_resource_id(payload.resource_id, limits.max_resource_id_bytes))
            throw ResourceError(ResourceErrorCode::InvalidResourceId, "resource id is not canonical");
        if (payload.locale && !valid_resource_locale(*payload.locale, limits.max_selector_bytes))
            throw ResourceError(ResourceErrorCode::InvalidLocale, "resource locale is not canonical");
        if (payload.activity && !valid_resource_activity(*payload.activity, limits.max_selector_bytes))
            throw ResourceError(ResourceErrorCode::InvalidActivity, "resource activity is not canonical");
        if (!valid_media_type(payload.media_type, limits.max_media_type_bytes))
            throw ResourceError(ResourceErrorCode::InvalidMediaType, "resource media type is not canonical");
        if (!valid_digest(payload.sha256))
            throw ResourceError(ResourceErrorCode::InvalidDigest, "resource digest is not lowercase SHA-256");
        if (!payload.bytes)
            throw ResourceError(ResourceErrorCode::SizeMismatch, "resource bytes are absent");
        if (payload.declared_size != payload.bytes->size())
            throw ResourceError(ResourceErrorCode::SizeMismatch, "resource size does not match declaration");
        if (payload.bytes->size() > limits.max_entry_bytes ||
            payload.bytes->size() > limits.max_total_bytes - std::min(total_bytes, limits.max_total_bytes))
            throw ResourceError(ResourceErrorCode::ByteLimitExceeded, "resource byte limit exceeded");
        total_bytes += payload.bytes->size();
        if (sha256_hex(*payload.bytes) != payload.sha256)
            throw ResourceError(ResourceErrorCode::DigestMismatch, "resource digest does not match bytes");

        hash_length_prefixed(identity, payload.resource_id);
        hash_length_prefixed(identity, payload.locale.value_or(""));
        hash_length_prefixed(identity, payload.activity.value_or(""));
        hash_length_prefixed(identity, payload.media_type);
        hash_length_prefixed(identity, payload.sha256);
        hash_length_prefixed(identity, std::to_string(payload.declared_size));
        auto entry = std::shared_ptr<const ResourceEntry>(new ResourceEntry(std::move(payload)));
        impl->entries.emplace(key, std::move(entry));
    }
    impl->retained_bytes = total_bytes;
    const auto digest = identity.finish();
    impl->snapshot_id = hex(digest);
    for (std::size_t index = 0; index < 8; ++index)
        impl->numeric_snapshot_id = (impl->numeric_snapshot_id << 8U) |
            std::to_integer<std::uint64_t>(digest[index]);
    if (impl->numeric_snapshot_id == 0) impl->numeric_snapshot_id = 1;
    return std::shared_ptr<const ResourceSnapshot>(new ResourceSnapshot(std::move(impl)));
}

std::shared_ptr<const ResourceEntry> ResourceSnapshot::resolve(
    const std::string_view resource_id,
    const std::optional<std::string_view> locale_override) const
{
    if (!valid_resource_id(resource_id) ||
        (locale_override && !valid_resource_locale(*locale_override))) return {};
    const auto locale = std::string(locale_override.value_or(impl_->selector.locale));
    const auto activity = impl_->selector.current_activity.value_or("");
    const std::array<VariantKey, 4> candidates{
        VariantKey{std::string(resource_id), locale, activity},
        VariantKey{std::string(resource_id), locale, ""},
        VariantKey{std::string(resource_id), "", activity},
        VariantKey{std::string(resource_id), "", ""},
    };
    for (const auto& candidate : candidates) {
        const auto found = impl_->entries.find(candidate);
        if (found != impl_->entries.end()) return found->second;
    }
    return {};
}

const ResourceSelector& ResourceSnapshot::selector() const noexcept { return impl_->selector; }
const std::string& ResourceSnapshot::snapshot_id() const noexcept { return impl_->snapshot_id; }
std::uint64_t ResourceSnapshot::numeric_snapshot_id() const noexcept { return impl_->numeric_snapshot_id; }
std::size_t ResourceSnapshot::entry_count() const noexcept { return impl_->entries.size(); }
std::size_t ResourceSnapshot::retained_bytes() const noexcept { return impl_->retained_bytes; }

}  // namespace baas::resources
