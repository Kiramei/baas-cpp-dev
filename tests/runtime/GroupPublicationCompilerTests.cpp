#include "resources/ResourceSnapshot.h"
#include "runtime/procedure/CoDetectSupportBundle.h"
#include "runtime/publisher/GroupPublicationCompiler.h"
#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "runtime/resources/RuntimeResourceSnapshotLoader.h"

#include <git2.h>
#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace publisher = baas::runtime::publisher;
namespace procedure = baas::runtime::procedure;
namespace repository = baas::runtime::repository;
namespace runtime_resources = baas::runtime::resources;
namespace resources = baas::resources;
using Json = nlohmann::ordered_json;

std::atomic<int> failures{};

#if defined(BAAS_GROUP_PUBLICATION_TEST_HOOKS)
enum class IoRaceAction {
    none,
    replace_read_file_with_directory,
    replace_write_file_with_directory,
    replace_parent_directory,
};
IoRaceAction io_race_action{};
std::filesystem::path io_race_target;
std::filesystem::path io_race_backup;
bool io_race_invoked{};

void publication_io_race_hook(const std::string_view phase)
{
    const bool selected =
        (io_race_action == IoRaceAction::replace_read_file_with_directory &&
         phase == "before-read-open") ||
        (io_race_action == IoRaceAction::replace_write_file_with_directory &&
         phase == "before-write-create") ||
        (io_race_action == IoRaceAction::replace_parent_directory &&
         phase == "before-write-create");
    if (!selected) return;
    publisher::set_group_publication_io_test_hook(nullptr);
    io_race_invoked = true;
    std::filesystem::rename(io_race_target, io_race_backup);
    std::filesystem::create_directory(io_race_target);
}

void restore_io_race_target()
{
    publisher::set_group_publication_io_test_hook(nullptr);
    std::error_code ignored;
    std::filesystem::remove_all(io_race_target, ignored);
    std::filesystem::rename(io_race_backup, io_race_target);
    io_race_action = IoRaceAction::none;
}
#endif

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

template <typename Function>
void expect_error(
    Function&& operation, const publisher::PublicationErrorCode expected,
    const std::string_view message)
{
    try {
        operation();
        check(false, message);
    } catch (const publisher::PublicationError& error) {
        check(error.code() == expected, message);
    } catch (...) {
        check(false, message);
    }
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        static std::atomic<unsigned long long> serial{};
        path_ = std::filesystem::temp_directory_path() /
            ("baas-group-publisher-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(serial.fetch_add(1)));
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

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view value)
{
    const auto view = std::as_bytes(std::span{value.data(), value.size()});
    return {view.begin(), view.end()};
}

void write_file(const std::filesystem::path& path, const std::span<const std::byte> value)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("fixture open failed");
    output.write(reinterpret_cast<const char*>(value.data()),
                 static_cast<std::streamsize>(value.size()));
    if (!output) throw std::runtime_error("fixture write failed");
}

void write_file(const std::filesystem::path& path, const std::string_view value)
{
    write_file(path, std::as_bytes(std::span{value.data(), value.size()}));
}

void append_be32(std::vector<std::byte>& output, const std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        output.push_back(static_cast<std::byte>(value >> shift));
}

void png_chunk(
    std::vector<std::byte>& output, const std::string_view type,
    const std::span<const std::byte> payload)
{
    append_be32(output, static_cast<std::uint32_t>(payload.size()));
    const auto type_offset = output.size();
    const auto type_bytes = std::as_bytes(std::span{type.data(), type.size()});
    output.insert(output.end(), type_bytes.begin(), type_bytes.end());
    output.insert(output.end(), payload.begin(), payload.end());
    const auto crc = static_cast<std::uint32_t>(mz_crc32(
        MZ_CRC32_INIT, reinterpret_cast<const mz_uint8*>(output.data() + type_offset),
        type.size() + payload.size()));
    append_be32(output, crc);
}

[[nodiscard]] std::vector<std::byte> png(
    const std::uint32_t width, const std::uint32_t height, const bool varied = true,
    const bool alpha = false, const bool alpha_varied = false,
    const std::size_t idat_chunks = 1)
{
    std::vector<std::byte> output{
        std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'},
        std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};
    std::vector<std::byte> ihdr;
    append_be32(ihdr, width); append_be32(ihdr, height);
    ihdr.insert(ihdr.end(), {std::byte{8}, alpha ? std::byte{6} : std::byte{2}, std::byte{0},
                             std::byte{0}, std::byte{0}});
    png_chunk(output, "IHDR", ihdr);
    const auto channels = alpha ? 4U : 3U;
    std::vector<unsigned char> raw(
        static_cast<std::size_t>(height) * (width * channels + 1U));
    for (std::uint32_t row = 0; row < height; ++row) {
        const auto offset = static_cast<std::size_t>(row) * (width * channels + 1U);
        raw[offset] = 0;
        for (std::uint32_t column = 0; column < width; ++column) {
            raw[offset + 1U + column * channels] =
                varied ? static_cast<unsigned char>(10U + row) : 42U;
            raw[offset + 2U + column * channels] =
                varied ? static_cast<unsigned char>(20U + column) : 42U;
            raw[offset + 3U + column * channels] = varied ? 30U : 42U;
            if (alpha) raw[offset + 4U + column * channels] = alpha_varied
                ? static_cast<unsigned char>(1U + (row + column) % 254U) : 255U;
        }
    }
    mz_ulong compressed_size = mz_compressBound(static_cast<mz_ulong>(raw.size()));
    std::vector<std::byte> compressed(compressed_size);
    if (mz_compress2(reinterpret_cast<unsigned char*>(compressed.data()), &compressed_size,
                     raw.data(), static_cast<mz_ulong>(raw.size()),
                     idat_chunks == 1 ? MZ_BEST_COMPRESSION : MZ_NO_COMPRESSION) != MZ_OK)
        throw std::runtime_error("fixture PNG compression failed");
    compressed.resize(compressed_size);
    if (idat_chunks == 0 || compressed.size() < idat_chunks)
        throw std::runtime_error("fixture PNG IDAT split is invalid");
    std::size_t compressed_offset{};
    for (std::size_t index = 0; index < idat_chunks; ++index) {
        const auto remaining = compressed.size() - compressed_offset;
        const auto remaining_chunks = idat_chunks - index;
        const auto chunk_size = remaining / remaining_chunks;
        png_chunk(output, "IDAT", std::span{compressed}.subspan(
            compressed_offset, chunk_size));
        compressed_offset += chunk_size;
    }
    png_chunk(output, "IEND", {});
    return output;
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

[[nodiscard]] std::string oid_text(const git_oid* oid)
{
    std::array<char, GIT_OID_SHA1_HEXSIZE + 1> text{};
    git_oid_tostr(text.data(), text.size(), oid);
    return text.data();
}

struct Source final {
    std::string path;
    std::vector<std::byte> value;
    std::string oid;
};

class GitFixture final {
public:
    GitFixture()
    {
        if (git_libgit2_init() < 0) throw std::runtime_error("git init failed");
        std::string crop_metadata =
            "prefix = \"main_page\"\npath = \"main_page\"\nx_y_range = {\n"
            "    'news': (0, 0, 3, 3),\n"
            "    'placeholder': (0, 0, 2, 2),\n"
            "    'blank': (0, 0, 2, 2),\n"
            "    'rgb-boundary': (0, 0, 2, 2),\n"
            "    'rgba-boundary': (0, 0, 2, 2),\n"
            "    'alpha-only-variation': (0, 0, 2, 2),\n"
            "    'chunk-limit': (0, 0, 2, 2),\n"
            "    'chunk-over-limit': (0, 0, 2, 2),\n"
            "    'stored-oversized': (0, 0, 2, 2),\n"
            "    'oversized': (0, 0, 2, 2),\n"
            "    # 'missing': (0, 0, 2, 2),\n";
        for (std::size_t index = 0; index < 49; ++index)
            crop_metadata += "    'budget-" + std::to_string(index) +
                "': (0, 0, 2, 2),\n";
        for (std::size_t index = 0; index < 40; ++index)
            crop_metadata += "    'rgba-budget-" + std::to_string(index) +
                "': (0, 0, 2, 2),\n";
        crop_metadata += "}\n";
        sources_ = {
            {"src/rgb_feature/JP.json", bytes(
                R"({"rgb_feature":{"main_page":[[[1,1],[1,1]],[[1,2,3,4,5,6],[1,2,3,4,5,6]]]}})"), {}},
            {"src/images/JP/main_page/news.png", png(2, 2), {}},
            {"src/images/JP/main_page/placeholder.png", png(1, 1), {}},
            {"src/images/JP/main_page/blank.png", png(2, 2, false), {}},
            {"src/images/JP/main_page/rgb-boundary.png", png(1280, 720), {}},
            {"src/images/JP/main_page/rgba-boundary.png", png(1280, 720, true, true), {}},
            {"src/images/JP/main_page/alpha-only-variation.png",
             png(2, 2, false, true, true), {}},
            {"src/images/JP/main_page/chunk-limit.png",
             png(1280, 720, true, false, false, 4'094), {}},
            {"src/images/JP/main_page/chunk-over-limit.png",
             png(1280, 720, true, false, false, 4'095), {}},
            {"src/images/JP/main_page/stored-oversized.png",
             std::vector<std::byte>(4U * 1024U * 1024U + 1U), {}},
            {"src/images/JP/main_page/oversized.png", png(1400, 1000), {}},
            {"src/images/JP/dead/news.png", png(2, 2), {}},
            {"src/images/JP/main_page/missing.png", png(2, 2), {}},
            {"src/images/JP/x_y_range/main_page.py", bytes(crop_metadata), {}},
            {"src/images/JP/x_y_range/dead.py", bytes(
                "if False:\n    prefix = \"dead\"\n    path = \"dead\"\n"
                "    x_y_range = {\n        'news': (0, 0, 2, 2),\n    }\n"), {}},
        };
        for (std::size_t index = 0; index < 49; ++index)
            sources_.push_back({"src/images/JP/main_page/budget-" +
                std::to_string(index) + ".png", png(1280, 720), {}});
        for (std::size_t index = 0; index < 40; ++index)
            sources_.push_back({"src/images/JP/main_page/rgba-budget-" +
                std::to_string(index) + ".png", png(1280, 720, true, true), {}});
        git_repository* raw_repository{};
        const auto root = path_.path().string();
        if (git_repository_init(&raw_repository, root.c_str(), 0) < 0)
            throw std::runtime_error("fixture repository init failed");
        repository_ = GitOwner<git_repository, git_repository_free>{raw_repository};
        for (const auto& source : sources_) write_file(path_.path() / source.path, source.value);
        git_index* raw_index{};
        if (git_repository_index(&raw_index, repository_.get()) < 0)
            throw std::runtime_error("fixture index open failed");
        GitOwner<git_index, git_index_free> index{raw_index};
        for (const auto& source : sources_)
            if (git_index_add_bypath(index.get(), source.path.c_str()) < 0)
                throw std::runtime_error("fixture index add failed");
        git_oid tree_oid{};
        if (git_index_write_tree(&tree_oid, index.get()) < 0 || git_index_write(index.get()) < 0)
            throw std::runtime_error("fixture tree write failed");
        git_tree* raw_tree{};
        if (git_tree_lookup(&raw_tree, repository_.get(), &tree_oid) < 0)
            throw std::runtime_error("fixture tree lookup failed");
        GitOwner<git_tree, git_tree_free> tree{raw_tree};
        git_signature* raw_signature{};
        if (git_signature_new(&raw_signature, "BAAS Test", "test@example.invalid", 1, 0) < 0)
            throw std::runtime_error("fixture signature failed");
        GitOwner<git_signature, git_signature_free> signature{raw_signature};
        git_oid commit_oid{};
        if (git_commit_create(&commit_oid, repository_.get(), "HEAD", signature.get(),
                              signature.get(), nullptr, "fixture", tree.get(), 0, nullptr) < 0)
            throw std::runtime_error("fixture commit failed");
        commit_ = oid_text(&commit_oid);
        for (auto& source : sources_) {
            git_tree_entry* raw_entry{};
            if (git_tree_entry_bypath(&raw_entry, tree.get(), source.path.c_str()) < 0)
                throw std::runtime_error("fixture entry lookup failed");
            GitOwner<git_tree_entry, git_tree_entry_free> entry{raw_entry};
            source.oid = oid_text(git_tree_entry_id(entry.get()));
        }
    }
    ~GitFixture()
    {
        repository_ = GitOwner<git_repository, git_repository_free>{};
        git_libgit2_shutdown();
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_.path(); }
    [[nodiscard]] const std::string& commit() const noexcept { return commit_; }
    [[nodiscard]] const Source& source(const std::string_view path) const
    {
        const auto found = std::ranges::find(sources_, path, &Source::path);
        if (found == sources_.end()) throw std::runtime_error("fixture source absent");
        return *found;
    }
    void dirty_checkout()
    {
        write_file(path_.path() / "src/rgb_feature/JP.json", "dirty\r\ncheckout\r\n");
        write_file(path_.path() / "src/images/JP/main_page/news.png", "not a png");
        write_file(path_.path() / "src/images/JP/x_y_range/main_page.py", "dirty\r\n");
    }
private:
    TemporaryDirectory path_;
    GitOwner<git_repository, git_repository_free> repository_;
    std::vector<Source> sources_;
    std::string commit_;
};

[[nodiscard]] Json source_json(const Source& source)
{
    return Json{{"path", source.path}, {"oid", source.oid},
                {"size", source.value.size()}, {"sha256", resources::sha256_hex(source.value)}};
}

[[nodiscard]] Json lock_json(
    const GitFixture& git, const std::string_view png_path =
        "src/images/JP/main_page/news.png", const std::string_view feature = "main_page_news",
    const std::string_view crop_path = "src/images/JP/x_y_range/main_page.py",
    Json crop_value = Json::array({0, 0, 3, 3}))
{
    const auto& rgb = git.source("src/rgb_feature/JP.json");
    const auto& image = git.source(png_path);
    const auto& crop = git.source(crop_path);
    Json members = Json::array({
        Json{{"id", "feature/navigation.to-main-page"}, {"kind", "feature-graph"}},
        Json{{"id", "rgb/main-page"}, {"kind", "rgb-range-set"},
             {"feature", "main_page"}, {"source", source_json(rgb)},
             {"source_key", "main_page"}},
        Json{{"id", "image/" + std::string{feature}}, {"kind", "png-template"},
             {"feature", feature}, {"source", source_json(image)},
             {"crop_source", source_json(crop)}, {"crop", std::move(crop_value)},
             {"threshold_milli", 800}, {"mean_rgb_tolerance", 20}},
    });
    return Json{
        {"schema", publisher::group_publication_lock_schema},
        {"compiler", Json{{"schema", publisher::group_publication_compiler_schema},
                            {"version", publisher::group_publication_compiler_version}}},
        {"source", Json{{"commit", git.commit()}}},
        {"bundles", Json::array({Json{
            {"bundle_id", "procedure-support/navigation.to-main-page/v1"},
            {"locale", "JP"}, {"profile", "JP"},
            {"output_path", "payload/navigation-JP.bundle"},
            {"member_count", members.size()}, {"members", std::move(members)}}})},
    };
}

[[nodiscard]] publisher::GroupPublicationLock parse(const Json& value)
{
    return publisher::parse_group_publication_lock(value.dump());
}

struct File final { std::string path; std::vector<std::byte> value; };

[[nodiscard]] std::string tree_manifest(std::vector<File> files)
{
    std::ranges::sort(files, {}, &File::path);
    Json entries = Json::array();
    for (const auto& file : files)
        entries.push_back(Json{{"path", file.path}, {"size", std::to_string(file.value.size())},
                               {"sha256", resources::sha256_hex(file.value)},
                               {"mode", "file"}});
    return Json{{"schema", "baas.runtime-repository.tree-manifest/v1"},
                {"entries", std::move(entries)}}.dump();
}

[[nodiscard]] std::shared_ptr<const procedure::CoDetectSupportBundle> activate_bundle(
    const std::vector<publisher::PublicationOutput>& output,
    const std::string_view bundle_id = "procedure-support/navigation.to-main-page/v1",
    const std::string_view locale = "JP",
    const procedure::CoDetectProfile profile = procedure::CoDetectProfile::jp)
{
    TemporaryDirectory root;
    std::vector<File> resource_files;
    for (const auto& item : output) resource_files.push_back({item.relative_path, item.bytes});
    const std::vector<File> script_files{{"placeholder.baas", bytes("let ready = true;\n")}};
    const auto resource_tree = tree_manifest(resource_files);
    const auto script_tree = tree_manifest(script_files);
    const std::string resource_commit(40, '1');
    const std::string script_commit(40, '2');
    std::array<repository::RuntimeRepository, 2> repositories{{
        {"resources", resource_commit, "objects/resources/" + resource_commit,
         "manifest.json", resources::sha256_hex(bytes(resource_tree))},
        {"scripts", script_commit, "objects/scripts/" + script_commit,
         "manifest.json", resources::sha256_hex(bytes(script_tree))},
    }};
    for (const auto& file : resource_files)
        write_file(root.path() / repositories[0].root / file.path, file.value);
    for (const auto& file : script_files)
        write_file(root.path() / repositories[1].root / file.path, file.value);
    write_file(root.path() / repositories[0].root / repositories[0].manifest, resource_tree);
    write_file(root.path() / repositories[1].root / repositories[1].manifest, script_tree);
    const auto generation = repository::runtime_repository_generation(repositories);
    Json repository_entries = Json::array();
    for (const auto& item : repositories)
        repository_entries.push_back(Json{{"id", item.id}, {"commit", item.commit},
            {"root", item.root}, {"manifest", item.manifest},
            {"manifest_sha256", item.manifest_sha256}});
    const auto snapshot = Json{{"schema", "baas.runtime-repositories.snapshot/v1"},
        {"generation", generation}, {"repositories", std::move(repository_entries)}}.dump();
    write_file(root.path() / "snapshots" / (generation + ".json"), snapshot);
    write_file(root.path() / "current.json", Json{
        {"schema", "baas.runtime-repositories.current/v1"}, {"generation", generation},
        {"snapshot", "snapshots/" + generation + ".json"}}.dump());
    auto activated = repository::RuntimeRepositorySnapshot::activate(root.path());
    auto read = activated->open_read_bundle();
    const auto resources_result = runtime_resources::load_runtime_resource_snapshot(
        read->resources(), {std::string{locale}, std::nullopt});
    if (!resources_result) throw std::runtime_error("compiled resource snapshot rejected");
    const auto loaded = procedure::load_co_detect_support_bundle(
        resources_result.activation, generation,
        bundle_id, locale, profile);
    if (!loaded) throw std::runtime_error(
        "compiled support bundle rejected: " +
        std::string{procedure::co_detect_support_bundle_error_name(loaded.error)});
    return loaded.bundle;
}

void test_real_production_lock(
    const std::filesystem::path& source_repository,
    const std::filesystem::path& lock_path)
{
    std::ifstream input(lock_path, std::ios::binary);
    if (!input) throw std::runtime_error("real production lock open failed");
    const std::string lock_text{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
    if (input.bad()) throw std::runtime_error("real production lock read failed");
    auto lock = publisher::parse_group_publication_lock(lock_text);
    publisher::validate_group_production_lock(lock);
    publisher::verify_group_publication_sources(lock, source_repository);
    const auto first = publisher::compile_group_publication(lock, source_repository);
    const auto second = publisher::compile_group_publication(lock, source_repository);
    bool reproducible = first.size() == second.size();
    for (std::size_t index = 0; reproducible && index < first.size(); ++index)
        reproducible = first[index].relative_path == second[index].relative_path &&
            first[index].bytes == second[index].bytes;
    check(reproducible && first.size() == 11,
          "real production publication must contain ten reproducible bundles and manifest");
    struct Expected final {
        std::string_view id;
        std::string_view locale;
        procedure::CoDetectProfile profile;
        std::size_t count;
    };
    constexpr std::array<Expected, 10> expected{{
        {"procedure-support/group.open/v1", "CN", procedure::CoDetectProfile::cn, 16},
        {"procedure-support/group.open/v1", "Global_en-us",
         procedure::CoDetectProfile::global_en_us, 17},
        {"procedure-support/group.open/v1", "Global_ko-kr",
         procedure::CoDetectProfile::global_ko_kr, 13},
        {"procedure-support/group.open/v1", "Global_zh-tw",
         procedure::CoDetectProfile::global_zh_tw, 14},
        {"procedure-support/group.open/v1", "JP", procedure::CoDetectProfile::jp, 12},
        {"procedure-support/navigation.to-main-page/v1", "CN",
         procedure::CoDetectProfile::cn, 63},
        {"procedure-support/navigation.to-main-page/v1", "Global_en-us",
         procedure::CoDetectProfile::global_en_us, 60},
        {"procedure-support/navigation.to-main-page/v1", "Global_ko-kr",
         procedure::CoDetectProfile::global_ko_kr, 56},
        {"procedure-support/navigation.to-main-page/v1", "Global_zh-tw",
         procedure::CoDetectProfile::global_zh_tw, 57},
        {"procedure-support/navigation.to-main-page/v1", "JP",
         procedure::CoDetectProfile::jp, 56},
    }};
    for (const auto& item : expected) {
        const auto bundle = activate_bundle(first, item.id, item.locale, item.profile);
        check(bundle && bundle->member_count() == item.count,
              "every real production variant must activate with its exact closure");
    }
}

void test_compile_reproducible_and_activate()
{
    GitFixture git;
    auto lock = parse(lock_json(git));
    publisher::verify_group_publication_sources(lock, git.path());
    const auto first = publisher::compile_group_publication(lock, git.path());
    const auto second = publisher::compile_group_publication(lock, git.path());
    check(first.size() == 2 && first[0].relative_path == "payload/navigation-JP.bundle" &&
              first[1].relative_path == "baas.resources.json",
          "compiler must emit one bundle and manifest last");
    check(first[0].bytes == second[0].bytes && first[1].bytes == second[1].bytes,
          "two compilations must be byte-identical");
    const auto digest = resources::sha256_hex(first[0].bytes);
    check(digest == "edaa4f617dd3a3938fc4ba1543f61b3f69dab5541029d758a2087b6359039d24",
          "canonical archive digest must stay fixed; actual=" + digest);
    auto bundle = activate_bundle(first);
    check(bundle->member_count() == 3 && bundle->find_rgb("main_page") != nullptr &&
              bundle->find_rgb("main_page")->samples.size() == 2 &&
              bundle->find_image("main_page_news") != nullptr,
          "loader must preserve repeated RGB samples and resized-template metadata");
    expect_error([&] { publisher::validate_group_production_lock(lock); },
        publisher::PublicationErrorCode::incomplete_bundle,
        "generic fixture must not satisfy the production CLI gate");

    git.dirty_checkout();
    const auto dirty = publisher::compile_group_publication(lock, git.path());
    check(dirty[0].bytes == first[0].bytes && dirty[1].bytes == first[1].bytes,
          "dirty and CRLF checkout bytes must not affect ODB compilation");

    TemporaryDirectory publication;
    auto unbound = first;
    unbound[0].bytes.push_back(std::byte{0});
    expect_error([&] { publisher::write_group_publication(
        unbound, publication.path(), false); },
        publisher::PublicationErrorCode::publication_invalid,
        "atomic writer must reject manifest/output disagreement before mutation");
    auto noncanonical = first;
    noncanonical.back().bytes.insert(
        noncanonical.back().bytes.begin(), std::byte{static_cast<unsigned char>(' ')});
    expect_error([&] { publisher::write_group_publication(
        noncanonical, publication.path(), false); },
        publisher::PublicationErrorCode::publication_invalid,
        "atomic writer must reject a non-canonical resource manifest");
    auto case_collision = first;
    case_collision.insert(case_collision.end() - 1, first[0]);
    case_collision[case_collision.size() - 2].relative_path =
        "PAYLOAD/NAVIGATION-jp.BUNDLE";
    expect_error([&] { publisher::write_group_publication(
        case_collision, publication.path(), false); },
        publisher::PublicationErrorCode::publication_invalid,
        "case-folding output collision must fail before mutation");
    publisher::write_group_publication(first, publication.path(), false);
    publisher::write_group_publication(first, publication.path(), true);
    publisher::verify_group_publication(lock, git.path(), publication.path());
#if defined(BAAS_GROUP_PUBLICATION_TEST_HOOKS)
    io_race_action = IoRaceAction::replace_read_file_with_directory;
    io_race_target = publication.path() / first[0].relative_path;
    io_race_backup = publication.path() / "read-race-backup.bundle";
    io_race_invoked = false;
    publisher::set_group_publication_io_test_hook(&publication_io_race_hook);
    expect_error([&] { publisher::write_group_publication(
        first, publication.path(), true); },
        publisher::PublicationErrorCode::publication_mismatch,
        "anchored read must reject a deterministic final-component replacement race");
    restore_io_race_target();
    check(io_race_invoked, "anchored read race hook must run");
    io_race_action = IoRaceAction::replace_write_file_with_directory;
    io_race_target = publication.path() / first[0].relative_path;
    io_race_backup = publication.path() / "write-race-backup.bundle";
    io_race_invoked = false;
    publisher::set_group_publication_io_test_hook(&publication_io_race_hook);
    expect_error([&] { publisher::write_group_publication(
        first, publication.path(), false); },
        publisher::PublicationErrorCode::output_io_failed,
        "anchored writer must reject a deterministic destination replacement race");
    restore_io_race_target();
    check(io_race_invoked, "anchored write destination race hook must run");
#ifndef _WIN32
    io_race_action = IoRaceAction::replace_parent_directory;
    io_race_target = publication.path() / "payload";
    io_race_backup = publication.path() / "payload-race-backup";
    io_race_invoked = false;
    publisher::set_group_publication_io_test_hook(&publication_io_race_hook);
    expect_error([&] { publisher::write_group_publication(
        first, publication.path(), false); },
        publisher::PublicationErrorCode::publication_mismatch,
        "anchored writer must not escape through a replaced parent directory");
    restore_io_race_target();
    check(io_race_invoked, "anchored write race hook must run");
#endif
#endif
#ifndef _WIN32
    std::filesystem::create_symlink(
        publication.path() / first[0].relative_path, publication.path() / "linked.bundle");
    expect_error([&] { publisher::write_group_publication(
        first, publication.path(), false); },
        publisher::PublicationErrorCode::publication_mismatch,
        "writer must reject a symlink before mutation");
    std::filesystem::remove(publication.path() / "linked.bundle");
#endif
    write_file(publication.path() / "undeclared.bin", "extra");
    expect_error([&] { publisher::write_group_publication(
        first, publication.path(), false); },
        publisher::PublicationErrorCode::publication_mismatch,
        "write mode must reject stale undeclared publication files before mutation");
    expect_error([&] { publisher::write_group_publication(
        first, publication.path(), true); },
        publisher::PublicationErrorCode::publication_mismatch,
        "--check must reject undeclared publication files");
    std::filesystem::remove(publication.path() / "undeclared.bin");
    auto corrupt = first[0].bytes;
    corrupt.push_back(std::byte{0});
    write_file(publication.path() / first[0].relative_path, corrupt);
    expect_error([&] { publisher::verify_group_publication(
        lock, git.path(), publication.path()); },
        publisher::PublicationErrorCode::publication_mismatch,
        "trailing archive bytes must fail publication verification");
}

void test_lock_and_source_negatives()
{
    GitFixture git;
    auto valid = lock_json(git);
    auto oversized = valid;
    oversized["bundles"][0]["members"][1]["source"]["size"] =
        16U * 1024U * 1024U + 1U;
    expect_error([&] { static_cast<void>(parse(oversized)); },
        publisher::PublicationErrorCode::invalid_lock,
        "source member over the consumer-aligned limit must fail");
    auto duplicate = valid.dump();
    duplicate.insert(1, R"("schema":"baas.group-publication-lock/v1",)");
    expect_error([&] { static_cast<void>(
        publisher::parse_group_publication_lock(duplicate)); },
        publisher::PublicationErrorCode::invalid_lock,
        "duplicate strict JSON key must fail");

    auto wrong_commit = valid;
    wrong_commit["source"]["commit"] = std::string(40, '0');
    auto lock = parse(wrong_commit);
    expect_error([&] { publisher::verify_group_publication_sources(lock, git.path()); },
        publisher::PublicationErrorCode::commit_mismatch, "wrong commit must fail");

    for (const auto& [field, replacement, expected] : std::array{
             std::tuple{"oid", Json(std::string(40, '0')),
                        publisher::PublicationErrorCode::source_oid_mismatch},
             std::tuple{"size", Json(999U),
                        publisher::PublicationErrorCode::source_size_mismatch},
             std::tuple{"sha256", Json(std::string(64, '0')),
                        publisher::PublicationErrorCode::source_digest_mismatch}}) {
        auto changed = valid;
        changed["bundles"][0]["members"][1]["source"][field] = replacement;
        auto changed_lock = parse(changed);
        expect_error([&] { publisher::verify_group_publication_sources(
            changed_lock, git.path()); }, expected, "wrong blob metadata must fail");
    }

    auto incomplete = valid;
    incomplete["bundles"][0]["members"].erase(
        incomplete["bundles"][0]["members"].begin() + 2);
    incomplete["bundles"][0]["member_count"] = 2;
    expect_error([&] { static_cast<void>(parse(incomplete)); },
        publisher::PublicationErrorCode::incomplete_bundle,
        "missing member closure must fail");

    auto alias = valid;
    alias["bundles"][0]["members"][2]["feature"] = "main_page_news2";
    alias["bundles"][0]["members"][2]["id"] = "image/main_page_news2";
    auto alias_lock = parse(alias);
    expect_error([&] { publisher::verify_group_publication_sources(
        alias_lock, git.path()); }, publisher::PublicationErrorCode::alias_forbidden,
        "alias repair must fail");

    auto fallback = valid;
    fallback["bundles"][0]["members"][2]["source"]["path"] =
        "src/images/CN/main_page/news.png";
    expect_error([&] { static_cast<void>(parse(fallback)); },
        publisher::PublicationErrorCode::locale_fallback_forbidden,
        "cross-locale fallback must fail");

    auto nondefault_matcher = valid;
    nondefault_matcher["bundles"][0]["members"][2]["threshold_milli"] = 801;
    expect_error([&] { static_cast<void>(parse(nondefault_matcher)); },
        publisher::PublicationErrorCode::invalid_lock,
        "support bundle matcher defaults must remain exactly 800 and 20");

    auto placeholder = lock_json(
        git, "src/images/JP/main_page/placeholder.png", "main_page_placeholder",
        "src/images/JP/x_y_range/main_page.py", Json::array({0, 0, 2, 2}));
    auto placeholder_lock = parse(placeholder);
    expect_error([&] { publisher::verify_group_publication_sources(
        placeholder_lock, git.path()); },
        publisher::PublicationErrorCode::placeholder_forbidden,
        "placeholder dimensions must fail");

    auto blank = lock_json(
        git, "src/images/JP/main_page/blank.png", "main_page_blank",
        "src/images/JP/x_y_range/main_page.py", Json::array({0, 0, 2, 2}));
    auto blank_lock = parse(blank);
    expect_error([&] { publisher::verify_group_publication_sources(
        blank_lock, git.path()); }, publisher::PublicationErrorCode::placeholder_forbidden,
        "same-size blank placeholder must fail");

    auto alpha_only = lock_json(
        git, "src/images/JP/main_page/alpha-only-variation.png",
        "main_page_alpha-only-variation", "src/images/JP/x_y_range/main_page.py",
        Json::array({0, 0, 2, 2}));
    auto alpha_only_lock = parse(alpha_only);
    expect_error([&] { publisher::verify_group_publication_sources(
        alpha_only_lock, git.path()); }, publisher::PublicationErrorCode::placeholder_forbidden,
        "alpha-only variation must not make constant visible RGB a real template");

    auto chunk_limit = lock_json(
        git, "src/images/JP/main_page/chunk-limit.png", "main_page_chunk-limit",
        "src/images/JP/x_y_range/main_page.py", Json::array({0, 0, 2, 2}));
    auto chunk_limit_lock = parse(chunk_limit);
    const auto chunk_limit_outputs = publisher::compile_group_publication(
        chunk_limit_lock, git.path());
    check(activate_bundle(chunk_limit_outputs)->find_image("main_page_chunk-limit") != nullptr,
          "4096 total PNG chunks must compile and activate through the consumer");

    auto chunk_over_limit = lock_json(
        git, "src/images/JP/main_page/chunk-over-limit.png",
        "main_page_chunk-over-limit", "src/images/JP/x_y_range/main_page.py",
        Json::array({0, 0, 2, 2}));
    auto chunk_over_limit_lock = parse(chunk_over_limit);
    expect_error([&] { publisher::verify_group_publication_sources(
        chunk_over_limit_lock, git.path()); }, publisher::PublicationErrorCode::resource_exhausted,
        "4097 total PNG chunks must fail before the next IDAT append");

    auto oversized_png = lock_json(
        git, "src/images/JP/main_page/oversized.png", "main_page_oversized",
        "src/images/JP/x_y_range/main_page.py", Json::array({0, 0, 2, 2}));
    auto oversized_png_lock = parse(oversized_png);
    expect_error([&] { publisher::verify_group_publication_sources(
        oversized_png_lock, git.path()); }, publisher::PublicationErrorCode::resource_exhausted,
        "PNG dimensions or decoded pixels over the consumer limit must fail before allocation");

    auto stored_oversized_png = lock_json(
        git, "src/images/JP/main_page/stored-oversized.png",
        "main_page_stored-oversized", "src/images/JP/x_y_range/main_page.py",
        Json::array({0, 0, 2, 2}));
    auto stored_oversized_png_lock = parse(stored_oversized_png);
    expect_error([&] { publisher::verify_group_publication_sources(
        stored_oversized_png_lock, git.path()); },
        publisher::PublicationErrorCode::resource_exhausted,
        "stored PNG over the fixed 4 MiB limit must fail closed");

    for (const auto& [path, feature] : std::array{
             std::pair{"src/images/JP/main_page/rgb-boundary.png",
                       "main_page_rgb-boundary"},
             std::pair{"src/images/JP/main_page/rgba-boundary.png",
                       "main_page_rgba-boundary"},
         }) {
        auto boundary = lock_json(
            git, path, feature, "src/images/JP/x_y_range/main_page.py",
            Json::array({0, 0, 2, 2}));
        auto boundary_lock = parse(boundary);
        publisher::verify_group_publication_sources(boundary_lock, git.path());
    }

    auto rgba_cumulative = valid;
    constexpr std::uint64_t decoded_limit = 128ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t boundary_rgb_bytes = 1'280ULL * 720ULL * 3ULL;
    constexpr std::uint64_t boundary_rgba_scanlines = 720ULL * (1'280ULL * 4ULL + 1ULL);
    static_assert(40ULL * boundary_rgb_bytes < decoded_limit);
    static_assert(40ULL * boundary_rgba_scanlines > decoded_limit);
    Json rgba_cumulative_members = Json::array({
        valid["bundles"][0]["members"][0], valid["bundles"][0]["members"][1]});
    std::vector<Json> rgba_cumulative_images;
    const auto crop_source = source_json(
        git.source("src/images/JP/x_y_range/main_page.py"));
    for (std::size_t index = 0; index < 40; ++index) {
        const auto name = "rgba-budget-" + std::to_string(index);
        const auto feature = "main_page_" + name;
        rgba_cumulative_images.push_back(Json{
            {"id", "image/" + feature}, {"kind", "png-template"},
            {"feature", feature},
            {"source", source_json(git.source(
                "src/images/JP/main_page/" + name + ".png"))},
            {"crop_source", crop_source}, {"crop", Json::array({0, 0, 2, 2})},
            {"threshold_milli", 800}, {"mean_rgb_tolerance", 20}});
    }
    std::ranges::sort(rgba_cumulative_images, {}, [](const Json& member) {
        return member.at("id").get<std::string>();
    });
    for (auto& member : rgba_cumulative_images)
        rgba_cumulative_members.push_back(std::move(member));
    rgba_cumulative["bundles"][0]["member_count"] = rgba_cumulative_members.size();
    rgba_cumulative["bundles"][0]["members"] = std::move(rgba_cumulative_members);
    auto rgba_cumulative_lock = parse(rgba_cumulative);
    publisher::verify_group_publication_sources(rgba_cumulative_lock, git.path());

    auto cumulative = valid;
    static_assert(48ULL * boundary_rgb_bytes < decoded_limit);
    static_assert(49ULL * boundary_rgb_bytes > decoded_limit);
    Json cumulative_members = Json::array({
        valid["bundles"][0]["members"][0], valid["bundles"][0]["members"][1]});
    std::vector<Json> cumulative_images;
    for (std::size_t index = 0; index < 49; ++index) {
        const auto name = "budget-" + std::to_string(index);
        const auto feature = "main_page_" + name;
        cumulative_images.push_back(Json{
            {"id", "image/" + feature}, {"kind", "png-template"},
            {"feature", feature},
            {"source", source_json(git.source(
                "src/images/JP/main_page/" + name + ".png"))},
            {"crop_source", crop_source}, {"crop", Json::array({0, 0, 2, 2})},
            {"threshold_milli", 800}, {"mean_rgb_tolerance", 20}});
    }
    std::ranges::sort(cumulative_images, {}, [](const Json& member) {
        return member.at("id").get<std::string>();
    });
    for (auto& member : cumulative_images)
        cumulative_members.push_back(std::move(member));
    cumulative["bundles"][0]["member_count"] = cumulative_members.size();
    cumulative["bundles"][0]["members"] = std::move(cumulative_members);
    auto cumulative_lock = parse(cumulative);
    expect_error([&] { publisher::verify_group_publication_sources(
        cumulative_lock, git.path()); }, publisher::PublicationErrorCode::resource_exhausted,
        "cumulative decoded PNG work over 128 MiB must fail closed");

    auto dead = lock_json(git, "src/images/JP/dead/news.png", "dead_news",
                          "src/images/JP/x_y_range/dead.py", Json::array({0, 0, 2, 2}));
    auto dead_lock = parse(dead);
    expect_error([&] { publisher::verify_group_publication_sources(
        dead_lock, git.path()); }, publisher::PublicationErrorCode::source_content_invalid,
        "dead-code crop declarations must not become active resources");

    auto rgb_alias = valid;
    rgb_alias["bundles"][0]["members"][1]["id"] = "rgb/not-main-page";
    expect_error([&] { static_cast<void>(parse(rgb_alias)); },
        publisher::PublicationErrorCode::alias_forbidden,
        "RGB member identity must be bound to its exact feature");

    auto missing_crop = lock_json(
        git, "src/images/JP/main_page/missing.png", "main_page_missing");
    auto missing_crop_lock = parse(missing_crop);
    expect_error([&] { publisher::verify_group_publication_sources(
        missing_crop_lock, git.path()); },
        publisher::PublicationErrorCode::source_content_invalid,
        "commented-out crop must stay absent");
}

}  // namespace

int main(const int argc, char** argv)
{
    try {
        if (argc == 5 && std::string_view{argv[1]} == "--real-repository" &&
            std::string_view{argv[3]} == "--lock") {
            test_real_production_lock(argv[2], argv[4]);
        } else if (argc == 1) {
            test_compile_reproducible_and_activate();
            test_lock_and_source_negatives();
        } else {
            throw std::runtime_error("invalid group publication test arguments");
        }
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "UNEXPECTED: " << error.what() << '\n';
    }
    if (failures.load() != 0) {
        std::cerr << failures.load() << " group publication compiler test(s) failed\n";
        return 1;
    }
    std::cout << "group publication compiler tests passed\n";
    return 0;
}
