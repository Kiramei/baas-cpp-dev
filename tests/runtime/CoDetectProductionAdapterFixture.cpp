#include "CoDetectProductionAdapterFixture.h"

#include "resources/ResourceSnapshot.h"
#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "runtime/resources/RuntimeResourceSnapshotLoader.h"

#include <miniz.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace procedure = baas::runtime::procedure;
namespace repository = baas::runtime::repository;
namespace runtime_resources = baas::runtime::resources;
namespace resources = baas::resources;

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view value)
{
    const auto view = std::as_bytes(std::span{value.data(), value.size()});
    return {view.begin(), view.end()};
}

[[nodiscard]] std::string sha256(const std::span<const std::byte> value)
{
    return resources::sha256_hex(value);
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        static std::atomic<unsigned long long> serial{};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("baas-co-detect-production-adapter-" + std::to_string(stamp) + "-" +
             std::to_string(serial.fetch_add(1)));
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
    if (!output) throw std::runtime_error("fixture create failed");
    output.write(reinterpret_cast<const char*>(value.data()),
                 static_cast<std::streamsize>(value.size()));
    if (!output) throw std::runtime_error("fixture write failed");
}

void write_file(const std::filesystem::path& path, const std::string_view value)
{
    write_file(path, std::as_bytes(std::span{value.data(), value.size()}));
}

struct File final {
    std::string path;
    std::vector<std::byte> value;
};

[[nodiscard]] std::string tree_manifest(std::vector<File> files)
{
    std::ranges::sort(files, {}, &File::path);
    std::string result =
        R"({"schema":"baas.runtime-repository.tree-manifest/v1","entries":[)";
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (index != 0) result.push_back(',');
        result += R"({"path":")" + files[index].path + R"(","size":")" +
            std::to_string(files[index].value.size()) + R"(","sha256":")" +
            sha256(files[index].value) + R"(","mode":"file"})";
    }
    return result + "]}";
}

[[nodiscard]] std::string snapshot_json(
    const std::array<repository::RuntimeRepository, 2>& repositories,
    const std::string_view generation)
{
    std::string result =
        R"({"schema":"baas.runtime-repositories.snapshot/v1","generation":")" +
        std::string{generation} + R"(","repositories":[)";
    for (std::size_t index = 0; index < repositories.size(); ++index) {
        if (index != 0) result.push_back(',');
        const auto& item = repositories[index];
        result += R"({"id":")" + item.id + R"(","commit":")" + item.commit +
            R"(","root":")" + item.root + R"(","manifest":")" + item.manifest +
            R"(","manifest_sha256":")" + item.manifest_sha256 + R"("})";
    }
    return result + "]}";
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
    const auto view = std::as_bytes(std::span{value.data(), value.size()});
    output.insert(output.end(), view.begin(), view.end());
}

struct ZipItem final {
    std::string name;
    std::vector<std::byte> value;
};

[[nodiscard]] std::vector<std::byte> canonical_store_zip(const std::vector<ZipItem>& items)
{
    struct Central final {
        const ZipItem* item{};
        std::uint32_t offset{};
        std::uint32_t crc{};
    };
    std::vector<std::byte> output;
    std::vector<Central> central;
    for (const auto& item : items) {
        const auto offset = static_cast<std::uint32_t>(output.size());
        const auto crc = static_cast<std::uint32_t>(mz_crc32(
            MZ_CRC32_INIT, reinterpret_cast<const mz_uint8*>(item.value.data()),
            item.value.size()));
        append_u32(output, 0x04034b50U);
        append_u16(output, 10U);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u32(output, crc);
        append_u32(output, static_cast<std::uint32_t>(item.value.size()));
        append_u32(output, static_cast<std::uint32_t>(item.value.size()));
        append_u16(output, static_cast<std::uint16_t>(item.name.size()));
        append_u16(output, 0U);
        append_text(output, item.name);
        output.insert(output.end(), item.value.begin(), item.value.end());
        central.push_back({&item, offset, crc});
    }
    const auto central_offset = static_cast<std::uint32_t>(output.size());
    for (const auto& entry : central) {
        append_u32(output, 0x02014b50U);
        append_u16(output, 20U);
        append_u16(output, 10U);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u32(output, entry.crc);
        append_u32(output, static_cast<std::uint32_t>(entry.item->value.size()));
        append_u32(output, static_cast<std::uint32_t>(entry.item->value.size()));
        append_u16(output, static_cast<std::uint16_t>(entry.item->name.size()));
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u16(output, 0U);
        append_u32(output, 0U);
        append_u32(output, entry.offset);
        append_text(output, entry.item->name);
    }
    const auto central_size = static_cast<std::uint32_t>(output.size()) - central_offset;
    append_u32(output, 0x06054b50U);
    append_u16(output, 0U);
    append_u16(output, 0U);
    append_u16(output, static_cast<std::uint16_t>(items.size()));
    append_u16(output, static_cast<std::uint16_t>(items.size()));
    append_u32(output, central_size);
    append_u32(output, central_offset);
    append_u16(output, 0U);
    return output;
}

[[nodiscard]] std::vector<std::byte> png_fixture()
{
    cv::Mat image(2, 2, CV_8UC3);
    image.at<cv::Vec3b>(0, 0) = {10, 20, 30};
    image.at<cv::Vec3b>(0, 1) = {40, 50, 60};
    image.at<cv::Vec3b>(1, 0) = {70, 80, 90};
    image.at<cv::Vec3b>(1, 1) = {100, 110, 120};
    std::vector<unsigned char> encoded;
    if (!cv::imencode(".png", image, encoded))
        throw std::runtime_error("fixture PNG encode failed");
    std::vector<std::byte> result(encoded.size());
    std::memcpy(result.data(), encoded.data(), encoded.size());
    return result;
}

[[nodiscard]] std::vector<std::byte> support_archive()
{
    const auto graph = bytes(
        R"({"schema":"baas.co-detect-feature-graph/v1","features":[{"name":"rgb-hit","type":"rgb","member":"rgb/hit"},{"name":"image-hit","type":"image","member":"image/hit","crop":[0,0,4,4],"threshold_milli":800,"mean_rgb_tolerance":20}]})");
    const auto rgb = bytes(
        R"({"schema":"baas.co-detect-rgb-ranges/v1","samples":[{"x":0,"y":0,"r":[30,30],"g":[20,20],"b":[10,10]}]})");
    const auto png = png_fixture();
    const auto payload_size = graph.size() + rgb.size() + png.size();
    const std::string manifest =
        R"({"schema":"baas.co-detect-support-bundle/v1","format_version":1,"bundle_id":"procedure-support/navigation.to-main-page/v1","locale":"JP","profile":"JP","member_count":3,"payload_size":)" +
        std::to_string(payload_size) +
        R"(,"members":[{"id":"feature/navigation.to-main-page","kind":"feature-graph","media_type":"application/vnd.baas.co-detect-feature-graph.v1+json","size":)" +
        std::to_string(graph.size()) + R"(,"sha256":")" + sha256(graph) +
        R"("},{"id":"rgb/hit","kind":"rgb-range-set","media_type":"application/vnd.baas.co-detect-rgb-ranges.v1+json","size":)" +
        std::to_string(rgb.size()) + R"(,"sha256":")" + sha256(rgb) +
        R"("},{"id":"image/hit","kind":"png-template","media_type":"image/png","size":)" +
        std::to_string(png.size()) + R"(,"sha256":")" + sha256(png) + R"("}]})";
    return canonical_store_zip({
        {"bundle.magic", {procedure::co_detect_support_bundle_magic.begin(),
                           procedure::co_detect_support_bundle_magic.end()}},
        {"manifest.json", bytes(manifest)},
        {"m00000000", graph},
        {"m00000001", rgb},
        {"m00000002", png},
    });
}

}  // namespace

std::shared_ptr<const baas::runtime::procedure::CoDetectSupportBundle>
make_co_detect_production_test_bundle()
{
    TemporaryDirectory temporary;
    auto archive = support_archive();
    const std::string archive_path = "payload/support.bundle";
    const std::string resource_manifest =
        R"({"schema":"baas.resources/v1","entries":[{"id":"procedure-support/navigation.to-main-page/v1","path":")" +
        archive_path + R"(","media_type":")" +
        std::string{procedure::co_detect_support_bundle_media_type} + R"(","size":)" +
        std::to_string(archive.size()) + R"(,"sha256":")" + sha256(archive) +
        R"(","locale":"JP"}]})";
    const std::vector<File> resource_files{
        {std::string{runtime_resources::runtime_resource_manifest_path}, bytes(resource_manifest)},
        {archive_path, archive},
    };
    const std::vector<File> script_files{{"placeholder.baas", bytes("let ready = true;\n")}};
    const auto resource_tree = tree_manifest(resource_files);
    const auto script_tree = tree_manifest(script_files);
    const std::string resource_commit(40, '1');
    const auto script_commit = sha256(bytes(script_tree)).substr(0, 40);
    std::array<repository::RuntimeRepository, 2> repositories{{
        {"resources", resource_commit, "objects/resources/" + resource_commit,
         "manifest.json", sha256(bytes(resource_tree))},
        {"scripts", script_commit, "objects/scripts/" + script_commit,
         "manifest.json", sha256(bytes(script_tree))},
    }};
    for (const auto& file : resource_files)
        write_file(temporary.path() / repositories[0].root / file.path, file.value);
    for (const auto& file : script_files)
        write_file(temporary.path() / repositories[1].root / file.path, file.value);
    write_file(temporary.path() / repositories[0].root / repositories[0].manifest,
               resource_tree);
    write_file(temporary.path() / repositories[1].root / repositories[1].manifest,
               script_tree);
    const auto generation = repository::runtime_repository_generation(repositories);
    write_file(temporary.path() / "snapshots" / (generation + ".json"),
               snapshot_json(repositories, generation));
    write_file(temporary.path() / "current.json",
               R"({"schema":"baas.runtime-repositories.current/v1","generation":")" +
                   generation + R"(","snapshot":"snapshots/)" + generation + ".json\"}");
    const auto snapshot = repository::RuntimeRepositorySnapshot::activate(temporary.path());
    const auto read_bundle = snapshot->open_read_bundle();
    const auto loaded_resources = runtime_resources::load_runtime_resource_snapshot(
        read_bundle->resources(), {"JP", std::nullopt});
    if (!loaded_resources) throw std::runtime_error("resource activation fixture failed");
    auto loaded = procedure::load_co_detect_support_bundle(
        loaded_resources.activation, loaded_resources.activation->generation(),
        "procedure-support/navigation.to-main-page/v1", "JP", procedure::CoDetectProfile::jp);
    if (!loaded) throw std::runtime_error("support bundle fixture failed");
    return std::move(loaded.bundle);
}
