#include "runtime/procedure/CoDetectSupportBundle.h"

#include "resources/ResourceSnapshot.h"

#include <miniz.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace baas::runtime::procedure {
namespace {

using Json = nlohmann::json;
namespace snapshot_resources = ::baas::resources;

struct LoadFailure final {
    CoDetectSupportBundleError error;
    json::StrictJsonError json_error{json::StrictJsonError::none};
    std::optional<std::size_t> member_index;
};

[[noreturn]] void fail(
    const CoDetectSupportBundleError error,
    const std::optional<std::size_t> member_index = std::nullopt,
    const json::StrictJsonError json_error = json::StrictJsonError::none)
{
    throw LoadFailure{error, json_error, member_index};
}

void check_cancelled(const std::stop_token stop)
{
    if (stop.stop_requested()) fail(CoDetectSupportBundleError::cancelled);
}

class WorkBudget final {
public:
    explicit WorkBudget(const std::size_t maximum) noexcept : remaining_(maximum) {}
    void charge(const std::size_t amount)
    {
        if (amount > remaining_) fail(CoDetectSupportBundleError::work_limit_exceeded);
        remaining_ -= amount;
    }
private:
    std::size_t remaining_;
};

[[nodiscard]] bool valid_limits(const CoDetectSupportBundleLimits& limits) noexcept
{
    constexpr std::size_t max_archive = 64U * 1024U * 1024U;
    constexpr std::size_t max_total = 256U * 1024U * 1024U;
    return limits.max_archive_bytes != 0 && limits.max_archive_bytes <= max_archive &&
        limits.max_entries != 0 && limits.max_entries <= 4'096 &&
        limits.max_manifest_bytes != 0 &&
        limits.max_manifest_bytes <= limits.max_member_bytes &&
        limits.max_member_bytes != 0 &&
        limits.max_member_bytes <= limits.max_total_uncompressed_bytes &&
        limits.max_total_uncompressed_bytes != 0 &&
        limits.max_total_uncompressed_bytes <= max_total &&
        limits.max_compression_ratio != 0 && limits.max_compression_ratio <= 4'096 &&
        limits.max_string_bytes != 0 && limits.max_string_bytes <= 16'384 &&
        limits.max_json_depth != 0 && limits.max_json_depth <= 128 &&
        limits.max_json_nodes != 0 && limits.max_features != 0 &&
        limits.max_rgb_samples != 0 && limits.max_png_chunks != 0 &&
        limits.max_png_width != 0 && limits.max_png_width <= 1'280 &&
        limits.max_png_height != 0 && limits.max_png_height <= 720 &&
        limits.max_decoded_png_bytes != 0 &&
        limits.max_decoded_png_bytes <= limits.max_total_decoded_png_bytes &&
        limits.max_total_decoded_png_bytes != 0 &&
        limits.max_total_decoded_png_bytes <= max_total && limits.max_work != 0;
}

[[nodiscard]] bool valid_profile(const CoDetectProfile profile) noexcept
{
    switch (profile) {
    case CoDetectProfile::cn:
    case CoDetectProfile::jp:
    case CoDetectProfile::global_en_us:
    case CoDetectProfile::global_zh_tw:
    case CoDetectProfile::global_ko_kr:
        return true;
    }
    return false;
}

[[nodiscard]] std::uint16_t le16(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U;
}

[[nodiscard]] std::uint32_t le32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U |
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 16U |
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 24U;
}

[[nodiscard]] std::uint32_t be32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 24U |
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 16U |
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 8U |
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3]));
}

[[nodiscard]] std::string physical_member_name(const std::size_t logical_index)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result(9, '0');
    result.front() = 'm';
    auto value = static_cast<std::uint32_t>(logical_index);
    for (std::size_t digit = 0; digit < 8; ++digit) {
        result[8 - digit] = digits[value & 0xfU];
        value >>= 4U;
    }
    return result;
}

[[nodiscard]] std::string expected_entry_name(const std::size_t archive_index)
{
    if (archive_index == 0) return "bundle.magic";
    if (archive_index == 1) return "manifest.json";
    return physical_member_name(archive_index - 2);
}

struct ZipEntry final {
    std::string name;
    std::uint16_t version_needed{};
    std::uint16_t flags{};
    std::uint16_t method{};
    std::uint32_t crc32{};
    std::uint32_t compressed_size{};
    std::uint32_t uncompressed_size{};
    std::uint32_t local_offset{};
};

[[nodiscard]] bool regular_physical_type(
    const std::uint16_t version_made, const std::uint32_t external_attributes) noexcept
{
    if ((external_attributes & 0x10U) != 0) return false;
    constexpr std::uint32_t type_mask = 0170000U;
    constexpr std::uint32_t regular_type = 0100000U;
    constexpr std::uint16_t unix_creator = 3U;
    if (static_cast<std::uint16_t>(version_made >> 8U) != unix_creator) return true;
    const auto type = ((external_attributes >> 16U) & 0xffffU) & type_mask;
    return type == 0 || type == regular_type;
}

[[nodiscard]] std::vector<ZipEntry> preflight_zip(
    const std::span<const std::byte> archive,
    const CoDetectSupportBundleLimits& limits,
    WorkBudget& work,
    const std::stop_token stop)
{
    constexpr std::uint32_t local_signature = 0x04034b50U;
    constexpr std::uint32_t central_signature = 0x02014b50U;
    constexpr std::uint32_t eocd_signature = 0x06054b50U;
    if (archive.size() < 22 || archive.size() > limits.max_archive_bytes)
        fail(archive.size() > limits.max_archive_bytes
                 ? CoDetectSupportBundleError::archive_too_large
                 : CoDetectSupportBundleError::invalid_zip);
    work.charge(archive.size());
    check_cancelled(stop);
    if (le32(archive, 0) != local_signature)
        fail(CoDetectSupportBundleError::invalid_zip);
    const auto eocd = archive.size() - 22;
    if (le32(archive, eocd) != eocd_signature || le16(archive, eocd + 20) != 0)
        fail(CoDetectSupportBundleError::invalid_zip);
    if (le16(archive, eocd + 4) != 0 || le16(archive, eocd + 6) != 0)
        fail(CoDetectSupportBundleError::unsupported_zip_feature);
    const auto disk_entries = le16(archive, eocd + 8);
    const auto entry_count = le16(archive, eocd + 10);
    if (disk_entries == 0xffffU || entry_count == 0xffffU)
        fail(CoDetectSupportBundleError::unsupported_zip_feature);
    if (disk_entries != entry_count || entry_count < 3)
        fail(CoDetectSupportBundleError::invalid_zip);
    if (static_cast<std::size_t>(entry_count) > limits.max_entries + 2U)
        fail(CoDetectSupportBundleError::entry_limit_exceeded);
    const auto central_size = static_cast<std::size_t>(le32(archive, eocd + 12));
    const auto central_offset = static_cast<std::size_t>(le32(archive, eocd + 16));
    if (central_size == 0xffffffffU || central_offset == 0xffffffffU ||
        central_offset > eocd || central_size != eocd - central_offset)
        fail(CoDetectSupportBundleError::unsupported_zip_feature);

    std::vector<ZipEntry> entries;
    entries.reserve(entry_count);
    std::set<std::string, std::less<>> names;
    std::size_t cursor = central_offset;
    std::size_t total_uncompressed{};
    for (std::size_t index = 0; index < entry_count; ++index) {
        check_cancelled(stop);
        if (cursor > eocd || eocd - cursor < 46 ||
            le32(archive, cursor) != central_signature)
            fail(CoDetectSupportBundleError::invalid_zip, index);
        const auto version_made = le16(archive, cursor + 4);
        ZipEntry entry;
        entry.version_needed = le16(archive, cursor + 6);
        entry.flags = le16(archive, cursor + 8);
        entry.method = le16(archive, cursor + 10);
        entry.crc32 = le32(archive, cursor + 16);
        entry.compressed_size = le32(archive, cursor + 20);
        entry.uncompressed_size = le32(archive, cursor + 24);
        const auto name_size = static_cast<std::size_t>(le16(archive, cursor + 28));
        const auto extra_size = static_cast<std::size_t>(le16(archive, cursor + 30));
        const auto comment_size = static_cast<std::size_t>(le16(archive, cursor + 32));
        const auto disk_start = le16(archive, cursor + 34);
        const auto external_attributes = le32(archive, cursor + 38);
        entry.local_offset = le32(archive, cursor + 42);
        if (entry.version_needed >= 45 || entry.compressed_size == 0xffffffffU ||
            entry.uncompressed_size == 0xffffffffU || entry.local_offset == 0xffffffffU)
            fail(CoDetectSupportBundleError::unsupported_zip_feature, index);
        if (entry.flags != 0 || (entry.method != 0 && entry.method != 8) ||
            (entry.method == 0 && entry.version_needed != 10) ||
            (entry.method == 8 && entry.version_needed != 20) ||
            extra_size != 0 || comment_size != 0 || disk_start != 0 ||
            !regular_physical_type(version_made, external_attributes))
            fail(CoDetectSupportBundleError::unsupported_zip_feature, index);
        if (name_size == 0 || name_size > 32 ||
            name_size > eocd - cursor - 46U - extra_size - comment_size)
            fail(CoDetectSupportBundleError::entry_name_invalid, index);
        entry.name.assign(reinterpret_cast<const char*>(archive.data() + cursor + 46), name_size);
        if (entry.name.find('\0') != std::string::npos ||
            entry.name.find('/') != std::string::npos ||
            entry.name.find('\\') != std::string::npos)
            fail(CoDetectSupportBundleError::entry_name_invalid, index);
        if (!names.insert(entry.name).second)
            fail(CoDetectSupportBundleError::duplicate_entry, index);
        if (entry.name != expected_entry_name(index))
            fail(CoDetectSupportBundleError::entry_order_mismatch, index);
        if ((index < 2 && entry.method != 0) ||
            (index == 0 && entry.uncompressed_size != co_detect_support_bundle_magic.size()) ||
            (index == 1 && entry.uncompressed_size > limits.max_manifest_bytes))
            fail(index == 1 ? CoDetectSupportBundleError::manifest_too_large
                            : CoDetectSupportBundleError::unsupported_zip_feature,
                 index);
        if (index >= 2) {
            if (entry.uncompressed_size == 0 ||
                entry.uncompressed_size > limits.max_member_bytes)
                fail(CoDetectSupportBundleError::member_limit_exceeded, index - 2);
            if (entry.uncompressed_size > limits.max_total_uncompressed_bytes -
                    std::min(total_uncompressed, limits.max_total_uncompressed_bytes))
                fail(CoDetectSupportBundleError::member_limit_exceeded, index - 2);
            total_uncompressed += entry.uncompressed_size;
        }
        if (entry.uncompressed_size != 0 &&
            (entry.compressed_size == 0 ||
             entry.compressed_size > std::numeric_limits<std::uint64_t>::max() /
                 limits.max_compression_ratio ||
             entry.uncompressed_size >
                 static_cast<std::uint64_t>(entry.compressed_size) * limits.max_compression_ratio))
            fail(CoDetectSupportBundleError::compression_limit_exceeded,
                 index >= 2 ? std::optional<std::size_t>{index - 2} : std::nullopt);
        entries.push_back(std::move(entry));
        cursor += 46U + name_size;
    }
    if (cursor != eocd) fail(CoDetectSupportBundleError::invalid_zip);

    std::size_t expected_local_offset{};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        check_cancelled(stop);
        const auto& entry = entries[index];
        const auto local_offset = static_cast<std::size_t>(entry.local_offset);
        if (local_offset != expected_local_offset || local_offset > central_offset ||
            central_offset - local_offset < 30 || le32(archive, local_offset) != local_signature)
            fail(CoDetectSupportBundleError::entry_order_mismatch, index);
        const auto name_size = static_cast<std::size_t>(le16(archive, local_offset + 26));
        const auto extra_size = static_cast<std::size_t>(le16(archive, local_offset + 28));
        if (extra_size != 0 || name_size != entry.name.size() ||
            entry.version_needed != le16(archive, local_offset + 4) ||
            entry.flags != le16(archive, local_offset + 6) ||
            entry.method != le16(archive, local_offset + 8) ||
            entry.crc32 != le32(archive, local_offset + 14) ||
            entry.compressed_size != le32(archive, local_offset + 18) ||
            entry.uncompressed_size != le32(archive, local_offset + 22))
            fail(CoDetectSupportBundleError::unsupported_zip_feature, index);
        const auto name_offset = local_offset + 30U;
        if (name_offset > central_offset || name_size > central_offset - name_offset ||
            std::memcmp(archive.data() + name_offset, entry.name.data(), name_size) != 0)
            fail(CoDetectSupportBundleError::entry_order_mismatch, index);
        const auto data_offset = name_offset + name_size;
        if (data_offset > central_offset ||
            entry.compressed_size > central_offset - data_offset)
            fail(CoDetectSupportBundleError::invalid_zip, index);
        expected_local_offset = data_offset + entry.compressed_size;
    }
    if (expected_local_offset != central_offset)
        fail(CoDetectSupportBundleError::invalid_zip);
    return entries;
}

class MinizReader final {
public:
    explicit MinizReader(const std::span<const std::byte> bytes)
    {
        initialized_ = mz_zip_reader_init_mem(&archive_, bytes.data(), bytes.size(), 0) == MZ_TRUE;
    }
    ~MinizReader()
    {
        if (initialized_) static_cast<void>(mz_zip_reader_end(&archive_));
    }
    MinizReader(const MinizReader&) = delete;
    MinizReader& operator=(const MinizReader&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept { return initialized_; }
    [[nodiscard]] std::vector<std::byte> extract(
        const std::size_t index, const std::size_t size)
    {
        std::vector<std::byte> result(size);
        if (!mz_zip_reader_extract_to_mem(
                &archive_, static_cast<mz_uint>(index), result.data(), result.size(), 0)) {
            if (mz_zip_get_last_error(&archive_) == MZ_ZIP_ALLOC_FAILED) throw std::bad_alloc{};
            fail(CoDetectSupportBundleError::invalid_zip,
                 index >= 2 ? std::optional<std::size_t>{index - 2} : std::nullopt);
        }
        return result;
    }
private:
    mz_zip_archive archive_{};
    bool initialized_{};
};

[[nodiscard]] bool exact_fields(
    const Json& object, const std::initializer_list<std::string_view> fields)
{
    if (!object.is_object() || object.size() != fields.size()) return false;
    for (const auto field : fields)
        if (!object.contains(field)) return false;
    return true;
}

[[nodiscard]] bool lower_sha256(const std::string_view value) noexcept
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
    });
}

[[nodiscard]] bool safe_feature_name(
    const std::string_view value, const std::size_t maximum) noexcept
{
    if (value.empty() || value.size() > maximum) return false;
    return std::none_of(value.begin(), value.end(), [](const char value) {
        const auto byte = static_cast<unsigned char>(value);
        return byte == 0 || byte < 0x20U || byte == 0x7fU;
    });
}

[[nodiscard]] Json parse_json(
    const std::span<const std::byte> bytes,
    const CoDetectSupportBundleLimits& limits,
    const CoDetectSupportBundleError error,
    const std::optional<std::size_t> member_index = std::nullopt)
{
    const auto text = std::string_view{
        reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    auto parsed = json::parse_strict_json(
        text, {limits.max_json_depth, limits.max_json_nodes, limits.max_string_bytes});
    if (!parsed) {
        if (parsed.error == json::StrictJsonError::resource_exhausted)
            fail(CoDetectSupportBundleError::resource_exhausted, member_index, parsed.error);
        if (parsed.error == json::StrictJsonError::internal_failure)
            fail(CoDetectSupportBundleError::internal_failure, member_index, parsed.error);
        fail(error, member_index, parsed.error);
    }
    return std::move(*parsed.document);
}

enum class MemberKind : std::uint8_t { graph, rgb, png };

struct ManifestMember final {
    std::string id;
    MemberKind kind{};
    std::string media_type;
    std::size_t size{};
    std::string sha256;
};

struct Manifest final {
    std::vector<ManifestMember> members;
    std::size_t payload_size{};
};

[[nodiscard]] std::optional<MemberKind> parse_kind(const std::string_view value) noexcept
{
    if (value == "feature-graph") return MemberKind::graph;
    if (value == "rgb-range-set") return MemberKind::rgb;
    if (value == "png-template") return MemberKind::png;
    return std::nullopt;
}

[[nodiscard]] std::string_view media_type_for(const MemberKind kind) noexcept
{
    switch (kind) {
    case MemberKind::graph:
        return "application/vnd.baas.co-detect-feature-graph.v1+json";
    case MemberKind::rgb:
        return "application/vnd.baas.co-detect-rgb-ranges.v1+json";
    case MemberKind::png:
        return "image/png";
    }
    return {};
}

[[nodiscard]] std::size_t kind_rank(const MemberKind kind) noexcept
{
    switch (kind) {
    case MemberKind::graph: return 0;
    case MemberKind::rgb: return 1;
    case MemberKind::png: return 2;
    }
    return 3;
}

[[nodiscard]] Manifest parse_manifest(
    const std::span<const std::byte> bytes,
    const std::string_view resource_id,
    const std::string_view frozen_locale,
    const CoDetectProfile frozen_profile,
    const CoDetectSupportBundleLimits& limits,
    WorkBudget& work)
{
    work.charge(bytes.size());
    const auto document = parse_json(
        bytes, limits, CoDetectSupportBundleError::manifest_json_invalid);
    if (!exact_fields(document, {"schema", "format_version", "bundle_id", "locale",
                                 "profile", "member_count", "payload_size", "members"}))
        fail(CoDetectSupportBundleError::manifest_field_invalid);
    if (!document.at("schema").is_string() ||
        document.at("schema").get<std::string_view>() != co_detect_support_bundle_schema)
        fail(CoDetectSupportBundleError::manifest_schema_invalid);
    if (!document.at("format_version").is_number_unsigned() ||
        document.at("format_version").get<std::uint64_t>() != 1)
        fail(CoDetectSupportBundleError::unsupported_version);
    if (!document.at("bundle_id").is_string() ||
        document.at("bundle_id").get<std::string_view>() != resource_id ||
        (resource_id != "procedure-support/navigation.to-main-page/v1" &&
         resource_id != "procedure-support/group.open/v1"))
        fail(CoDetectSupportBundleError::manifest_field_invalid);
    if (!document.at("locale").is_string() ||
        document.at("locale").get<std::string_view>() != frozen_locale ||
        !snapshot_resources::valid_resource_locale(frozen_locale, limits.max_string_bytes))
        fail(CoDetectSupportBundleError::profile_mismatch);
    if (!document.at("profile").is_string() ||
        document.at("profile").get<std::string_view>() != co_detect_profile_name(frozen_profile))
        fail(CoDetectSupportBundleError::profile_mismatch);
    if (!document.at("member_count").is_number_unsigned() ||
        !document.at("payload_size").is_number_unsigned() ||
        !document.at("members").is_array())
        fail(CoDetectSupportBundleError::manifest_field_invalid);
    const auto declared_count = document.at("member_count").get<std::uint64_t>();
    const auto declared_payload = document.at("payload_size").get<std::uint64_t>();
    const auto& members_json = document.at("members");
    if (declared_count != members_json.size())
        fail(CoDetectSupportBundleError::member_count_mismatch);
    if (members_json.empty() || members_json.size() > limits.max_entries)
        fail(CoDetectSupportBundleError::entry_limit_exceeded);

    Manifest result;
    result.members.reserve(members_json.size());
    std::set<std::string, std::less<>> ids;
    std::optional<MemberKind> previous_kind;
    std::string previous_id;
    std::size_t payload_size{};
    for (std::size_t index = 0; index < members_json.size(); ++index) {
        const auto& value = members_json.at(index);
        if (!exact_fields(value, {"id", "kind", "media_type", "size", "sha256"}) ||
            !value.at("id").is_string() || !value.at("kind").is_string() ||
            !value.at("media_type").is_string() || !value.at("size").is_number_unsigned() ||
            !value.at("sha256").is_string())
            fail(CoDetectSupportBundleError::manifest_field_invalid, index);
        ManifestMember member;
        member.id = value.at("id").get<std::string>();
        const auto parsed_kind = parse_kind(value.at("kind").get<std::string_view>());
        if (!parsed_kind) fail(CoDetectSupportBundleError::member_kind_mismatch, index);
        member.kind = *parsed_kind;
        member.media_type = value.at("media_type").get<std::string>();
        member.sha256 = value.at("sha256").get<std::string>();
        const auto declared_size = value.at("size").get<std::uint64_t>();
        if (declared_size == 0 || declared_size > limits.max_member_bytes ||
            declared_size > std::numeric_limits<std::size_t>::max())
            fail(CoDetectSupportBundleError::member_limit_exceeded, index);
        member.size = static_cast<std::size_t>(declared_size);
        if (!snapshot_resources::valid_resource_id(member.id, limits.max_string_bytes) ||
            !ids.insert(member.id).second || !lower_sha256(member.sha256))
            fail(CoDetectSupportBundleError::manifest_field_invalid, index);
        if (index == 0U) {
            const auto expected_graph_id =
                resource_id == "procedure-support/navigation.to-main-page/v1"
                    ? std::string_view{"feature/navigation.to-main-page"}
                    : std::string_view{"feature/group.open"};
            if (member.id != expected_graph_id)
                fail(CoDetectSupportBundleError::member_kind_mismatch, index);
        }
        if (member.media_type != media_type_for(member.kind) ||
            (member.kind == MemberKind::graph && !member.id.starts_with("feature/")) ||
            (member.kind == MemberKind::rgb && !member.id.starts_with("rgb/")) ||
            (member.kind == MemberKind::png && !member.id.starts_with("image/")))
            fail(CoDetectSupportBundleError::member_kind_mismatch, index);
        if (index == 0 && member.kind != MemberKind::graph)
            fail(CoDetectSupportBundleError::member_kind_mismatch, index);
        if (index != 0 && member.kind == MemberKind::graph)
            fail(CoDetectSupportBundleError::member_kind_mismatch, index);
        if (previous_kind) {
            if (kind_rank(member.kind) < kind_rank(*previous_kind) ||
                (member.kind == *previous_kind && member.id <= previous_id))
                fail(CoDetectSupportBundleError::entry_order_mismatch, index);
        }
        if (member.size > limits.max_total_uncompressed_bytes -
                std::min(payload_size, limits.max_total_uncompressed_bytes))
            fail(CoDetectSupportBundleError::member_limit_exceeded, index);
        payload_size += member.size;
        previous_kind = member.kind;
        previous_id = member.id;
        result.members.push_back(std::move(member));
    }
    if (declared_payload != payload_size)
        fail(CoDetectSupportBundleError::member_size_mismatch);
    result.payload_size = payload_size;
    return result;
}

struct FeatureDeclaration final {
    std::string name;
    MemberKind kind{};
    std::string member_id;
    CoDetectCrop crop;
    std::uint16_t threshold_milli{};
    std::uint16_t mean_rgb_tolerance{};
};

[[nodiscard]] std::uint64_t unsigned_json(
    const Json& value, const CoDetectSupportBundleError error,
    const std::size_t member_index)
{
    if (!value.is_number_unsigned()) fail(error, member_index);
    return value.get<std::uint64_t>();
}

[[nodiscard]] std::vector<FeatureDeclaration> parse_graph(
    const std::span<const std::byte> bytes,
    const std::size_t member_index,
    const CoDetectSupportBundleLimits& limits,
    WorkBudget& work)
{
    work.charge(bytes.size());
    const auto document = parse_json(
        bytes, limits, CoDetectSupportBundleError::feature_graph_invalid, member_index);
    if (!exact_fields(document, {"schema", "features"}) ||
        !document.at("schema").is_string() ||
        document.at("schema").get<std::string_view>() != co_detect_feature_graph_schema ||
        !document.at("features").is_array())
        fail(CoDetectSupportBundleError::feature_graph_invalid, member_index);
    const auto& features = document.at("features");
    if (features.empty() || features.size() > limits.max_features)
        fail(CoDetectSupportBundleError::feature_graph_invalid, member_index);
    std::vector<FeatureDeclaration> result;
    result.reserve(features.size());
    std::set<std::string, std::less<>> names;
    std::set<std::string, std::less<>> member_references;
    for (const auto& value : features) {
        if (!value.is_object() || !value.contains("type") || !value.at("type").is_string())
            fail(CoDetectSupportBundleError::feature_graph_invalid, member_index);
        const auto type = value.at("type").get<std::string_view>();
        const bool rgb = type == "rgb";
        const bool image = type == "image";
        if ((!rgb && !image) ||
            (rgb && !exact_fields(value, {"name", "type", "member"})) ||
            (image && !exact_fields(value, {"name", "type", "member", "crop",
                                            "threshold_milli", "mean_rgb_tolerance"})) ||
            !value.at("name").is_string() || !value.at("member").is_string())
            fail(CoDetectSupportBundleError::feature_graph_invalid, member_index);
        FeatureDeclaration feature;
        feature.name = value.at("name").get<std::string>();
        feature.member_id = value.at("member").get<std::string>();
        feature.kind = rgb ? MemberKind::rgb : MemberKind::png;
        if (!safe_feature_name(feature.name, limits.max_string_bytes) ||
            !snapshot_resources::valid_resource_id(feature.member_id, limits.max_string_bytes) ||
            !names.insert(feature.name).second ||
            !member_references.insert(feature.member_id).second)
            fail(CoDetectSupportBundleError::feature_graph_invalid, member_index);
        if (image) {
            const auto& crop = value.at("crop");
            if (!crop.is_array() || crop.size() != 4)
                fail(CoDetectSupportBundleError::feature_graph_invalid, member_index);
            std::array<std::uint64_t, 4> coordinates{};
            for (std::size_t index = 0; index < coordinates.size(); ++index)
                coordinates[index] = unsigned_json(
                    crop.at(index), CoDetectSupportBundleError::feature_graph_invalid,
                    member_index);
            const auto threshold = unsigned_json(
                value.at("threshold_milli"), CoDetectSupportBundleError::feature_graph_invalid,
                member_index);
            const auto tolerance = unsigned_json(
                value.at("mean_rgb_tolerance"),
                CoDetectSupportBundleError::feature_graph_invalid, member_index);
            if (coordinates[0] >= coordinates[2] || coordinates[1] >= coordinates[3] ||
                coordinates[2] > 1'280 || coordinates[3] > 720 || threshold > 1'000 ||
                tolerance > 255)
                fail(CoDetectSupportBundleError::feature_graph_invalid, member_index);
            feature.crop = {static_cast<std::uint16_t>(coordinates[0]),
                            static_cast<std::uint16_t>(coordinates[1]),
                            static_cast<std::uint16_t>(coordinates[2]),
                            static_cast<std::uint16_t>(coordinates[3])};
            feature.threshold_milli = static_cast<std::uint16_t>(threshold);
            feature.mean_rgb_tolerance = static_cast<std::uint16_t>(tolerance);
        }
        result.push_back(std::move(feature));
    }
    return result;
}

[[nodiscard]] std::array<std::uint8_t, 2> parse_channel_range(
    const Json& value, const std::size_t member_index)
{
    if (!value.is_array() || value.size() != 2)
        fail(CoDetectSupportBundleError::rgb_invalid, member_index);
    const auto minimum = unsigned_json(
        value.at(0), CoDetectSupportBundleError::rgb_invalid, member_index);
    const auto maximum = unsigned_json(
        value.at(1), CoDetectSupportBundleError::rgb_invalid, member_index);
    if (minimum > maximum || maximum > 255)
        fail(CoDetectSupportBundleError::rgb_invalid, member_index);
    return {static_cast<std::uint8_t>(minimum), static_cast<std::uint8_t>(maximum)};
}

[[nodiscard]] std::vector<CoDetectRgbSample> parse_rgb(
    const std::span<const std::byte> bytes,
    const std::size_t member_index,
    const CoDetectSupportBundleLimits& limits,
    WorkBudget& work)
{
    work.charge(bytes.size());
    const auto document = parse_json(
        bytes, limits, CoDetectSupportBundleError::rgb_invalid, member_index);
    if (!exact_fields(document, {"schema", "samples"}) ||
        !document.at("schema").is_string() ||
        document.at("schema").get<std::string_view>() != co_detect_rgb_ranges_schema ||
        !document.at("samples").is_array())
        fail(CoDetectSupportBundleError::rgb_invalid, member_index);
    const auto& samples = document.at("samples");
    if (samples.empty() || samples.size() > limits.max_rgb_samples)
        fail(CoDetectSupportBundleError::rgb_invalid, member_index);
    std::vector<CoDetectRgbSample> result;
    result.reserve(samples.size());
    for (const auto& value : samples) {
        if (!exact_fields(value, {"x", "y", "r", "g", "b"}))
            fail(CoDetectSupportBundleError::rgb_invalid, member_index);
        const auto x = unsigned_json(value.at("x"), CoDetectSupportBundleError::rgb_invalid,
                                     member_index);
        const auto y = unsigned_json(value.at("y"), CoDetectSupportBundleError::rgb_invalid,
                                     member_index);
        if (x >= 1'280 || y >= 720)
            fail(CoDetectSupportBundleError::rgb_invalid, member_index);
        CoDetectRgbSample sample;
        sample.x = static_cast<std::uint16_t>(x);
        sample.y = static_cast<std::uint16_t>(y);
        sample.red = parse_channel_range(value.at("r"), member_index);
        sample.green = parse_channel_range(value.at("g"), member_index);
        sample.blue = parse_channel_range(value.at("b"), member_index);
        result.push_back(sample);
    }
    return result;
}

struct DecodedPng final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_stride{};
    std::shared_ptr<const std::vector<std::byte>> pixels;
};

[[nodiscard]] bool chunk_type(
    const std::span<const std::byte> bytes, const std::size_t offset,
    const std::string_view expected) noexcept
{
    return std::memcmp(bytes.data() + offset, expected.data(), expected.size()) == 0;
}

[[nodiscard]] bool allowed_ancillary_chunk(
    const std::span<const std::byte> bytes, const std::size_t type_offset,
    const std::uint32_t size, const std::uint8_t color_type) noexcept
{
    if (chunk_type(bytes, type_offset, "cHRM")) return size == 32;
    if (chunk_type(bytes, type_offset, "gAMA")) return size == 4;
    if (chunk_type(bytes, type_offset, "sBIT"))
        return size == (color_type == 2 ? 3U : 4U);
    if (chunk_type(bytes, type_offset, "sRGB")) return size == 1;
    if (chunk_type(bytes, type_offset, "pHYs")) return size == 9;
    return false;
}

[[nodiscard]] DecodedPng decode_png(
    const std::span<const std::byte> bytes,
    const std::size_t member_index,
    const CoDetectSupportBundleLimits& limits,
    std::size_t& total_decoded,
    WorkBudget& work,
    const std::stop_token stop)
{
    constexpr std::array<std::byte, 8> signature{
        std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'},
        std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};
    work.charge(bytes.size());
    if (bytes.size() < signature.size() + 25U ||
        !std::equal(signature.begin(), signature.end(), bytes.begin()))
        fail(CoDetectSupportBundleError::png_invalid, member_index);
    std::size_t cursor = signature.size();
    std::size_t chunk_count{};
    bool ihdr{};
    bool idat{};
    bool idat_ended{};
    bool iend{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint8_t color_type{};
    std::set<std::string, std::less<>> singleton_ancillary;
    while (cursor < bytes.size()) {
        check_cancelled(stop);
        if (++chunk_count > limits.max_png_chunks || bytes.size() - cursor < 12)
            fail(CoDetectSupportBundleError::png_limit_exceeded, member_index);
        const auto size = be32(bytes, cursor);
        const auto type_offset = cursor + 4U;
        const auto data_offset = cursor + 8U;
        if (size > bytes.size() - data_offset - 4U)
            fail(CoDetectSupportBundleError::png_invalid, member_index);
        for (std::size_t index = 0; index < 4; ++index) {
            const auto value = std::to_integer<unsigned char>(bytes[type_offset + index]);
            if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')) ||
                (index == 2 && !(value >= 'A' && value <= 'Z')))
                fail(CoDetectSupportBundleError::png_invalid, member_index);
        }
        const auto expected_crc = be32(bytes, data_offset + size);
        auto crc = mz_crc32(MZ_CRC32_INIT,
                            reinterpret_cast<const mz_uint8*>(bytes.data() + type_offset),
                            static_cast<std::size_t>(size) + 4U);
        if (crc != expected_crc)
            fail(CoDetectSupportBundleError::png_invalid, member_index);
        if (chunk_type(bytes, type_offset, "IHDR")) {
            if (ihdr || chunk_count != 1 || size != 13)
                fail(CoDetectSupportBundleError::png_invalid, member_index);
            ihdr = true;
            width = be32(bytes, data_offset);
            height = be32(bytes, data_offset + 4);
            const auto bit_depth = std::to_integer<std::uint8_t>(bytes[data_offset + 8]);
            color_type = std::to_integer<std::uint8_t>(bytes[data_offset + 9]);
            if (width == 0 || height == 0 || width > limits.max_png_width ||
                height > limits.max_png_height || bit_depth != 8 ||
                (color_type != 2 && color_type != 6) ||
                bytes[data_offset + 10] != std::byte{0} ||
                bytes[data_offset + 11] != std::byte{0} ||
                bytes[data_offset + 12] != std::byte{0})
                fail(CoDetectSupportBundleError::png_limit_exceeded, member_index);
        } else if (chunk_type(bytes, type_offset, "IDAT")) {
            if (!ihdr || iend || idat_ended || size == 0)
                fail(CoDetectSupportBundleError::png_invalid, member_index);
            idat = true;
        } else if (chunk_type(bytes, type_offset, "IEND")) {
            if (!ihdr || !idat || iend || size != 0)
                fail(CoDetectSupportBundleError::png_invalid, member_index);
            iend = true;
        } else {
            if (idat) idat_ended = true;
            if (!ihdr || iend ||
                !allowed_ancillary_chunk(bytes, type_offset, size, color_type))
                fail(CoDetectSupportBundleError::png_invalid, member_index);
            std::string type(reinterpret_cast<const char*>(bytes.data() + type_offset), 4);
            if (!singleton_ancillary.insert(std::move(type)).second)
                fail(CoDetectSupportBundleError::png_invalid, member_index);
        }
        cursor = data_offset + size + 4U;
        if (iend && cursor != bytes.size())
            fail(CoDetectSupportBundleError::png_invalid, member_index);
    }
    if (!ihdr || !idat || !iend || cursor != bytes.size())
        fail(CoDetectSupportBundleError::png_invalid, member_index);
    const auto decoded_size = static_cast<std::uint64_t>(width) * height * 3U;
    if (decoded_size > limits.max_decoded_png_bytes ||
        decoded_size > limits.max_total_decoded_png_bytes -
            std::min(total_decoded, limits.max_total_decoded_png_bytes))
        fail(CoDetectSupportBundleError::png_limit_exceeded, member_index);
    work.charge(static_cast<std::size_t>(decoded_size));
    check_cancelled(stop);
    const cv::Mat encoded(1, static_cast<int>(bytes.size()), CV_8UC1,
                          const_cast<std::byte*>(bytes.data()));
    const auto decoded = cv::imdecode(encoded, cv::IMREAD_COLOR);
    check_cancelled(stop);
    if (decoded.empty() || decoded.type() != CV_8UC3 ||
        decoded.cols != static_cast<int>(width) || decoded.rows != static_cast<int>(height))
        fail(CoDetectSupportBundleError::png_invalid, member_index);
    auto pixels = std::make_shared<std::vector<std::byte>>(
        static_cast<std::size_t>(decoded_size));
    const auto stride = static_cast<std::size_t>(width) * 3U;
    for (std::uint32_t row = 0; row < height; ++row)
        std::memcpy(pixels->data() + static_cast<std::size_t>(row) * stride,
                    decoded.ptr(static_cast<int>(row)), stride);
    total_decoded += static_cast<std::size_t>(decoded_size);
    return {width, height, stride,
            std::shared_ptr<const std::vector<std::byte>>(std::move(pixels))};
}

}  // namespace

struct CoDetectSupportBundle::Impl final {
    std::shared_ptr<const resources::RuntimeResourceSnapshotActivation> activation;
    std::shared_ptr<const snapshot_resources::ResourceEntry> archive;
    std::string snapshot_id;
    std::string resource_id;
    std::string locale;
    CoDetectProfile profile{};
    std::size_t member_count{};
    std::map<std::string, CoDetectRgbTemplate, std::less<>> rgb;
    std::map<std::string, CoDetectImageTemplate, std::less<>> images;
};

const std::string& CoDetectImageTemplate::feature() const noexcept { return feature_; }
const std::string& CoDetectImageTemplate::member_id() const noexcept { return member_id_; }
const CoDetectCrop& CoDetectImageTemplate::crop() const noexcept { return crop_; }
std::uint16_t CoDetectImageTemplate::threshold_milli() const noexcept {
    return threshold_milli_;
}
std::uint16_t CoDetectImageTemplate::mean_rgb_tolerance() const noexcept {
    return mean_rgb_tolerance_;
}
std::uint32_t CoDetectImageTemplate::width() const noexcept { return width_; }
std::uint32_t CoDetectImageTemplate::height() const noexcept { return height_; }
std::size_t CoDetectImageTemplate::row_stride() const noexcept { return row_stride_; }
std::span<const std::byte> CoDetectImageTemplate::bgr_pixels() const noexcept {
    return *bgr_pixels_;
}

CoDetectSupportBundle::CoDetectSupportBundle(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
CoDetectSupportBundle::~CoDetectSupportBundle() = default;
const std::string& CoDetectSupportBundle::generation() const noexcept {
    return impl_->activation->generation();
}
const std::string& CoDetectSupportBundle::commit() const noexcept {
    return impl_->activation->commit();
}
const std::string& CoDetectSupportBundle::snapshot_id() const noexcept {
    return impl_->snapshot_id;
}
const std::string& CoDetectSupportBundle::resource_id() const noexcept {
    return impl_->resource_id;
}
const std::string& CoDetectSupportBundle::locale() const noexcept { return impl_->locale; }
CoDetectProfile CoDetectSupportBundle::profile() const noexcept { return impl_->profile; }
const std::string& CoDetectSupportBundle::archive_sha256() const noexcept {
    return impl_->archive->sha256();
}
std::size_t CoDetectSupportBundle::member_count() const noexcept {
    return impl_->member_count;
}
const CoDetectRgbTemplate* CoDetectSupportBundle::find_rgb(
    const std::string_view feature) const noexcept
{
    const auto found = impl_->rgb.find(feature);
    return found == impl_->rgb.end() ? nullptr : &found->second;
}
const CoDetectImageTemplate* CoDetectSupportBundle::find_image(
    const std::string_view feature) const noexcept
{
    const auto found = impl_->images.find(feature);
    return found == impl_->images.end() ? nullptr : &found->second;
}

std::string_view co_detect_support_bundle_error_name(
    const CoDetectSupportBundleError error) noexcept
{
    using enum CoDetectSupportBundleError;
    switch (error) {
    case none: return "CDSB000_NONE";
    case invalid_limits: return "CDSB001_INVALID_LIMITS";
    case generation_mismatch: return "CDSB002_GENERATION_MISMATCH";
    case resource_not_found: return "CDSB003_RESOURCE_NOT_FOUND";
    case selector_mismatch: return "CDSB004_SELECTOR_MISMATCH";
    case media_type_mismatch: return "CDSB005_MEDIA_TYPE_MISMATCH";
    case archive_too_large: return "CDSB006_ARCHIVE_TOO_LARGE";
    case invalid_zip: return "CDSB007_INVALID_ZIP";
    case unsupported_zip_feature: return "CDSB008_UNSUPPORTED_ZIP_FEATURE";
    case bad_magic: return "CDSB009_BAD_MAGIC";
    case unsupported_version: return "CDSB010_UNSUPPORTED_VERSION";
    case entry_limit_exceeded: return "CDSB011_ENTRY_LIMIT_EXCEEDED";
    case entry_name_invalid: return "CDSB012_ENTRY_NAME_INVALID";
    case entry_order_mismatch: return "CDSB013_ENTRY_ORDER_MISMATCH";
    case duplicate_entry: return "CDSB014_DUPLICATE_ENTRY";
    case manifest_too_large: return "CDSB015_MANIFEST_TOO_LARGE";
    case manifest_json_invalid: return "CDSB016_MANIFEST_JSON_INVALID";
    case manifest_schema_invalid: return "CDSB017_MANIFEST_SCHEMA_INVALID";
    case manifest_field_invalid: return "CDSB018_MANIFEST_FIELD_INVALID";
    case profile_mismatch: return "CDSB019_PROFILE_MISMATCH";
    case member_count_mismatch: return "CDSB020_MEMBER_COUNT_MISMATCH";
    case member_limit_exceeded: return "CDSB021_MEMBER_LIMIT_EXCEEDED";
    case compression_limit_exceeded: return "CDSB022_COMPRESSION_LIMIT_EXCEEDED";
    case member_size_mismatch: return "CDSB023_MEMBER_SIZE_MISMATCH";
    case member_digest_mismatch: return "CDSB024_MEMBER_DIGEST_MISMATCH";
    case member_kind_mismatch: return "CDSB025_MEMBER_KIND_MISMATCH";
    case png_invalid: return "CDSB026_PNG_INVALID";
    case png_limit_exceeded: return "CDSB027_PNG_LIMIT_EXCEEDED";
    case rgb_invalid: return "CDSB028_RGB_INVALID";
    case feature_graph_invalid: return "CDSB029_FEATURE_GRAPH_INVALID";
    case work_limit_exceeded: return "CDSB030_WORK_LIMIT_EXCEEDED";
    case cancelled: return "CDSB031_CANCELLED";
    case resource_exhausted: return "CDSB032_RESOURCE_EXHAUSTED";
    case internal_failure: return "CDSB033_INTERNAL_FAILURE";
    }
    return "CDSB999_UNKNOWN";
}

CoDetectSupportBundleLoadResult load_co_detect_support_bundle(
    std::shared_ptr<const resources::RuntimeResourceSnapshotActivation> activation,
    const std::string_view expected_generation,
    const std::string_view resource_id,
    const std::string_view frozen_locale,
    const CoDetectProfile frozen_profile,
    const CoDetectSupportBundleLimits& limits,
    const std::stop_token stop) noexcept
{
    try {
        if (!valid_limits(limits)) fail(CoDetectSupportBundleError::invalid_limits);
        if (!activation || !activation->snapshot())
            fail(CoDetectSupportBundleError::resource_not_found);
        if (activation->generation() != expected_generation)
            fail(CoDetectSupportBundleError::generation_mismatch);
        if (!valid_profile(frozen_profile))
            fail(CoDetectSupportBundleError::profile_mismatch);
        if (!snapshot_resources::valid_resource_id(resource_id, limits.max_string_bytes) ||
            (resource_id != "procedure-support/navigation.to-main-page/v1" &&
             resource_id != "procedure-support/group.open/v1") ||
            !snapshot_resources::valid_resource_locale(frozen_locale, limits.max_string_bytes))
            fail(CoDetectSupportBundleError::selector_mismatch);
        check_cancelled(stop);
        auto archive = activation->snapshot()->resolve(resource_id, frozen_locale);
        if (!archive) fail(CoDetectSupportBundleError::resource_not_found);
        if (!archive->locale() || *archive->locale() != frozen_locale || archive->activity())
            fail(CoDetectSupportBundleError::selector_mismatch);
        if (archive->media_type() != co_detect_support_bundle_media_type)
            fail(CoDetectSupportBundleError::media_type_mismatch);
        if (archive->bytes().size() > limits.max_archive_bytes)
            fail(CoDetectSupportBundleError::archive_too_large);

        WorkBudget work{limits.max_work};
        const auto entries = preflight_zip(archive->bytes(), limits, work, stop);
        MinizReader reader{archive->bytes()};
        if (!reader) fail(CoDetectSupportBundleError::invalid_zip);
        work.charge(entries[0].uncompressed_size);
        auto magic = reader.extract(0, entries[0].uncompressed_size);
        check_cancelled(stop);
        constexpr std::string_view magic_prefix = "BAASCDSB";
        if (magic.size() != co_detect_support_bundle_magic.size() ||
            std::memcmp(magic.data(), magic_prefix.data(), magic_prefix.size()) != 0)
            fail(CoDetectSupportBundleError::bad_magic);
        if (!std::ranges::equal(magic, co_detect_support_bundle_magic))
            fail(CoDetectSupportBundleError::unsupported_version);
        work.charge(entries[1].uncompressed_size);
        auto manifest_bytes = reader.extract(1, entries[1].uncompressed_size);
        check_cancelled(stop);
        const auto manifest = parse_manifest(
            manifest_bytes, resource_id, frozen_locale, frozen_profile, limits, work);
        if (entries.size() != manifest.members.size() + 2U)
            fail(CoDetectSupportBundleError::member_count_mismatch);
        for (std::size_t index = 0; index < manifest.members.size(); ++index) {
            if (entries[index + 2].uncompressed_size != manifest.members[index].size)
                fail(CoDetectSupportBundleError::member_size_mismatch, index);
        }

        std::vector<FeatureDeclaration> graph;
        std::map<std::string, std::vector<CoDetectRgbSample>, std::less<>> rgb_payloads;
        std::map<std::string, DecodedPng, std::less<>> png_payloads;
        std::size_t total_decoded{};
        for (std::size_t index = 0; index < manifest.members.size(); ++index) {
            check_cancelled(stop);
            const auto& member = manifest.members[index];
            // Reserve the full declared output before miniz can allocate or inflate it.
            work.charge(member.size);
            auto payload = reader.extract(index + 2, member.size);
            check_cancelled(stop);
            // Digesting is a separate bounded pass over the extracted payload.
            work.charge(payload.size());
            if (snapshot_resources::sha256_hex(payload) != member.sha256)
                fail(CoDetectSupportBundleError::member_digest_mismatch, index);
            switch (member.kind) {
            case MemberKind::graph:
                graph = parse_graph(payload, index, limits, work);
                break;
            case MemberKind::rgb:
                rgb_payloads.emplace(member.id, parse_rgb(payload, index, limits, work));
                break;
            case MemberKind::png:
                png_payloads.emplace(
                    member.id, decode_png(payload, index, limits, total_decoded, work, stop));
                break;
            }
        }

        auto impl = std::make_unique<CoDetectSupportBundle::Impl>();
        impl->activation = std::move(activation);
        impl->archive = std::move(archive);
        impl->snapshot_id = impl->activation->snapshot()->snapshot_id();
        impl->resource_id = std::string{resource_id};
        impl->locale = std::string{frozen_locale};
        impl->profile = frozen_profile;
        impl->member_count = manifest.members.size();
        std::set<std::string, std::less<>> referenced_members;
        for (auto& feature : graph) {
            if (!referenced_members.insert(feature.member_id).second)
                fail(CoDetectSupportBundleError::feature_graph_invalid, 0);
            if (feature.kind == MemberKind::rgb) {
                auto found = rgb_payloads.find(feature.member_id);
                if (found == rgb_payloads.end())
                    fail(CoDetectSupportBundleError::member_kind_mismatch, 0);
                CoDetectRgbTemplate published;
                published.feature = feature.name;
                published.member_id = feature.member_id;
                published.samples = std::move(found->second);
                impl->rgb.emplace(published.feature, std::move(published));
            } else {
                auto found = png_payloads.find(feature.member_id);
                if (found == png_payloads.end())
                    fail(CoDetectSupportBundleError::member_kind_mismatch, 0);
                CoDetectImageTemplate published;
                published.feature_ = feature.name;
                published.member_id_ = feature.member_id;
                published.crop_ = feature.crop;
                published.threshold_milli_ = feature.threshold_milli;
                published.mean_rgb_tolerance_ = feature.mean_rgb_tolerance;
                published.width_ = found->second.width;
                published.height_ = found->second.height;
                published.row_stride_ = found->second.row_stride;
                published.bgr_pixels_ = std::move(found->second.pixels);
                impl->images.emplace(published.feature_, std::move(published));
            }
        }
        if (referenced_members.size() + 1U != manifest.members.size())
            fail(CoDetectSupportBundleError::feature_graph_invalid, 0);
        check_cancelled(stop);
        auto bundle = std::shared_ptr<const CoDetectSupportBundle>(
            new CoDetectSupportBundle{std::move(impl)});
        return {std::move(bundle), CoDetectSupportBundleError::none,
                json::StrictJsonError::none, std::nullopt};
    } catch (const LoadFailure& error) {
        return {{}, error.error, error.json_error, error.member_index};
    } catch (const cv::Exception& error) {
        return {{}, error.code == cv::Error::StsNoMem
                        ? CoDetectSupportBundleError::resource_exhausted
                        : CoDetectSupportBundleError::png_invalid,
                json::StrictJsonError::none, std::nullopt};
    } catch (const std::bad_alloc&) {
        return {{}, CoDetectSupportBundleError::resource_exhausted,
                json::StrictJsonError::none, std::nullopt};
    } catch (...) {
        return {{}, CoDetectSupportBundleError::internal_failure,
                json::StrictJsonError::none, std::nullopt};
    }
}

}  // namespace baas::runtime::procedure
