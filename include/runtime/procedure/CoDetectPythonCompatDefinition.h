#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace baas::runtime::procedure {

inline constexpr std::string_view co_detect_definition_schema =
    "baas.procedure-definition/v1";
inline constexpr std::string_view co_detect_python_compat_engine =
    "co_detect.python-compat/v1";
inline constexpr std::string_view co_detect_profile_source =
    "device.server-and-locale/v1";

enum class CoDetectProfile : std::uint8_t {
    cn,
    jp,
    global_en_us,
    global_zh_tw,
    global_ko_kr,
};

[[nodiscard]] std::string_view co_detect_profile_name(CoDetectProfile profile) noexcept;

struct CoDetectClick final {
    std::uint16_t x{};
    std::uint16_t y{};
    friend bool operator==(const CoDetectClick&, const CoDetectClick&) = default;
};

struct CoDetectReaction final {
    std::string feature;
    CoDetectClick click;
    std::vector<CoDetectProfile> profiles;
};

struct CoDetectForegroundCheck final {
    bool android_only{};
    std::uint64_t interval_ms{};
    std::uint64_t idle_feature_ms{};
};

struct CoDetectTentative final {
    bool enabled{};
    std::uint64_t after_failed_cycles{};
    bool repeat_each_failed_cycle{};
    CoDetectClick click;
    std::uint64_t post_wait_screenshot_intervals{};
};

struct CoDetectLoop final {
    bool skip_first_screenshot{};
    std::uint64_t timeout_ms{};
    std::uint64_t duplicate_click_window_ms{};
    CoDetectTentative tentative;
};

struct CoDetectDefinitionLimits final {
    std::size_t max_definition_bytes{1024U * 1024U};
    std::size_t max_json_depth{24};
    std::size_t max_json_nodes{65'536};
    std::size_t max_string_bytes{1'024};
    std::size_t max_total_string_bytes{4U * 1024U * 1024U};
    std::size_t max_items_per_array{4'096};
    std::size_t max_total_items{32'768};
    std::size_t max_work{8U * 1024U * 1024U};
    std::size_t max_canonical_identity_bytes{8U * 1024U * 1024U};
};

enum class CoDetectDefinitionError : std::uint8_t {
    none,
    invalid_limits,
    definition_too_large,
    invalid_utf8,
    invalid_json,
    duplicate_json_key,
    json_depth_limit_exceeded,
    json_node_limit_exceeded,
    string_limit_exceeded,
    work_limit_exceeded,
    invalid_root,
    unsupported_schema,
    unsupported_engine,
    invalid_payload,
    invalid_profile_source,
    invalid_feature,
    duplicate_feature,
    array_limit_exceeded,
    invalid_reaction,
    invalid_profile,
    duplicate_profile,
    invalid_click,
    invalid_foreground_check,
    invalid_loop,
    invalid_tentative,
    canonical_identity_limit_exceeded,
    resource_exhausted,
    internal_failure,
};

[[nodiscard]] std::string_view co_detect_definition_error_name(
    CoDetectDefinitionError error) noexcept;

struct CoDetectDefinitionLoadResult;

class CoDetectPythonCompatDefinition final {
public:
    ~CoDetectPythonCompatDefinition();
    CoDetectPythonCompatDefinition(const CoDetectPythonCompatDefinition&) = delete;
    CoDetectPythonCompatDefinition& operator=(const CoDetectPythonCompatDefinition&) = delete;
    CoDetectPythonCompatDefinition(CoDetectPythonCompatDefinition&&) = delete;
    CoDetectPythonCompatDefinition& operator=(CoDetectPythonCompatDefinition&&) = delete;

    [[nodiscard]] std::span<const std::byte> source_bytes() const noexcept;
    [[nodiscard]] const std::string& source_sha256() const noexcept;
    [[nodiscard]] std::span<const std::byte> canonical_identity_material() const noexcept;
    [[nodiscard]] const std::string& canonical_sha256() const noexcept;

    [[nodiscard]] const std::vector<std::string>& ends_rgb() const noexcept;
    [[nodiscard]] const std::vector<std::string>& ends_image() const noexcept;
    [[nodiscard]] const std::vector<CoDetectReaction>& reactions_rgb() const noexcept;
    [[nodiscard]] const std::vector<CoDetectReaction>& reactions_rgb_profiled() const noexcept;
    [[nodiscard]] const std::vector<CoDetectReaction>& reactions_image() const noexcept;
    [[nodiscard]] const std::vector<CoDetectReaction>& reactions_image_profiled() const noexcept;
    [[nodiscard]] const std::vector<CoDetectReaction>& popups_rgb() const noexcept;
    [[nodiscard]] const std::vector<CoDetectReaction>& popups_profiled_image() const noexcept;
    [[nodiscard]] const std::vector<std::string>& loading_all_rgb() const noexcept;
    [[nodiscard]] const CoDetectForegroundCheck& foreground_check() const noexcept;
    [[nodiscard]] const CoDetectLoop& loop() const noexcept;

private:
    struct Impl;
    explicit CoDetectPythonCompatDefinition(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;

    friend struct CoDetectDefinitionLoadResult;
    friend CoDetectDefinitionLoadResult load_co_detect_python_compat_definition(
        std::span<const std::byte>, CoDetectDefinitionLimits) noexcept;
};

struct CoDetectDefinitionLoadResult final {
    std::shared_ptr<const CoDetectPythonCompatDefinition> definition;
    CoDetectDefinitionError error{CoDetectDefinitionError::none};
    std::string field;

    [[nodiscard]] explicit operator bool() const noexcept {
        return definition != nullptr && error == CoDetectDefinitionError::none;
    }
};

[[nodiscard]] CoDetectDefinitionLoadResult load_co_detect_python_compat_definition(
    std::span<const std::byte> bytes,
    CoDetectDefinitionLimits limits = {}) noexcept;

}  // namespace baas::runtime::procedure
