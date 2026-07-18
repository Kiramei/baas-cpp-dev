#include "runtime/publisher/GroupPublicationCompiler.h"

#include "resources/ResourceSnapshot.h"
#include "runtime/json/StrictJson.h"
#include "runtime/procedure/CoDetectSupportBundle.h"

#include <git2.h>
#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <system_error>
#include <tuple>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace baas::runtime::publisher {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;
namespace resources = ::baas::resources;
namespace strict_json = ::baas::runtime::json;
namespace procedure = ::baas::runtime::procedure;

constexpr std::size_t max_lock_bytes = 4U * 1024U * 1024U;
constexpr std::size_t max_bundles = 32;
constexpr std::size_t max_members = 256;
constexpr std::size_t max_member_bytes = 16U * 1024U * 1024U;
constexpr std::size_t max_archive_bytes = 64U * 1024U * 1024U;
constexpr std::size_t max_total_bytes = 128U * 1024U * 1024U;
constexpr std::size_t max_work = 1024U * 1024U * 1024U;
constexpr std::size_t max_source_bytes = max_member_bytes;
constexpr std::size_t max_path_bytes = 1'024;

[[noreturn]] void fail(const PublicationErrorCode code, std::string message)
{
    throw PublicationError{code, std::move(message)};
}

[[nodiscard]] bool exact_fields(
    const Json& value, const std::initializer_list<std::string_view> fields)
{
    if (!value.is_object() || value.size() != fields.size()) return false;
    return std::ranges::all_of(fields, [&value](const std::string_view field) {
        return value.contains(field);
    });
}

[[nodiscard]] bool lower_hex(const std::string_view value, const std::size_t size) noexcept
{
    return value.size() == size && std::ranges::all_of(value, [](const char byte) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    });
}

[[nodiscard]] bool valid_utf8(const std::span<const std::byte> bytes) noexcept
{
    std::size_t index{};
    while (index < bytes.size()) {
        const auto first = std::to_integer<unsigned char>(bytes[index++]);
        if (first <= 0x7fU) {
            if (first == 0U) return false;
            continue;
        }
        std::size_t continuation{};
        std::uint32_t value{};
        std::uint32_t minimum{};
        if (first >= 0xc2U && first <= 0xdfU) {
            continuation = 1; value = first & 0x1fU; minimum = 0x80U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuation = 2; value = first & 0x0fU; minimum = 0x800U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation = 3; value = first & 0x07U; minimum = 0x10000U;
        } else {
            return false;
        }
        if (continuation > bytes.size() - index) return false;
        for (std::size_t count = 0; count < continuation; ++count) {
            const auto next = std::to_integer<unsigned char>(bytes[index++]);
            if ((next & 0xc0U) != 0x80U) return false;
            value = (value << 6U) | (next & 0x3fU);
        }
        if (value < minimum || value > 0x10ffffU ||
            (value >= 0xd800U && value <= 0xdfffU)) return false;
    }
    return true;
}

[[nodiscard]] bool safe_relative_path(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > max_path_bytes || value.front() == '/' ||
        value.back() == '/' || value.find('\\') != std::string_view::npos ||
        value.find('\0') != std::string_view::npos || value.find(':') != std::string_view::npos)
        return false;
    std::size_t offset{};
    while (offset < value.size()) {
        const auto end = value.find('/', offset);
        const auto token = value.substr(offset, end == std::string_view::npos
                                                    ? value.size() - offset
                                                    : end - offset);
        if (token.empty() || token == "." || token == "..") return false;
        if (!std::ranges::all_of(token, [](const char byte) {
                return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                    (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.';
            })) return false;
        if (end == std::string_view::npos) break;
        offset = end + 1;
    }
    return true;
}

[[nodiscard]] std::vector<std::string_view> split_path(const std::string_view path)
{
    std::vector<std::string_view> result;
    std::size_t offset{};
    while (offset <= path.size()) {
        const auto end = path.find('/', offset);
        result.push_back(path.substr(offset, end == std::string_view::npos
                                                ? path.size() - offset
                                                : end - offset));
        if (end == std::string_view::npos) break;
        offset = end + 1;
    }
    return result;
}

[[nodiscard]] std::string ascii_lower(const std::string_view value)
{
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](const unsigned char byte) {
        return static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte);
    });
    return result;
}

[[nodiscard]] bool valid_feature(const std::string_view value) noexcept
{
    return !value.empty() && value.size() <= max_path_bytes &&
        std::ranges::all_of(value, [](const char byte) {
            return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                (byte >= '0' && byte <= '9') || byte == '-' || byte == '_';
        });
}

[[nodiscard]] bool valid_profile(const std::string_view value) noexcept
{
    return value == "CN" || value == "JP" || value == "Global_en-us" ||
        value == "Global_zh-tw" || value == "Global_ko-kr";
}

struct ProductionBundleSpec final {
    std::string_view bundle_id;
    std::string_view profile;
    std::size_t member_count;
    std::string_view output_path;
    std::string_view identity_sha256;
};

constexpr std::array<ProductionBundleSpec, 10> production_bundles{{
    {"procedure-support/group.open/v1", "CN", 16, "payload/group.open.CN.bundle",
     "575245fe321537f123b25a8fa58d33b1703d1972e362d123edd1e77c83a1b471"},
    {"procedure-support/group.open/v1", "Global_en-us", 17,
     "payload/group.open.Global_en-us.bundle",
     "dfad8e8d974d3eacf0ca0b98cf3d55263c7b1744d96f6286ca4277df393af5c7"},
    {"procedure-support/group.open/v1", "Global_ko-kr", 13,
     "payload/group.open.Global_ko-kr.bundle",
     "7f3526d65191f452d91b1ac49bdfd4c4d175d52d3624ef2b16add90eec7c3b75"},
    {"procedure-support/group.open/v1", "Global_zh-tw", 14,
     "payload/group.open.Global_zh-tw.bundle",
     "d83a308218230cb313e546dae508593d0816d8a501347607aac11d21e6ce137b"},
    {"procedure-support/group.open/v1", "JP", 12, "payload/group.open.JP.bundle",
     "0372edf0f3bc6008064568634587c64b3d7ede4f8f4602e48226f9795d095668"},
    {"procedure-support/navigation.to-main-page/v1", "CN", 63,
     "payload/navigation.to-main-page.CN.bundle",
     "2eb6b38b2be61b41376ba7f4f664c0edb7649fa254740495cf399fd95b3febc4"},
    {"procedure-support/navigation.to-main-page/v1", "Global_en-us", 60,
     "payload/navigation.to-main-page.Global_en-us.bundle",
     "8a47786d300399eb9885b8df089dc7ea272a2969f3beb480b468a55856ef6947"},
    {"procedure-support/navigation.to-main-page/v1", "Global_ko-kr", 56,
     "payload/navigation.to-main-page.Global_ko-kr.bundle",
     "94c1cbd8c8cde1d11ecaae7ee6d813eff8896211ffd92b56e2409271bf8fe187"},
    {"procedure-support/navigation.to-main-page/v1", "Global_zh-tw", 57,
     "payload/navigation.to-main-page.Global_zh-tw.bundle",
     "3511df883e7953ece8c0f246f34bc10c438e3c981590b2e6449d6c60f4c6f1ec"},
    {"procedure-support/navigation.to-main-page/v1", "JP", 56,
     "payload/navigation.to-main-page.JP.bundle",
     "5f01eaca7e266997e4e66e945eb3c3c26d329f9ed2ac7cc7ce548f5acb09f2e5"},
}};

[[nodiscard]] bool production_image_allowed(
    const std::string_view bundle_id, const std::string_view id) noexcept
{
    constexpr std::array<std::string_view, 69> navigation{{
        "image/main_page_game-download-resource-notice",
        "image/main_page_game-download-resource-notice2",
        "image/main_page_game-download-resource-notice3", "image/main_page_privacy-policy",
        "image/main_page_quick-home", "image/main_page_daily-attendance",
        "image/main_page_item-expire", "image/main_page_skip-notice",
        "image/draw-card-point-exchange-to-stone-piece-notice",
        "image/normal_task_fight-end-back-to-main-page", "image/main_page_enter-existing-fight",
        "image/main_page_login-feature", "image/main_page_relationship-rank-up",
        "image/main_page_full-notice", "image/normal_task_task-info",
        "image/normal_task_fight-confirm", "image/normal_task_task-finish",
        "image/normal_task_prize-confirm", "image/normal_task_fail-confirm",
        "image/normal_task_fight-task-info", "image/normal_task_sweep-complete",
        "image/normal_task_start-sweep-notice", "image/normal_task_unlock-notice",
        "image/normal_task_skip-sweep-complete", "image/normal_task_charge-challenge-counts",
        "image/purchase_ap_notice", "image/purchase_ap_notice-localized",
        "image/normal_task_task-operating-feature",
        "image/normal_task_mission-operating-task-info",
        "image/normal_task_mission-operating-task-info-notice", "image/normal_task_mission-pause",
        "image/normal_task_task-begin-without-further-editing-notice",
        "image/normal_task_task-operating-round-over-notice", "image/momo_talk_momotalk-peach",
        "image/cafe_students-arrived", "image/cafe_quick-home", "image/pass_menu",
        "image/pass_mission-menu", "image/group_sign-up-reward", "image/cafe_invitation-ticket",
        "image/lesson_lesson-information", "image/lesson_all-locations",
        "image/lesson_lesson-report", "image/lesson_purchase-lesson-ticket-menu",
        "image/rewarded_task_purchase-bounty-ticket-menu",
        "image/scrimmage_purchase-scrimmage-ticket-menu", "image/arena_battle-win",
        "image/arena_battle-lost", "image/arena_season-record", "image/arena_best-record",
        "image/arena_opponent-info", "image/plot_menu", "image/plot_skip-plot-button",
        "image/plot_skip-plot-notice", "image/activity_fight-success-confirm",
        "image/total_assault_reach-season-highest-record",
        "image/total_assault_total-assault-info", "image/cafe_cafe-reward-status",
        "image/main_page_news", "image/main_page_news2", "image/main_page_net-work-unstable",
        "image/main_page_fail-to-load-game-resources", "image/main_page_attendance-reward",
        "image/main_page_download-additional-resources", "image/main_page_login-store",
        "image/main_page_insufficient-inventory-space",
        "image/main_page_failed-to-convert-errorresponse",
        "image/main_page_store-service-unavailable", "image/main_page_request-failed-notice",
    }};
    constexpr std::array<std::string_view, 18> group{{
        "image/group_sign-up-reward", "image/group_menu", "image/group_join-club",
        "image/group_enter-button", "image/main_page_renewal-month-card",
        "image/main_page_item-expired-notice", "image/main_page_item-expiring-notice",
        "image/main_page_failed-to-receive-platform-steam-getentitlementsasjsonarray",
        "image/main_page_failed-to-request-prices", "image/main_page_news",
        "image/main_page_news2", "image/main_page_item-expire",
        "image/draw-card-point-exchange-to-stone-piece-notice",
        "image/main_page_failed-to-convert-errorresponse", "image/main_page_login-store",
        "image/main_page_net-work-unstable", "image/main_page_store-service-unavailable",
        "image/main_page_request-failed-notice",
    }};
    if (bundle_id == "procedure-support/navigation.to-main-page/v1")
        return std::ranges::find(navigation, id) != navigation.end();
    if (bundle_id == "procedure-support/group.open/v1")
        return std::ranges::find(group, id) != group.end();
    return false;
}

struct SourceDescriptor final {
    std::string path;
    std::string oid;
    std::size_t size{};
    std::string sha256;
};

enum class MemberKind : std::uint8_t { graph, rgb, png };

struct Member final {
    MemberKind kind{};
    std::string id;
    std::string feature;
    std::optional<SourceDescriptor> source;
    std::optional<SourceDescriptor> crop_source;
    std::string source_key;
    std::array<std::uint16_t, 4> crop{};
    std::uint16_t threshold_milli{};
    std::uint16_t mean_rgb_tolerance{};
};

struct Bundle final {
    std::string bundle_id;
    std::string locale;
    std::string profile;
    std::string output_path;
    std::vector<Member> members;
};

[[nodiscard]] SourceDescriptor parse_source(const Json& value)
{
    if (!exact_fields(value, {"path", "oid", "size", "sha256"}) ||
        !value.at("path").is_string() || !value.at("oid").is_string() ||
        !value.at("size").is_number_unsigned() || !value.at("sha256").is_string())
        fail(PublicationErrorCode::invalid_lock, "source descriptor has invalid fields");
    SourceDescriptor result;
    result.path = value.at("path").get<std::string>();
    result.oid = value.at("oid").get<std::string>();
    result.sha256 = value.at("sha256").get<std::string>();
    const auto size = value.at("size").get<std::uint64_t>();
    if (!safe_relative_path(result.path) || !lower_hex(result.oid, 40) ||
        !lower_hex(result.sha256, 64) || size == 0 || size > max_source_bytes)
        fail(PublicationErrorCode::invalid_lock, "source descriptor is outside fixed limits");
    result.size = static_cast<std::size_t>(size);
    return result;
}

[[nodiscard]] std::string expected_graph_id(const std::string_view bundle_id)
{
    if (bundle_id == "procedure-support/navigation.to-main-page/v1")
        return "feature/navigation.to-main-page";
    if (bundle_id == "procedure-support/group.open/v1") return "feature/group.open";
    fail(PublicationErrorCode::invalid_lock, "unsupported group bundle id");
}

[[nodiscard]] std::optional<std::string_view> expected_rgb_id(
    const std::string_view bundle_id, const std::string_view feature) noexcept
{
    using Pair = std::pair<std::string_view, std::string_view>;
    constexpr std::array<Pair, 7> navigation{{
        {"main_page", "rgb/main-page"},
        {"relationship_rank_up", "rgb/relationship-rank-up"},
        {"area_rank_up", "rgb/area-rank-up"},
        {"level_up", "rgb/level-up"},
        {"reward_acquired", "rgb/reward-acquired"},
        {"loadingNotWhite", "rgb/loading-not-white"},
        {"loadingWhite", "rgb/loading-white"},
    }};
    constexpr std::array<Pair, 6> group{{
        {"main_page", "rgb/main-page"},
        {"relationship_rank_up", "rgb/relationship-rank-up"},
        {"level_up", "rgb/level-up"},
        {"reward_acquired", "rgb/reward-acquired"},
        {"loadingNotWhite", "rgb/loading-not-white"},
        {"loadingWhite", "rgb/loading-white"},
    }};
    const auto find = [feature](const auto& values) -> std::optional<std::string_view> {
        const auto item = std::ranges::find(values, feature, &Pair::first);
        return item == values.end() ? std::nullopt : std::optional{item->second};
    };
    if (bundle_id == "procedure-support/navigation.to-main-page/v1")
        return find(navigation);
    if (bundle_id == "procedure-support/group.open/v1") return find(group);
    return std::nullopt;
}

void validate_image_identity(const Member& member, const std::string_view profile)
{
    const auto& png = *member.source;
    const auto parts = split_path(png.path);
    if (parts.size() < 5 || parts[0] != "src" || parts[1] != "images" ||
        parts[2] != profile || !parts.back().ends_with(".png"))
        fail(PublicationErrorCode::locale_fallback_forbidden,
             "PNG source is not an exact same-profile legacy path");
    if (member.id != "image/" + ascii_lower(member.feature))
        fail(PublicationErrorCode::alias_forbidden,
             "image member id is not the lowercase exact feature identity");
    const auto crop_parts = split_path(member.crop_source->path);
    if (crop_parts.size() != 5 || crop_parts[0] != "src" || crop_parts[1] != "images" ||
        crop_parts[2] != profile || crop_parts[3] != "x_y_range" ||
        !crop_parts[4].ends_with(".py"))
        fail(PublicationErrorCode::locale_fallback_forbidden,
             "crop source is not an exact same-profile metadata file");
}

[[nodiscard]] Member parse_member(
    const Json& value, const std::string_view bundle_id, const std::string_view profile,
    const std::size_t index)
{
    if (!value.is_object() || !value.contains("kind") || !value.at("kind").is_string())
        fail(PublicationErrorCode::invalid_lock, "member kind is missing");
    const auto kind = value.at("kind").get<std::string_view>();
    Member result;
    if (kind == "feature-graph") {
        if (!exact_fields(value, {"id", "kind"}) || index != 0 ||
            !value.at("id").is_string())
            fail(PublicationErrorCode::invalid_lock, "feature graph must be the first member");
        result.kind = MemberKind::graph;
        result.id = value.at("id").get<std::string>();
        if (result.id != expected_graph_id(bundle_id))
            fail(PublicationErrorCode::invalid_lock, "feature graph id does not match bundle");
        return result;
    }
    if (kind == "rgb-range-set") {
        if (!exact_fields(value, {"id", "kind", "feature", "source", "source_key"}) ||
            !value.at("id").is_string() || !value.at("feature").is_string() ||
            !value.at("source_key").is_string())
            fail(PublicationErrorCode::invalid_lock, "RGB member has invalid fields");
        result.kind = MemberKind::rgb;
        result.id = value.at("id").get<std::string>();
        result.feature = value.at("feature").get<std::string>();
        result.source_key = value.at("source_key").get<std::string>();
        result.source = parse_source(value.at("source"));
        const auto expected_id = expected_rgb_id(bundle_id, result.feature);
        if (!valid_feature(result.feature) || result.feature != result.source_key ||
            !expected_id || result.id != *expected_id ||
            !resources::valid_resource_id(result.id, max_path_bytes))
            fail(PublicationErrorCode::alias_forbidden,
                 "RGB feature must exactly name its source key and canonical member");
        const auto expected = "src/rgb_feature/" + std::string{profile} + ".json";
        if (result.source->path != expected)
            fail(PublicationErrorCode::locale_fallback_forbidden,
                 "RGB source is not the exact same-profile blob");
        return result;
    }
    if (kind == "png-template") {
        if (!exact_fields(value, {"id", "kind", "feature", "source", "crop_source",
                                  "crop", "threshold_milli", "mean_rgb_tolerance"}) ||
            !value.at("id").is_string() || !value.at("feature").is_string() ||
            !value.at("crop").is_array() || value.at("crop").size() != 4 ||
            !value.at("threshold_milli").is_number_unsigned() ||
            !value.at("mean_rgb_tolerance").is_number_unsigned())
            fail(PublicationErrorCode::invalid_lock, "PNG member has invalid fields");
        result.kind = MemberKind::png;
        result.id = value.at("id").get<std::string>();
        result.feature = value.at("feature").get<std::string>();
        result.source = parse_source(value.at("source"));
        result.crop_source = parse_source(value.at("crop_source"));
        if (!valid_feature(result.feature) || !resources::valid_resource_id(result.id, max_path_bytes) ||
            result.id != ascii_lower(result.id))
            fail(PublicationErrorCode::alias_forbidden, "PNG identity is not canonical");
        std::array<std::uint64_t, 4> crop{};
        for (std::size_t coordinate = 0; coordinate < crop.size(); ++coordinate) {
            if (!value.at("crop").at(coordinate).is_number_unsigned())
                fail(PublicationErrorCode::invalid_lock, "crop coordinate is not unsigned");
            crop[coordinate] = value.at("crop").at(coordinate).get<std::uint64_t>();
        }
        const auto threshold = value.at("threshold_milli").get<std::uint64_t>();
        const auto tolerance = value.at("mean_rgb_tolerance").get<std::uint64_t>();
        if (crop[0] >= crop[2] || crop[1] >= crop[3] || crop[2] > 1'280 ||
            crop[3] > 720 || threshold != 800 || tolerance != 20)
            fail(PublicationErrorCode::invalid_lock, "crop or matcher settings are invalid");
        for (std::size_t coordinate = 0; coordinate < crop.size(); ++coordinate)
            result.crop[coordinate] = static_cast<std::uint16_t>(crop[coordinate]);
        result.threshold_milli = static_cast<std::uint16_t>(threshold);
        result.mean_rgb_tolerance = static_cast<std::uint16_t>(tolerance);
        validate_image_identity(result, profile);
        return result;
    }
    fail(PublicationErrorCode::invalid_lock, "unsupported member kind");
}

template <typename T, void (*Free)(T*)>
class GitOwner final {
public:
    explicit GitOwner(T* value = nullptr) noexcept : value_(value) {}
    ~GitOwner() { if (value_) Free(value_); }
    GitOwner(const GitOwner&) = delete;
    GitOwner& operator=(const GitOwner&) = delete;
    GitOwner(GitOwner&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    GitOwner& operator=(GitOwner&& other) noexcept
    {
        if (this != &other) {
            if (value_) Free(value_);
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] T* get() const noexcept { return value_; }
private:
    T* value_{};
};

class GitLifetime final {
public:
    GitLifetime()
    {
        if (git_libgit2_init() < 0)
            fail(PublicationErrorCode::invalid_repository, "libgit2 initialization failed");
    }
    ~GitLifetime() { git_libgit2_shutdown(); }
};

void ensure_git_lifetime()
{
    static GitLifetime lifetime;
    static_cast<void>(lifetime);
}

[[nodiscard]] std::string native_utf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::string git_message(const std::string_view prefix)
{
    const auto* error = git_error_last();
    return std::string{prefix} + (error && error->message ? ": " + std::string{error->message} : "");
}

struct VerifiedSource final {
    SourceDescriptor descriptor;
    std::vector<std::byte> bytes;
};

class PinnedRepository final {
public:
    PinnedRepository(const std::filesystem::path& path, const std::string_view commit)
    {
        ensure_git_lifetime();
        git_repository* raw_repository{};
        const auto path_utf8 = native_utf8(path);
        if (git_repository_open_ext(&raw_repository, path_utf8.c_str(),
                                    GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr) < 0)
            fail(PublicationErrorCode::invalid_repository, git_message("repository open failed"));
        repository_ = GitOwner<git_repository, git_repository_free>{raw_repository};
        git_oid commit_oid{};
        if (!lower_hex(commit, 40) || git_oid_fromstr(&commit_oid, std::string{commit}.c_str()) < 0)
            fail(PublicationErrorCode::commit_mismatch, "source commit is not exact SHA-1");
        git_commit* raw_commit{};
        if (git_commit_lookup(&raw_commit, repository_.get(), &commit_oid) < 0)
            fail(PublicationErrorCode::commit_mismatch, git_message("pinned commit lookup failed"));
        commit_ = GitOwner<git_commit, git_commit_free>{raw_commit};
        git_tree* raw_tree{};
        if (git_commit_tree(&raw_tree, commit_.get()) < 0)
            fail(PublicationErrorCode::commit_mismatch, git_message("pinned commit tree failed"));
        tree_ = GitOwner<git_tree, git_tree_free>{raw_tree};
        git_odb* raw_odb{};
        if (git_repository_odb(&raw_odb, repository_.get()) < 0)
            fail(PublicationErrorCode::invalid_repository, git_message("object database open failed"));
        odb_ = GitOwner<git_odb, git_odb_free>{raw_odb};
    }

    [[nodiscard]] VerifiedSource read(const SourceDescriptor& descriptor) const
    {
        git_tree_entry* raw_entry{};
        if (git_tree_entry_bypath(&raw_entry, tree_.get(), descriptor.path.c_str()) < 0)
            fail(PublicationErrorCode::source_not_found,
                 "pinned source path is absent: " + descriptor.path);
        GitOwner<git_tree_entry, git_tree_entry_free> entry{raw_entry};
        if (git_tree_entry_type(entry.get()) != GIT_OBJECT_BLOB)
            fail(PublicationErrorCode::source_type_mismatch,
                 "pinned source is not a blob: " + descriptor.path);
        git_oid expected{};
        if (git_oid_fromstr(&expected, descriptor.oid.c_str()) < 0 ||
            !git_oid_equal(&expected, git_tree_entry_id(entry.get())))
            fail(PublicationErrorCode::source_oid_mismatch,
                 "pinned source OID mismatch: " + descriptor.path);
        git_odb_object* raw_object{};
        if (git_odb_read(&raw_object, odb_.get(), &expected) < 0)
            fail(PublicationErrorCode::source_not_found,
                 git_message("pinned blob ODB read failed"));
        GitOwner<git_odb_object, git_odb_object_free> object{raw_object};
        if (git_odb_object_type(object.get()) != GIT_OBJECT_BLOB)
            fail(PublicationErrorCode::source_type_mismatch,
                 "pinned ODB object is not a blob: " + descriptor.path);
        const auto size = git_odb_object_size(object.get());
        if (size != descriptor.size)
            fail(PublicationErrorCode::source_size_mismatch,
                 "pinned source size mismatch: " + descriptor.path);
        std::vector<std::byte> bytes(size);
        if (size != 0) std::memcpy(bytes.data(), git_odb_object_data(object.get()), size);
        if (resources::sha256_hex(bytes) != descriptor.sha256)
            fail(PublicationErrorCode::source_digest_mismatch,
                 "pinned source digest mismatch: " + descriptor.path);
        return {descriptor, std::move(bytes)};
    }

private:
    GitOwner<git_repository, git_repository_free> repository_;
    GitOwner<git_commit, git_commit_free> commit_;
    GitOwner<git_tree, git_tree_free> tree_;
    GitOwner<git_odb, git_odb_free> odb_;
};

[[nodiscard]] std::string bytes_text(const std::span<const std::byte> bytes)
{
    if (!valid_utf8(bytes))
        fail(PublicationErrorCode::source_content_invalid, "source is not strict UTF-8");
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] OrderedJson compile_rgb(
    const Member& member, const std::span<const std::byte> source_bytes)
{
    const auto text = bytes_text(source_bytes);
    const auto parsed = strict_json::parse_strict_json(
        text, {.max_depth = 16, .max_nodes = 262'144, .max_string_bytes = max_path_bytes});
    if (!parsed || !parsed.document->is_object() || parsed.document->size() != 1 ||
        !parsed.document->contains("rgb_feature") ||
        !parsed.document->at("rgb_feature").is_object())
        fail(PublicationErrorCode::source_content_invalid, "legacy RGB blob is invalid");
    const auto& table = parsed.document->at("rgb_feature");
    if (!table.contains(member.source_key))
        fail(PublicationErrorCode::source_content_invalid,
             "legacy RGB key is absent: " + member.source_key);
    const auto& legacy = table.at(member.source_key);
    if (!legacy.is_array() || legacy.size() != 2 || !legacy.at(0).is_array() ||
        !legacy.at(1).is_array() || legacy.at(0).empty() ||
        legacy.at(0).size() != legacy.at(1).size() || legacy.at(0).size() > 8'192)
        fail(PublicationErrorCode::source_content_invalid, "legacy RGB value is invalid");
    OrderedJson samples = OrderedJson::array();
    for (std::size_t index = 0; index < legacy.at(0).size(); ++index) {
        const auto& coordinate = legacy.at(0).at(index);
        const auto& range = legacy.at(1).at(index);
        if (!coordinate.is_array() || coordinate.size() != 2 ||
            !range.is_array() || range.size() != 6)
            fail(PublicationErrorCode::source_content_invalid, "legacy RGB sample shape is invalid");
        std::array<std::uint64_t, 2> xy{};
        std::array<std::uint64_t, 6> channels{};
        for (std::size_t item = 0; item < xy.size(); ++item) {
            if (!coordinate.at(item).is_number_unsigned())
                fail(PublicationErrorCode::source_content_invalid, "legacy RGB coordinate is invalid");
            xy[item] = coordinate.at(item).get<std::uint64_t>();
        }
        for (std::size_t item = 0; item < channels.size(); ++item) {
            if (!range.at(item).is_number_unsigned())
                fail(PublicationErrorCode::source_content_invalid, "legacy RGB channel is invalid");
            channels[item] = range.at(item).get<std::uint64_t>();
        }
        // Python evaluates the source list in order and permits repeated pixels.
        // Preserve that list semantics instead of inventing a uniqueness rule.
        if (xy[0] >= 1'280 || xy[1] >= 720 ||
            channels[0] > channels[1] || channels[2] > channels[3] ||
            channels[4] > channels[5] ||
            std::ranges::any_of(channels, [](const auto value) { return value > 255; }))
            fail(PublicationErrorCode::source_content_invalid, "legacy RGB values are out of range");
        OrderedJson sample;
        sample["x"] = xy[0]; sample["y"] = xy[1];
        sample["r"] = OrderedJson::array({channels[0], channels[1]});
        sample["g"] = OrderedJson::array({channels[2], channels[3]});
        sample["b"] = OrderedJson::array({channels[4], channels[5]});
        samples.push_back(std::move(sample));
    }
    OrderedJson result;
    result["schema"] = procedure::co_detect_rgb_ranges_schema;
    result["samples"] = std::move(samples);
    return result;
}

[[nodiscard]] std::string trim(const std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r");
    return std::string{value.substr(first, last - first + 1)};
}

[[nodiscard]] std::optional<std::string> assignment(
    const std::string_view line, const std::string_view name)
{
    auto value = trim(line);
    if (!value.starts_with(name)) return std::nullopt;
    std::size_t cursor = name.size();
    while (cursor < value.size() && (value[cursor] == ' ' || value[cursor] == '\t')) ++cursor;
    if (cursor >= value.size() || value[cursor++] != '=') return std::nullopt;
    while (cursor < value.size() && (value[cursor] == ' ' || value[cursor] == '\t')) ++cursor;
    if (cursor >= value.size() || (value[cursor] != '\'' && value[cursor] != '"')) return std::nullopt;
    const char quote = value[cursor++];
    const auto end = value.find(quote, cursor);
    if (end == std::string::npos) return std::nullopt;
    const auto tail = trim(std::string_view{value}.substr(end + 1));
    if (!tail.empty() && !tail.starts_with('#')) return std::nullopt;
    return value.substr(cursor, end - cursor);
}

[[nodiscard]] std::optional<std::array<std::uint16_t, 4>> crop_declaration(
    const std::string_view line, const std::string_view key)
{
    const auto value = trim(line);
    if (value.empty() || value.front() == '#') return std::nullopt;
    const auto quote_position = value.find_first_of("'\"");
    if (quote_position == std::string::npos) return std::nullopt;
    const char quote = value[quote_position];
    const auto quote_end = value.find(quote, quote_position + 1);
    if (quote_end == std::string::npos ||
        std::string_view{value}.substr(quote_position + 1, quote_end - quote_position - 1) != key)
        return std::nullopt;
    std::size_t cursor = quote_end + 1;
    while (cursor < value.size() && (value[cursor] == ' ' || value[cursor] == '\t')) ++cursor;
    if (cursor >= value.size() || value[cursor++] != ':') return std::nullopt;
    while (cursor < value.size() && (value[cursor] == ' ' || value[cursor] == '\t')) ++cursor;
    if (cursor >= value.size() || value[cursor++] != '(') return std::nullopt;
    std::array<std::uint16_t, 4> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        while (cursor < value.size() && (value[cursor] == ' ' || value[cursor] == '\t')) ++cursor;
        const auto begin = value.data() + cursor;
        std::uint64_t parsed{};
        const auto converted = std::from_chars(begin, value.data() + value.size(), parsed);
        if (converted.ec != std::errc{} || parsed > std::numeric_limits<std::uint16_t>::max())
            return std::nullopt;
        cursor = static_cast<std::size_t>(converted.ptr - value.data());
        result[index] = static_cast<std::uint16_t>(parsed);
        while (cursor < value.size() && (value[cursor] == ' ' || value[cursor] == '\t')) ++cursor;
        if (index + 1 != result.size()) {
            if (cursor >= value.size() || value[cursor++] != ',') return std::nullopt;
        }
    }
    while (cursor < value.size() && (value[cursor] == ' ' || value[cursor] == '\t')) ++cursor;
    if (cursor >= value.size() || value[cursor++] != ')') return std::nullopt;
    while (cursor < value.size() && (value[cursor] == ' ' || value[cursor] == '\t')) ++cursor;
    if (cursor < value.size() && value[cursor] == ',') ++cursor;
    while (cursor < value.size() && (value[cursor] == ' ' || value[cursor] == '\t')) ++cursor;
    if (cursor < value.size() && value[cursor] != '#') return std::nullopt;
    return result;
}

[[nodiscard]] bool binding_prefix(
    const std::string_view line, const std::string_view name) noexcept
{
    if (!line.starts_with(name)) return false;
    auto cursor = name.size();
    while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) ++cursor;
    return cursor < line.size() && line[cursor] == '=';
}

void verify_crop_source(
    const Member& member, const std::span<const std::byte> source_bytes)
{
    const auto text = bytes_text(source_bytes);
    const auto png_parts = split_path(member.source->path);
    const auto filename = png_parts.back().substr(0, png_parts.back().size() - 4U);
    bool prefix_found{};
    bool path_found{};
    std::string prefix;
    std::string image_path;
    bool range_open{};
    bool range_closed{};
    std::optional<std::array<std::uint16_t, 4>> found;
    std::size_t offset{};
    while (offset <= text.size()) {
        const auto end = text.find('\n', offset);
        const auto line = std::string_view{text}.substr(
            offset, end == std::string::npos ? text.size() - offset : end - offset);
        const auto stripped = trim(line);
        const bool indented = !line.empty() && (line.front() == ' ' || line.front() == '\t');
        if (indented && (binding_prefix(stripped, "prefix") ||
                         binding_prefix(stripped, "path") ||
                         binding_prefix(stripped, "x_y_range")))
            fail(PublicationErrorCode::source_content_invalid,
                 "crop metadata bindings must be active top-level statements");
        if (!range_open && !range_closed && !indented &&
            binding_prefix(line, "prefix")) {
            const auto value = assignment(line, "prefix");
            if (!value)
                fail(PublicationErrorCode::source_content_invalid, "crop prefix syntax is invalid");
            if (prefix_found || !valid_feature(*value))
                fail(PublicationErrorCode::alias_forbidden, "crop prefix is not exact");
            prefix = *value;
            prefix_found = true;
        } else if (!range_open && !range_closed && !indented &&
                   binding_prefix(line, "path")) {
            const auto value = assignment(line, "path");
            if (!value)
                fail(PublicationErrorCode::source_content_invalid, "crop path syntax is invalid");
            if (path_found || !safe_relative_path(*value))
                fail(PublicationErrorCode::alias_forbidden, "crop path is not exact");
            image_path = *value;
            path_found = true;
        } else if (!range_open && !range_closed && !indented &&
                   binding_prefix(line, "x_y_range")) {
            if (stripped != "x_y_range = {")
                fail(PublicationErrorCode::source_content_invalid,
                     "crop table must be one active top-level dictionary");
            range_open = true;
        } else if (range_open) {
            if (!indented && (stripped == "}" || stripped == "},")) {
                range_open = false;
                range_closed = true;
            } else if (!stripped.empty() && !stripped.starts_with('#')) {
                if (!indented)
                    fail(PublicationErrorCode::source_content_invalid,
                         "crop table contains a non-dictionary statement");
                if (const auto crop = crop_declaration(line, filename)) {
                    if (found) fail(PublicationErrorCode::source_content_invalid,
                                    "duplicate active crop declaration");
                    found = crop;
                }
            }
        } else if (range_closed && !indented &&
                   (binding_prefix(line, "prefix") || binding_prefix(line, "path") ||
                    binding_prefix(line, "x_y_range"))) {
            fail(PublicationErrorCode::source_content_invalid,
                 "crop metadata binding is reassigned");
        }
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    if (!prefix_found || !path_found || !range_closed || !found)
        fail(PublicationErrorCode::source_content_invalid,
             "exact active crop declaration is absent");
    if (*found != member.crop)
        fail(PublicationErrorCode::source_content_invalid,
             "reviewed crop does not match pinned metadata");
    if (member.feature != prefix + "_" + std::string{filename})
        fail(PublicationErrorCode::alias_forbidden,
             "image feature does not exactly match active prefix and filename");
    const auto expected_png = "src/images/" + std::string{png_parts[2]} + "/" +
        image_path + "/" + std::string{filename} + ".png";
    if (member.source->path != expected_png)
        fail(PublicationErrorCode::alias_forbidden,
             "PNG path does not exactly match active crop metadata path");
}

[[nodiscard]] std::uint32_t be32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) << 24U |
        std::to_integer<std::uint32_t>(bytes[offset + 1]) << 16U |
        std::to_integer<std::uint32_t>(bytes[offset + 2]) << 8U |
        std::to_integer<std::uint32_t>(bytes[offset + 3]);
}

void verify_png_source(const std::span<const std::byte> bytes)
{
    constexpr std::array<std::byte, 8> signature{
        std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'},
        std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};
    if (bytes.size() < 57 || !std::ranges::equal(signature, bytes.first(signature.size())))
        fail(PublicationErrorCode::placeholder_forbidden, "PNG source is empty or not PNG");
    std::size_t cursor = signature.size();
    bool ihdr{};
    bool idat{};
    bool idat_ended{};
    bool iend{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint8_t color{};
    std::vector<unsigned char> compressed;
    std::set<std::string, std::less<>> ancillary;
    while (cursor < bytes.size()) {
        if (bytes.size() - cursor < 12)
            fail(PublicationErrorCode::source_content_invalid, "PNG chunk is truncated");
        const auto size = be32(bytes, cursor);
        const auto type = cursor + 4U;
        const auto data = cursor + 8U;
        if (size > bytes.size() - data - 4U)
            fail(PublicationErrorCode::source_content_invalid, "PNG chunk size is invalid");
        const auto crc = static_cast<std::uint32_t>(mz_crc32(
            MZ_CRC32_INIT, reinterpret_cast<const mz_uint8*>(bytes.data() + type),
            static_cast<std::size_t>(size) + 4U));
        if (crc != be32(bytes, data + size))
            fail(PublicationErrorCode::source_content_invalid, "PNG CRC is invalid");
        const auto is = [&bytes, type](const std::string_view expected) {
            return std::memcmp(bytes.data() + type, expected.data(), 4) == 0;
        };
        for (std::size_t index = 0; index < 4; ++index) {
            const auto byte = std::to_integer<unsigned char>(bytes[type + index]);
            if (!((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')) ||
                (index == 2 && !(byte >= 'A' && byte <= 'Z')))
                fail(PublicationErrorCode::source_content_invalid,
                     "PNG chunk type is invalid");
        }
        if (is("IHDR")) {
            if (ihdr || cursor != signature.size() || size != 13)
                fail(PublicationErrorCode::source_content_invalid, "PNG IHDR is invalid");
            ihdr = true;
            width = be32(bytes, data); height = be32(bytes, data + 4);
            const auto depth = std::to_integer<unsigned char>(bytes[data + 8]);
            color = std::to_integer<unsigned char>(bytes[data + 9]);
            if (width == 0 || height == 0 || depth != 8 || (color != 2 && color != 6) ||
                bytes[data + 10] != std::byte{0} || bytes[data + 11] != std::byte{0} ||
                bytes[data + 12] != std::byte{0})
                fail(PublicationErrorCode::source_content_invalid, "PNG IHDR values are invalid");
        } else if (is("IDAT")) {
            if (!ihdr || iend || idat_ended || size == 0)
                fail(PublicationErrorCode::source_content_invalid, "PNG IDAT is invalid");
            idat = true;
            if (size > max_source_bytes - std::min<std::size_t>(compressed.size(), max_source_bytes))
                fail(PublicationErrorCode::source_content_invalid, "PNG IDAT is too large");
            const auto first = reinterpret_cast<const unsigned char*>(bytes.data() + data);
            compressed.insert(compressed.end(), first, first + size);
        } else if (is("IEND")) {
            if (!ihdr || !idat || iend || size != 0)
                fail(PublicationErrorCode::source_content_invalid, "PNG IEND is invalid");
            iend = true;
        } else {
            if (idat) idat_ended = true;
            const bool allowed =
                (is("cHRM") && size == 32) || (is("gAMA") && size == 4) ||
                (is("sBIT") && size == (color == 2 ? 3U : 4U)) ||
                (is("sRGB") && size == 1) || (is("pHYs") && size == 9);
            const std::string type_name{
                reinterpret_cast<const char*>(bytes.data() + type), 4};
            if (!ihdr || iend || !allowed || !ancillary.insert(type_name).second)
                fail(PublicationErrorCode::source_content_invalid,
                     "PNG contains a non-canonical or duplicate chunk");
        }
        cursor = data + size + 4U;
        if (iend && cursor != bytes.size())
            fail(PublicationErrorCode::source_content_invalid, "PNG has trailing bytes");
    }
    // Python and the production C++ adapter resize the screenshot crop to the
    // template size. Real pinned templates therefore need not equal crop size.
    if (!ihdr || !idat || !iend)
        fail(PublicationErrorCode::placeholder_forbidden, "PNG payload is incomplete");
    const auto channels = color == 2 ? 3U : 4U;
    const auto decoded_size = static_cast<std::uint64_t>(height) *
        (static_cast<std::uint64_t>(width) * channels + 1U);
    if (decoded_size > max_source_bytes || decoded_size > std::numeric_limits<mz_ulong>::max())
        fail(PublicationErrorCode::source_content_invalid, "PNG decoded size is invalid");
    std::vector<unsigned char> decoded(static_cast<std::size_t>(decoded_size));
    auto actual_size = static_cast<mz_ulong>(decoded.size());
    if (mz_uncompress(decoded.data(), &actual_size, compressed.data(),
                      static_cast<mz_ulong>(compressed.size())) != MZ_OK ||
        actual_size != decoded.size())
        fail(PublicationErrorCode::source_content_invalid,
             "PNG compressed pixel stream is invalid");
    const auto row_size = static_cast<std::size_t>(width) * channels + 1U;
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * channels);
    const auto paeth = [](const int left, const int up, const int upper_left) {
        const auto estimate = left + up - upper_left;
        const auto left_distance = std::abs(estimate - left);
        const auto up_distance = std::abs(estimate - up);
        const auto diagonal_distance = std::abs(estimate - upper_left);
        return left_distance <= up_distance && left_distance <= diagonal_distance ? left
            : up_distance <= diagonal_distance ? up : upper_left;
    };
    for (std::uint32_t row = 0; row < height; ++row) {
        const auto filter = decoded[static_cast<std::size_t>(row) * row_size];
        if (filter > 4U)
            fail(PublicationErrorCode::source_content_invalid, "PNG row filter is invalid");
        for (std::size_t column = 0; column < static_cast<std::size_t>(width) * channels;
             ++column) {
            const auto raw = decoded[static_cast<std::size_t>(row) * row_size + 1U + column];
            const auto target = static_cast<std::size_t>(row) * width * channels + column;
            const auto left = column < channels ? 0 : pixels[target - channels];
            const auto up = row == 0 ? 0 : pixels[target - static_cast<std::size_t>(width) * channels];
            const auto upper_left = row == 0 || column < channels ? 0
                : pixels[target - static_cast<std::size_t>(width) * channels - channels];
            const auto predictor = filter == 0 ? 0 : filter == 1 ? left : filter == 2 ? up
                : filter == 3 ? (left + up) / 2 : paeth(left, up, upper_left);
            pixels[target] = static_cast<unsigned char>(raw + predictor);
        }
    }
    if (width < 2 || height < 2)
        fail(PublicationErrorCode::placeholder_forbidden, "PNG template is too small");
    bool varied{};
    for (std::size_t pixel = 1; pixel < static_cast<std::size_t>(width) * height; ++pixel)
        varied = varied || !std::equal(
            pixels.begin(), pixels.begin() + channels,
            pixels.begin() + static_cast<std::ptrdiff_t>(pixel * channels));
    bool all_transparent = channels == 4;
    if (channels == 4)
        for (std::size_t index = 3; index < pixels.size(); index += 4)
            all_transparent = all_transparent && pixels[index] == 0;
    if (!varied || all_transparent)
        fail(PublicationErrorCode::placeholder_forbidden, "PNG template is blank");
}

void append_u16(std::vector<std::byte>& output, const std::uint16_t value)
{
    output.push_back(static_cast<std::byte>(value & 0xffU));
    output.push_back(static_cast<std::byte>(value >> 8U));
}

void append_u32(std::vector<std::byte>& output, const std::uint32_t value)
{
    for (std::size_t index = 0; index < 4; ++index)
        output.push_back(static_cast<std::byte>(value >> (index * 8U)));
}

void append_text(std::vector<std::byte>& output, const std::string_view value)
{
    const auto bytes = std::as_bytes(std::span{value.data(), value.size()});
    output.insert(output.end(), bytes.begin(), bytes.end());
}

[[nodiscard]] std::vector<std::byte> text_bytes(const std::string_view value)
{
    const auto bytes = std::as_bytes(std::span{value.data(), value.size()});
    return {bytes.begin(), bytes.end()};
}

struct ArchiveItem final { std::string name; std::vector<std::byte> bytes; };

[[nodiscard]] std::vector<std::byte> canonical_store_zip(
    const std::vector<ArchiveItem>& items)
{
    struct Central final {
        const ArchiveItem* item{};
        std::uint32_t offset{};
        std::uint32_t crc{};
    };
    if (items.size() > std::numeric_limits<std::uint16_t>::max())
        fail(PublicationErrorCode::publication_invalid, "too many archive entries");
    std::vector<std::byte> output;
    std::vector<Central> central;
    for (const auto& item : items) {
        if (item.name.size() > std::numeric_limits<std::uint16_t>::max() ||
            item.bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
            output.size() > std::numeric_limits<std::uint32_t>::max())
            fail(PublicationErrorCode::publication_invalid, "archive exceeds ZIP32");
        const auto offset = static_cast<std::uint32_t>(output.size());
        const auto crc = static_cast<std::uint32_t>(mz_crc32(
            MZ_CRC32_INIT, reinterpret_cast<const mz_uint8*>(item.bytes.data()), item.bytes.size()));
        append_u32(output, 0x04034b50U); append_u16(output, 10U);
        append_u16(output, 0U); append_u16(output, 0U); append_u16(output, 0U);
        append_u16(output, 0U); append_u32(output, crc);
        append_u32(output, static_cast<std::uint32_t>(item.bytes.size()));
        append_u32(output, static_cast<std::uint32_t>(item.bytes.size()));
        append_u16(output, static_cast<std::uint16_t>(item.name.size()));
        append_u16(output, 0U); append_text(output, item.name);
        output.insert(output.end(), item.bytes.begin(), item.bytes.end());
        central.push_back({&item, offset, crc});
    }
    if (output.size() > std::numeric_limits<std::uint32_t>::max())
        fail(PublicationErrorCode::publication_invalid, "archive exceeds ZIP32");
    const auto central_offset = static_cast<std::uint32_t>(output.size());
    for (const auto& entry : central) {
        append_u32(output, 0x02014b50U); append_u16(output, 20U); append_u16(output, 10U);
        append_u16(output, 0U); append_u16(output, 0U); append_u16(output, 0U);
        append_u16(output, 0U); append_u32(output, entry.crc);
        append_u32(output, static_cast<std::uint32_t>(entry.item->bytes.size()));
        append_u32(output, static_cast<std::uint32_t>(entry.item->bytes.size()));
        append_u16(output, static_cast<std::uint16_t>(entry.item->name.size()));
        append_u16(output, 0U); append_u16(output, 0U); append_u16(output, 0U);
        append_u16(output, 0U); append_u32(output, 0U); append_u32(output, entry.offset);
        append_text(output, entry.item->name);
    }
    const auto central_size = output.size() - central_offset;
    if (central_size > std::numeric_limits<std::uint32_t>::max())
        fail(PublicationErrorCode::publication_invalid, "archive central directory exceeds ZIP32");
    append_u32(output, 0x06054b50U); append_u16(output, 0U); append_u16(output, 0U);
    append_u16(output, static_cast<std::uint16_t>(items.size()));
    append_u16(output, static_cast<std::uint16_t>(items.size()));
    append_u32(output, static_cast<std::uint32_t>(central_size));
    append_u32(output, central_offset); append_u16(output, 0U);
    return output;
}

[[nodiscard]] std::string physical_member_name(const std::size_t index)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result(9, '0'); result[0] = 'm';
    auto value = static_cast<std::uint32_t>(index);
    for (std::size_t digit = 0; digit < 8; ++digit) {
        result[8 - digit] = digits[value & 0xfU]; value >>= 4U;
    }
    return result;
}

struct CompiledMember final {
    const Member* lock{};
    std::vector<std::byte> bytes;
    std::string kind;
    std::string media_type;
};

struct PublicationBudget final {
    std::size_t sources{};
    std::size_t outputs{};
    std::size_t work{};

    void charge(std::size_t& value, const std::size_t amount, const std::size_t limit,
                const std::string_view label)
    {
        if (amount > limit - std::min(value, limit))
            fail(PublicationErrorCode::resource_exhausted,
                 std::string{label} + " budget exceeded");
        value += amount;
    }
    void source(const std::size_t amount)
    {
        charge(sources, amount, max_total_bytes, "source");
        charge(work, amount, max_work, "work");
    }
    void output(const std::size_t amount)
    {
        charge(outputs, amount, max_total_bytes, "publication output");
        charge(work, amount, max_work, "work");
    }
};

[[nodiscard]] std::vector<std::byte> compile_bundle(
    const Bundle& bundle, const PinnedRepository& repository, PublicationBudget& budget)
{
    std::vector<VerifiedSource> verified;
    verified.reserve(bundle.members.size() * 2U);
    auto read = [&repository, &verified, &budget](
                    const SourceDescriptor& source) -> const VerifiedSource& {
        const auto found = std::ranges::find_if(verified, [&source](const VerifiedSource& item) {
            return item.descriptor.path == source.path;
        });
        if (found != verified.end()) {
            if (found->descriptor.oid != source.oid || found->descriptor.size != source.size ||
                found->descriptor.sha256 != source.sha256)
                fail(PublicationErrorCode::invalid_lock,
                     "one source path has conflicting lock identities");
            return *found;
        }
        budget.source(source.size);
        verified.push_back(repository.read(source));
        return verified.back();
    };
    OrderedJson features = OrderedJson::array();
    std::vector<CompiledMember> compiled;
    compiled.reserve(bundle.members.size());
    for (std::size_t index = 1; index < bundle.members.size(); ++index) {
        const auto& member = bundle.members[index];
        OrderedJson feature;
        feature["name"] = member.feature;
        if (member.kind == MemberKind::rgb) {
            feature["type"] = "rgb"; feature["member"] = member.id;
            const auto rgb = compile_rgb(member, read(*member.source).bytes);
            compiled.push_back({&member, text_bytes(rgb.dump()), "rgb-range-set",
                                "application/vnd.baas.co-detect-rgb-ranges.v1+json"});
        } else if (member.kind == MemberKind::png) {
            const auto& png = read(*member.source).bytes;
            const auto& crop = read(*member.crop_source).bytes;
            verify_crop_source(member, crop);
            verify_png_source(png);
            feature["type"] = "image"; feature["member"] = member.id;
            feature["crop"] = OrderedJson::array(
                {member.crop[0], member.crop[1], member.crop[2], member.crop[3]});
            feature["threshold_milli"] = member.threshold_milli;
            feature["mean_rgb_tolerance"] = member.mean_rgb_tolerance;
            compiled.push_back({&member, png, "png-template", "image/png"});
        }
        features.push_back(std::move(feature));
    }
    OrderedJson graph;
    graph["schema"] = procedure::co_detect_feature_graph_schema;
    graph["features"] = std::move(features);
    compiled.insert(compiled.begin(),
                    {&bundle.members[0], text_bytes(graph.dump()), "feature-graph",
                     "application/vnd.baas.co-detect-feature-graph.v1+json"});

    OrderedJson members = OrderedJson::array();
    std::uint64_t payload_size{};
    for (const auto& member : compiled) {
        if (member.bytes.empty() || member.bytes.size() > max_member_bytes ||
            member.bytes.size() > max_total_bytes -
                std::min<std::uint64_t>(payload_size, max_total_bytes))
            fail(PublicationErrorCode::resource_exhausted,
                 "support bundle member or payload budget exceeded");
        payload_size += member.bytes.size();
        OrderedJson entry;
        entry["id"] = member.lock->id; entry["kind"] = member.kind;
        entry["media_type"] = member.media_type; entry["size"] = member.bytes.size();
        entry["sha256"] = resources::sha256_hex(member.bytes);
        members.push_back(std::move(entry));
    }
    OrderedJson manifest;
    manifest["schema"] = procedure::co_detect_support_bundle_schema;
    manifest["format_version"] = 1; manifest["bundle_id"] = bundle.bundle_id;
    manifest["locale"] = bundle.locale; manifest["profile"] = bundle.profile;
    manifest["member_count"] = compiled.size(); manifest["payload_size"] = payload_size;
    manifest["members"] = std::move(members);
    std::vector<ArchiveItem> archive;
    archive.reserve(compiled.size() + 2U);
    archive.push_back({"bundle.magic", {procedure::co_detect_support_bundle_magic.begin(),
                                         procedure::co_detect_support_bundle_magic.end()}});
    archive.push_back({"manifest.json", text_bytes(manifest.dump())});
    for (std::size_t index = 0; index < compiled.size(); ++index)
        archive.push_back({physical_member_name(index), std::move(compiled[index].bytes)});
    auto result = canonical_store_zip(archive);
    if (result.size() > max_archive_bytes)
        fail(PublicationErrorCode::resource_exhausted,
             "support bundle archive exceeds consumer limit");
    return result;
}

[[nodiscard]] bool link_like(
    const std::filesystem::path&, std::filesystem::file_status) noexcept;

[[nodiscard]] std::vector<std::byte> read_exact_file(
    const std::filesystem::path& path, const std::size_t expected_size)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status) || link_like(path, status))
        fail(PublicationErrorCode::publication_mismatch,
             "publication output is missing or not a regular file: " + native_utf8(path));
    const auto size = std::filesystem::file_size(path, error);
    if (error || size != expected_size)
        fail(PublicationErrorCode::publication_mismatch,
             "publication output size mismatch: " + native_utf8(path));
    std::ifstream input(path, std::ios::binary);
    std::vector<std::byte> bytes(expected_size);
    if (!input || (expected_size != 0 && !input.read(
            reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(expected_size))))
        fail(PublicationErrorCode::output_io_failed, "publication read failed");
    if (input.peek() != std::char_traits<char>::eof())
        fail(PublicationErrorCode::publication_mismatch, "publication has trailing bytes");
    return bytes;
}

[[nodiscard]] bool link_like(const std::filesystem::path& path,
                             const std::filesystem::file_status status) noexcept
{
#ifdef _WIN32
    static_cast<void>(status);
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    static_cast<void>(path);
    return std::filesystem::is_symlink(status);
#endif
}

void reject_undeclared_outputs(
    const std::filesystem::path& publication_root,
    const std::set<std::string, std::less<>>& expected_paths)
{
    std::error_code error;
    if (!std::filesystem::is_directory(publication_root, error) || error)
        fail(PublicationErrorCode::publication_mismatch, "publication root is absent");
    for (std::filesystem::recursive_directory_iterator iterator(
             publication_root, std::filesystem::directory_options::none, error), end;
         !error && iterator != end; iterator.increment(error)) {
        const auto status = iterator->symlink_status(error);
        if (error || link_like(iterator->path(), status))
            fail(PublicationErrorCode::publication_mismatch,
                 "publication contains a symlink or unreadable entry");
        if (std::filesystem::is_regular_file(status)) {
            const auto relative = std::filesystem::relative(iterator->path(), publication_root, error);
            if (error) fail(PublicationErrorCode::publication_mismatch,
                            "publication relative path failed");
            const auto value = relative.generic_string();
            if (!expected_paths.contains(value))
                fail(PublicationErrorCode::publication_mismatch,
                     "publication contains undeclared output: " + value);
        } else if (!std::filesystem::is_directory(status)) {
            fail(PublicationErrorCode::publication_mismatch,
                 "publication contains a non-regular entry");
        }
    }
    if (error) fail(PublicationErrorCode::publication_mismatch,
                    "publication enumeration failed");
}

void reject_symlink_ancestors(
    const std::filesystem::path& root, const std::filesystem::path& relative)
{
    std::error_code error;
    const auto root_status = std::filesystem::symlink_status(root, error);
    if (error || link_like(root, root_status))
        fail(PublicationErrorCode::output_io_failed, "publication root must not be a symlink");
    auto current = root;
    for (const auto& part : relative.parent_path()) {
        current /= part;
        error.clear();
        const auto status = std::filesystem::symlink_status(current, error);
        if (!error && link_like(current, status))
            fail(PublicationErrorCode::output_io_failed, "publication parent must not be a symlink");
    }
}

void validate_outputs(const std::span<const PublicationOutput> outputs)
{
    if (outputs.size() < 2 || outputs.back().relative_path != "baas.resources.json")
        fail(PublicationErrorCode::publication_invalid,
             "publication manifest must be the final commit point");
    std::set<std::string, std::less<>> paths;
    std::set<std::string, std::less<>> folded_paths;
    for (const auto& output : outputs) {
        if (!safe_relative_path(output.relative_path) ||
            !paths.insert(output.relative_path).second ||
            !folded_paths.insert(ascii_lower(output.relative_path)).second)
            fail(PublicationErrorCode::publication_invalid,
                 "publication output path is unsafe or case-colliding");
    }
    const auto& manifest_bytes = outputs.back().bytes;
    const std::string_view manifest_text{
        reinterpret_cast<const char*>(manifest_bytes.data()), manifest_bytes.size()};
    const auto parsed = strict_json::parse_strict_json(
        manifest_text, {.max_depth = 8, .max_nodes = 4'096, .max_string_bytes = max_path_bytes});
    if (!parsed || !exact_fields(*parsed.document, {"schema", "entries"}) ||
        !parsed.document->at("schema").is_string() ||
        parsed.document->at("schema").get<std::string_view>() != "baas.resources/v1" ||
        !parsed.document->at("entries").is_array() ||
        parsed.document->at("entries").size() + 1U != outputs.size())
        fail(PublicationErrorCode::publication_invalid,
             "publication resource manifest is invalid");
    std::set<std::tuple<std::string, std::string>> variants;
    std::optional<std::tuple<std::string, std::string>> previous;
    OrderedJson canonical_entries = OrderedJson::array();
    for (std::size_t index = 0; index + 1U < outputs.size(); ++index) {
        const auto& entry = parsed.document->at("entries").at(index);
        if (!exact_fields(entry, {"id", "path", "media_type", "size", "sha256", "locale"}) ||
            !entry.at("id").is_string() || !entry.at("path").is_string() ||
            !entry.at("media_type").is_string() || !entry.at("size").is_number_unsigned() ||
            !entry.at("sha256").is_string() || !entry.at("locale").is_string())
            fail(PublicationErrorCode::publication_invalid,
                 "publication resource entry is invalid");
        const auto id = entry.at("id").get<std::string>();
        const auto locale = entry.at("locale").get<std::string>();
        const auto path = entry.at("path").get<std::string_view>();
        const auto digest = entry.at("sha256").get<std::string_view>();
        if ((id != "procedure-support/navigation.to-main-page/v1" &&
             id != "procedure-support/group.open/v1") || !valid_profile(locale) ||
            path != outputs[index].relative_path ||
            entry.at("media_type").get<std::string_view>() !=
                procedure::co_detect_support_bundle_media_type ||
            entry.at("size").get<std::uint64_t>() != outputs[index].bytes.size() ||
            !lower_hex(digest, 64) || resources::sha256_hex(outputs[index].bytes) != digest)
            fail(PublicationErrorCode::publication_invalid,
                 "publication resource entry does not bind output bytes");
        const auto key = std::tuple{id, locale};
        if (!variants.insert(key).second || (previous && key <= *previous))
            fail(PublicationErrorCode::publication_invalid,
                 "publication resource entries are duplicated or unsorted");
        previous = key;
        OrderedJson canonical_entry;
        canonical_entry["id"] = id;
        canonical_entry["path"] = outputs[index].relative_path;
        canonical_entry["media_type"] = procedure::co_detect_support_bundle_media_type;
        canonical_entry["size"] = outputs[index].bytes.size();
        canonical_entry["sha256"] = resources::sha256_hex(outputs[index].bytes);
        canonical_entry["locale"] = locale;
        canonical_entries.push_back(std::move(canonical_entry));
    }
    OrderedJson canonical_manifest;
    canonical_manifest["schema"] = "baas.resources/v1";
    canonical_manifest["entries"] = std::move(canonical_entries);
    if (text_bytes(canonical_manifest.dump()) != manifest_bytes)
        fail(PublicationErrorCode::publication_invalid,
             "publication resource manifest is not canonical");
}

void atomic_replace(const std::filesystem::path& destination, const std::span<const std::byte> bytes)
{
    static std::atomic<std::uint64_t> serial{};
#ifndef _WIN32
    struct Descriptor final {
        int value{-1};
        ~Descriptor() { if (value >= 0) static_cast<void>(::close(value)); }
    } directory{::open(destination.parent_path().c_str(),
                       O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    if (directory.value < 0)
        fail(PublicationErrorCode::output_io_failed,
             "publication parent open without symlink following failed");
#endif
    std::filesystem::path temporary;
    bool written{};
    for (std::size_t attempt = 0; attempt < 32 && !written; ++attempt) {
#ifdef _WIN32
        const auto process = static_cast<unsigned long long>(GetCurrentProcessId());
#else
        const auto process = static_cast<unsigned long long>(getpid());
#endif
        const auto suffix = ".tmp-" + std::to_string(process) + "-" +
            std::to_string(serial.fetch_add(1, std::memory_order_relaxed));
        temporary = destination.parent_path() /
            (destination.filename().native() + std::filesystem::path{suffix}.native());
#ifdef _WIN32
        const HANDLE handle = CreateFileW(
            temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS)
                continue;
            fail(PublicationErrorCode::output_io_failed,
                 "exclusive temporary publication create failed");
        }
        bool success = true;
        std::size_t offset{};
        while (offset < bytes.size()) {
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
                bytes.size() - offset, std::numeric_limits<DWORD>::max()));
            DWORD completed{};
            if (!WriteFile(handle, bytes.data() + offset, chunk, &completed, nullptr) ||
                completed != chunk) {
                success = false;
                break;
            }
            offset += completed;
        }
        success = success && FlushFileBuffers(handle) != FALSE;
        success = CloseHandle(handle) != FALSE && success;
        if (!success) {
            std::error_code ignored; std::filesystem::remove(temporary, ignored);
            fail(PublicationErrorCode::output_io_failed,
                 "temporary publication write failed");
        }
#else
        const auto temporary_name = temporary.filename();
        const int descriptor = ::openat(directory.value, temporary_name.c_str(),
                                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                                        0600);
        if (descriptor < 0) {
            if (errno == EEXIST) continue;
            fail(PublicationErrorCode::output_io_failed,
                 "exclusive temporary publication create failed");
        }
        bool success = true;
        std::size_t offset{};
        while (offset < bytes.size()) {
            const auto completed = ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
            if (completed < 0 && errno == EINTR) continue;
            if (completed <= 0) { success = false; break; }
            offset += static_cast<std::size_t>(completed);
        }
        success = success && ::fsync(descriptor) == 0;
        success = ::close(descriptor) == 0 && success;
        if (!success) {
            static_cast<void>(::unlinkat(directory.value, temporary_name.c_str(), 0));
            fail(PublicationErrorCode::output_io_failed,
                 "temporary publication write failed");
        }
#endif
        written = true;
    }
    if (!written)
        fail(PublicationErrorCode::output_io_failed,
             "exclusive temporary publication name exhausted");
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored; std::filesystem::remove(temporary, ignored);
        fail(PublicationErrorCode::output_io_failed, "atomic publication replace failed");
    }
#else
    const auto temporary_name = temporary.filename();
    const auto destination_name = destination.filename();
    if (::renameat(directory.value, temporary_name.c_str(),
                   directory.value, destination_name.c_str()) != 0 ||
        ::fsync(directory.value) != 0) {
        static_cast<void>(::unlinkat(directory.value, temporary_name.c_str(), 0));
        fail(PublicationErrorCode::output_io_failed, "atomic publication replace failed");
    }
#endif
}

}  // namespace

struct GroupPublicationLock::Impl final {
    std::string source_commit;
    std::vector<Bundle> bundles;
};

std::string_view publication_error_name(const PublicationErrorCode code) noexcept
{
    using enum PublicationErrorCode;
    switch (code) {
    case invalid_lock: return "PUB001_INVALID_LOCK";
    case unsupported_version: return "PUB002_UNSUPPORTED_VERSION";
    case invalid_repository: return "PUB003_INVALID_REPOSITORY";
    case commit_mismatch: return "PUB004_COMMIT_MISMATCH";
    case source_not_found: return "PUB005_SOURCE_NOT_FOUND";
    case source_type_mismatch: return "PUB006_SOURCE_TYPE_MISMATCH";
    case source_oid_mismatch: return "PUB007_SOURCE_OID_MISMATCH";
    case source_size_mismatch: return "PUB008_SOURCE_SIZE_MISMATCH";
    case source_digest_mismatch: return "PUB009_SOURCE_DIGEST_MISMATCH";
    case source_content_invalid: return "PUB010_SOURCE_CONTENT_INVALID";
    case incomplete_bundle: return "PUB011_INCOMPLETE_BUNDLE";
    case alias_forbidden: return "PUB012_ALIAS_FORBIDDEN";
    case locale_fallback_forbidden: return "PUB013_LOCALE_FALLBACK_FORBIDDEN";
    case placeholder_forbidden: return "PUB014_PLACEHOLDER_FORBIDDEN";
    case publication_invalid: return "PUB015_PUBLICATION_INVALID";
    case publication_mismatch: return "PUB016_PUBLICATION_MISMATCH";
    case output_io_failed: return "PUB017_OUTPUT_IO_FAILED";
    case resource_exhausted: return "PUB018_RESOURCE_EXHAUSTED";
    case internal_failure: return "PUB019_INTERNAL_FAILURE";
    }
    return "PUB999_UNKNOWN";
}

PublicationError::PublicationError(const PublicationErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}
PublicationErrorCode PublicationError::code() const noexcept { return code_; }

GroupPublicationLock::GroupPublicationLock(Impl* impl) noexcept : impl_(impl) {}
GroupPublicationLock::GroupPublicationLock(GroupPublicationLock&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr)) {}
GroupPublicationLock& GroupPublicationLock::operator=(GroupPublicationLock&& other) noexcept
{
    if (this != &other) { delete impl_; impl_ = std::exchange(other.impl_, nullptr); }
    return *this;
}
GroupPublicationLock::~GroupPublicationLock() { delete impl_; }
const std::string& GroupPublicationLock::source_commit() const noexcept
{
    static const std::string empty;
    if (impl_ == nullptr) {
        return empty;
    }
    return impl_->source_commit;
}
std::size_t GroupPublicationLock::bundle_count() const noexcept
{
    return impl_ == nullptr ? 0U : impl_->bundles.size();
}

void validate_group_production_lock(const GroupPublicationLock& lock)
{
    if (lock.impl_ == nullptr ||
        lock.impl_->source_commit != group_publication_python_baseline ||
        lock.impl_->bundles.size() != production_bundles.size())
        fail(PublicationErrorCode::incomplete_bundle,
             "production lock must bind the frozen commit and all ten variants");
    for (std::size_t index = 0; index < production_bundles.size(); ++index) {
        const auto& expected = production_bundles[index];
        const auto& bundle = lock.impl_->bundles[index];
        if (bundle.bundle_id != expected.bundle_id || bundle.profile != expected.profile ||
            bundle.locale != expected.profile || bundle.output_path != expected.output_path ||
            bundle.members.size() != expected.member_count)
            fail(PublicationErrorCode::incomplete_bundle,
                 "production bundle identity, path, or reviewed count is incomplete");
        const auto rgb_count = static_cast<std::size_t>(std::ranges::count_if(
            bundle.members, [](const Member& member) { return member.kind == MemberKind::rgb; }));
        const auto expected_rgb = bundle.bundle_id ==
                "procedure-support/navigation.to-main-page/v1" ? 7U : 6U;
        std::string identities;
        for (const auto& member : bundle.members) {
            identities.append(member.id);
            identities.push_back('\n');
        }
        const auto identity_bytes = std::as_bytes(
            std::span{identities.data(), identities.size()});
        if (rgb_count != expected_rgb ||
            resources::sha256_hex(identity_bytes) != expected.identity_sha256 ||
            !std::ranges::all_of(bundle.members, [&](const Member& member) {
                return member.kind != MemberKind::png ||
                    production_image_allowed(bundle.bundle_id, member.id);
            }))
            fail(PublicationErrorCode::incomplete_bundle,
                 "production member identities do not match the frozen procedure closure");
    }
}

GroupPublicationLock parse_group_publication_lock(const std::string_view text)
{
    try {
        if (text.empty() || text.size() > max_lock_bytes)
            fail(PublicationErrorCode::invalid_lock, "publication lock size is invalid");
        const auto parsed = strict_json::parse_strict_json(
            text, {.max_depth = 16, .max_nodes = 262'144, .max_string_bytes = max_path_bytes});
        if (!parsed)
            fail(PublicationErrorCode::invalid_lock,
                 "publication lock is not strict JSON: " +
                     std::string{strict_json::strict_json_error_name(parsed.error)});
        const auto& root = *parsed.document;
        if (!exact_fields(root, {"schema", "compiler", "source", "bundles"}) ||
            !root.at("schema").is_string() ||
            root.at("schema").get<std::string_view>() != group_publication_lock_schema ||
            !exact_fields(root.at("compiler"), {"schema", "version"}) ||
            !root.at("compiler").at("schema").is_string() ||
            root.at("compiler").at("schema").get<std::string_view>() !=
                group_publication_compiler_schema ||
            !root.at("compiler").at("version").is_number_unsigned())
            fail(PublicationErrorCode::invalid_lock, "publication lock root is invalid");
        if (root.at("compiler").at("version").get<std::uint64_t>() !=
            group_publication_compiler_version)
            fail(PublicationErrorCode::unsupported_version, "publisher version is unsupported");
        if (!exact_fields(root.at("source"), {"commit"}) ||
            !root.at("source").at("commit").is_string() || !root.at("bundles").is_array())
            fail(PublicationErrorCode::invalid_lock, "publication source is invalid");
        auto impl = std::make_unique<GroupPublicationLock::Impl>();
        impl->source_commit = root.at("source").at("commit").get<std::string>();
        if (!lower_hex(impl->source_commit, 40) || root.at("bundles").empty() ||
            root.at("bundles").size() > max_bundles)
            fail(PublicationErrorCode::invalid_lock, "commit or bundle count is invalid");
        std::set<std::tuple<std::string, std::string>> bundle_keys;
        std::set<std::string, std::less<>> outputs;
        std::optional<std::tuple<std::string, std::string>> previous_bundle;
        for (const auto& value : root.at("bundles")) {
            if (!exact_fields(value, {"bundle_id", "locale", "profile", "output_path",
                                      "member_count", "members"}) ||
                !value.at("bundle_id").is_string() || !value.at("locale").is_string() ||
                !value.at("profile").is_string() || !value.at("output_path").is_string() ||
                !value.at("member_count").is_number_unsigned() || !value.at("members").is_array())
                fail(PublicationErrorCode::invalid_lock, "bundle fields are invalid");
            Bundle bundle;
            bundle.bundle_id = value.at("bundle_id").get<std::string>();
            bundle.locale = value.at("locale").get<std::string>();
            bundle.profile = value.at("profile").get<std::string>();
            bundle.output_path = value.at("output_path").get<std::string>();
            static_cast<void>(expected_graph_id(bundle.bundle_id));
            if (!valid_profile(bundle.profile) || bundle.locale != bundle.profile)
                fail(PublicationErrorCode::locale_fallback_forbidden,
                     "bundle locale must exactly equal its reviewed profile");
            if (!safe_relative_path(bundle.output_path) ||
                !bundle.output_path.starts_with("payload/") ||
                !bundle.output_path.ends_with(".bundle") ||
                !outputs.insert(bundle.output_path).second)
                fail(PublicationErrorCode::invalid_lock, "bundle output path is invalid");
            const auto declared_count = value.at("member_count").get<std::uint64_t>();
            if (value.at("members").size() < 2 || value.at("members").size() > max_members ||
                declared_count != value.at("members").size())
                fail(PublicationErrorCode::incomplete_bundle, "bundle member closure is incomplete");
            for (std::size_t index = 0; index < value.at("members").size(); ++index)
                bundle.members.push_back(parse_member(
                    value.at("members").at(index), bundle.bundle_id, bundle.profile, index));
            std::set<std::string, std::less<>> ids;
            std::set<std::string, std::less<>> features;
            std::optional<MemberKind> previous_kind;
            std::string previous_id;
            bool has_rgb{};
            bool has_png{};
            for (const auto& member : bundle.members) {
                if (!ids.insert(member.id).second ||
                    (!member.feature.empty() && !features.insert(member.feature).second))
                    fail(PublicationErrorCode::incomplete_bundle,
                         "bundle has duplicate member or feature identity");
                if (previous_kind) {
                    if (member.kind < *previous_kind ||
                        (member.kind == *previous_kind && member.id <= previous_id))
                        fail(PublicationErrorCode::invalid_lock,
                             "members must be graph, sorted RGB, then sorted PNG");
                }
                has_rgb = has_rgb || member.kind == MemberKind::rgb;
                has_png = has_png || member.kind == MemberKind::png;
                previous_kind = member.kind; previous_id = member.id;
            }
            if (!has_rgb || !has_png)
                fail(PublicationErrorCode::incomplete_bundle,
                     "group support bundle requires real RGB and PNG members");
            const auto key = std::tuple{bundle.bundle_id, bundle.profile};
            if (!bundle_keys.insert(key).second || (previous_bundle && key <= *previous_bundle))
                fail(PublicationErrorCode::invalid_lock,
                     "bundles must be unique and bytewise sorted by id/profile");
            previous_bundle = key;
            impl->bundles.push_back(std::move(bundle));
        }
        return GroupPublicationLock{impl.release()};
    } catch (const PublicationError&) {
        throw;
    } catch (const std::bad_alloc&) {
        fail(PublicationErrorCode::resource_exhausted, "publication lock allocation failed");
    } catch (...) {
        fail(PublicationErrorCode::invalid_lock, "publication lock parse failed");
    }
}

void verify_group_publication_sources(
    const GroupPublicationLock& lock, const std::filesystem::path& repository_path)
{
    static_cast<void>(compile_group_publication(lock, repository_path));
}

std::vector<PublicationOutput> compile_group_publication(
    const GroupPublicationLock& lock, const std::filesystem::path& repository_path)
{
    try {
        if (!lock.impl_) fail(PublicationErrorCode::invalid_lock, "publication lock is empty");
        PinnedRepository repository{repository_path, lock.impl_->source_commit};
        PublicationBudget budget;
        std::vector<PublicationOutput> outputs;
        outputs.reserve(lock.impl_->bundles.size() + 1U);
        OrderedJson entries = OrderedJson::array();
        for (const auto& bundle : lock.impl_->bundles) {
            auto archive = compile_bundle(bundle, repository, budget);
            budget.output(archive.size());
            OrderedJson entry;
            entry["id"] = bundle.bundle_id; entry["path"] = bundle.output_path;
            entry["media_type"] = procedure::co_detect_support_bundle_media_type;
            entry["size"] = archive.size(); entry["sha256"] = resources::sha256_hex(archive);
            entry["locale"] = bundle.locale;
            entries.push_back(std::move(entry));
            outputs.push_back({bundle.output_path, std::move(archive)});
        }
        OrderedJson manifest;
        manifest["schema"] = "baas.resources/v1";
        manifest["entries"] = std::move(entries);
        auto manifest_bytes = text_bytes(manifest.dump());
        budget.output(manifest_bytes.size());
        outputs.push_back({"baas.resources.json", std::move(manifest_bytes)});
        return outputs;
    } catch (const PublicationError&) {
        throw;
    } catch (const std::bad_alloc&) {
        fail(PublicationErrorCode::resource_exhausted, "publication compilation allocation failed");
    } catch (...) {
        fail(PublicationErrorCode::internal_failure, "publication compilation failed");
    }
}

void verify_group_publication(
    const GroupPublicationLock& lock, const std::filesystem::path& repository_path,
    const std::filesystem::path& publication_root)
{
    const auto expected = compile_group_publication(lock, repository_path);
    std::set<std::string, std::less<>> expected_paths;
    for (const auto& output : expected) {
        expected_paths.insert(output.relative_path);
        const auto actual = read_exact_file(publication_root / output.relative_path,
                                            output.bytes.size());
        if (actual != output.bytes)
            fail(PublicationErrorCode::publication_mismatch,
                 "publication bytes are not canonical: " + output.relative_path);
    }
    reject_undeclared_outputs(publication_root, expected_paths);
}

void write_group_publication(
    const std::span<const PublicationOutput> outputs,
    const std::filesystem::path& publication_root, const bool check_only)
{
    validate_outputs(outputs);
    if (check_only) {
        std::set<std::string, std::less<>> expected;
        for (const auto& output : outputs) {
            expected.insert(output.relative_path);
            if (read_exact_file(publication_root / output.relative_path, output.bytes.size()) !=
                output.bytes)
                fail(PublicationErrorCode::publication_mismatch,
                     "--check output differs: " + output.relative_path);
        }
        reject_undeclared_outputs(publication_root, expected);
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(publication_root, error);
    if (error) fail(PublicationErrorCode::output_io_failed,
                    "publication root creation failed");
    std::set<std::string, std::less<>> expected;
    for (const auto& output : outputs) expected.insert(output.relative_path);
    reject_undeclared_outputs(publication_root, expected);
    for (const auto& output : outputs) {
        if (!safe_relative_path(output.relative_path))
            fail(PublicationErrorCode::publication_invalid, "unsafe output path");
        const auto relative = std::filesystem::path{output.relative_path};
        reject_symlink_ancestors(publication_root, relative);
        const auto destination = publication_root / relative;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) fail(PublicationErrorCode::output_io_failed,
                        "publication parent creation failed");
        atomic_replace(destination, output.bytes);
    }
    reject_undeclared_outputs(publication_root, expected);
}

}  // namespace baas::runtime::publisher
