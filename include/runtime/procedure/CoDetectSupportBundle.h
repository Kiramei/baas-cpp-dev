#pragma once

#include "runtime/json/StrictJson.h"
#include "runtime/procedure/CoDetectPythonCompatDefinition.h"
#include "runtime/resources/RuntimeResourceSnapshotLoader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace baas::runtime::procedure {

inline constexpr std::string_view co_detect_support_bundle_media_type =
    "application/vnd.baas.co-detect-support-bundle.v1+zip";
inline constexpr std::string_view co_detect_support_bundle_schema =
    "baas.co-detect-support-bundle/v1";
inline constexpr std::string_view co_detect_feature_graph_schema =
    "baas.co-detect-feature-graph/v1";
inline constexpr std::string_view co_detect_rgb_ranges_schema =
    "baas.co-detect-rgb-ranges/v1";
inline constexpr std::array<std::byte, 16> co_detect_support_bundle_magic{
    std::byte{'B'}, std::byte{'A'}, std::byte{'A'}, std::byte{'S'},
    std::byte{'C'}, std::byte{'D'}, std::byte{'S'}, std::byte{'B'},
    std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0},
    std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
};

struct CoDetectSupportBundleLimits final {
    std::size_t max_archive_bytes{64U * 1024U * 1024U};
    std::size_t max_entries{256};
    std::size_t max_manifest_bytes{1024U * 1024U};
    std::size_t max_member_bytes{16U * 1024U * 1024U};
    std::size_t max_total_uncompressed_bytes{128U * 1024U * 1024U};
    std::uint64_t max_compression_ratio{128};
    std::size_t max_string_bytes{1'024};
    std::size_t max_json_depth{16};
    std::size_t max_json_nodes{131'072};
    std::size_t max_features{512};
    std::size_t max_rgb_samples{8'192};
    std::size_t max_png_chunks{4'096};
    std::uint32_t max_png_width{1'280};
    std::uint32_t max_png_height{720};
    std::size_t max_decoded_png_bytes{4U * 1024U * 1024U};
    std::size_t max_total_decoded_png_bytes{128U * 1024U * 1024U};
    std::size_t max_work{1024U * 1024U * 1024U};
};

enum class CoDetectSupportBundleError : std::uint8_t {
    none,
    invalid_limits,
    generation_mismatch,
    resource_not_found,
    selector_mismatch,
    media_type_mismatch,
    archive_too_large,
    invalid_zip,
    unsupported_zip_feature,
    bad_magic,
    unsupported_version,
    entry_limit_exceeded,
    entry_name_invalid,
    entry_order_mismatch,
    duplicate_entry,
    manifest_too_large,
    manifest_json_invalid,
    manifest_schema_invalid,
    manifest_field_invalid,
    profile_mismatch,
    member_count_mismatch,
    member_limit_exceeded,
    compression_limit_exceeded,
    member_size_mismatch,
    member_digest_mismatch,
    member_kind_mismatch,
    png_invalid,
    png_limit_exceeded,
    rgb_invalid,
    feature_graph_invalid,
    work_limit_exceeded,
    cancelled,
    resource_exhausted,
    internal_failure,
};

[[nodiscard]] std::string_view co_detect_support_bundle_error_name(
    CoDetectSupportBundleError error) noexcept;

struct CoDetectCrop final {
    std::uint16_t left{};
    std::uint16_t top{};
    std::uint16_t right{};
    std::uint16_t bottom{};
    friend bool operator==(const CoDetectCrop&, const CoDetectCrop&) = default;
};

struct CoDetectRgbSample final {
    std::uint16_t x{};
    std::uint16_t y{};
    std::array<std::uint8_t, 2> red{};
    std::array<std::uint8_t, 2> green{};
    std::array<std::uint8_t, 2> blue{};
    friend bool operator==(const CoDetectRgbSample&, const CoDetectRgbSample&) = default;
};

struct CoDetectRgbTemplate final {
    std::string feature;
    std::string member_id;
    std::vector<CoDetectRgbSample> samples;
};

struct CoDetectSupportBundleLoadResult;

class CoDetectImageTemplate final {
public:
    [[nodiscard]] const std::string& feature() const noexcept;
    [[nodiscard]] const std::string& member_id() const noexcept;
    [[nodiscard]] const CoDetectCrop& crop() const noexcept;
    [[nodiscard]] std::uint16_t threshold_milli() const noexcept;
    [[nodiscard]] std::uint16_t mean_rgb_tolerance() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::size_t row_stride() const noexcept;
    // Immutable, tightly packed OpenCV-compatible BGR8 pixels.
    [[nodiscard]] std::span<const std::byte> bgr_pixels() const noexcept;

private:
    friend class CoDetectSupportBundle;
    friend CoDetectSupportBundleLoadResult load_co_detect_support_bundle(
        std::shared_ptr<const resources::RuntimeResourceSnapshotActivation>,
        std::string_view, std::string_view, std::string_view, CoDetectProfile,
        const CoDetectSupportBundleLimits&, std::stop_token) noexcept;
    std::string feature_;
    std::string member_id_;
    CoDetectCrop crop_;
    std::uint16_t threshold_milli_{};
    std::uint16_t mean_rgb_tolerance_{};
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::size_t row_stride_{};
    std::shared_ptr<const std::vector<std::byte>> bgr_pixels_;
};

class CoDetectSupportBundle final {
public:
    ~CoDetectSupportBundle();
    CoDetectSupportBundle(const CoDetectSupportBundle&) = delete;
    CoDetectSupportBundle& operator=(const CoDetectSupportBundle&) = delete;
    CoDetectSupportBundle(CoDetectSupportBundle&&) = delete;
    CoDetectSupportBundle& operator=(CoDetectSupportBundle&&) = delete;

    [[nodiscard]] const std::string& generation() const noexcept;
    [[nodiscard]] const std::string& commit() const noexcept;
    [[nodiscard]] const std::string& snapshot_id() const noexcept;
    [[nodiscard]] const std::string& resource_id() const noexcept;
    [[nodiscard]] const std::string& locale() const noexcept;
    [[nodiscard]] CoDetectProfile profile() const noexcept;
    [[nodiscard]] const std::string& archive_sha256() const noexcept;
    [[nodiscard]] std::size_t member_count() const noexcept;
    [[nodiscard]] const CoDetectRgbTemplate* find_rgb(std::string_view feature) const noexcept;
    [[nodiscard]] const CoDetectImageTemplate* find_image(
        std::string_view feature) const noexcept;

private:
    struct Impl;
    explicit CoDetectSupportBundle(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;

    friend struct CoDetectSupportBundleLoadResult;
    friend CoDetectSupportBundleLoadResult load_co_detect_support_bundle(
        std::shared_ptr<const resources::RuntimeResourceSnapshotActivation>,
        std::string_view, std::string_view, std::string_view, CoDetectProfile,
        const CoDetectSupportBundleLimits&, std::stop_token) noexcept;
};

struct CoDetectSupportBundleLoadResult final {
    std::shared_ptr<const CoDetectSupportBundle> bundle;
    CoDetectSupportBundleError error{CoDetectSupportBundleError::none};
    json::StrictJsonError json_error{json::StrictJsonError::none};
    std::optional<std::size_t> member_index;

    [[nodiscard]] explicit operator bool() const noexcept {
        return bundle != nullptr && error == CoDetectSupportBundleError::none;
    }
};

[[nodiscard]] CoDetectSupportBundleLoadResult load_co_detect_support_bundle(
    std::shared_ptr<const resources::RuntimeResourceSnapshotActivation> activation,
    std::string_view expected_generation,
    std::string_view resource_id,
    std::string_view frozen_locale,
    CoDetectProfile frozen_profile,
    const CoDetectSupportBundleLimits& limits = {},
    std::stop_token stop = {}) noexcept;

}  // namespace baas::runtime::procedure
