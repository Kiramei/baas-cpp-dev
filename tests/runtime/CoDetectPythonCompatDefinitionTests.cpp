#include "runtime/procedure/CoDetectPythonCompatDefinition.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace procedure = ::baas::runtime::procedure;
using Error = procedure::CoDetectDefinitionError;

int failures{};

void check(const bool condition, const std::string_view message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

[[nodiscard]] std::span<const std::byte> bytes(const std::string& value) {
    return std::as_bytes(std::span{value.data(), value.size()});
}

[[nodiscard]] std::string valid_definition() {
    return R"({
  "schema": "baas.procedure-definition/v1",
  "engine": "co_detect.python-compat/v1",
  "payload": {
    "profile_source": "device.server-and-locale/v1",
    "ends": {"rgb": ["main_page", "group_menu"], "image": ["home-image"]},
    "reactions": {
      "rgb": [{"feature": "notice", "click": [120, 80]}],
      "rgb_profiled": [{"profiles": ["JP", "Global_en-us"], "feature": "server-button", "click": [565, 600]}],
      "image": [{"feature": "image-button", "click": [1279, 719]}],
      "image_profiled": [{"profiles": ["CN"], "feature": "cn-image", "click": [1, 2]}]
    },
    "popups": {
      "rgb": [{"feature": "common-popup", "click": [5, 6]}],
      "profiled_image": [{"profiles": ["Global_zh-tw", "Global_ko-kr"], "feature": "global-popup", "click": [7, 8]}]
    },
    "loading": {"all_rgb": ["loadingNotWhite", "loadingWhite"]},
    "foreground_check": {"android_only": true, "interval_ms": 1000, "idle_feature_ms": 5000},
    "loop": {
      "skip_first_screenshot": true,
      "timeout_ms": 600000,
      "duplicate_click_window_ms": 2000,
      "tentative": {"enabled": true, "after_failed_cycles": 10, "repeat_each_failed_cycle": true, "click": [1238, 45], "post_wait_screenshot_intervals": 5}
    }
  }
})";
}

void replace_once(std::string& value, const std::string_view before,
                  const std::string_view after) {
    const auto at = value.find(before);
    check(at != std::string::npos, "test mutation anchor must exist");
    if (at != std::string::npos) value.replace(at, before.size(), after);
}

[[nodiscard]] procedure::CoDetectDefinitionLoadResult load(const std::string& value) {
    return procedure::load_co_detect_python_compat_definition(bytes(value));
}

void expect_error(std::string value, const Error error, const std::string_view message) {
    const auto result = load(value);
    check(!result, message);
    check(result.error == error, message);
    check(result.definition == nullptr, "failure must never publish a partial model");
}

void test_valid_model_and_owned_lifetime() {
    auto source = valid_definition();
    const auto expected = source;
    auto result = load(source);
    check(static_cast<bool>(result), "complete definition must load");
    if (!result) return;
    source.assign(source.size(), 'x');

    const auto& model = *result.definition;
    check(model.source_bytes().size() == expected.size(), "source bytes are retained");
    check(std::equal(model.source_bytes().begin(), model.source_bytes().end(),
                     bytes(expected).begin()), "retained source bytes are owned");
    check(model.source_sha256().size() == 64, "source SHA-256 is exposed");
    check(model.canonical_sha256().size() == 64, "canonical SHA-256 is exposed");
    check(!model.canonical_identity_material().empty(),
          "canonical digest material is retained");
    check(model.ends_rgb() == std::vector<std::string>{"main_page", "group_menu"},
          "feature order is preserved");
    check(model.reactions_rgb_profiled().front().profiles ==
              std::vector<procedure::CoDetectProfile>{
                  procedure::CoDetectProfile::jp,
                  procedure::CoDetectProfile::global_en_us},
          "profile order is preserved");
    check(model.reactions_image().front().click == procedure::CoDetectClick{1279, 719},
          "canonical coordinate upper bounds are accepted");
    check(model.loading_all_rgb().size() == 2, "loading conjunction is materialized");
    check(model.foreground_check().android_only &&
              model.foreground_check().interval_ms == 1000,
          "foreground contract is materialized");
    check(model.loop().tentative.enabled &&
              model.loop().tentative.after_failed_cycles == 10 &&
              model.loop().tentative.repeat_each_failed_cycle,
          "tentative contract is materialized");
}

void test_canonical_identity() {
    auto first = valid_definition();
    auto whitespace = first;
    replace_once(whitespace, "\n  \"schema\"", "     \"schema\"");
    const auto a = load(first);
    const auto b = load(whitespace);
    check(a && b, "whitespace variants must load");
    if (a && b) {
        check(a.definition->source_sha256() != b.definition->source_sha256(),
              "exact source digest observes bytes");
        check(a.definition->canonical_sha256() == b.definition->canonical_sha256(),
              "canonical identity ignores insignificant whitespace");
        check(std::ranges::equal(a.definition->canonical_identity_material(),
                                 b.definition->canonical_identity_material()),
              "canonical digest input ignores insignificant whitespace");
    }

    auto object_order = first;
    replace_once(object_order,
        "  \"schema\": \"baas.procedure-definition/v1\",\n  \"engine\": \"co_detect.python-compat/v1\",",
        "  \"engine\": \"co_detect.python-compat/v1\",\n  \"schema\": \"baas.procedure-definition/v1\",");
    const auto object_order_result = load(object_order);
    check(static_cast<bool>(object_order_result), "object-order variant must load");
    if (a && object_order_result)
        check(a.definition->canonical_sha256() ==
                  object_order_result.definition->canonical_sha256(),
              "object member order does not change semantic identity");

    auto reordered = first;
    replace_once(reordered, "[\"main_page\", \"group_menu\"]",
                 "[\"group_menu\", \"main_page\"]");
    const auto c = load(reordered);
    check(static_cast<bool>(c), "array-order variant must load");
    if (a && c)
        check(a.definition->canonical_sha256() != c.definition->canonical_sha256(),
              "array order changes canonical identity");
}

void test_strict_json_and_field_closure() {
    expect_error(valid_definition() + " trailing", Error::invalid_json,
                 "trailing data is rejected");
    auto nonfinite = valid_definition();
    replace_once(nonfinite, "\"timeout_ms\": 600000", "\"timeout_ms\": 1e10000");
    expect_error(std::move(nonfinite), Error::invalid_json,
                 "numbers outside finite JSON representation are rejected");
    auto comment = valid_definition();
    replace_once(comment, "{\n", "{/*comment*/\n");
    expect_error(std::move(comment), Error::invalid_json, "comments are rejected");
    auto duplicate = valid_definition();
    replace_once(duplicate, "\"schema\":", "\"schema\": \"bad\", \"schema\":");
    expect_error(std::move(duplicate), Error::duplicate_json_key,
                 "duplicate object names are typed");
    auto invalid_utf8 = valid_definition();
    invalid_utf8.insert(invalid_utf8.find("main_page"), 1, static_cast<char>(0xff));
    expect_error(std::move(invalid_utf8), Error::invalid_utf8,
                 "non-UTF-8 input is rejected");
    auto unknown_root = valid_definition();
    replace_once(unknown_root, "\"payload\":", "\"extra\": 1, \"payload\":");
    expect_error(std::move(unknown_root), Error::invalid_root,
                 "root field closure is strict");
    auto unknown_payload = valid_definition();
    replace_once(unknown_payload, "\"profile_source\":",
                 "\"extra\": 1, \"profile_source\":");
    expect_error(std::move(unknown_payload), Error::invalid_payload,
                 "payload field closure is strict");
    auto unknown_reaction = valid_definition();
    replace_once(unknown_reaction, "{\"feature\": \"notice\"",
                 "{\"extra\": 1, \"feature\": \"notice\"");
    expect_error(std::move(unknown_reaction), Error::invalid_reaction,
                 "reaction field closure is strict");
}

void test_wrapper_and_feature_validation() {
    auto value = valid_definition();
    replace_once(value, "baas.procedure-definition/v1", "baas.procedure-definition/v2");
    expect_error(std::move(value), Error::unsupported_schema, "schema is closed");
    value = valid_definition();
    replace_once(value, "co_detect.python-compat/v1", "legacy.appear_then_click/v1");
    expect_error(std::move(value), Error::unsupported_engine, "engine is exact");
    value = valid_definition();
    replace_once(value, "device.server-and-locale/v1", "caller/profile/v1");
    expect_error(std::move(value), Error::invalid_profile_source,
                 "profile source is exact");
    value = valid_definition();
    replace_once(value, "\"main_page\"", "\"\"");
    expect_error(std::move(value), Error::invalid_feature,
                 "feature IDs cannot be empty");
    value = valid_definition();
    replace_once(value, "[\"main_page\", \"group_menu\"]",
                 "[\"main_page\", \"main_page\"]");
    expect_error(std::move(value), Error::duplicate_feature,
                 "end features are duplicate-free");
    value = valid_definition();
    replace_once(value, "[\"loadingNotWhite\", \"loadingWhite\"]", "[]");
    expect_error(std::move(value), Error::invalid_feature,
                 "loading conjunction cannot be empty");
}

void test_reaction_profile_and_click_validation() {
    auto value = valid_definition();
    replace_once(value, "[\"JP\", \"Global_en-us\"]", "[]");
    expect_error(std::move(value), Error::invalid_profile,
                 "profiled reaction requires profiles");
    value = valid_definition();
    replace_once(value, "[\"JP\", \"Global_en-us\"]", "[\"JP\", \"JP\"]");
    expect_error(std::move(value), Error::duplicate_profile,
                 "profiles are duplicate-free");
    value = valid_definition();
    replace_once(value, "\"Global_en-us\"", "\"Global_fr-fr\"");
    expect_error(std::move(value), Error::invalid_profile, "profile set is closed");

    for (const auto& [replacement, message] :
         std::vector<std::pair<std::string, std::string_view>>{
             {"[-1, 80]", "negative click coordinate"},
             {"[1280, 80]", "x coordinate outside canonical space"},
             {"[120, 720]", "y coordinate outside canonical space"},
             {"[120.0, 80]", "non-integral click coordinate"},
             {"[120]", "wrong click arity"}}) {
        value = valid_definition();
        replace_once(value, "[120, 80]", replacement);
        expect_error(std::move(value), Error::invalid_click, message);
    }
}

void test_foreground_loop_and_tentative_validation() {
    auto value = valid_definition();
    replace_once(value, "\"android_only\": true", "\"android_only\": false");
    expect_error(std::move(value), Error::invalid_foreground_check,
                 "v1 foreground check is Android-only");
    value = valid_definition();
    replace_once(value, "\"interval_ms\": 1000", "\"interval_ms\": 0");
    expect_error(std::move(value), Error::invalid_foreground_check,
                 "foreground interval is positive");
    value = valid_definition();
    replace_once(value, "\"timeout_ms\": 600000", "\"timeout_ms\": 0");
    expect_error(std::move(value), Error::invalid_loop, "timeout is positive");
    value = valid_definition();
    replace_once(value, "\"duplicate_click_window_ms\": 2000",
                 "\"duplicate_click_window_ms\": -1");
    expect_error(std::move(value), Error::invalid_loop,
                 "duplicate click window is non-negative");
    value = valid_definition();
    replace_once(value, "\"repeat_each_failed_cycle\": true",
                 "\"repeat_each_failed_cycle\": false");
    expect_error(std::move(value), Error::invalid_tentative,
                 "v1 tentative click repeats");
    value = valid_definition();
    replace_once(value, "\"after_failed_cycles\": 10",
                 "\"after_failed_cycles\": 0");
    expect_error(std::move(value), Error::invalid_tentative,
                 "tentative failure threshold is positive");

    value = valid_definition();
    replace_once(value,
        "{\"enabled\": true, \"after_failed_cycles\": 10, \"repeat_each_failed_cycle\": true, \"click\": [1238, 45], \"post_wait_screenshot_intervals\": 5}",
        "{\"enabled\": false}");
    const auto disabled = load(value);
    check(disabled && !disabled.definition->loop().tentative.enabled,
          "closed disabled tentative form is accepted");
    replace_once(value, "{\"enabled\": false}",
                 "{\"enabled\": false, \"click\": [1, 2]}");
    expect_error(std::move(value), Error::invalid_tentative,
                 "disabled tentative form rejects extra fields");
}

void test_limits_and_stable_errors() {
    auto source = valid_definition();
    auto limits = procedure::CoDetectDefinitionLimits{};
    limits.max_definition_bytes = source.size() - 1;
    check(procedure::load_co_detect_python_compat_definition(bytes(source), limits).error ==
              Error::definition_too_large,
          "definition byte limit is enforced");
    limits = {};
    limits.max_items_per_array = 1;
    check(procedure::load_co_detect_python_compat_definition(bytes(source), limits).error ==
              Error::array_limit_exceeded,
          "per-array item limit is enforced");
    limits = {};
    limits.max_string_bytes = 4;
    check(procedure::load_co_detect_python_compat_definition(bytes(source), limits).error ==
              Error::string_limit_exceeded,
          "strict parser string limit is enforced");
    limits = {};
    limits.max_work = 1;
    check(procedure::load_co_detect_python_compat_definition(bytes(source), limits).error ==
              Error::work_limit_exceeded,
          "semantic work budget is enforced");
    limits = {};
    limits.max_canonical_identity_bytes = 1;
    check(procedure::load_co_detect_python_compat_definition(bytes(source), limits).error ==
              Error::canonical_identity_limit_exceeded,
          "canonical material is bounded");
    limits = {};
    limits.max_json_nodes = 1;
    check(procedure::load_co_detect_python_compat_definition(bytes(source), limits).error ==
              Error::json_node_limit_exceeded,
          "JSON node limit has a typed error");
    limits = {};
    limits.max_definition_bytes = 0;
    check(procedure::load_co_detect_python_compat_definition(bytes(source), limits).error ==
              Error::invalid_limits,
          "zero limits are rejected before parsing");

    std::set<std::string_view> names;
    for (unsigned raw = 0; raw <= static_cast<unsigned>(Error::internal_failure); ++raw) {
        const auto name = procedure::co_detect_definition_error_name(
            static_cast<Error>(raw));
        check(!name.empty() && names.insert(name).second,
              "typed definition errors have unique stable names");
    }
}

}  // namespace

int main() {
    test_valid_model_and_owned_lifetime();
    test_canonical_identity();
    test_strict_json_and_field_closure();
    test_wrapper_and_feature_validation();
    test_reaction_profile_and_click_validation();
    test_foreground_loop_and_tentative_validation();
    test_limits_and_stable_errors();
    if (failures != 0) {
        std::cerr << failures << " co-detect definition model test(s) failed\n";
        return 1;
    }
    std::cout << "co-detect definition model tests passed\n";
    return 0;
}
