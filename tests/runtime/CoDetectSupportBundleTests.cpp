#include "resources/ResourceSnapshot.h"
#include "runtime/procedure/CoDetectSupportBundle.h"
#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "runtime/resources/RuntimeResourceSnapshotLoader.h"

#include <miniz.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace procedure = baas::runtime::procedure;
namespace repository = baas::runtime::repository;
namespace runtime_resources = baas::runtime::resources;
namespace snapshot_resources = baas::resources;

std::atomic<int> failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view value)
{
    const auto view = std::as_bytes(std::span{value.data(), value.size()});
    return {view.begin(), view.end()};
}

[[nodiscard]] std::string sha256(const std::span<const std::byte> value)
{
    return snapshot_resources::sha256_hex(value);
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("baas-co-detect-bundle-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::span<const std::byte> value)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not create fixture file");
    output.write(reinterpret_cast<const char*>(value.data()),
                 static_cast<std::streamsize>(value.size()));
    if (!output) throw std::runtime_error("could not write fixture file");
}

void write_file(const std::filesystem::path& path, const std::string_view value)
{
    write_file(path, std::as_bytes(std::span{value.data(), value.size()}));
}

struct FixtureFile final {
    std::string path;
    std::vector<std::byte> bytes;
};

[[nodiscard]] FixtureFile text_file(std::string path, const std::string_view value)
{
    return {std::move(path), bytes(value)};
}

[[nodiscard]] std::string tree_manifest(std::vector<FixtureFile> files)
{
    std::ranges::sort(files, {}, &FixtureFile::path);
    std::string result = R"({"schema":"baas.runtime-repository.tree-manifest/v1","entries":[)";
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (index != 0) result.push_back(',');
        result += R"({"path":")" + files[index].path + R"(","size":")" +
            std::to_string(files[index].bytes.size()) + R"(","sha256":")" +
            sha256(files[index].bytes) + R"(","mode":"file"})";
    }
    result += "]}";
    return result;
}

[[nodiscard]] std::string snapshot_json(
    const std::array<repository::RuntimeRepository, 2>& repositories,
    const std::string_view generation)
{
    std::string result = R"({"schema":"baas.runtime-repositories.snapshot/v1","generation":")" +
        std::string{generation} + R"(","repositories":[)";
    for (std::size_t index = 0; index < repositories.size(); ++index) {
        if (index != 0) result.push_back(',');
        const auto& item = repositories[index];
        result += R"({"id":")" + item.id + R"(","commit":")" + item.commit +
            R"(","root":")" + item.root + R"(","manifest":")" + item.manifest +
            R"(","manifest_sha256":")" + item.manifest_sha256 + R"("})";
    }
    result += "]}";
    return result;
}

class RepositoryFixture final {
public:
    RepositoryFixture(
        std::vector<std::byte> archive, std::string media_type,
        std::optional<std::string> entry_locale)
    {
        const std::string archive_path = "payload/co-detect.bundle";
        std::string package_manifest =
            R"({"schema":"baas.resources/v1","entries":[{"id":"procedure-support/navigation.to-main-page/v1","path":")" +
            archive_path + R"(","media_type":")" + media_type + R"(","size":)" +
            std::to_string(archive.size()) + R"(,"sha256":")" + sha256(archive) + '"';
        if (entry_locale)
            package_manifest += R"(,"locale":")" + *entry_locale + '"';
        package_manifest += "}]}";
        std::vector<FixtureFile> resource_files{
            {archive_path, std::move(archive)},
            text_file(std::string{runtime_resources::runtime_resource_manifest_path},
                      package_manifest),
        };
        const std::vector<FixtureFile> script_files{
            text_file("placeholder.baas", "let placeholder = true;\n")};
        const auto resource_tree = tree_manifest(resource_files);
        const auto script_tree = tree_manifest(script_files);
        const std::string resource_commit(40, '1');
        const std::string script_commit(64, '2');
        repositories_ = {{
            {"resources", resource_commit, "objects/resources/" + resource_commit,
             "manifest.json", sha256(bytes(resource_tree))},
            {"scripts", script_commit, "objects/scripts/" + script_commit,
             "scripts.json", sha256(bytes(script_tree))},
        }};
        for (const auto& file : resource_files)
            write_file(temporary_.path() / repositories_[0].root / file.path, file.bytes);
        for (const auto& file : script_files)
            write_file(temporary_.path() / repositories_[1].root / file.path, file.bytes);
        write_file(temporary_.path() / repositories_[0].root / repositories_[0].manifest,
                   resource_tree);
        write_file(temporary_.path() / repositories_[1].root / repositories_[1].manifest,
                   script_tree);
        generation_ = repository::runtime_repository_generation(repositories_);
        write_file(temporary_.path() / "snapshots" / (generation_ + ".json"),
                   snapshot_json(repositories_, generation_));
        write_file(temporary_.path() / "current.json",
                   R"({"schema":"baas.runtime-repositories.current/v1","generation":")" +
                       generation_ + R"(","snapshot":"snapshots/)" + generation_ + ".json\"}");
        snapshot_ = repository::RuntimeRepositorySnapshot::activate(temporary_.path());
        read_bundle_ = snapshot_->open_read_bundle();
        const auto loaded = runtime_resources::load_runtime_resource_snapshot(
            read_bundle_->resources(), {"JP", std::nullopt});
        if (!loaded) throw std::runtime_error("resource snapshot fixture did not activate");
        activation_ = loaded.activation;
    }
    [[nodiscard]] const std::shared_ptr<
        const runtime_resources::RuntimeResourceSnapshotActivation>& activation() const noexcept
    {
        return activation_;
    }
private:
    TemporaryDirectory temporary_;
    std::array<repository::RuntimeRepository, 2> repositories_;
    std::string generation_;
    std::shared_ptr<const repository::RuntimeRepositorySnapshot> snapshot_;
    std::shared_ptr<const repository::RuntimeRepositoryReadBundle> read_bundle_;
    std::shared_ptr<const runtime_resources::RuntimeResourceSnapshotActivation> activation_;
};

struct ZipItem final {
    std::string name;
    std::vector<std::byte> value;
    mz_uint compression{MZ_NO_COMPRESSION};
    std::string comment;
    std::string local_extra;
    std::string central_extra;
};

[[nodiscard]] std::vector<std::byte> zip(const std::vector<ZipItem>& items)
{
    struct Central final {
        const ZipItem* item;
        std::uint32_t offset;
        std::uint32_t crc;
        std::uint32_t compressed_size;
        std::uint16_t method;
    };
    std::vector<std::byte> result;
    std::vector<Central> central;
    auto u16 = [&result](const std::uint16_t value) {
        result.push_back(static_cast<std::byte>(value & 0xffU));
        result.push_back(static_cast<std::byte>(value >> 8U));
    };
    auto u32 = [&result](const std::uint32_t value) {
        for (std::size_t index = 0; index < 4; ++index)
            result.push_back(static_cast<std::byte>(value >> (index * 8U)));
    };
    auto append = [&result](const std::span<const std::byte> value) {
        result.insert(result.end(), value.begin(), value.end());
    };
    auto append_text = [&result](const std::string_view value) {
        const auto view = std::as_bytes(std::span{value.data(), value.size()});
        result.insert(result.end(), view.begin(), view.end());
    };
    for (const auto& item : items) {
        const auto crc = static_cast<std::uint32_t>(mz_crc32(
            MZ_CRC32_INIT, reinterpret_cast<const mz_uint8*>(item.value.data()),
            item.value.size()));
        const bool deflated = item.compression != MZ_NO_COMPRESSION;
        const std::array<std::byte, 1> invalid_but_bounded_deflate{std::byte{0}};
        const auto compressed = deflated
            ? std::span<const std::byte>{invalid_but_bounded_deflate}
            : std::span<const std::byte>{item.value};
        const auto offset = static_cast<std::uint32_t>(result.size());
        u32(0x04034b50U);
        u16(deflated ? 20U : 10U);
        u16(0);
        u16(deflated ? 8U : 0U);
        u16(0);
        u16(0);
        u32(crc);
        u32(static_cast<std::uint32_t>(compressed.size()));
        u32(static_cast<std::uint32_t>(item.value.size()));
        u16(static_cast<std::uint16_t>(item.name.size()));
        u16(static_cast<std::uint16_t>(item.local_extra.size()));
        append_text(item.name);
        append_text(item.local_extra);
        append(compressed);
        central.push_back({&item, offset, crc,
                           static_cast<std::uint32_t>(compressed.size()),
                           static_cast<std::uint16_t>(deflated ? 8U : 0U)});
    }
    const auto central_offset = static_cast<std::uint32_t>(result.size());
    for (const auto& entry : central) {
        u32(0x02014b50U);
        u16(20U);
        u16(entry.method == 8 ? 20U : 10U);
        u16(0);
        u16(entry.method);
        u16(0);
        u16(0);
        u32(entry.crc);
        u32(entry.compressed_size);
        u32(static_cast<std::uint32_t>(entry.item->value.size()));
        u16(static_cast<std::uint16_t>(entry.item->name.size()));
        u16(static_cast<std::uint16_t>(entry.item->central_extra.size()));
        u16(static_cast<std::uint16_t>(entry.item->comment.size()));
        u16(0);
        u16(0);
        u32(0);
        u32(entry.offset);
        append_text(entry.item->name);
        append_text(entry.item->central_extra);
        append_text(entry.item->comment);
    }
    const auto central_size = static_cast<std::uint32_t>(result.size()) - central_offset;
    u32(0x06054b50U);
    u16(0);
    u16(0);
    u16(static_cast<std::uint16_t>(items.size()));
    u16(static_cast<std::uint16_t>(items.size()));
    u32(central_size);
    u32(central_offset);
    u16(0);
    return result;
}

[[nodiscard]] std::vector<std::byte> png_fixture()
{
    cv::Mat image(1, 2, CV_8UC3);
    image.at<cv::Vec3b>(0, 0) = {1, 2, 3};
    image.at<cv::Vec3b>(0, 1) = {4, 5, 6};
    std::vector<unsigned char> encoded;
    if (!cv::imencode(".png", image, encoded))
        throw std::runtime_error("png encode failed");
    std::vector<std::byte> result(encoded.size());
    std::memcpy(result.data(), encoded.data(), encoded.size());
    return result;
}

struct BundleOptions final {
    std::string locale{"JP"};
    std::string profile{"JP"};
    std::string graph_member_id{"feature/navigation.to-main-page"};
    std::string graph{
        R"({"schema":"baas.co-detect-feature-graph/v1","features":[{"name":"main_page","type":"rgb","member":"rgb/main-page"},{"name":"main_page_news","type":"image","member":"image/main-page-news","crop":[10,20,30,40],"threshold_milli":800,"mean_rgb_tolerance":20}]})"};
    std::string rgb{
        R"({"schema":"baas.co-detect-rgb-ranges/v1","samples":[{"x":10,"y":20,"r":[1,2],"g":[3,4],"b":[5,6]}]})"};
    std::vector<std::byte> png{png_fixture()};
    bool bad_magic{};
    bool bad_version{};
    bool duplicate_manifest_schema{};
    bool extra_manifest_field{};
    bool bad_rgb_digest{};
    std::optional<std::size_t> declared_graph_size;
    std::array<std::string, 3> payload_names{"m00000000", "m00000001", "m00000002"};
    bool zip_comment{};
    bool zip_extra{};
    mz_uint graph_compression{MZ_NO_COMPRESSION};
};

[[nodiscard]] std::vector<std::byte> support_archive(const BundleOptions& options = {})
{
    auto graph = bytes(options.graph);
    auto rgb = bytes(options.rgb);
    const auto graph_size = options.declared_graph_size.value_or(graph.size());
    const auto payload_size = graph_size + rgb.size() + options.png.size();
    std::string manifest =
        R"({"schema":"baas.co-detect-support-bundle/v1","format_version":1,"bundle_id":"procedure-support/navigation.to-main-page/v1","locale":")" +
        options.locale + R"(","profile":")" + options.profile +
        R"(","member_count":3,"payload_size":)" + std::to_string(payload_size) +
        R"(,"members":[{"id":")" + options.graph_member_id +
        R"(","kind":"feature-graph","media_type":"application/vnd.baas.co-detect-feature-graph.v1+json","size":)" +
        std::to_string(graph_size) + R"(,"sha256":")" + sha256(graph) +
        R"("},{"id":"rgb/main-page","kind":"rgb-range-set","media_type":"application/vnd.baas.co-detect-rgb-ranges.v1+json","size":)" +
        std::to_string(rgb.size()) + R"(,"sha256":")" +
        (options.bad_rgb_digest ? std::string(64, '0') : sha256(rgb)) +
        R"("},{"id":"image/main-page-news","kind":"png-template","media_type":"image/png","size":)" +
        std::to_string(options.png.size()) + R"(,"sha256":")" + sha256(options.png) +
        R"("}]})";
    if (options.duplicate_manifest_schema)
        manifest.insert(1, R"("schema":"baas.co-detect-support-bundle/v1",)");
    if (options.extra_manifest_field)
        manifest.insert(manifest.size() - 1, R"(,"extra":true)");
    std::vector<std::byte> magic(
        procedure::co_detect_support_bundle_magic.begin(),
        procedure::co_detect_support_bundle_magic.end());
    if (options.bad_magic) magic[0] = std::byte{'X'};
    if (options.bad_version) magic[8] = std::byte{2};
    return zip({
        {"bundle.magic", std::move(magic), MZ_NO_COMPRESSION,
         options.zip_comment ? "comment" : "", options.zip_extra ? "xx" : "",
         options.zip_extra ? "yy" : ""},
        {"manifest.json", bytes(manifest)},
        {options.payload_names[0], std::move(graph), options.graph_compression},
        {options.payload_names[1], std::move(rgb)},
        {options.payload_names[2], options.png},
    });
}

[[nodiscard]] procedure::CoDetectSupportBundleLoadResult load(
    std::vector<std::byte> archive,
    const procedure::CoDetectSupportBundleLimits& limits = {},
    const std::stop_token stop = {},
    const std::optional<std::string_view> expected_generation = std::nullopt,
    std::string media_type = std::string{procedure::co_detect_support_bundle_media_type},
    std::optional<std::string> entry_locale = std::string{"JP"},
    const procedure::CoDetectProfile frozen_profile = procedure::CoDetectProfile::jp)
{
    RepositoryFixture fixture{
        std::move(archive), std::move(media_type), std::move(entry_locale)};
    return procedure::load_co_detect_support_bundle(
        fixture.activation(),
        expected_generation.value_or(fixture.activation()->generation()),
        "procedure-support/navigation.to-main-page/v1", "JP",
        frozen_profile, limits, stop);
}

void expect_error(
    const procedure::CoDetectSupportBundleLoadResult& result,
    const procedure::CoDetectSupportBundleError expected,
    const std::string_view message)
{
    check(!result && !result.bundle && result.error == expected, message);
    if (result.error != expected)
        std::cerr << "  actual=" << procedure::co_detect_support_bundle_error_name(result.error)
                  << " expected=" << procedure::co_detect_support_bundle_error_name(expected)
                  << '\n';
}

[[nodiscard]] std::size_t signature_offset(
    const std::span<const std::byte> archive, const std::uint32_t signature,
    const std::size_t occurrence = 0)
{
    std::size_t seen{};
    for (std::size_t offset = 0; offset + 4 <= archive.size(); ++offset) {
        const auto value = std::to_integer<std::uint32_t>(archive[offset]) |
            std::to_integer<std::uint32_t>(archive[offset + 1]) << 8U |
            std::to_integer<std::uint32_t>(archive[offset + 2]) << 16U |
            std::to_integer<std::uint32_t>(archive[offset + 3]) << 24U;
        if (value == signature && seen++ == occurrence) return offset;
    }
    throw std::runtime_error("signature not found");
}

void put_u16(std::vector<std::byte>& value, const std::size_t offset, const std::uint16_t input)
{
    value[offset] = static_cast<std::byte>(input & 0xffU);
    value[offset + 1] = static_cast<std::byte>(input >> 8U);
}

[[nodiscard]] std::uint16_t get_u16(
    const std::span<const std::byte> value, const std::size_t offset)
{
    return std::to_integer<std::uint16_t>(value[offset]) |
        std::to_integer<std::uint16_t>(value[offset + 1]) << 8U;
}

[[nodiscard]] std::uint32_t get_u32(
    const std::span<const std::byte> value, const std::size_t offset)
{
    return std::to_integer<std::uint32_t>(value[offset]) |
        std::to_integer<std::uint32_t>(value[offset + 1]) << 8U |
        std::to_integer<std::uint32_t>(value[offset + 2]) << 16U |
        std::to_integer<std::uint32_t>(value[offset + 3]) << 24U;
}

void test_success_and_owned_pixels()
{
    std::shared_ptr<const procedure::CoDetectSupportBundle> retained;
    {
        const auto result = load(support_archive());
        check(static_cast<bool>(result), "canonical support bundle must load");
        if (!result) return;
        retained = result.bundle;
        check(retained->locale() == "JP" &&
                  retained->profile() == procedure::CoDetectProfile::jp &&
                  retained->member_count() == 3,
              "bundle must retain locale/profile/member provenance");
        const auto* rgb = retained->find_rgb("main_page");
        const auto* image = retained->find_image("main_page_news");
        check(rgb && rgb->samples.size() == 1 && rgb->samples.front().red[0] == 1,
              "RGB ranges must publish immutable parsed samples");
        check(image && image->width() == 2 && image->height() == 1 &&
                  image->row_stride() == 6 && image->bgr_pixels().size() == 6 &&
                  image->bgr_pixels()[0] == std::byte{1} &&
                  image->crop() == procedure::CoDetectCrop{10, 20, 30, 40},
              "PNG must be actually decoded to immutable packed BGR pixels");
        check(!retained->find_rgb("profile_missing") &&
                  !retained->find_image("profile_missing"),
              "a profile-inapplicable or absent feature must evaluate as lookup miss");
    }
    check(retained && retained->generation().size() == 64 &&
              retained->archive_sha256().size() == 64,
          "published bundle must retain activation and archive lifetime");
}

void test_generation_selector_and_media_guards()
{
    expect_error(load(support_archive(), {}, {}, "wrong-generation"),
                 procedure::CoDetectSupportBundleError::generation_mismatch,
                 "generation mismatch must fail closed");
    expect_error(load(support_archive(), {}, {}, std::nullopt,
                      "application/octet-stream"),
                 procedure::CoDetectSupportBundleError::media_type_mismatch,
                 "outer media type must be exact");
    expect_error(load(support_archive(), {}, {}, std::nullopt,
                      std::string{procedure::co_detect_support_bundle_media_type}, std::nullopt),
                 procedure::CoDetectSupportBundleError::selector_mismatch,
                 "neutral ResourceSnapshot fallback must be rejected");
    expect_error(load(support_archive(), {}, {}, std::nullopt,
                      std::string{procedure::co_detect_support_bundle_media_type},
                      std::string{"JP"},
                      static_cast<procedure::CoDetectProfile>(0xffU)),
                 procedure::CoDetectSupportBundleError::profile_mismatch,
                 "out-of-range profile enum values must fail closed");
}

void test_magic_manifest_and_member_integrity()
{
    BundleOptions option;
    option.bad_magic = true;
    expect_error(load(support_archive(option)), procedure::CoDetectSupportBundleError::bad_magic,
                 "semantic magic must be exact");
    option = {};
    option.bad_version = true;
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::unsupported_version,
                 "semantic bundle version must be exact");
    option = {};
    option.duplicate_manifest_schema = true;
    const auto duplicate = load(support_archive(option));
    expect_error(duplicate, procedure::CoDetectSupportBundleError::manifest_json_invalid,
                 "duplicate manifest keys must be rejected");
    check(duplicate.json_error == baas::runtime::json::StrictJsonError::duplicate_key,
          "strict JSON cause must remain typed");
    option = {};
    option.extra_manifest_field = true;
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::manifest_field_invalid,
                 "unknown manifest fields must be rejected");
    option = {};
    option.graph_member_id = "feature/group.open";
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::member_kind_mismatch,
                 "the graph member id must match the selected support resource");
    option = {};
    option.profile = "CN";
    expect_error(load(support_archive(option)), procedure::CoDetectSupportBundleError::profile_mismatch,
                 "manifest profile must match frozen profile");
    option = {};
    option.bad_rgb_digest = true;
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::member_digest_mismatch,
                 "every member SHA-256 must be verified");
    option = {};
    option.declared_graph_size = option.graph.size() + 1U;
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::member_size_mismatch,
                 "manifest and ZIP member sizes must agree");
}

void test_graph_rgb_and_png_validation()
{
    BundleOptions option;
    option.graph =
        R"({"schema":"baas.co-detect-feature-graph/v1","features":[{"name":"bad","type":"image","member":"image/main-page-news","crop":[30,20,10,40],"threshold_milli":800,"mean_rgb_tolerance":20},{"name":"main_page","type":"rgb","member":"rgb/main-page"}]})";
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::feature_graph_invalid,
                 "inverted crop must be rejected");
    option = {};
    option.graph =
        R"({"schema":"baas.co-detect-feature-graph/v1","features":[{"name":"main_page","type":"rgb","member":"rgb/missing"},{"name":"main_page_news","type":"image","member":"image/main-page-news","crop":[10,20,30,40],"threshold_milli":800,"mean_rgb_tolerance":20}]})";
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::member_kind_mismatch,
                 "graph member reference must resolve with the declared kind");
    option = {};
    option.rgb =
        R"({"schema":"baas.co-detect-rgb-ranges/v1","samples":[{"x":1280,"y":20,"r":[1,2],"g":[3,4],"b":[5,6]}]})";
    expect_error(load(support_archive(option)), procedure::CoDetectSupportBundleError::rgb_invalid,
                 "RGB coordinates outside 1280x720 must be rejected");
    option = {};
    option.png.back() ^= std::byte{1};
    expect_error(load(support_archive(option)), procedure::CoDetectSupportBundleError::png_invalid,
                 "PNG chunk CRC and stream must be validated before publication");
}

void test_zip_canonical_form_and_attacks()
{
    BundleOptions option;
    option.zip_comment = true;
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::unsupported_zip_feature,
                 "entry comments must be rejected");
    option = {};
    option.zip_extra = true;
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::unsupported_zip_feature,
                 "local and central extra fields must be rejected");
    option = {};
    option.payload_names[1] = "dir/member";
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::entry_name_invalid,
                 "path-like physical names must be rejected");
    option = {};
    option.payload_names[1] = "m00000000";
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::duplicate_entry,
                 "duplicate physical names must be rejected");

    auto archive = support_archive();
    archive.push_back(std::byte{0});
    expect_error(load(std::move(archive)), procedure::CoDetectSupportBundleError::invalid_zip,
                 "trailing archive bytes must be rejected");
    archive = support_archive();
    archive.insert(archive.begin(), std::byte{0});
    expect_error(load(std::move(archive)), procedure::CoDetectSupportBundleError::invalid_zip,
                 "leading archive bytes must be rejected");
    archive = support_archive();
    const auto local = signature_offset(archive, 0x04034b50U);
    const auto central = signature_offset(archive, 0x02014b50U);
    put_u16(archive, local + 6, 1U);
    put_u16(archive, central + 8, 1U);
    expect_error(load(std::move(archive)),
                 procedure::CoDetectSupportBundleError::unsupported_zip_feature,
                 "encrypted entries must be rejected before extraction");
    archive = support_archive();
    const auto local2 = signature_offset(archive, 0x04034b50U);
    const auto central2 = signature_offset(archive, 0x02014b50U);
    put_u16(archive, local2 + 4, 45U);
    put_u16(archive, central2 + 6, 45U);
    expect_error(load(std::move(archive)),
                 procedure::CoDetectSupportBundleError::unsupported_zip_feature,
                 "Zip64-capable entries must be rejected");
    archive = support_archive();
    const auto local3 = signature_offset(archive, 0x04034b50U);
    archive[local3 + 30] ^= std::byte{1};
    expect_error(load(std::move(archive)),
                 procedure::CoDetectSupportBundleError::entry_order_mismatch,
                 "local and central names must agree exactly");
}

void test_bombs_limits_and_cancellation()
{
    BundleOptions option;
    option.graph.assign(64U * 1024U, ' ');
    option.graph_compression = MZ_BEST_COMPRESSION;
    expect_error(load(support_archive(option)),
                 procedure::CoDetectSupportBundleError::compression_limit_exceeded,
                 "high-ratio payload must fail before inflate");
    auto archive = support_archive();
    procedure::CoDetectSupportBundleLimits limits;
    limits.max_archive_bytes = archive.size() - 1U;
    expect_error(load(archive, limits), procedure::CoDetectSupportBundleError::archive_too_large,
                 "outer archive bytes must be bounded");
    limits = {};
    limits.max_entries = 0;
    expect_error(load(archive, limits), procedure::CoDetectSupportBundleError::invalid_limits,
                 "zero limits must be rejected");
    limits = {};
    limits.max_work = 1;
    expect_error(load(archive, limits),
                 procedure::CoDetectSupportBundleError::work_limit_exceeded,
                 "aggregate work must be bounded");

    option = {};
    option.graph.resize(64U);
    std::uint32_t random_state = 0x6d2b79f5U;
    for (auto& value : option.graph) {
        random_state ^= random_state << 13U;
        random_state ^= random_state >> 17U;
        random_state ^= random_state << 5U;
        value = static_cast<char>(random_state & 0xffU);
    }
    option.graph_compression = MZ_BEST_COMPRESSION;
    archive = support_archive(option);
    const auto manifest_local = signature_offset(archive, 0x04034b50U, 1);
    const auto graph_local = signature_offset(archive, 0x04034b50U, 2);
    const auto graph_data = graph_local + 30U + get_u16(archive, graph_local + 26U);
    archive[graph_data] ^= std::byte{0xffU};
    limits = {};
    limits.max_work = archive.size() + 16U +
        2U * get_u32(archive, manifest_local + 22U) +
        get_u32(archive, graph_local + 22U) - 1U;
    expect_error(load(archive, limits),
                 procedure::CoDetectSupportBundleError::work_limit_exceeded,
                 "declared inflate output must be reserved before touching compressed bytes");

    std::stop_source cancelled;
    cancelled.request_stop();
    expect_error(load(std::move(archive), {}, cancelled.get_token()),
                 procedure::CoDetectSupportBundleError::cancelled,
                 "pre-cancelled load must be deterministic");
}

void test_stable_error_names()
{
    check(procedure::co_detect_support_bundle_error_name(
              procedure::CoDetectSupportBundleError::none) == "CDSB000_NONE" &&
              procedure::co_detect_support_bundle_error_name(
                  procedure::CoDetectSupportBundleError::internal_failure) ==
                  "CDSB033_INTERNAL_FAILURE",
          "support bundle error names must remain stable");
}

}  // namespace

int main()
{
    try {
        test_success_and_owned_pixels();
        test_generation_selector_and_media_guards();
        test_magic_manifest_and_member_integrity();
        test_graph_rgb_and_png_validation();
        test_zip_canonical_form_and_attacks();
        test_bombs_limits_and_cancellation();
        test_stable_error_names();
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
    }
    if (failures.load() != 0) {
        std::cerr << failures.load() << " test(s) failed\n";
        return 1;
    }
    std::cout << "co-detect support bundle tests passed\n";
    return 0;
}
