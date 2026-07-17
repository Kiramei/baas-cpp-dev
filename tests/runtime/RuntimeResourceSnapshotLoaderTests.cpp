#include "resources/ResourceSnapshot.h"
#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "runtime/resources/RuntimeResourceSnapshotLoader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace repository = baas::runtime::repository;
namespace loader = baas::runtime::resources;
namespace resources = baas::resources;

std::atomic<int> failures{};
std::stop_source* read_cancellation{};

void check(const bool condition, const std::string_view message) {
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

class TemporaryDirectory final {
  public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("baas-runtime-resource-loader-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::span<const std::byte> value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("could not create fixture file");
    output.write(reinterpret_cast<const char*>(value.data()),
                 static_cast<std::streamsize>(value.size()));
    if (!output)
        throw std::runtime_error("could not write fixture file");
}

void write_file(const std::filesystem::path& path, const std::string_view value) {
    write_file(path, std::as_bytes(std::span{value.data(), value.size()}));
}

[[nodiscard]] std::string sha256(const std::span<const std::byte> bytes) {
    return resources::sha256_hex(bytes);
}

[[nodiscard]] std::string sha256(const std::string_view value) {
    return sha256(std::as_bytes(std::span{value.data(), value.size()}));
}

struct FixtureFile final {
    std::string path;
    std::vector<std::byte> bytes;
};

[[nodiscard]] std::vector<std::byte> runtime_payload(const unsigned seed) {
    std::vector<std::byte> result(257);
    auto value = seed;
    for (auto& byte : result) {
        value = value * 1664525U + 1013904223U;
        byte = static_cast<std::byte>((value >> 19U) & 0xffU);
    }
    return result;
}

[[nodiscard]] std::string tree_manifest(std::vector<FixtureFile> files) {
    std::ranges::sort(files, {}, &FixtureFile::path);
    std::string result = R"({"schema":"baas.runtime-repository.tree-manifest/v1","entries":[)";
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (index != 0)
            result.push_back(',');
        result += R"({"path":")" + files[index].path + R"(","size":")" +
                  std::to_string(files[index].bytes.size()) + R"(","sha256":")" +
                  sha256(files[index].bytes) + R"(","mode":"file"})";
    }
    result += "]}";
    return result;
}

[[nodiscard]] std::string
snapshot_json(const std::array<repository::RuntimeRepository, 2>& repositories,
              const std::string_view generation) {
    std::string result = R"({"schema":"baas.runtime-repositories.snapshot/v1","generation":")" +
                         std::string{generation} + R"(","repositories":[)";
    for (std::size_t index = 0; index < repositories.size(); ++index) {
        if (index != 0)
            result.push_back(',');
        const auto& item = repositories[index];
        result += R"({"id":")" + item.id + R"(","commit":")" + item.commit + R"(","root":")" +
                  item.root + R"(","manifest":")" + item.manifest + R"(","manifest_sha256":")" +
                  item.manifest_sha256 + R"("})";
    }
    result += "]}";
    return result;
}

[[nodiscard]] FixtureFile text_file(std::string path, const std::string_view value) {
    const auto bytes = std::as_bytes(std::span{value.data(), value.size()});
    return {std::move(path), {bytes.begin(), bytes.end()}};
}

class RepositoryFixture final {
  public:
    RepositoryFixture(std::string package_manifest, std::vector<FixtureFile> payloads)
        : package_manifest_(std::move(package_manifest)), payloads_(std::move(payloads)) {
        std::vector<FixtureFile> resource_files = payloads_;
        resource_files.push_back(
            text_file(std::string{loader::runtime_resource_manifest_path}, package_manifest_));
        const std::vector<FixtureFile> scripts{
            text_file("placeholder.baas", "let placeholder = true;\n")};
        const auto resource_tree = tree_manifest(resource_files);
        const auto script_tree = tree_manifest(scripts);
        const std::string resource_commit(40, '1');
        const std::string script_commit(64, '2');
        repositories_ = {{
            {"resources", resource_commit, "objects/resources/" + resource_commit, "manifest.json",
             sha256(resource_tree)},
            {"scripts", script_commit, "objects/scripts/" + script_commit, "scripts.json",
             sha256(script_tree)},
        }};
        for (const auto& file : resource_files)
            write_file(root() / file.path, file.bytes);
        for (const auto& file : scripts)
            write_file(temporary_.path() / repositories_[1].root / file.path, file.bytes);
        write_file(root() / repositories_[0].manifest, resource_tree);
        write_file(temporary_.path() / repositories_[1].root / repositories_[1].manifest,
                   script_tree);
        generation_ = repository::runtime_repository_generation(repositories_);
        write_file(temporary_.path() / "snapshots" / (generation_ + ".json"),
                   snapshot_json(repositories_, generation_));
        write_file(temporary_.path() / "current.json",
                   R"({"schema":"baas.runtime-repositories.current/v1","generation":")" +
                       generation_ + R"(","snapshot":"snapshots/)" + generation_ + ".json\"}");
        snapshot_ = repository::RuntimeRepositorySnapshot::activate(temporary_.path());
        bundle_ = snapshot_->open_read_bundle();
    }

    [[nodiscard]] const repository::RuntimeRepositoryReadView& resources() const {
        return bundle_->resources();
    }
    [[nodiscard]] const repository::RuntimeRepositoryReadView& scripts() const {
        return bundle_->scripts();
    }
    [[nodiscard]] std::filesystem::path root() const {
        return temporary_.path() / repositories_[0].root;
    }
    void erase_payload(const std::string_view path) {
        std::filesystem::remove(root() / std::string{path});
    }
    void replace_payload(const std::string_view path, const std::span<const std::byte> bytes) {
        write_file(root() / std::string{path}, bytes);
    }

  private:
    TemporaryDirectory temporary_;
    std::string package_manifest_;
    std::vector<FixtureFile> payloads_;
    std::array<repository::RuntimeRepository, 2> repositories_;
    std::string generation_;
    std::shared_ptr<const repository::RuntimeRepositorySnapshot> snapshot_;
    std::shared_ptr<const repository::RuntimeRepositoryReadBundle> bundle_;
};

struct Entry final {
    std::string id;
    std::string path;
    std::string media_type{"application/octet-stream"};
    std::vector<std::byte> bytes;
    std::string locale;
    std::string activity;
};

[[nodiscard]] std::string package_manifest(const std::vector<Entry>& entries) {
    std::string result = R"({"schema":"baas.resources/v1","entries":[)";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (index != 0)
            result.push_back(',');
        const auto& entry = entries[index];
        result += R"({"id":")" + entry.id + R"(","path":")" + entry.path + R"(","media_type":")" +
                  entry.media_type + R"(","size":)" + std::to_string(entry.bytes.size()) +
                  R"(,"sha256":")" + sha256(entry.bytes) + '"';
        if (!entry.locale.empty())
            result += R"(,"locale":")" + entry.locale + '"';
        if (!entry.activity.empty())
            result += R"(,"activity":")" + entry.activity + '"';
        result += '}';
    }
    result += "]}";
    return result;
}

[[nodiscard]] std::vector<FixtureFile> fixture_files(const std::vector<Entry>& entries) {
    std::vector<FixtureFile> result;
    for (const auto& entry : entries)
        result.push_back({entry.path, entry.bytes});
    return result;
}

void expect_error(const loader::RuntimeResourceSnapshotLoadResult& result,
                  const loader::RuntimeResourceSnapshotLoadError error,
                  const std::string_view message) {
    check(!result, message);
    check(!result.snapshot, message);
    check(result.error == error, message);
}

[[nodiscard]] std::vector<Entry> valid_entries() {
    return {
        {"image/menu", "payload/neutral.bin", "application/octet-stream", runtime_payload(1)},
        {"image/menu", "payload/cn.bin", "application/octet-stream", runtime_payload(2), "CN"},
        {"image/menu", "payload/cn-event.bin", "application/octet-stream", runtime_payload(3), "CN",
         "Event"},
        {"image/menu", "payload/event.bin", "application/octet-stream", runtime_payload(4), "",
         "Event"},
    };
}

void test_success_selector_and_owned_lifetime() {
    const auto entries = valid_entries();
    RepositoryFixture fixture{package_manifest(entries), fixture_files(entries)};
    const auto loaded =
        loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", "Event"});
    check(static_cast<bool>(loaded), "valid package must load");
    if (!loaded)
        return;
    check(loaded.snapshot->entry_count() == entries.size(), "all variants must be retained");
    const auto exact = loaded.snapshot->resolve("image/menu");
    check(exact && exact->sha256() == sha256(entries[2].bytes),
          "selector must prefer locale plus activity");
    const auto override = loaded.snapshot->resolve("image/menu", "JP");
    check(override && override->sha256() == sha256(entries[3].bytes),
          "locale override must retain activity fallback");
    for (const auto& entry : entries)
        fixture.erase_payload(entry.path);
    check(exact && std::ranges::equal(exact->bytes(), entries[2].bytes),
          "snapshot bytes must survive repository deletion");
}

void test_exact_schema_and_manifest_identity() {
    const auto entries = valid_entries();
    const auto files = fixture_files(entries);
    for (const auto manifest :
         {R"({"schema":"baas.resources/v1","entries":[],"extra":true})",
          R"({"schema":"baas.resources/v1"})",
          R"({"schema":"baas.resources/v1","schema":"baas.resources/v1","entries":[]})",
          R"({"schema":"baas.resources/v2","entries":[]})"}) {
        RepositoryFixture fixture{std::string{manifest}, files};
        expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}),
                     loader::RuntimeResourceSnapshotLoadError::invalid_manifest,
                     "top-level field set, duplicates, and schema must be exact");
    }

    auto unknown = package_manifest({entries.front()});
    unknown.insert(unknown.find("}"), R"(,"extra":0)");
    RepositoryFixture unknown_fixture{unknown, fixture_files({entries.front()})};
    expect_error(loader::load_runtime_resource_snapshot(unknown_fixture.resources(), {"CN", {}}),
                 loader::RuntimeResourceSnapshotLoadError::invalid_manifest,
                 "entry field set must be exact");

    auto wrong_size = package_manifest({entries.front()});
    const auto size_position = wrong_size.find(R"("size":257)");
    wrong_size.replace(size_position, std::string_view{R"("size":257)"}.size(), R"("size":256)");
    RepositoryFixture size_fixture{wrong_size, fixture_files({entries.front()})};
    expect_error(loader::load_runtime_resource_snapshot(size_fixture.resources(), {"CN", {}}),
                 loader::RuntimeResourceSnapshotLoadError::manifest_entry_mismatch,
                 "package size must match the tree manifest");

    auto wrong_digest = package_manifest({entries.front()});
    const auto digest_position = wrong_digest.find(sha256(entries.front().bytes));
    wrong_digest[digest_position] = wrong_digest[digest_position] == '0' ? '1' : '0';
    RepositoryFixture digest_fixture{wrong_digest, fixture_files({entries.front()})};
    expect_error(loader::load_runtime_resource_snapshot(digest_fixture.resources(), {"CN", {}}),
                 loader::RuntimeResourceSnapshotLoadError::manifest_entry_mismatch,
                 "package digest must match the tree manifest");
}

void test_duplicate_case_path_and_read_view_tamper() {
    const auto entries = valid_entries();
    auto duplicate = std::vector<Entry>{entries[0], entries[0]};
    duplicate[1].path = "payload/duplicate.bin";
    RepositoryFixture duplicate_fixture{package_manifest(duplicate), fixture_files(duplicate)};
    expect_error(loader::load_runtime_resource_snapshot(duplicate_fixture.resources(), {"CN", {}}),
                 loader::RuntimeResourceSnapshotLoadError::duplicate_entry,
                 "duplicate id/locale/activity variants must fail");

    auto case_path = entries.front();
    const auto real_files = fixture_files({case_path});
    case_path.path = "Payload/neutral.bin";
    RepositoryFixture case_fixture{package_manifest({case_path}), real_files};
    expect_error(loader::load_runtime_resource_snapshot(case_fixture.resources(), {"CN", {}}),
                 loader::RuntimeResourceSnapshotLoadError::path_not_manifested,
                 "path lookup must be exact and case-sensitive");

    auto case_id = entries.front();
    case_id.id = "Image/menu";
    RepositoryFixture case_id_fixture{package_manifest({case_id}), fixture_files({case_id})};
    expect_error(loader::load_runtime_resource_snapshot(case_id_fixture.resources(), {"CN", {}}),
                 loader::RuntimeResourceSnapshotLoadError::snapshot_validation_failed,
                 "resource ids must retain canonical lowercase spelling");

    auto traversal = entries.front();
    traversal.path = "../neutral.bin";
    RepositoryFixture traversal_fixture{package_manifest({traversal}),
                                        fixture_files({entries.front()})};
    expect_error(loader::load_runtime_resource_snapshot(traversal_fixture.resources(), {"CN", {}}),
                 loader::RuntimeResourceSnapshotLoadError::path_not_manifested,
                 "ambient and traversal paths must never be opened");

    RepositoryFixture tamper_fixture{package_manifest({entries.front()}),
                                     fixture_files({entries.front()})};
    tamper_fixture.replace_payload(entries.front().path, runtime_payload(99));
    expect_error(loader::load_runtime_resource_snapshot(tamper_fixture.resources(), {"CN", {}}),
                 loader::RuntimeResourceSnapshotLoadError::repository_read_failed,
                 "read view must reject payload replacement before snapshot publication");

    RepositoryFixture symlink_fixture{package_manifest({entries.front()}),
                                      fixture_files({entries.front()})};
    const auto original = symlink_fixture.root() / entries.front().path;
    const auto outside = symlink_fixture.root() / "outside.bin";
    write_file(outside, entries.front().bytes);
    std::error_code error;
    std::filesystem::remove(original, error);
    error.clear();
    std::filesystem::create_symlink(outside, original, error);
    if (!error) {
        expect_error(
            loader::load_runtime_resource_snapshot(symlink_fixture.resources(), {"CN", {}}),
            loader::RuntimeResourceSnapshotLoadError::repository_read_failed,
            "read view must reject a symlink substituted after pinning");
    }
}

void cancel_during_manifest_read(const repository::RuntimeRepositoryReadHookPoint point,
                                 const std::string_view repository_id,
                                 const std::string_view logical_path) {
    if (read_cancellation != nullptr && repository_id == "resources" &&
        logical_path == loader::runtime_resource_manifest_path &&
        point == repository::RuntimeRepositoryReadHookPoint::payload_digest_finalizing)
        read_cancellation->request_stop();
}

void throw_allocation(const loader::RuntimeResourceSnapshotLoaderHookPoint) {
    throw std::bad_alloc{};
}

std::atomic<int> payload_copy_hooks{};
void throw_second_payload_allocation(const loader::RuntimeResourceSnapshotLoaderHookPoint point) {
    if (point == loader::RuntimeResourceSnapshotLoaderHookPoint::before_payload_copy &&
        ++payload_copy_hooks == 2)
        throw std::bad_alloc{};
}

void test_limits_cancel_oom_and_wrong_capability() {
    const auto entries = valid_entries();
    RepositoryFixture fixture{package_manifest(entries), fixture_files(entries)};

    loader::RuntimeResourceSnapshotLoaderLimits limits;
    limits.max_manifest_bytes = 1;
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}, limits),
                 loader::RuntimeResourceSnapshotLoadError::manifest_too_large,
                 "manifest limit must bind reads");
    limits = {};
    limits.max_entries = 1;
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}, limits),
                 loader::RuntimeResourceSnapshotLoadError::entry_limit_exceeded,
                 "entry limit must bind parse");
    limits = {};
    limits.max_file_bytes = 256;
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}, limits),
                 loader::RuntimeResourceSnapshotLoadError::file_limit_exceeded,
                 "file limit must bind reads");
    limits = {};
    limits.max_total_bytes = 500;
    limits.max_file_bytes = 500;
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}, limits),
                 loader::RuntimeResourceSnapshotLoadError::total_byte_limit_exceeded,
                 "total limit must bind package");
    limits = {};
    limits.max_string_bytes = 8;
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}, limits),
                 loader::RuntimeResourceSnapshotLoadError::string_limit_exceeded,
                 "string limit must bind fields");
    limits = {};
    limits.max_work = 1;
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}, limits),
                 loader::RuntimeResourceSnapshotLoadError::work_limit_exceeded,
                 "work limit must bind total work");
    limits = {};
    limits.max_json_depth = 1;
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}, limits),
                 loader::RuntimeResourceSnapshotLoadError::invalid_manifest,
                 "JSON depth must be rejected during parse");
    limits = {};
    limits.max_json_nodes = 2;
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}, limits),
                 loader::RuntimeResourceSnapshotLoadError::invalid_manifest,
                 "JSON nodes must be rejected during parse");
    limits = {};
    limits.max_entries = 0;
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}, limits),
                 loader::RuntimeResourceSnapshotLoadError::invalid_limits,
                 "zero limits must fail closed");
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"bad/locale", {}}),
                 loader::RuntimeResourceSnapshotLoadError::invalid_selector,
                 "caller selector must be canonical");
    expect_error(loader::load_runtime_resource_snapshot(fixture.scripts(), {"CN", {}}),
                 loader::RuntimeResourceSnapshotLoadError::wrong_repository,
                 "scripts capability must be rejected");

    std::stop_source cancelled;
    cancelled.request_stop();
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}, {},
                                                        cancelled.get_token()),
                 loader::RuntimeResourceSnapshotLoadError::cancelled,
                 "pre-cancel must fail closed");
    std::stop_source during_read;
    read_cancellation = &during_read;
    repository::set_runtime_repository_read_view_hook(cancel_during_manifest_read);
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}, {},
                                                        during_read.get_token()),
                 loader::RuntimeResourceSnapshotLoadError::cancelled,
                 "read cancellation must fail closed");
    repository::set_runtime_repository_read_view_hook(nullptr);
    read_cancellation = nullptr;

    loader::set_runtime_resource_snapshot_loader_hook(throw_allocation);
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}),
                 loader::RuntimeResourceSnapshotLoadError::resource_exhausted,
                 "allocation failure must fail closed");
    loader::set_runtime_resource_snapshot_loader_hook(nullptr);
    payload_copy_hooks = 0;
    loader::set_runtime_resource_snapshot_loader_hook(throw_second_payload_allocation);
    expect_error(loader::load_runtime_resource_snapshot(fixture.resources(), {"CN", {}}),
                 loader::RuntimeResourceSnapshotLoadError::resource_exhausted,
                 "allocation failure after partial assembly must publish no snapshot");
    loader::set_runtime_resource_snapshot_loader_hook(nullptr);
}

void test_stable_error_names_and_no_embedded_payload(const std::filesystem::path& executable) {
    check(loader::runtime_resource_snapshot_load_error_name(
              loader::RuntimeResourceSnapshotLoadError::cancelled) == "RRL017_CANCELLED",
          "typed error names must remain stable");
    const auto payload = runtime_payload(0x5a17U);
    std::ifstream input(executable, std::ios::binary);
    const std::vector<char> characters{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};
    const auto binary = std::as_bytes(std::span{characters});
    check(std::search(binary.begin(), binary.end(), payload.begin(), payload.end()) == binary.end(),
          "test payload must not be embedded into the executable");
}

} // namespace

int main(const int argc, char** argv) {
    try {
        test_success_selector_and_owned_lifetime();
        test_exact_schema_and_manifest_identity();
        test_duplicate_case_path_and_read_view_tamper();
        test_limits_cancel_oom_and_wrong_capability();
        if (argc > 0)
            test_stable_error_names_and_no_embedded_payload(argv[0]);
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "unexpected exception: " << error.what() << '\n';
    }
    return failures.load() == 0 ? 0 : 1;
}
