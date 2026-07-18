#include "runtime/procedure/CoDetectPythonCompatDefinition.h"

#include "resources/ResourceSnapshot.h"
#include "runtime/json/StrictJson.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>
#include <optional>
#include <set>
#include <utility>

namespace baas::runtime::procedure {
namespace {

using Json = nlohmann::json;
namespace strict_json = ::baas::runtime::json;

struct LoadFailure final {
    CoDetectDefinitionError error;
    std::string field;
};

[[noreturn]] void fail(const CoDetectDefinitionError error, std::string field = {}) {
    throw LoadFailure{error, std::move(field)};
}

class Budget final {
public:
    explicit Budget(const CoDetectDefinitionLimits& limits) noexcept
        : work_(limits.max_work), strings_(limits.max_total_string_bytes),
          items_(limits.max_total_items), per_array_(limits.max_items_per_array) {}

    void array(const std::size_t size, const std::string_view field) {
        if (size > per_array_) fail(CoDetectDefinitionError::array_limit_exceeded,
                                    std::string{field});
        charge(items_, size, CoDetectDefinitionError::array_limit_exceeded, field);
        work(size + 1, field);
    }

    void string(const std::string_view value, const std::string_view field) {
        charge(strings_, value.size(), CoDetectDefinitionError::string_limit_exceeded,
               field);
        work(value.size() + 1, field);
    }

    void work(const std::size_t amount, const std::string_view field) {
        charge(work_, amount, CoDetectDefinitionError::work_limit_exceeded, field);
    }

private:
    static void charge(std::size_t& remaining, const std::size_t amount,
                       const CoDetectDefinitionError error,
                       const std::string_view field) {
        if (amount > remaining) fail(error, std::string{field});
        remaining -= amount;
    }

    std::size_t work_;
    std::size_t strings_;
    std::size_t items_;
    std::size_t per_array_;
};

[[nodiscard]] bool valid_limits(const CoDetectDefinitionLimits& value) noexcept {
    return value.max_definition_bytes != 0 && value.max_json_depth != 0 &&
           value.max_json_nodes != 0 && value.max_string_bytes != 0 &&
           value.max_total_string_bytes != 0 && value.max_items_per_array != 0 &&
           value.max_total_items != 0 && value.max_work != 0 &&
           value.max_canonical_identity_bytes != 0;
}

[[nodiscard]] bool exact_fields(
    const Json& object, const std::initializer_list<std::string_view> required) {
    if (!object.is_object() || object.size() != required.size()) return false;
    return std::ranges::all_of(required, [&](const std::string_view field) {
        return object.contains(field);
    });
}

[[nodiscard]] bool logical_feature_id(const std::string_view value) noexcept {
    if (value.empty()) return false;
    return std::ranges::none_of(value, [](const unsigned char byte) {
        return byte <= 0x1fU || byte == 0x7fU;
    });
}

[[nodiscard]] std::optional<std::uint64_t> unsigned_integer(const Json& value) {
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    if (!value.is_number_integer()) return std::nullopt;
    const auto signed_value = value.get<std::int64_t>();
    if (signed_value < 0) return std::nullopt;
    return static_cast<std::uint64_t>(signed_value);
}

[[nodiscard]] std::uint64_t integer(const Json& value, const bool positive,
                                    const CoDetectDefinitionError error,
                                    const std::string_view field) {
    const auto result = unsigned_integer(value);
    if (!result || (positive && *result == 0)) fail(error, std::string{field});
    return *result;
}

[[nodiscard]] std::optional<std::int64_t> signed_integer(const Json& value) {
    if (value.is_number_unsigned()) {
        const auto parsed = value.get<std::uint64_t>();
        if (parsed > static_cast<std::uint64_t>(
                         std::numeric_limits<std::int32_t>::max()))
            return std::nullopt;
        return static_cast<std::int64_t>(parsed);
    }
    if (!value.is_number_integer()) return std::nullopt;
    return value.get<std::int64_t>();
}

[[nodiscard]] CoDetectClick click(const Json& value, const std::string_view field,
                                  const bool allow_match_only) {
    if (!value.is_array() || value.size() != 2)
        fail(CoDetectDefinitionError::invalid_click, std::string{field});
    const auto x = signed_integer(value[0]);
    const auto y = signed_integer(value[1]);
    if (!x || !y || *x < std::numeric_limits<std::int32_t>::min() ||
        *y < std::numeric_limits<std::int32_t>::min() ||
        (!allow_match_only && (*x < 0 || *y < 0)) ||
        (*x >= 0 && *x > 1280) || (*y >= 0 && *y > 720))
        fail(CoDetectDefinitionError::invalid_click, std::string{field});
    return {static_cast<std::int32_t>(*x), static_cast<std::int32_t>(*y)};
}

[[nodiscard]] CoDetectImageMatch image_match(const Json& value,
                                              const std::string_view field) {
    CoDetectImageMatch result;
    if (value.contains("threshold")) {
        if (!value["threshold"].is_number())
            fail(CoDetectDefinitionError::invalid_image_match, std::string{field});
        auto threshold = value["threshold"].get<double>();
        if (!std::isfinite(threshold) || threshold < 0.0 || threshold > 1.0)
            fail(CoDetectDefinitionError::invalid_image_match, std::string{field});
        if (threshold == 0.0) threshold = 0.0;
        result.threshold = threshold;
    }
    if (value.contains("rgb_diff")) {
        if (!result.threshold)
            fail(CoDetectDefinitionError::invalid_image_match, std::string{field});
        const auto rgb_diff = unsigned_integer(value["rgb_diff"]);
        if (!rgb_diff || *rgb_diff > 255)
            fail(CoDetectDefinitionError::invalid_image_match, std::string{field});
        result.rgb_diff = static_cast<std::uint16_t>(*rgb_diff);
    }
    return result;
}

[[nodiscard]] CoDetectProfile profile(const std::string_view value,
                                      const std::string_view field) {
    if (value == "CN") return CoDetectProfile::cn;
    if (value == "JP") return CoDetectProfile::jp;
    if (value == "Global_en-us") return CoDetectProfile::global_en_us;
    if (value == "Global_zh-tw") return CoDetectProfile::global_zh_tw;
    if (value == "Global_ko-kr") return CoDetectProfile::global_ko_kr;
    fail(CoDetectDefinitionError::invalid_profile, std::string{field});
}

[[nodiscard]] std::string feature(const Json& value, Budget& budget,
                                  const std::string_view field) {
    if (!value.is_string())
        fail(CoDetectDefinitionError::invalid_feature, std::string{field});
    const auto& text = value.get_ref<const std::string&>();
    budget.string(text, field);
    if (!logical_feature_id(text))
        fail(CoDetectDefinitionError::invalid_feature, std::string{field});
    return text;
}

[[nodiscard]] std::vector<std::string> feature_array(
    const Json& value, Budget& budget, const std::string_view field,
    const bool require_nonempty) {
    if (!value.is_array() || (require_nonempty && value.empty()))
        fail(CoDetectDefinitionError::invalid_feature, std::string{field});
    budget.array(value.size(), field);
    std::vector<std::string> result;
    result.reserve(value.size());
    std::set<std::string, std::less<>> seen;
    for (const auto& item : value) {
        auto parsed = feature(item, budget, field);
        if (!seen.insert(parsed).second)
            fail(CoDetectDefinitionError::duplicate_feature, std::string{field});
        result.push_back(std::move(parsed));
    }
    return result;
}

[[nodiscard]] CoDetectImageFeature image_feature(const Json& value, Budget& budget,
                                                 const std::string_view field) {
    if (value.is_string()) return {feature(value, budget, field), {}};
    if (!value.is_object() || !value.contains("feature"))
        fail(CoDetectDefinitionError::invalid_image_match, std::string{field});
    const bool has_threshold = value.contains("threshold");
    const bool has_rgb_diff = value.contains("rgb_diff");
    const auto expected_size = 1U + static_cast<unsigned>(has_threshold) +
                               static_cast<unsigned>(has_rgb_diff);
    if (value.size() != expected_size || (has_rgb_diff && !has_threshold))
        fail(CoDetectDefinitionError::invalid_image_match, std::string{field});
    return {feature(value["feature"], budget, field), image_match(value, field)};
}

[[nodiscard]] std::vector<CoDetectImageFeature> image_feature_array(
    const Json& value, Budget& budget, const std::string_view field) {
    if (!value.is_array())
        fail(CoDetectDefinitionError::invalid_image_match, std::string{field});
    budget.array(value.size(), field);
    std::vector<CoDetectImageFeature> result;
    result.reserve(value.size());
    std::set<std::string, std::less<>> seen;
    for (const auto& item : value) {
        auto parsed = image_feature(item, budget, field);
        if (!seen.insert(parsed.feature).second)
            fail(CoDetectDefinitionError::duplicate_feature, std::string{field});
        result.push_back(std::move(parsed));
    }
    return result;
}

[[nodiscard]] CoDetectReaction reaction(const Json& value, Budget& budget,
                                        const std::string_view field,
                                        const bool profiled, const bool image,
                                        const bool allow_image_override) {
    const bool has_threshold = value.is_object() && value.contains("threshold");
    const bool has_rgb_diff = value.is_object() && value.contains("rgb_diff");
    const auto base_size = profiled ? 3U : 2U;
    const bool closed = value.is_object() && value.contains("feature") &&
        value.contains("click") && (!profiled || value.contains("profiles")) &&
        value.size() == base_size + static_cast<unsigned>(has_threshold) +
                            static_cast<unsigned>(has_rgb_diff) &&
        (allow_image_override || (!has_threshold && !has_rgb_diff)) &&
        (!has_rgb_diff || has_threshold);
    if (!closed) fail(CoDetectDefinitionError::invalid_reaction, std::string{field});

    CoDetectReaction result;
    result.feature = feature(value["feature"], budget, field);
    result.click = click(value["click"], field, true);
    if (image) result.image_match = image_match(value, field);
    if (!profiled) return result;

    const auto& profiles = value["profiles"];
    if (!profiles.is_array() || profiles.empty())
        fail(CoDetectDefinitionError::invalid_profile, std::string{field});
    budget.array(profiles.size(), field);
    std::set<CoDetectProfile> seen;
    result.profiles.reserve(profiles.size());
    for (const auto& item : profiles) {
        if (!item.is_string())
            fail(CoDetectDefinitionError::invalid_profile, std::string{field});
        const auto& name = item.get_ref<const std::string&>();
        budget.string(name, field);
        const auto parsed = profile(name, field);
        if (!seen.insert(parsed).second)
            fail(CoDetectDefinitionError::duplicate_profile, std::string{field});
        result.profiles.push_back(parsed);
    }
    return result;
}

[[nodiscard]] std::vector<CoDetectReaction> reaction_array(
    const Json& value, Budget& budget, const std::string_view field,
    const bool profiled, const bool image, const bool allow_image_override) {
    if (!value.is_array())
        fail(CoDetectDefinitionError::invalid_reaction, std::string{field});
    budget.array(value.size(), field);
    std::vector<CoDetectReaction> result;
    result.reserve(value.size());
    for (const auto& item : value)
        result.push_back(reaction(item, budget, field, profiled, image,
                                  allow_image_override));
    return result;
}

struct Parsed final {
    std::vector<std::string> ends_rgb;
    std::vector<CoDetectImageFeature> ends_image;
    std::vector<CoDetectReaction> reactions_rgb;
    std::vector<CoDetectReaction> reactions_rgb_profiled;
    std::vector<CoDetectReaction> reactions_image;
    std::vector<CoDetectReaction> reactions_image_profiled;
    std::vector<CoDetectReaction> popups_rgb;
    std::vector<CoDetectReaction> popups_profiled_image;
    std::vector<std::string> loading_all_rgb;
    CoDetectForegroundCheck foreground_check;
    CoDetectLoop loop;
};

[[nodiscard]] Parsed parse_payload(const Json& payload, Budget& budget) {
    if (!exact_fields(payload, {"profile_source", "ends", "reactions", "popups",
                                "loading", "foreground_check", "loop"}))
        fail(CoDetectDefinitionError::invalid_payload, "payload");
    if (!payload["profile_source"].is_string() ||
        payload["profile_source"].get_ref<const std::string&>() !=
            co_detect_profile_source)
        fail(CoDetectDefinitionError::invalid_profile_source,
             "payload.profile_source");
    budget.string(payload["profile_source"].get_ref<const std::string&>(),
                  "payload.profile_source");

    const auto& ends = payload["ends"];
    if (!exact_fields(ends, {"rgb", "image"}))
        fail(CoDetectDefinitionError::invalid_payload, "payload.ends");
    const auto& reactions = payload["reactions"];
    if (!exact_fields(reactions, {"rgb", "rgb_profiled", "image", "image_profiled"}))
        fail(CoDetectDefinitionError::invalid_payload, "payload.reactions");
    const auto& popups = payload["popups"];
    if (!exact_fields(popups, {"rgb", "profiled_image"}))
        fail(CoDetectDefinitionError::invalid_payload, "payload.popups");
    const auto& loading = payload["loading"];
    if (!exact_fields(loading, {"all_rgb"}))
        fail(CoDetectDefinitionError::invalid_payload, "payload.loading");

    Parsed result;
    result.ends_rgb = feature_array(ends["rgb"], budget, "payload.ends.rgb", false);
    result.ends_image = image_feature_array(ends["image"], budget,
                                            "payload.ends.image");
    result.reactions_rgb = reaction_array(reactions["rgb"], budget,
                                          "payload.reactions.rgb", false, false,
                                          false);
    result.reactions_rgb_profiled = reaction_array(
        reactions["rgb_profiled"], budget, "payload.reactions.rgb_profiled", true,
        false, false);
    result.reactions_image = reaction_array(reactions["image"], budget,
                                            "payload.reactions.image", false, true,
                                            true);
    result.reactions_image_profiled = reaction_array(
        reactions["image_profiled"], budget,
        "payload.reactions.image_profiled", true, true, true);
    result.popups_rgb = reaction_array(popups["rgb"], budget,
                                       "payload.popups.rgb", false, false, false);
    result.popups_profiled_image = reaction_array(
        popups["profiled_image"], budget, "payload.popups.profiled_image", true,
        true, false);
    result.loading_all_rgb = feature_array(loading["all_rgb"], budget,
                                           "payload.loading.all_rgb", true);

    const auto& foreground = payload["foreground_check"];
    if (!exact_fields(foreground, {"android_only", "interval_ms", "idle_feature_ms"}) ||
        !foreground["android_only"].is_boolean() ||
        !foreground["android_only"].get<bool>())
        fail(CoDetectDefinitionError::invalid_foreground_check,
             "payload.foreground_check");
    result.foreground_check = {
        true,
        integer(foreground["interval_ms"], true,
                CoDetectDefinitionError::invalid_foreground_check,
                "payload.foreground_check.interval_ms"),
        integer(foreground["idle_feature_ms"], true,
                CoDetectDefinitionError::invalid_foreground_check,
                "payload.foreground_check.idle_feature_ms")};

    const auto& loop = payload["loop"];
    if (!exact_fields(loop, {"skip_first_screenshot", "timeout_ms",
                             "duplicate_click_window_ms", "tentative"}) ||
        !loop["skip_first_screenshot"].is_boolean())
        fail(CoDetectDefinitionError::invalid_loop, "payload.loop");
    result.loop.skip_first_screenshot = loop["skip_first_screenshot"].get<bool>();
    result.loop.timeout_ms = integer(loop["timeout_ms"], true,
                                     CoDetectDefinitionError::invalid_loop,
                                     "payload.loop.timeout_ms");
    result.loop.duplicate_click_window_ms = integer(
        loop["duplicate_click_window_ms"], false,
        CoDetectDefinitionError::invalid_loop,
        "payload.loop.duplicate_click_window_ms");

    const auto& tentative = loop["tentative"];
    if (!tentative.is_object() || !tentative.contains("enabled") ||
        !tentative["enabled"].is_boolean())
        fail(CoDetectDefinitionError::invalid_tentative,
             "payload.loop.tentative");
    result.loop.tentative.enabled = tentative["enabled"].get<bool>();
    if (!result.loop.tentative.enabled) {
        if (!exact_fields(tentative, {"enabled"}))
            fail(CoDetectDefinitionError::invalid_tentative,
                 "payload.loop.tentative");
    } else {
        if (!exact_fields(tentative, {"enabled", "after_failed_cycles",
                                      "repeat_each_failed_cycle", "click",
                                      "post_wait_screenshot_intervals"}) ||
            !tentative["repeat_each_failed_cycle"].is_boolean() ||
            !tentative["repeat_each_failed_cycle"].get<bool>())
            fail(CoDetectDefinitionError::invalid_tentative,
                 "payload.loop.tentative");
        result.loop.tentative.after_failed_cycles = integer(
            tentative["after_failed_cycles"], true,
            CoDetectDefinitionError::invalid_tentative,
            "payload.loop.tentative.after_failed_cycles");
        result.loop.tentative.repeat_each_failed_cycle = true;
        result.loop.tentative.click = click(tentative["click"],
                                            "payload.loop.tentative.click", false);
        result.loop.tentative.post_wait_screenshot_intervals = integer(
            tentative["post_wait_screenshot_intervals"], true,
            CoDetectDefinitionError::invalid_tentative,
            "payload.loop.tentative.post_wait_screenshot_intervals");
    }
    return result;
}

class CanonicalBuilder final {
public:
    explicit CanonicalBuilder(const std::size_t limit) : limit_(limit) {}

    void boolean(const bool value) { byte(value ? 1U : 0U); }
    void number(const std::uint64_t value) {
        reserve(8);
        for (unsigned shift = 0; shift != 64; shift += 8)
            bytes_.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
    void text(const std::string_view value) {
        number(value.size());
        reserve(value.size());
        const auto source = std::as_bytes(std::span{value.data(), value.size()});
        bytes_.insert(bytes_.end(), source.begin(), source.end());
    }
    void signed_number(const std::int32_t value) {
        number(std::bit_cast<std::uint32_t>(value));
    }
    void click(const CoDetectClick value) {
        signed_number(value.x);
        signed_number(value.y);
    }

    [[nodiscard]] std::vector<std::byte> finish() && { return std::move(bytes_); }

private:
    void byte(const unsigned value) {
        reserve(1);
        bytes_.push_back(static_cast<std::byte>(value));
    }
    void reserve(const std::size_t amount) {
        if (amount > limit_ - bytes_.size())
            fail(CoDetectDefinitionError::canonical_identity_limit_exceeded,
                 "canonical_identity");
    }
    std::size_t limit_;
    std::vector<std::byte> bytes_;
};

void append_features(CanonicalBuilder& out, const std::vector<std::string>& values) {
    out.number(values.size());
    for (const auto& value : values) out.text(value);
}

void append_image_match(CanonicalBuilder& out, const CoDetectImageMatch& value) {
    out.number(std::bit_cast<std::uint64_t>(value.effective_threshold()));
    out.number(value.effective_rgb_diff());
}

void append_image_features(CanonicalBuilder& out,
                           const std::vector<CoDetectImageFeature>& values) {
    out.number(values.size());
    for (const auto& value : values) {
        out.text(value.feature);
        append_image_match(out, value.match);
    }
}

void append_reactions(CanonicalBuilder& out,
                      const std::vector<CoDetectReaction>& values,
                      const bool image) {
    out.number(values.size());
    for (const auto& value : values) {
        out.number(value.profiles.size());
        for (const auto item : value.profiles)
            out.text(co_detect_profile_name(item));
        out.text(value.feature);
        out.click(value.click);
        if (image) append_image_match(out, value.image_match);
    }
}

[[nodiscard]] std::vector<std::byte> canonical_identity(
    const Parsed& value, const std::size_t limit) {
    CanonicalBuilder out{limit};
    out.text("BAAS co_detect definition identity v2");
    out.text(co_detect_definition_schema);
    out.text(co_detect_python_compat_engine);
    out.text(co_detect_profile_source);
    append_features(out, value.ends_rgb);
    append_image_features(out, value.ends_image);
    append_reactions(out, value.reactions_rgb, false);
    append_reactions(out, value.reactions_rgb_profiled, false);
    append_reactions(out, value.reactions_image, true);
    append_reactions(out, value.reactions_image_profiled, true);
    append_reactions(out, value.popups_rgb, false);
    append_reactions(out, value.popups_profiled_image, true);
    append_features(out, value.loading_all_rgb);
    out.boolean(value.foreground_check.android_only);
    out.number(value.foreground_check.interval_ms);
    out.number(value.foreground_check.idle_feature_ms);
    out.boolean(value.loop.skip_first_screenshot);
    out.number(value.loop.timeout_ms);
    out.number(value.loop.duplicate_click_window_ms);
    out.boolean(value.loop.tentative.enabled);
    if (value.loop.tentative.enabled) {
        out.number(value.loop.tentative.after_failed_cycles);
        out.boolean(value.loop.tentative.repeat_each_failed_cycle);
        out.click(value.loop.tentative.click);
        out.number(value.loop.tentative.post_wait_screenshot_intervals);
    }
    return std::move(out).finish();
}

[[nodiscard]] CoDetectDefinitionError map_json_error(
    const strict_json::StrictJsonError error) noexcept {
    using enum strict_json::StrictJsonError;
    switch (error) {
    case invalid_limits: return CoDetectDefinitionError::invalid_limits;
    case invalid_utf8: return CoDetectDefinitionError::invalid_utf8;
    case invalid_syntax: return CoDetectDefinitionError::invalid_json;
    case duplicate_key: return CoDetectDefinitionError::duplicate_json_key;
    case depth_limit_exceeded:
        return CoDetectDefinitionError::json_depth_limit_exceeded;
    case node_limit_exceeded:
        return CoDetectDefinitionError::json_node_limit_exceeded;
    case string_limit_exceeded:
        return CoDetectDefinitionError::string_limit_exceeded;
    case resource_exhausted: return CoDetectDefinitionError::resource_exhausted;
    case internal_failure: return CoDetectDefinitionError::internal_failure;
    case none: return CoDetectDefinitionError::none;
    }
    return CoDetectDefinitionError::internal_failure;
}

}  // namespace

struct CoDetectPythonCompatDefinition::Impl final {
    std::shared_ptr<const std::vector<std::byte>> source;
    std::string source_sha256;
    std::shared_ptr<const std::vector<std::byte>> canonical;
    std::string canonical_sha256;
    Parsed parsed;
};

CoDetectPythonCompatDefinition::CoDetectPythonCompatDefinition(
    std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
CoDetectPythonCompatDefinition::~CoDetectPythonCompatDefinition() = default;

#define BAAS_CO_DETECT_GETTER(name, field, type) \
    const type& CoDetectPythonCompatDefinition::name() const noexcept { \
        return impl_->parsed.field; \
    }
BAAS_CO_DETECT_GETTER(ends_rgb, ends_rgb, std::vector<std::string>)
BAAS_CO_DETECT_GETTER(ends_image, ends_image, std::vector<CoDetectImageFeature>)
BAAS_CO_DETECT_GETTER(reactions_rgb, reactions_rgb, std::vector<CoDetectReaction>)
BAAS_CO_DETECT_GETTER(reactions_rgb_profiled, reactions_rgb_profiled, std::vector<CoDetectReaction>)
BAAS_CO_DETECT_GETTER(reactions_image, reactions_image, std::vector<CoDetectReaction>)
BAAS_CO_DETECT_GETTER(reactions_image_profiled, reactions_image_profiled, std::vector<CoDetectReaction>)
BAAS_CO_DETECT_GETTER(popups_rgb, popups_rgb, std::vector<CoDetectReaction>)
BAAS_CO_DETECT_GETTER(popups_profiled_image, popups_profiled_image, std::vector<CoDetectReaction>)
BAAS_CO_DETECT_GETTER(loading_all_rgb, loading_all_rgb, std::vector<std::string>)
BAAS_CO_DETECT_GETTER(foreground_check, foreground_check, CoDetectForegroundCheck)
BAAS_CO_DETECT_GETTER(loop, loop, CoDetectLoop)
#undef BAAS_CO_DETECT_GETTER

std::span<const std::byte> CoDetectPythonCompatDefinition::source_bytes() const noexcept {
    return *impl_->source;
}
const std::string& CoDetectPythonCompatDefinition::source_sha256() const noexcept {
    return impl_->source_sha256;
}
std::span<const std::byte>
CoDetectPythonCompatDefinition::canonical_identity_material() const noexcept {
    return *impl_->canonical;
}
const std::string& CoDetectPythonCompatDefinition::canonical_sha256() const noexcept {
    return impl_->canonical_sha256;
}

std::string_view co_detect_profile_name(const CoDetectProfile profile) noexcept {
    using enum CoDetectProfile;
    switch (profile) {
    case cn: return "CN";
    case jp: return "JP";
    case global_en_us: return "Global_en-us";
    case global_zh_tw: return "Global_zh-tw";
    case global_ko_kr: return "Global_ko-kr";
    }
    return {};
}

std::string_view co_detect_definition_error_name(
    const CoDetectDefinitionError error) noexcept {
    using enum CoDetectDefinitionError;
    switch (error) {
    case none: return "CDD000_NONE";
    case invalid_limits: return "CDD001_INVALID_LIMITS";
    case definition_too_large: return "CDD002_DEFINITION_TOO_LARGE";
    case invalid_utf8: return "CDD003_INVALID_UTF8";
    case invalid_json: return "CDD004_INVALID_JSON";
    case duplicate_json_key: return "CDD005_DUPLICATE_JSON_KEY";
    case json_depth_limit_exceeded: return "CDD006_JSON_DEPTH_LIMIT_EXCEEDED";
    case json_node_limit_exceeded: return "CDD007_JSON_NODE_LIMIT_EXCEEDED";
    case string_limit_exceeded: return "CDD008_STRING_LIMIT_EXCEEDED";
    case work_limit_exceeded: return "CDD009_WORK_LIMIT_EXCEEDED";
    case invalid_root: return "CDD010_INVALID_ROOT";
    case unsupported_schema: return "CDD011_UNSUPPORTED_SCHEMA";
    case unsupported_engine: return "CDD012_UNSUPPORTED_ENGINE";
    case invalid_payload: return "CDD013_INVALID_PAYLOAD";
    case invalid_profile_source: return "CDD014_INVALID_PROFILE_SOURCE";
    case invalid_feature: return "CDD015_INVALID_FEATURE";
    case duplicate_feature: return "CDD016_DUPLICATE_FEATURE";
    case array_limit_exceeded: return "CDD017_ARRAY_LIMIT_EXCEEDED";
    case invalid_reaction: return "CDD018_INVALID_REACTION";
    case invalid_image_match: return "CDD028_INVALID_IMAGE_MATCH";
    case invalid_profile: return "CDD019_INVALID_PROFILE";
    case duplicate_profile: return "CDD020_DUPLICATE_PROFILE";
    case invalid_click: return "CDD021_INVALID_CLICK";
    case invalid_foreground_check: return "CDD022_INVALID_FOREGROUND_CHECK";
    case invalid_loop: return "CDD023_INVALID_LOOP";
    case invalid_tentative: return "CDD024_INVALID_TENTATIVE";
    case canonical_identity_limit_exceeded:
        return "CDD025_CANONICAL_IDENTITY_LIMIT_EXCEEDED";
    case resource_exhausted: return "CDD026_RESOURCE_EXHAUSTED";
    case internal_failure: return "CDD027_INTERNAL_FAILURE";
    }
    return "CDD027_INTERNAL_FAILURE";
}

CoDetectDefinitionLoadResult load_co_detect_python_compat_definition(
    const std::span<const std::byte> bytes,
    const CoDetectDefinitionLimits limits) noexcept {
    if (!valid_limits(limits)) return {{}, CoDetectDefinitionError::invalid_limits, {}};
    if (bytes.size() > limits.max_definition_bytes)
        return {{}, CoDetectDefinitionError::definition_too_large, {}};
    try {
        std::string text(bytes.size(), '\0');
        if (!bytes.empty()) std::memcpy(text.data(), bytes.data(), bytes.size());
        auto document = strict_json::parse_strict_json(
            text, {limits.max_json_depth, limits.max_json_nodes,
                   limits.max_string_bytes});
        if (!document)
            return {{}, map_json_error(document.error), {}};
        const auto& root = *document.document;
        if (!exact_fields(root, {"schema", "engine", "payload"}) ||
            !root["schema"].is_string() || !root["engine"].is_string())
            fail(CoDetectDefinitionError::invalid_root, "root");
        if (root["schema"].get_ref<const std::string&>() != co_detect_definition_schema)
            fail(CoDetectDefinitionError::unsupported_schema, "schema");
        if (root["engine"].get_ref<const std::string&>() != co_detect_python_compat_engine)
            fail(CoDetectDefinitionError::unsupported_engine, "engine");

        Budget budget{limits};
        budget.string(root["schema"].get_ref<const std::string&>(), "schema");
        budget.string(root["engine"].get_ref<const std::string&>(), "engine");
        auto parsed = parse_payload(root["payload"], budget);
        auto canonical = std::make_shared<const std::vector<std::byte>>(
            canonical_identity(parsed, limits.max_canonical_identity_bytes));
        auto source = std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
        auto impl = std::make_unique<CoDetectPythonCompatDefinition::Impl>();
        impl->source_sha256 = ::baas::resources::sha256_hex(*source);
        impl->canonical_sha256 = ::baas::resources::sha256_hex(*canonical);
        impl->source = std::move(source);
        impl->canonical = std::move(canonical);
        impl->parsed = std::move(parsed);
        return {std::shared_ptr<const CoDetectPythonCompatDefinition>(
                    new CoDetectPythonCompatDefinition(std::move(impl))),
                CoDetectDefinitionError::none, {}};
    } catch (const LoadFailure& error) {
        return {{}, error.error, error.field};
    } catch (const std::bad_alloc&) {
        return {{}, CoDetectDefinitionError::resource_exhausted, {}};
    } catch (...) {
        return {{}, CoDetectDefinitionError::internal_failure, {}};
    }
}

}  // namespace baas::runtime::procedure
