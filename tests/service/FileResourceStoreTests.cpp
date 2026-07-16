#include "service/adapters/FileResourceStore.h"
#include "../../src/service/adapters/ConfigArchiveCodec.h"
#include "TestConfigurationDefaults.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <process.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

using namespace std::chrono_literals;
using Json = nlohmann::json;
namespace adapters = baas::service::adapters;
namespace archive = baas::service::adapters::config_archive;
namespace channels = baas::service::channels;
namespace test_defaults = baas::service::test;

std::atomic<int> failures{};

std::uint64_t current_process_id() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint64_t>(::_getpid());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

void check(const bool condition, const std::string& message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

std::filesystem::path utf8_path(const std::string& text)
{
    std::u8string encoded;
    encoded.reserve(text.size());
    for (const auto character : text) {
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path(encoded);
}

void write_bytes(const std::filesystem::path& path, const std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::runtime_error("fixture write failed");
}

std::string read_bytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::byte> byte_vector(const std::string_view bytes)
{
    std::vector<std::byte> result(bytes.size());
    if (!bytes.empty()) {
        std::memcpy(result.data(), bytes.data(), bytes.size());
    }
    return result;
}

std::vector<std::byte> archive_bytes(
    const std::initializer_list<std::pair<std::string, std::string_view>> files)
{
    std::vector<archive::Entry> entries;
    entries.reserve(files.size());
    for (const auto& [path, bytes] : files) {
        entries.push_back({path, byte_vector(bytes)});
    }
    auto encoded = archive::encode(entries, {});
    if (!encoded) throw std::runtime_error("fixture archive encoding failed");
    return std::move(encoded.bytes);
}

int run_import_crash_child(const std::filesystem::path& root)
{
    const auto input = archive_bytes({
        {"config.json", R"({"name":"Crash Lock","server":"日服"})"},
    });
    auto dependencies = test_defaults::with_synthetic_defaults();
    dependencies.clock = [] { return 32'000.0; };
    dependencies.config_archive_fault_injector =
        [root](const std::string_view step) {
            if (step != "before_target_commit") return false;
            write_bytes(root / "child-import-ready", "ready");
            for (;;) std::this_thread::sleep_for(100ms);
        };
    adapters::FileResourceStore store(root, std::move(dependencies));
    const auto result = store.import_config({input.data(), input.size()}, {});
    return result ? 0 : 2;
}

bool has_private_config_artifact(const std::filesystem::path& root)
{
    for (const auto& child : std::filesystem::directory_iterator(root / "config")) {
        if (child.path().filename().string().starts_with(".baas-")) return true;
    }
    return false;
}

class TempProject {
public:
    TempProject()
    {
        static std::atomic<std::uint64_t> sequence{};
        const auto timestamp = std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count();
        root = std::filesystem::temp_directory_path()
            / ("baas-file-resource-store-"
               + std::to_string(current_process_id()) + "-"
               + std::to_string(timestamp) + "-"
               + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(root / "config");
    }

    ~TempProject() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    TempProject(const TempProject&) = delete;
    TempProject& operator=(const TempProject&) = delete;

    void add_pair(const std::string& id, const std::string_view config,
                  const std::string_view event = R"({"enabled":true})") const
    {
        const auto directory = root / "config" / utf8_path(id);
        write_bytes(directory / "config.json", config);
        write_bytes(directory / "event.json", event);
    }

    void add_globals() const
    {
        write_bytes(root / "config" / "gui.json", R"({"theme":"dark"})");
        write_bytes(root / "config" / "static.json", R"({"version":3})");
        write_bytes(root / "setup.toml", "[general]\nchannel = 'stable'\n");
    }

    std::filesystem::path root;
};

adapters::FileResourceStoreDependencies dependencies(
    adapters::FileResourceStoreDependencies::AtomicWriter writer = {},
    adapters::FileResourceStoreDependencies::PostCommitDurabilityCheck check = {})
{
    adapters::FileResourceStoreDependencies result;
    result.clock = [] { return 9'000'000'000'000.0; };
    result.atomic_writer = std::move(writer);
    result.post_commit_durability_check = std::move(check);
    return test_defaults::with_synthetic_defaults(std::move(result));
}

channels::ResourceKey config_key(std::string id = "alpha")
{
    return {channels::SyncResource::config, std::move(id)};
}

channels::ResourceKey event_key(std::string id = "alpha")
{
    return {channels::SyncResource::event, std::move(id)};
}

std::string timestamp_of(adapters::FileResourceStore& store,
                         const channels::ResourceKey& key)
{
    auto pulled = store.pull(key, {});
    check(static_cast<bool>(pulled), "timestamp source pulls successfully");
    return pulled ? pulled->timestamp_json : "0";
}

channels::ResourcePatchRequest replace_request(
    channels::ResourceKey key, std::string timestamp, std::string path,
    std::string value)
{
    return {std::move(key), std::move(timestamp),
            {{"replace", std::move(path), std::move(value)}}};
}

void test_list_pull_and_resource_shapes()
{
    TempProject project;
    project.add_pair("zeta", R"({"name":"zeta"})");
    project.add_pair("alpha", R"({"nested":{"x":1},"arr":[1,2],"future":1.25})");
    write_bytes(project.root / "config" / "config-only" / "config.json", "{}");
    write_bytes(project.root / "config" / "plain-file", "{}");
    project.add_globals();

    adapters::FileResourceStore store(project.root, dependencies());
    auto listed = store.config_list({});
    check(static_cast<bool>(listed), "config list succeeds");
    if (listed) {
        check(Json::parse(listed->data_json) == Json::array({"alpha", "zeta"}),
              "config list is sorted and requires config plus event");
        check(Json::parse(listed->timestamp_json) == 9'000'000'000'000.0,
              "config list uses the injected millisecond clock");
    }

    auto config = store.pull(config_key(), {});
    auto event = store.pull(event_key(), {});
    auto gui = store.pull({channels::SyncResource::gui, std::nullopt}, {});
    auto static_data = store.pull(
        {channels::SyncResource::static_data, std::nullopt}, {});
    check(config && Json::parse(config->data_json)["future"] == 1.25,
          "config pull preserves unknown JSON values");
    check(event && Json::parse(event->data_json)["enabled"] == true,
          "event pull resolves the per-config event file");
    check(gui && Json::parse(gui->data_json)["theme"] == "dark",
          "gui pull resolves config/gui.json");
    check(static_data && Json::parse(static_data->data_json)["version"] == 3,
          "static pull resolves config/static.json");

    auto setup = store.pull(
        {channels::SyncResource::setup_toml, std::string{"global"}}, {});
    check(static_cast<bool>(setup),
          "setup TOML global projection pulls successfully");
    if (setup) {
        const auto projection = Json::parse(setup->data_json);
        check(projection == Json{{"transport", "websocket"},
                                 {"channel", "stable"},
                                 {"updateMethod", "github"},
                                 {"repoUrl", "https://github.com/pur1fying/blue_archive_auto_script.git"},
                                 {"shaMethod", ""},
                                 {"mirrorcCdk", ""},
                                 {"gitBackend", "auto"}},
              "setup TOML exposes the Python-compatible bounded projection");
    }
    auto no_id = store.pull({channels::SyncResource::config, std::nullopt}, {});
    check(!no_id && no_id.error == channels::ResourceStoreError::invalid_data,
          "config pull requires a resource id");
    auto global_id = store.pull(
        {channels::SyncResource::gui, std::string{"alpha"}}, {});
    check(!global_id && global_id.error == channels::ResourceStoreError::invalid_data,
          "global resources reject a resource id");
    auto invalid_setup_id = store.pull(
        {channels::SyncResource::setup_toml, std::string{"not-global"}}, {});
    check(!invalid_setup_id
              && invalid_setup_id.error == channels::ResourceStoreError::invalid_data,
          "setup pull validates before canonicalizing its compatibility id");
    auto missing = store.pull(config_key("missing"), {});
    check(!missing && missing.error == channels::ResourceStoreError::not_found,
          "missing safe resource is not found");

    std::stop_source stopped;
    stopped.request_stop();
    auto cancelled = store.pull(config_key(), stopped.get_token());
    check(!cancelled && cancelled.error == channels::ResourceStoreError::internal_error,
          "pre-cancelled pull does not enter filesystem work");
    check(store.project_root() == std::filesystem::absolute(project.root).lexically_normal(),
          "store exposes its normalized project root");
}

void test_path_traversal_and_symlink_rejection()
{
    TempProject project;
    project.add_pair("alpha", "{}");
    adapters::FileResourceStore store(project.root, dependencies());

    for (const auto& id : {"..", "../outside", "a/b", "a\\b", "C:escape", "."}) {
        auto result = store.pull(config_key(id), {});
        check(!result && result.error == channels::ResourceStoreError::invalid_data,
              std::string{"unsafe resource id is rejected: "} + id);
    }
    std::string invalid_utf8(1, static_cast<char>(0xff));
    auto invalid = store.pull(config_key(invalid_utf8), {});
    check(!invalid && invalid.error == channels::ResourceStoreError::invalid_data,
          "invalid UTF-8 resource id is rejected");

    const auto outside = project.root.parent_path() / "baas-file-resource-outside";
    std::filesystem::remove_all(outside);
    write_bytes(outside / "config.json", R"({"outside":true})");
    write_bytes(outside / "event.json", "{}");
    std::error_code symlink_error;
    std::filesystem::create_directory_symlink(
        outside, project.root / "config" / "linked", symlink_error);
    if (!symlink_error) {
        auto list = store.config_list({});
        check(list && Json::parse(list->data_json) == Json::array({"alpha"}),
              "symlinked config directory is omitted from config list");
        auto linked = store.pull(config_key("linked"), {});
        check(!linked && linked.error == channels::ResourceStoreError::invalid_data,
              "symlinked config directory cannot be pulled");
    } else {
        std::cout << "SKIP: directory symlink creation is unavailable on this Windows host\n";
    }
    std::filesystem::remove_all(outside);
}

#if defined(_WIN32)
void test_windows_aliases_and_anchored_directory_handles()
{
    TempProject project;
    project.add_pair("alpha", R"({"x":1})");
    const auto moved_root = project.root.parent_path()
        / (project.root.filename().string() + "-moved");
    std::filesystem::remove_all(moved_root);
    std::atomic<bool> rename_blocked{};
    adapters::FileResourceStore store(
        project.root,
        dependencies({}, [&](const std::filesystem::path&) {
            const auto config = project.root / "config";
            const auto moved = project.root / "config-moved";
            rename_blocked = MoveFileExW(config.c_str(), moved.c_str(), 0) == FALSE;
            return true;
        }));

    auto case_alias = store.pull(config_key("ALPHA"), {});
    check(!case_alias, "Windows case alias is not a second cache key");
    for (const auto& alias : {"alpha.", "alpha ", "CON", "NUL.txt"}) {
        auto result = store.pull(config_key(alias), {});
        check(!result && result.error == channels::ResourceStoreError::invalid_data,
              std::string{"Windows invalid physical alias is rejected: "} + alias);
    }

    wchar_t short_buffer[MAX_PATH]{};
    const auto alpha = project.root / "config" / "alpha";
    const auto short_size = GetShortPathNameW(
        alpha.c_str(), short_buffer, static_cast<DWORD>(std::size(short_buffer)));
    if (short_size > 0 && short_size < std::size(short_buffer)) {
        const auto short_name = std::filesystem::path(short_buffer).filename().string();
        if (short_name != "alpha") {
            auto short_alias = store.pull(config_key(short_name), {});
            check(!short_alias, "8.3 directory alias is rejected by exact handle path");
        }
    } else {
        std::cout << "SKIP: 8.3 short names are unavailable on this volume\n";
    }

    const auto timestamp = timestamp_of(store, config_key());
    auto patched = store.apply_patch(
        replace_request(config_key(), timestamp, "/x", "2"), {});
    check(patched && rename_blocked,
          "anchored directory chain prevents rename during safe replacement");
    check(Json::parse(read_bytes(alpha / "config.json"))["x"] == 2,
          "NtSetInformationFile commits the relative replacement contents");
    bool temporary_found{};
    for (const auto& entry : std::filesystem::directory_iterator(alpha)) {
        temporary_found = temporary_found
            || entry.path().filename().string().find(".baas.tmp.")
                != std::string::npos;
    }
    check(!temporary_found,
          "handle-relative Windows replacement leaves no temporary file");

    check(MoveFileExW(project.root.c_str(), moved_root.c_str(), 0) == FALSE,
          "persistent root anchor prevents project-root rename/replacement");

    TempProject cased_project;
    cased_project.add_pair("alpha", "{}");
    auto alternate_case = cased_project.root.native();
    std::transform(
        alternate_case.begin(), alternate_case.end(), alternate_case.begin(),
        [](const wchar_t character) { return std::towlower(character); });
    adapters::FileResourceStore cased_store(
        std::filesystem::path(alternate_case), dependencies());
    check(static_cast<bool>(cased_store.pull(config_key(), {})),
          "root anchor canonicalizes caller casing before exact-path reads");

    TempProject unicode_project;
    unicode_project.add_pair("alpha", "{}");
    auto unicode_name =
        utf8_path("baas-file-resource-store-unicode-\xe6\xb5\x8b\xe8\xaf\x95-");
    unicode_name += unicode_project.root.filename().native();
    const auto unicode_root = unicode_project.root.parent_path() / unicode_name;
    std::filesystem::remove_all(unicode_root);
    std::filesystem::rename(unicode_project.root, unicode_root);
    unicode_project.root = unicode_root;
    adapters::FileResourceStore unicode_store(unicode_project.root, dependencies());
    check(static_cast<bool>(unicode_store.pull(config_key(), {})),
          "Unicode project roots remain valid under exact handle paths");
}
#else
void test_posix_ancestor_symlink_swap_fails_closed()
{
    TempProject project;
    project.add_pair("alpha", R"({"x":1})");
    adapters::FileResourceStore store(project.root, dependencies());
    const auto timestamp = timestamp_of(store, config_key());
    const auto original_config = project.root / "config";
    const auto saved_config = project.root / "config-saved";
    const auto outside = project.root.parent_path() / "baas-file-resource-swap";
    std::filesystem::remove_all(outside);
    write_bytes(outside / "alpha" / "config.json", R"({"x":99})");
    write_bytes(outside / "alpha" / "event.json", "{}");
    std::filesystem::rename(original_config, saved_config);
    std::filesystem::create_directory_symlink(outside, original_config);

    bool constructor_rejected{};
    try {
        adapters::FileResourceStore fresh(project.root, dependencies());
    } catch (const std::invalid_argument&) {
        constructor_rejected = true;
    }
    check(constructor_rejected,
          "POSIX store construction refuses a swapped config ancestor symlink");
    check(store.refresh_and_publish(config_key(), "filesystem"),
          "POSIX anchored refresh publishes a fail-closed removal for a swapped config ancestor symlink");
    const auto invalidated = store.pull(config_key(), {});
    check(!invalidated,
          "POSIX anchored refresh leaves no cached snapshot after refusing the swapped ancestor");
    auto patched = store.apply_patch(
        replace_request(config_key(), timestamp, "/x", "2"), {});
    check(!patched && Json::parse(read_bytes(
              outside / "alpha" / "config.json"))["x"] == 99,
          "POSIX anchored writer never follows a swapped config ancestor");

    std::filesystem::remove(original_config);
    std::filesystem::rename(saved_config, original_config);
    std::filesystem::remove_all(outside);
}
#endif

void test_json_validation_and_capacity()
{
    TempProject project;
    project.add_pair("broken", "{");
    project.add_pair("duplicate", R"({"x":1,"x":2})");
    project.add_pair("utf8", std::string{"{\"x\":\""}
        + static_cast<char>(0xff) + "\"}");
    project.add_pair("deep", R"({"a":{"b":{"c":1}}})");
    project.add_pair("large", std::string(200, 'x'));

    adapters::FileResourceStore normal(project.root, dependencies());
    for (const auto& id : {"broken", "duplicate", "utf8"}) {
        auto result = normal.pull(config_key(id), {});
        check(!result && result.error == channels::ResourceStoreError::invalid_data,
              std::string{"invalid JSON is rejected: "} + id);
    }

    channels::ResourceStoreLimits shallow_limits;
    shallow_limits.max_json_depth = 2;
    adapters::FileResourceStore shallow(project.root, dependencies(), shallow_limits);
    auto deep = shallow.pull(config_key("deep"), {});
    check(!deep && deep.error == channels::ResourceStoreError::invalid_data,
          "JSON exceeding the depth bound is rejected");

    channels::ResourceStoreLimits small_limits;
    small_limits.max_json_bytes = 64;
    adapters::FileResourceStore small_store(
        project.root, dependencies(), small_limits);
    auto large = small_store.pull(config_key("large"), {});
    check(!large && large.error == channels::ResourceStoreError::capacity,
          "file size is rejected before unbounded JSON allocation");

    channels::ResourceStoreLimits list_limits;
    list_limits.max_resources = 1;
    adapters::FileResourceStore list_limited(project.root, dependencies(), list_limits);
    auto list = list_limited.config_list({});
    check(!list && list.error == channels::ResourceStoreError::capacity,
          "config list enforces the resource count bound");

    channels::ResourceStoreLimits invalid_limits;
    invalid_limits.max_json_nodes = 0;
    bool rejected{};
    try {
        adapters::FileResourceStore invalid_store(
            project.root, dependencies(), invalid_limits);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "constructor rejects zero production limits");
}

void test_patch_conflict_and_durable_commit()
{
    TempProject project;
    project.add_pair(
        "alpha", R"({"nested":{"x":1},"arr":[1,2],"future":1.25})");
    project.add_globals();
    adapters::FileResourceStore store(project.root, dependencies());
    const auto initial_timestamp = timestamp_of(store, config_key());

    std::vector<channels::ResourceUpdate> updates;
    auto subscription = store.subscribe_updates(
        [&updates](channels::ResourceUpdate update) { updates.push_back(std::move(update)); });
    check(static_cast<bool>(subscription), "patch subscriber is installed");

    channels::ResourcePatchRequest request{
        config_key(), initial_timestamp,
        {{"add", "/added", std::string{R"({"v":2.75})"}},
         {"replace", "/nested/x", std::string{"7"}},
         {"remove", "/arr/0", std::nullopt}}};
    auto patched = store.apply_patch(request, {});
    check(patched
              && patched->disposition == channels::ResourcePatchDisposition::applied,
          "valid patch commits");
    if (patched) {
        const auto data = Json::parse(patched->snapshot.data_json);
        check(data["added"]["v"] == 2.75 && data["nested"]["x"] == 7
                  && data["arr"] == Json::array({2}) && data["future"] == 1.25,
              "add replace remove match in-memory patch semantics");
        check(Json::parse(patched->snapshot.timestamp_json)
                  == 9'000'000'000'000.0,
              "commit timestamp advances to the injected clock");
    }
    const auto disk = Json::parse(
        read_bytes(project.root / "config" / "alpha" / "config.json"));
    check(disk["nested"]["x"] == 7 && disk["added"]["v"] == 2.75,
          "successful patch atomically replaces the disk document");
    check(updates.size() == 1 && updates.front().origin == "frontend"
              && Json::parse(updates.front().operations_json).size() == 3,
          "publication occurs after commit with exact patch operations");

    auto stale = store.apply_patch(
        replace_request(config_key(), initial_timestamp, "/nested/x", "8"), {});
    check(stale
              && stale->disposition == channels::ResourcePatchDisposition::conflict
              && Json::parse(stale->snapshot.data_json)["nested"]["x"] == 7,
          "older timestamp returns the current snapshot as a conflict");

    const auto current_timestamp = timestamp_of(store, config_key());
    channels::ResourcePatchRequest missing{
        config_key(), current_timestamp,
        {{"remove", "/missing", std::nullopt}}};
    auto conflict = store.apply_patch(std::move(missing), {});
    check(conflict
              && conflict->disposition == channels::ResourcePatchDisposition::conflict,
          "failed patch is isolated and reported as conflict");
    check(Json::parse(store.pull(config_key(), {})->data_json)["nested"]["x"] == 7,
          "failed patch leaves visible snapshot unchanged");
    check(updates.size() == 1, "conflicts are not published");

    auto immutable = store.apply_patch(
        {{channels::SyncResource::static_data, std::nullopt}, "0", {}}, {});
    check(!immutable && immutable.error == channels::ResourceStoreError::invalid_data,
          "static resource cannot be patched");
    auto invalid_setup_id = store.apply_patch(
        {{channels::SyncResource::setup_toml, std::string{"not-global"}}, "0", {}}, {});
    check(!invalid_setup_id
              && invalid_setup_id.error == channels::ResourceStoreError::invalid_data,
          "setup TOML rejects non-global resource identifiers");

    bool temporary_found{};
    for (const auto& entry : std::filesystem::directory_iterator(
             project.root / "config" / "alpha")) {
        if (entry.path().filename().string().find(".baas.tmp.") != std::string::npos) {
            temporary_found = true;
        }
    }
    check(!temporary_found, "durable writer leaves no sibling temporary file");
}

void test_atomic_writer_failure_is_invisible()
{
    TempProject project;
    project.add_pair("alpha", R"({"x":1})");
    const auto target = project.root / "config" / "alpha" / "config.json";
    const auto original = read_bytes(target);
    std::atomic<int> writer_calls{};
    adapters::FileResourceStore store(
        project.root,
        dependencies([&writer_calls](const std::filesystem::path&, std::string_view) {
            ++writer_calls;
            return adapters::AtomicWriteResult::not_committed;
        }));
    const auto timestamp = timestamp_of(store, config_key());
    std::atomic<int> callbacks{};
    auto subscription = store.subscribe_updates(
        [&callbacks](channels::ResourceUpdate) { ++callbacks; });

    auto result = store.apply_patch(
        replace_request(config_key(), timestamp, "/x", "2"), {});
    check(!result && result.error == channels::ResourceStoreError::internal_error,
          "atomic writer failure is reported as internal error");
    check(writer_calls == 1, "validated patch reaches the injected writer once");
    check(read_bytes(target) == original, "failed writer leaves disk unchanged");
    auto pulled = store.pull(config_key(), {});
    check(pulled && Json::parse(pulled->data_json)["x"] == 1,
          "failed writer leaves cached visible snapshot unchanged");
    check(callbacks == 0, "failed writer never publishes");

    channels::ResourcePatchRequest invalid{
        config_key(), timestamp, {{"copy", "/x", std::nullopt}}};
    auto invalid_result = store.apply_patch(std::move(invalid), {});
    check(!invalid_result
              && invalid_result.error == channels::ResourceStoreError::invalid_data
              && writer_calls == 1,
          "invalid patch is rejected before the writer");
}

void test_post_commit_durability_failure_is_committed()
{
    TempProject project;
    project.add_pair("alpha", R"({"x":1})");
    std::atomic<int> writer_calls{};
    adapters::FileResourceStore store(
        project.root,
        dependencies([&writer_calls](const std::filesystem::path& target,
                                     const std::string_view bytes) {
            ++writer_calls;
            write_bytes(target, bytes);
            return adapters::AtomicWriteResult::committed_durability_uncertain;
        }));
    std::atomic<int> callbacks{};
    auto subscription = store.subscribe_updates(
        [&callbacks](channels::ResourceUpdate) { ++callbacks; });
    const auto timestamp = timestamp_of(store, config_key());
    auto result = store.apply_patch(
        replace_request(config_key(), timestamp, "/x", "2"), {});
    check(result
              && result->disposition == channels::ResourcePatchDisposition::applied,
          "post-commit durability uncertainty is still a committed patch");
    check(writer_calls == 1 && callbacks == 1,
          "post-commit durability uncertainty updates cache and publishes once");
    check(Json::parse(read_bytes(
              project.root / "config" / "alpha" / "config.json"))["x"] == 2,
          "post-commit durability uncertainty leaves disk and cache aligned");
    auto pulled = store.pull(config_key(), {});
    check(pulled && Json::parse(pulled->data_json)["x"] == 2,
          "post-commit durability uncertainty is visible through pull");
}

void test_default_writer_post_commit_failure_is_committed()
{
    TempProject project;
    project.add_pair("alpha", R"({"x":1})");
    std::atomic<int> checks{};
    adapters::FileResourceStore store(
        project.root,
        dependencies({}, [&checks](const std::filesystem::path&) {
            ++checks;
            return false;
        }));
    std::atomic<int> callbacks{};
    auto subscription = store.subscribe_updates(
        [&callbacks](channels::ResourceUpdate) { ++callbacks; });
    const auto timestamp = timestamp_of(store, config_key());
    auto result = store.apply_patch(
        replace_request(config_key(), timestamp, "/x", "2"), {});
    check(result
              && result->disposition == channels::ResourcePatchDisposition::applied,
          "default writer treats post-replace durability failure as committed");
    check(checks == 1 && callbacks == 1,
          "default writer seam runs after replacement and still publishes");
    auto pulled = store.pull(config_key(), {});
    check(pulled && Json::parse(pulled->data_json)["x"] == 2,
          "default writer post-commit failure keeps disk and cache aligned");
}

void test_concurrent_patch_conflict()
{
    TempProject project;
    project.add_pair("alpha", R"({"x":1})");
    adapters::FileResourceStore store(project.root, dependencies());
    const auto timestamp = timestamp_of(store, config_key());
    const auto request = replace_request(config_key(), timestamp, "/x", "2");
    std::atomic<int> applied{};
    std::atomic<int> conflicts{};
    auto run = [&] {
        auto result = store.apply_patch(request, {});
        check(static_cast<bool>(result), "concurrent patch returns a store result");
        if (!result) return;
        if (result->disposition == channels::ResourcePatchDisposition::applied) {
            ++applied;
        } else {
            ++conflicts;
        }
    };
    std::thread first(run);
    std::thread second(run);
    first.join();
    second.join();
    check(applied == 1 && conflicts == 1,
          "timestamp compare, durable write, and cache commit are serialized");
}

void test_external_change_conflicts_instead_of_overwrite()
{
    TempProject project;
    project.add_pair("alpha", R"({"x":1})");
    adapters::FileResourceStore store(project.root, dependencies());
    const auto timestamp = timestamp_of(store, config_key());
    std::atomic<int> callbacks{};
    auto subscription = store.subscribe_updates(
        [&callbacks](channels::ResourceUpdate) { ++callbacks; });

    const auto target = project.root / "config" / "alpha" / "config.json";
    write_bytes(target, R"({"x":9})");
    auto result = store.apply_patch(
        replace_request(config_key(), timestamp, "/x", "2"), {});
    check(result
              && result->disposition == channels::ResourcePatchDisposition::conflict,
          "patch rereads disk and conflicts on an external change");
    check(Json::parse(read_bytes(target))["x"] == 9 && callbacks == 0,
          "external data is not overwritten or published as a frontend patch");
    auto pulled = store.pull(config_key(), {});
    check(pulled && Json::parse(pulled->data_json)["x"] == 9,
          "external conflict refreshes the visible conflict snapshot");
}

void test_subscription_entry_barrier_and_exception_isolation()
{
    TempProject project;
    project.add_pair("alpha", R"({"x":1})");
    adapters::FileResourceStore store(project.root, dependencies());
    auto timestamp = timestamp_of(store, config_key());

    std::mutex mutex;
    std::condition_variable condition;
    bool entered{};
    bool release{};
    std::atomic<int> calls{};
    auto subscription = store.subscribe_updates([&](channels::ResourceUpdate) {
        auto reentrant = store.pull(config_key(), {});
        check(static_cast<bool>(reentrant),
              "callback can reenter pull because publication holds no state lock");
        std::unique_lock lock(mutex);
        ++calls;
        entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return release; });
    });
    check(static_cast<bool>(subscription), "blocking subscriber is installed");

    std::thread publisher([&] {
        auto result = store.apply_patch(
            replace_request(config_key(), timestamp, "/x", "2"), {});
        check(static_cast<bool>(result), "barrier test patch commits");
    });
    {
        std::unique_lock lock(mutex);
        check(condition.wait_for(lock, 5s, [&] { return entered; }),
              "callback enters before unsubscribe");
    }
    std::atomic<bool> destroyed{};
    std::thread destroyer([&] {
        subscription.subscription.reset();
        destroyed = true;
    });
    std::this_thread::sleep_for(30ms);
    check(!destroyed.load(), "subscription destruction waits for admitted callback");
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    condition.notify_all();
    publisher.join();
    destroyer.join();
    check(destroyed.load(), "subscription destruction completes at callback exit");

    timestamp = timestamp_of(store, config_key());
    auto after = store.apply_patch(
        replace_request(config_key(), timestamp, "/x", "3"), {});
    check(after && calls == 1, "destroyed subscription admits no later callback");

    std::atomic<int> survived{};
    auto throwing = store.subscribe_updates(
        [](channels::ResourceUpdate) { throw std::runtime_error("subscriber"); });
    auto healthy = store.subscribe_updates(
        [&survived](channels::ResourceUpdate) { ++survived; });
    timestamp = timestamp_of(store, config_key());
    auto isolated = store.apply_patch(
        replace_request(config_key(), timestamp, "/x", "4"), {});
    check(isolated && survived == 1,
          "one callback exception does not block another subscriber or commit");

    auto empty = store.subscribe_updates({});
    check(!empty && empty.error == channels::ResourceStoreError::invalid_data,
          "empty callback is rejected");
}

void test_publication_queue_reentrancy_and_empty_head_enqueue()
{
    TempProject project;
    project.add_pair("alpha", R"({"x":1})");
    adapters::FileResourceStore store(project.root, dependencies());
    std::mutex mutex;
    std::condition_variable condition;
    bool first_entered{};
    bool release_first{};
    int active{};
    int maximum_active{};
    std::vector<int> observed;
    auto subscription = store.subscribe_updates([&](channels::ResourceUpdate update) {
        const auto operations = Json::parse(update.operations_json);
        const int value = operations[0]["value"].get<int>();
        std::unique_lock lock(mutex);
        ++active;
        maximum_active = std::max(maximum_active, active);
        observed.push_back(value);
        if (value == 2) {
            first_entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release_first; });
        }
        --active;
    });

    const auto initial = timestamp_of(store, config_key());
    std::thread first([&] {
        auto result = store.apply_patch(
            replace_request(config_key(), initial, "/x", "2"), {});
        check(static_cast<bool>(result), "first ordered publication patch commits");
    });
    {
        std::unique_lock lock(mutex);
        check(condition.wait_for(lock, 5s, [&] { return first_entered; }),
              "first callback enters after its job was popped from the queue");
    }

    const auto second_timestamp = timestamp_of(store, config_key());
    std::thread second([&] {
        auto result = store.apply_patch(
            replace_request(config_key(), second_timestamp, "/x", "3"), {});
        check(static_cast<bool>(result),
              "enqueue into active drainer's empty-head window completes");
    });
    std::this_thread::sleep_for(30ms);
    {
        std::lock_guard lock(mutex);
        release_first = true;
    }
    condition.notify_all();
    first.join();
    second.join();
    check(observed == std::vector<int>({2, 3}) && maximum_active == 1,
          "single drainer preserves commit order and callback non-concurrency");

    std::atomic<bool> nested{};
    channels::ResourceSubscribeResult reentrant;
    reentrant = store.subscribe_updates([&](channels::ResourceUpdate) {
        if (nested.exchange(true)) return;
        const auto timestamp = timestamp_of(store, config_key());
        auto result = store.apply_patch(
            replace_request(config_key(), timestamp, "/x", "5"), {});
        check(static_cast<bool>(result),
              "callback-reentrant patch queues without waiting on its drainer");
    });
    auto timestamp = timestamp_of(store, config_key());
    auto outer = store.apply_patch(
        replace_request(config_key(), timestamp, "/x", "4"), {});
    check(outer && Json::parse(store.pull(config_key(), {})->data_json)["x"] == 5,
          "single drainer finishes a callback-reentrant publication");
}

void test_cross_unsubscribe_and_store_self_destruction()
{
    TempProject project;
    project.add_pair("alpha", R"({"x":1})");
    auto store = std::make_unique<adapters::FileResourceStore>(
        project.root, dependencies());
    auto* const raw_store = store.get();
    channels::ResourceSubscribeResult later;
    auto destroy_later = raw_store->subscribe_updates(
        [&later](channels::ResourceUpdate) { later.subscription.reset(); });
    later = raw_store->subscribe_updates([](channels::ResourceUpdate) {});
    const auto timestamp = timestamp_of(*raw_store, config_key());
    auto result = raw_store->apply_patch(
        replace_request(config_key(), timestamp, "/x", "2"), {});
    check(static_cast<bool>(result),
          "one callback can destroy another planned subscription without deadlock");

    channels::ResourceSubscribeResult destroys_store;
    destroys_store = raw_store->subscribe_updates(
        [&store](channels::ResourceUpdate) { store.reset(); });
    const auto final_timestamp = timestamp_of(*raw_store, config_key());
    auto final_result = raw_store->apply_patch(
        replace_request(config_key(), final_timestamp, "/x", "3"), {});
    check(final_result && !store,
          "callback can destroy the store while the active operation keeps Impl alive");
}

void test_subscriber_capacity()
{
    TempProject project;
    project.add_pair("alpha", "{}");
    channels::ResourceStoreLimits limits;
    limits.max_subscribers = 1;
    adapters::FileResourceStore store(project.root, dependencies(), limits);
    auto first = store.subscribe_updates([](channels::ResourceUpdate) {});
    auto second = store.subscribe_updates([](channels::ResourceUpdate) {});
    check(first && !second && second.error == channels::ResourceStoreError::capacity,
          "subscriber count is bounded");
}

void test_self_unsubscribe_does_not_deadlock()
{
    TempProject project;
    project.add_pair("alpha", R"({"x":1})");
    adapters::FileResourceStore store(project.root, dependencies());
    channels::ResourceSubscribeResult self;
    std::atomic<int> calls{};
    self = store.subscribe_updates([&self, &calls](channels::ResourceUpdate) {
        ++calls;
        self.subscription.reset();
    });
    check(static_cast<bool>(self), "self-unsubscribing callback is installed");

    auto timestamp = timestamp_of(store, config_key());
    auto first = store.apply_patch(
        replace_request(config_key(), timestamp, "/x", "2"), {});
    check(first && calls == 1 && !self.subscription,
          "callback can destroy its own subscription without waiting on itself");

    timestamp = timestamp_of(store, config_key());
    auto second = store.apply_patch(
        replace_request(config_key(), timestamp, "/x", "3"), {});
    check(second && calls == 1,
          "self-unsubscription closes admission before the next publication");
}

void test_setup_toml_projection_patch_and_unknown_retention()
{
    TempProject project;
    project.add_pair("alpha", "{}");
    write_bytes(project.root / "config" / "gui.json", "{}");
    write_bytes(project.root / "config" / "static.json", "{}");
    const auto setup_path = project.root / "setup.toml";
    write_bytes(setup_path,
        "schema_version = 1\r\n"
        "# an unrelated top-level comment must survive\r\n"
        "[\"general\"]\r\n"
        "\"transport\" = \"websocket\" # keep transport comment\r\n"
        "mirrorcCdk = 'abc'\r\n"
        "channel = \"stable\"\r\n"
        "getRemoteShaMethod = \"github\"\r\n"
        "git_backend = \"auto\"\r\n"
        "unknown_future = \"\"\"keep-me\r\n"
        "[this.is.not.a.table]\r\n"
        "# nor is this a comment\r\n"
        "still-keep-me\"\"\"\r\n"
        "\r\n"
        "[paths]\r\n"
        "baas_root_path = \"D:/BAAS\"\r\n"
        "\r\n"
        "[[plugins]]\r\n"
        "name = \"future-plugin\"\r\n"
        "\r\n"
        "[General]\r\n"
        "git_backend = \"git2\"\r\n");

    adapters::FileResourceStore store(project.root, dependencies());
    const channels::ResourceKey null_key{
        channels::SyncResource::setup_toml, std::nullopt};
    const channels::ResourceKey key{
        channels::SyncResource::setup_toml, std::string{"global"}};
    auto initial = store.pull(null_key, {});
    check(static_cast<bool>(initial),
          "legacy and camel-case setup TOML projects successfully");
    if (!initial) return;
    auto canonical_pull = store.pull(key, {});
    check(canonical_pull
              && canonical_pull->timestamp_json == initial->timestamp_json
              && canonical_pull->data_json == initial->data_json,
          "null and global setup keys share one canonical cache entry");
    const auto projected = Json::parse(initial->data_json);
    check(projected["mirrorcCdk"] == "abc"
              && projected["gitBackend"] == "git2"
              && projected["updateMethod"] == "github",
          "setup projection applies Python alias and legacy precedence");

    std::vector<channels::ResourceUpdate> updates;
    auto subscription = store.subscribe_updates(
        [&updates](channels::ResourceUpdate update) {
            updates.push_back(std::move(update));
        });
    channels::ResourcePatchRequest patch{
        null_key,
        initial->timestamp_json,
        {{"replace", "/transport", "\"pipe\""},
         {"replace", "/channel", "\"dev\""},
         {"replace", "/updateMethod", "\"gitee\""},
         {"replace", "/mirrorcCdk", "\"new-cdk\""},
         {"replace", "/gitBackend", "\"git_cli\""}}};
    auto applied = store.apply_patch(std::move(patch), {});
    check(applied
              && applied->disposition
                     == channels::ResourcePatchDisposition::applied,
          "setup projection patch commits");
    if (applied) {
        const auto visible = Json::parse(applied->snapshot.data_json);
        check(visible["transport"] == "pipe" && visible["channel"] == "dev"
                  && visible["updateMethod"] == "gitee"
                  && visible["gitBackend"] == "git_cli",
              "setup patch returns the committed projection");
    }
    const auto disk = read_bytes(setup_path);
    check(disk.find("# an unrelated top-level comment must survive\r\n")
                  != std::string::npos
              && disk.find("unknown_future = \"\"\"keep-me\r\n")
                  != std::string::npos
              && disk.find("[this.is.not.a.table]\r\n"
                           "# nor is this a comment\r\n"
                           "still-keep-me\"\"\"\r\n") != std::string::npos
              && disk.find("baas_root_path = \"D:/BAAS\"\r\n")
                  != std::string::npos
              && disk.find("[[plugins]]\r\nname = \"future-plugin\"\r\n")
                  != std::string::npos,
          "setup merge preserves unrelated TOML fields and CRLF text");
    check(disk.find("\"transport\" = \"pipe\" # keep transport comment\r\n")
                  != std::string::npos
              && disk.find("channel = \"dev\"\r\n") != std::string::npos
              && disk.find("get_remote_sha_method = \"gitee\"\r\n")
                  != std::string::npos
              && disk.find("mirrorc_cdk = \"new-cdk\"\r\n")
                  != std::string::npos
              && disk.find("git_backend = \"git_cli\"\r\n")
                  != std::string::npos,
          "setup merge writes canonical general keys");
    check(updates.size() == 1 && updates.front().key == key
              && updates.front().origin == "frontend",
          "setup commit publishes exactly one canonical global update");
    if (applied && !updates.empty()) {
        const auto operations = Json::parse(updates.front().operations_json);
        const auto replayed = projected.patch(operations);
        auto pulled_after_patch = store.pull(key, {});
        check(operations.size() == 1 && operations[0]["op"] == "replace"
                  && operations[0]["path"] == ""
                  && replayed == Json::parse(applied->snapshot.data_json)
                  && pulled_after_patch
                  && replayed == Json::parse(pulled_after_patch->data_json),
              "setup publication replays exactly to the committed projection");
    }

    adapters::FileResourceStore reloaded(project.root, dependencies());
    auto after_restart = reloaded.pull(key, {});
    check(after_restart
              && Json::parse(after_restart->data_json)["repoUrl"]
                     == "https://gitee.com/kiramei/baas-dev.git",
          "setup projection is stable after restart and channel-aware");

    auto removed = reloaded.apply_patch(
        {key, after_restart ? after_restart->timestamp_json : "0",
         {{"remove", "/transport", std::nullopt}}}, {});
    check(removed
              && removed->disposition
                     == channels::ResourcePatchDisposition::applied
              && Json::parse(removed->snapshot.data_json)["transport"] == "pipe"
              && read_bytes(setup_path) == disk,
          "removing a projected field reprojects the retained full TOML value");

    auto invalid = reloaded.apply_patch(
        {key, removed ? removed->snapshot.timestamp_json : "0",
         {{"add", "/transport", "\"invalid\""}}}, {});
    check(!invalid, "invalid transport is rejected");
    check(invalid.error == channels::ResourceStoreError::invalid_data,
          "invalid transport has the stable invalid_data classification");
    check(read_bytes(setup_path) == disk,
          "invalid transport leaves setup.toml unchanged");
}

void test_refresh_and_publish()
{
    TempProject project;
    project.add_pair("alpha", "{}");
    project.add_globals();
    adapters::FileResourceStore store(project.root, dependencies());
    auto gui = store.pull({channels::SyncResource::gui, std::nullopt}, {});
    auto setup = store.pull(
        {channels::SyncResource::setup_toml, std::string{"global"}}, {});
    check(static_cast<bool>(gui), "refresh baseline is cached");
    check(static_cast<bool>(setup), "setup refresh baseline is cached");

    std::vector<channels::ResourceUpdate> updates;
    auto subscription = store.subscribe_updates(
        [&updates](channels::ResourceUpdate update) { updates.push_back(std::move(update)); });
    write_bytes(project.root / "config" / "gui.json", R"({"theme":"light","scale":2})");
    check(store.refresh_and_publish(
              {channels::SyncResource::gui, std::nullopt}, "filesystem"),
          "valid external change refreshes and publishes");
    auto refreshed = store.pull({channels::SyncResource::gui, std::nullopt}, {});
    check(refreshed && Json::parse(refreshed->data_json)["scale"] == 2,
          "refresh updates the visible snapshot");
    check(updates.size() == 1 && updates.front().origin == "filesystem",
          "refresh publication preserves origin");
    if (!updates.empty()) {
        const auto operations = Json::parse(updates.front().operations_json);
        check(operations.size() == 1 && operations[0]["op"] == "replace"
                  && operations[0]["path"] == ""
                  && operations[0]["value"]["theme"] == "light",
              "refresh publishes one root replacement operation");
    }
    check(!store.refresh_and_publish(
              {channels::SyncResource::gui, std::nullopt}, "filesystem"),
          "unchanged external document is not republished");

    write_bytes(project.root / "config" / "gui.json", "{");
    const auto malformed = store.refresh(
        {channels::SyncResource::gui, std::nullopt}, "filesystem");
    check(malformed.disposition
                  == adapters::ResourceRefreshDisposition::invalid_data
              && malformed.published && updates.size() == 2
              && Json::parse(updates.back().operations_json)
                     == Json::array({Json{{"op", "remove"}, {"path", ""}}}),
          "malformed external JSON invalidates and publishes root remove");
    auto invalidated = store.pull({channels::SyncResource::gui, std::nullopt}, {});
    bool replay_visible{};
    for (const auto& update : updates) {
        const auto operations = Json::parse(update.operations_json);
        if (operations[0]["op"] == "replace") replay_visible = true;
        if (operations[0]["op"] == "remove") replay_visible = false;
    }
    check(!invalidated
              && invalidated.error == channels::ResourceStoreError::invalid_data
              && updates.size() == 2 && !replay_visible,
          "malformed pull and subscriber replay both expose no resource");
    write_bytes(project.root / "setup.toml",
                "[general]\nchannel = 'dev'\nfuture = 'preserved'\n");
    check(store.refresh_and_publish(
              {channels::SyncResource::setup_toml, std::nullopt},
              "filesystem"),
          "null setup key refreshes the canonical projection and publishes");
    auto refreshed_setup = store.pull(
        {channels::SyncResource::setup_toml, std::string{"global"}}, {});
    check(refreshed_setup
              && Json::parse(refreshed_setup->data_json)["channel"] == "dev"
              && updates.size() == 3
              && updates.back().key
                     == channels::ResourceKey{
                         channels::SyncResource::setup_toml,
                         std::string{"global"}},
          "setup refresh and pull share one canonical global cache entry");
    write_bytes(project.root / "config" / "gui.json", R"({"theme":"restored"})");
    const auto restored = store.refresh(
        {channels::SyncResource::gui, std::nullopt}, "filesystem");
    check(restored.disposition == adapters::ResourceRefreshDisposition::updated,
          "exact refresh reports a restored document");
    write_bytes(project.root / "config" / "gui.json",
                std::string(1U * 1'024U * 1'024U + 1U, 'x'));
    const auto oversized = store.refresh(
        {channels::SyncResource::gui, std::nullopt}, "filesystem");
    auto oversized_pull = store.pull(
        {channels::SyncResource::gui, std::nullopt}, {});
    check(oversized.disposition == adapters::ResourceRefreshDisposition::capacity
              && oversized.published && !oversized_pull
              && oversized_pull.error == channels::ResourceStoreError::capacity
              && updates.size() == 5
              && Json::parse(updates.back().operations_json)
                     == Json::array({Json{{"op", "remove"}, {"path", ""}}}),
          "oversized replacement removes cached and replayed resource");
    write_bytes(project.root / "config" / "gui.json", R"({"theme":"again"})");
    check(store.refresh_and_publish(
              {channels::SyncResource::gui, std::nullopt}, "filesystem"),
          "resource can recover after capacity failure");
    std::filesystem::remove(project.root / "config" / "gui.json");
    const auto removed = store.refresh(
        {channels::SyncResource::gui, std::nullopt}, "filesystem");
    check(removed.disposition == adapters::ResourceRefreshDisposition::removed
              && updates.size() == 7
              && Json::parse(updates.back().operations_json)
                     == Json::array({Json{{"op", "remove"}, {"path", ""}}}),
          "external deletion invalidates cache and publishes one root remove");
    check(!store.refresh_and_publish(
              {channels::SyncResource::gui, std::nullopt}, std::string(65, 'o')),
          "refresh origin is bounded");
}

void test_setup_toml_legacy_proxy_projection()
{
    struct Case {
        bool dev;
        std::string method;
        std::string url;
    };
    const std::vector<Case> cases{
        {false, "github", "https://github.com/pur1fying/blue_archive_auto_script.git"},
        {false, "gitee", "https://gitee.com/pur1fy/blue_archive_auto_script.git"},
        {false, "gitcode", "https://gitcode.com/m0_74686738/blue_archive_auto_script.git"},
        {false, "github_proxy_v4", "https://v4.gh-proxy.org/https://github.com/pur1fying/blue_archive_auto_script.git"},
        {false, "github_proxy_v6", "https://v6.gh-proxy.org/https://github.com/pur1fying/blue_archive_auto_script.git"},
        {false, "github_proxy_cdn", "https://cdn.gh-proxy.org/https://github.com/pur1fying/blue_archive_auto_script.git"},
        {false, "gh_proxy", "https://gh-proxy.org/https://github.com/pur1fying/blue_archive_auto_script.git"},
        {false, "sevencdn", "https://gh.sevencdn.com/https://github.com/pur1fying/blue_archive_auto_script.git"},
        {false, "githubfast", "https://githubfast.com/pur1fying/blue_archive_auto_script.git"},
        {false, "baas_cdn", "https://baas-cdn.kiramei.workers.dev/https://github.com/pur1fying/blue_archive_auto_script.git"},
        {true, "github", "https://github.com/Kiramei/baas-dev.git"},
        {true, "gitee", "https://gitee.com/kiramei/baas-dev.git"},
        {true, "github_proxy_v4", "https://v4.gh-proxy.org/https://github.com/Kiramei/baas-dev.git"},
        {true, "github_proxy_v6", "https://v6.gh-proxy.org/https://github.com/Kiramei/baas-dev.git"},
        {true, "github_proxy_cdn", "https://cdn.gh-proxy.org/https://github.com/Kiramei/baas-dev.git"},
        {true, "gh_proxy", "https://gh-proxy.org/https://github.com/Kiramei/baas-dev.git"},
        {true, "sevencdn", "https://gh.sevencdn.com/https://github.com/Kiramei/baas-dev.git"},
        {true, "githubfast", "https://githubfast.com/Kiramei/baas-dev.git"},
        {true, "baas_cdn", "https://baas-cdn.kiramei.workers.dev/https://github.com/Kiramei/baas-dev.git"},
    };
    for (const auto& item : cases) {
        TempProject project;
        write_bytes(project.root / "setup.toml",
                    "[General]\ndev = "
                        + std::string(item.dev ? "true" : "false")
                        + "\n[URLs]\nREPO_URL_HTTP = \"" + item.url + "\"\n");
        adapters::FileResourceStore store(project.root, dependencies());
        auto pulled = store.pull(
            {channels::SyncResource::setup_toml, std::nullopt}, {});
        check(pulled
                  && Json::parse(pulled->data_json)["updateMethod"] == item.method
                  && Json::parse(pulled->data_json)["repoUrl"] == item.url,
              "legacy setup proxy URL round-trips to its update method");
    }
}

void test_setup_toml_eof_insertion_keeps_valid_line_boundaries()
{
    TempProject project;
    const auto path = project.root / "setup.toml";
    write_bytes(path, "[general]\ntransport = \"websocket\"");
    adapters::FileResourceStore store(project.root, dependencies());
    auto initial = store.pull(
        {channels::SyncResource::setup_toml, std::nullopt}, {});
    auto applied = store.apply_patch(
        {{channels::SyncResource::setup_toml, std::string{"global"}},
         initial ? initial->timestamp_json : "0",
         {{"replace", "/channel", "\"dev\""}}}, {});
    const auto disk = read_bytes(path);
    check(applied
              && disk.find("transport = \"websocket\"\nchannel = \"dev\"\n")
                     != std::string::npos,
          "setup additions after an unterminated EOF start on a new line");
    adapters::FileResourceStore reloaded(project.root, dependencies());
    auto after_restart = reloaded.pull(
        {channels::SyncResource::setup_toml, std::nullopt}, {});
    check(after_restart
              && Json::parse(after_restart->data_json)["channel"] == "dev",
          "setup merged at EOF remains readable after restart");
}

void test_setup_toml_scalar_table_conflicts_fail_closed()
{
    const std::array conflicting_sources{
        std::string{"[general]\ntransport.kind = \"future\"\n"},
        std::string{"[\"general\".\"transport\"]\nkind = \"future\"\n"},
        std::string{"general.transport.kind = \"future\"\n"},
        std::string{"[[general]]\nname = \"future\"\n"},
    };
    for (const auto& source : conflicting_sources) {
        TempProject project;
        const auto path = project.root / "setup.toml";
        write_bytes(path, source);
        adapters::FileResourceStore store(project.root, dependencies());
        const channels::ResourceKey key{
            channels::SyncResource::setup_toml, std::string{"global"}};
        auto initial = store.pull(key, {});
        check(static_cast<bool>(initial),
              "valid dotted/table setup conflict fixture projects safely");
        std::atomic_size_t publications{};
        auto subscription = store.subscribe_updates(
            [&publications](channels::ResourceUpdate) { ++publications; });
        auto applied = store.apply_patch(
            {key, initial ? initial->timestamp_json : "0",
             {{"replace", "/transport", "\"pipe\""}}}, {});
        check(!applied
                  && applied.error
                         == channels::ResourceStoreError::invalid_data,
              "projected scalar never overwrites a dotted key or table");
        check(read_bytes(path) == source && publications.load() == 0,
              "scalar/table conflict leaves valid TOML and publications unchanged");
    }

    TempProject child_project;
    const auto child_path = child_project.root / "setup.toml";
    write_bytes(child_path,
                "[[general.plugins]]\nname = \"future\"\n");
    adapters::FileResourceStore child_store(child_project.root, dependencies());
    const channels::ResourceKey child_key{
        channels::SyncResource::setup_toml, std::string{"global"}};
    auto child_initial = child_store.pull(child_key, {});
    auto child_applied = child_store.apply_patch(
        {child_key, child_initial ? child_initial->timestamp_json : "0",
         {{"replace", "/channel", "\"dev\""}}}, {});
    const auto child_disk = read_bytes(child_path);
    adapters::FileResourceStore child_reloaded(child_project.root, dependencies());
    auto child_after_restart = child_reloaded.pull(child_key, {});
    check(child_applied
              && child_disk.find("[general]\n") != std::string::npos
              && child_disk.find("channel = \"dev\"\n") != std::string::npos
              && child_after_restart
              && Json::parse(child_after_restart->data_json)["channel"] == "dev",
          "unrelated child arrays-of-tables do not block a projected scalar");
}

void test_create_config_transaction_concurrency_cancellation_and_cleanup()
{
    TempProject project;
    auto create_dependencies = test_defaults::with_synthetic_defaults();
    create_dependencies.clock = [] { return 7'000.9; };
    adapters::FileResourceStore store(
        project.root, std::move(create_dependencies));

    std::optional<adapters::ConfigCreateResult> first;
    std::optional<adapters::ConfigCreateResult> second;
    std::thread one([&] { first = store.create_config("one", "日服", {}); });
    std::thread two([&] { second = store.create_config("two", "官服", {}); });
    one.join();
    two.join();
    check(first && second && *first && *second
              && first->serial != second->serial
              && ((first->serial == "7000" && second->serial == "7001")
                  || (first->serial == "7001" && second->serial == "7000")),
          "concurrent creates serialize and allocate distinct millisecond ids");
    const auto listed = store.config_list({});
    check(listed && Json::parse(listed->data_json)
                        == Json::array({"7000", "7001"}),
          "both committed creates become atomically list-visible");
    for (const auto& result : {*first, *second}) {
        auto config = store.pull(config_key(result.serial), {});
        auto event = store.pull(event_key(result.serial), {});
        check(config && event && Json::parse(event->data_json).size() == 26
                  && std::filesystem::is_regular_file(
                      project.root / "config" / result.serial / "switch.json"),
              "create publishes complete config/event/switch resources");
    }

    const auto rejected = store.create_config(
        "rejected", "日服", {}, [](const std::string_view) { return false; });
    check(rejected.error == adapters::ConfigCommandError::cancelled
              && !std::filesystem::exists(project.root / "config" / "7002"),
          "a rejected commit claim leaves no visible directory");
    bool private_left{};
    for (const auto& child :
         std::filesystem::directory_iterator(project.root / "config")) {
        private_left = private_left
            || child.path().filename().string().starts_with(".baas-");
    }
    check(!private_left, "claim rejection reclaims every private staging sibling");

    std::stop_source cancelled;
    cancelled.request_stop();
    bool cancelled_claim_called{};
    const auto pre_cancelled = store.create_config(
        "cancelled", "日服", cancelled.get_token(),
        [&](const std::string_view) {
            cancelled_claim_called = true;
            return true;
        });
    check(pre_cancelled.error == adapters::ConfigCommandError::cancelled
              && !cancelled_claim_called
              && !std::filesystem::exists(project.root / "config" / "7002"),
          "pre-commit cancellation wins create without invoking its claim or mutating");

    std::stop_source late_stop;
    const auto committed = store.create_config(
        "committed", "日服", late_stop.get_token(),
        [&](const std::string_view) {
            static_cast<void>(late_stop.request_stop());
            return true;
        });
    check(committed && late_stop.stop_requested()
              && std::filesystem::is_directory(
                  project.root / "config" / committed.serial),
          "a successful create claim can synchronously request stop without deadlock and still commits");

    TempProject failed_project;
    std::filesystem::create_directory(
        failed_project.root / "config" / "static.json");
    adapters::FileResourceStore failed_store(
        failed_project.root, dependencies());
    const auto failed = failed_store.create_config("bad", "日服", {});
    check(failed.error == adapters::ConfigCommandError::invalid_data
              && Json::parse(failed_store.config_list({})->data_json).empty(),
          "invalid static target fails closed before config publication");
    private_left = false;
    for (const auto& child :
         std::filesystem::directory_iterator(failed_project.root / "config")) {
        private_left = private_left
            || child.path().filename().string().starts_with(".baas-");
    }
    check(!private_left, "static initialization failure cleans private staging");

    TempProject invalid_project;
    adapters::FileResourceStore invalid_store(
        invalid_project.root, dependencies());
    const auto before_invalid = invalid_store.config_list({});
    for (const auto& [name, server] :
         std::vector<std::pair<std::string, std::string>>{
             {"", "日服"}, {" \t\r\n", "日服"},
             {"\xE3\x80\x80", "日服"}, {"valid", "unknown"}}) {
        const auto rejected_invalid = invalid_store.create_config(name, server, {});
        check(rejected_invalid.error == adapters::ConfigCommandError::invalid_data,
              "empty/Unicode-whitespace names and unknown servers fail closed");
    }
    const auto after_invalid = invalid_store.config_list({});
    check(before_invalid && after_invalid
              && before_invalid->data_json == after_invalid->data_json
              && !std::filesystem::exists(invalid_project.root / "error.log")
              && !std::filesystem::exists(invalid_project.root / "config" / "static.json"),
          "invalid create inputs have zero filesystem and error-log side effects");

    TempProject frozen_project;
    for (std::size_t index = 0; index < 1'024; ++index) {
        std::filesystem::create_directory(
            frozen_project.root / "config" / std::to_string(10'000 + index));
    }
    auto frozen_dependencies = test_defaults::with_synthetic_defaults();
    frozen_dependencies.clock = [] { return 10'000.0; };
    adapters::FileResourceStore frozen_store(
        frozen_project.root, std::move(frozen_dependencies));
    const auto frozen = frozen_store.create_config("bounded", "日服", {});
    check(frozen.error == adapters::ConfigCommandError::conflict
              && !std::filesystem::exists(
                  frozen_project.root / "config" / "static.json"),
          "a frozen/rolled-back clock probes at most 1,024 ids then fails without side effects");

    TempProject full_project;
    full_project.add_pair(
        "only", R"({"name":"only","server":"日服"})", "[]");
    channels::ResourceStoreLimits one_limit;
    one_limit.max_resources = 1;
    adapters::FileResourceStore full_store(
        full_project.root, dependencies(), one_limit);
    const auto full = full_store.create_config("overflow", "日服", {});
    const auto full_copy = full_store.copy_config("only", {});
    check(full.error == adapters::ConfigCommandError::capacity
              && full_copy.error == adapters::ConfigCommandError::capacity
              && Json::parse(full_store.config_list({})->data_json)
                     == Json::array({"only"})
              && !std::filesystem::exists(
                  full_project.root / "config" / "static.json"),
          "create enforces max_resources before staging or static mutation");

    TempProject missing_static_failure_project;
    auto missing_static_failure_dependencies =
        test_defaults::with_synthetic_defaults();
    missing_static_failure_dependencies.clock = [] { return 11'000.0; };
    missing_static_failure_dependencies.atomic_writer =
        [](const std::filesystem::path&, const std::string_view) {
            return adapters::AtomicWriteResult::not_committed;
        };
    adapters::FileResourceStore missing_static_failure_store(
        missing_static_failure_project.root,
        std::move(missing_static_failure_dependencies));
    const auto missing_static_failure = missing_static_failure_store.create_config(
        "atomic-failure", "日服", {});
    check(missing_static_failure.error
                  == adapters::ConfigCommandError::internal_error
              && !std::filesystem::exists(
                  missing_static_failure_project.root / "config" / "static.json")
              && !std::filesystem::exists(
                  missing_static_failure_project.root / "config" / "11000"),
          "missing-static precommit failure exposes neither partial JSON nor config");

    TempProject injected_project;
    const std::string original_static = R"({"sentinel":"preserve"})";
    write_bytes(injected_project.root / "config" / "static.json", original_static);
    auto injected_dependencies = test_defaults::with_synthetic_defaults();
    injected_dependencies.clock = [] { return 12'000.0; };
    injected_dependencies.config_create_fault_injector =
        [](const std::string_view step) {
            return step == "before_directory_commit";
        };
    adapters::FileResourceStore injected_store(
        injected_project.root, std::move(injected_dependencies));
    const auto injected = injected_store.create_config("fault", "日服", {});
    const auto upgraded_static = Json::parse(read_bytes(
        injected_project.root / "config" / "static.json"));
    check(injected.error == adapters::ConfigCommandError::internal_error
              && upgraded_static.size() == 25
              && upgraded_static.contains("create_item_order")
              && !std::filesystem::exists(
                  injected_project.root / "config" / "12000"),
          "static metadata upgrade commits independently before directory publication");
    private_left = false;
    for (const auto& child :
         std::filesystem::directory_iterator(injected_project.root / "config")) {
        private_left = private_left
            || child.path().filename().string().starts_with(".baas-");
    }
    check(!private_left && !std::filesystem::exists(injected_project.root / "error.log"),
          "fault injection reclaims staging/backup and emits no error.log");

    TempProject throwing_fault_project;
    auto throwing_fault_dependencies = test_defaults::with_synthetic_defaults();
    throwing_fault_dependencies.clock = [] { return 13'000.0; };
    throwing_fault_dependencies.config_create_fault_injector =
        [](const std::string_view step) -> bool {
            if (step == "before_static_commit") {
                throw std::bad_alloc{};
            }
            return false;
        };
    adapters::FileResourceStore throwing_fault_store(
        throwing_fault_project.root, std::move(throwing_fault_dependencies));
    const auto throwing_fault = throwing_fault_store.create_config(
        "throwing-fault", "日服", {});
    private_left = false;
    for (const auto& child : std::filesystem::directory_iterator(
             throwing_fault_project.root / "config")) {
        private_left = private_left
            || child.path().filename().string().starts_with(".baas-");
    }
    check(throwing_fault.error == adapters::ConfigCommandError::internal_error
              && !private_left
              && !std::filesystem::exists(
                  throwing_fault_project.root / "config" / "13000")
              && !std::filesystem::exists(
                  throwing_fault_project.root / "config" / "static.json"),
          "an exception after staging creation reclaims the complete transaction");

    TempProject missing_root_project;
    std::filesystem::remove(missing_root_project.root / "config");
    adapters::FileResourceStore missing_root_store(
        missing_root_project.root, dependencies());
    const auto missing_root = missing_root_store.create_config(
        "never", "日服", {});
    check(missing_root.error == adapters::ConfigCommandError::invalid_data
              && !std::filesystem::exists(missing_root_project.root / "config"),
          "create requires the AuthOwner-protected config-root precondition");

    TempProject reparse_project;
    const auto external = reparse_project.root / "external-config";
    std::filesystem::create_directory(external);
    std::filesystem::remove(reparse_project.root / "config");
    std::error_code link_error;
    std::filesystem::create_directory_symlink(
        external, reparse_project.root / "config", link_error);
    if (!link_error) {
        bool rejected_reparse{};
        try {
            adapters::FileResourceStore unsafe(reparse_project.root, dependencies());
        } catch (const std::invalid_argument&) {
            rejected_reparse = true;
        }
        check(rejected_reparse
                  && std::filesystem::is_empty(external),
              "a reparse/symlink config root is rejected before create side effects");
    }
}

void test_cross_store_pull_preserves_refresh_publication_baseline()
{
    {
        TempProject project;
        project.add_pair("alpha", R"({"value":"old"})");
        project.add_globals();
        adapters::FileResourceStore writer(project.root, dependencies());
        adapters::FileResourceStore reader(project.root, dependencies());

        const auto old = reader.pull(config_key(), {});
        std::vector<channels::ResourceUpdate> updates;
        auto subscription = reader.subscribe_updates(
            [&updates](channels::ResourceUpdate update) {
                updates.push_back(std::move(update));
            });
        const auto writer_timestamp = timestamp_of(writer, config_key());
        const auto replaced = writer.apply_patch(
            replace_request(
                config_key(), writer_timestamp, "/value", R"("new")"),
            {});
        const auto fresh = reader.pull(config_key(), {});
        const auto refreshed = reader.refresh(config_key(), "filesystem");

        check(old && subscription && replaced
                  && replaced->disposition
                         == channels::ResourcePatchDisposition::applied
                  && fresh && Json::parse(fresh->data_json)["value"] == "new"
                  && refreshed.disposition
                         == adapters::ResourceRefreshDisposition::updated
                  && refreshed.published && updates.size() == 1,
              "a fresh pull after a second store replacement preserves exactly one refresh publication");
        if (updates.size() == 1) {
            const auto operations = Json::parse(updates.front().operations_json);
            check(operations.size() == 1
                      && operations[0]["op"] == "replace"
                      && operations[0]["path"] == ""
                      && operations[0]["value"]["value"] == "new",
                  "the preserved replacement baseline publishes the new root snapshot");
        }
        const auto unchanged = reader.refresh(config_key(), "filesystem");
        check(unchanged.disposition
                      == adapters::ResourceRefreshDisposition::unchanged
                  && !unchanged.published && updates.size() == 1,
              "the replacement is not published again after refresh advances the baseline");
    }

    {
        TempProject project;
        project.add_pair("remove-me", R"({"value":"old"})");
        project.add_pair("keep-me", R"({"value":"keep"})");
        project.add_globals();
        adapters::FileResourceStore writer(project.root, dependencies());
        adapters::FileResourceStore reader(project.root, dependencies());

        const auto old = reader.pull(config_key("remove-me"), {});
        std::vector<channels::ResourceUpdate> updates;
        auto subscription = reader.subscribe_updates(
            [&updates](channels::ResourceUpdate update) {
                updates.push_back(std::move(update));
            });
        const auto removed = writer.remove_config("remove-me", {}, [] {
            return true;
        });
        const auto missing = reader.pull(config_key("remove-me"), {});
        const auto refreshed = reader.refresh(
            config_key("remove-me"), "filesystem");

        check(old && subscription && removed && !missing
                  && missing.error == channels::ResourceStoreError::not_found
                  && refreshed.disposition
                         == adapters::ResourceRefreshDisposition::removed
                  && refreshed.published && updates.size() == 1,
              "a not-found pull after a second store removal preserves exactly one refresh publication");
        if (updates.size() == 1) {
            check(Json::parse(updates.front().operations_json)
                      == Json::array({
                          Json{{"op", "remove"}, {"path", ""}}}),
                  "the preserved removal baseline publishes one root remove");
        }
        const auto absent = reader.refresh(
            config_key("remove-me"), "filesystem");
        check(absent.disposition
                      == adapters::ResourceRefreshDisposition::not_found
                  && !absent.published && updates.size() == 1,
              "the removal is not published again after refresh clears the baseline");
    }
}

void test_config_archive_export_and_python_compatible_import()
{
    TempProject export_project;
    export_project.add_pair(
        "source",
        R"({"name":"  bad<name>:\"/\\|?*  ","server":"日服"})");
    std::string binary_payload{"\0\x01", 2};
    binary_payload += "archive";
    binary_payload.push_back(static_cast<char>(0xff));
    write_bytes(
        export_project.root / "config" / "source" / "assets" / "blob.bin",
        binary_payload);
    write_bytes(
        export_project.root / "config" / "source" / "display.json",
        R"({"transient":true})");
    adapters::FileResourceStore export_store(export_project.root, dependencies());
    const auto exported = export_store.export_config("source", {});
    check(exported && exported.filename == "bad_name________.zip"
              && !exported.content.empty(),
          "export uses the stripped Python-compatible sanitized filename");
    const auto decoded = archive::decode(exported.content, {});
    bool binary_preserved{};
    bool display_exported{};
    if (decoded) {
        for (const auto& entry : decoded.entries) {
            if (entry.path == "assets/blob.bin") {
                binary_preserved = entry.bytes == byte_vector(binary_payload);
            }
            display_exported = display_exported || entry.path == "display.json";
        }
    }
    check(decoded && binary_preserved && display_exported,
          "export recursively preserves attached binary and compatibility files");

    TempProject project;
    project.add_pair(
        "old-a", R"({"name":"Archive Name","server":"日服","old":1})");
    project.add_pair(
        "old-b", R"({"name":"  Archive Name  ","server":"日服","old":2})");
    project.add_pair(
        "other", R"({"name":"Other","server":"日服","keep":true})");
    auto import_dependencies = test_defaults::with_synthetic_defaults();
    import_dependencies.clock = [] { return 20'000.9; };
    adapters::FileResourceStore store(
        project.root, std::move(import_dependencies));
    check(store.pull(config_key("old-a"), {})
              && store.pull(config_key("old-b"), {})
              && store.pull(config_key("other"), {})
              && store.config_list({}),
          "archive replacement fixture primes config and list caches");

    const std::string imported_binary{"A\0B\xff", 4};
    const auto input = archive_bytes({
        {"config.json",
         R"({"name":"  Archive Name  ","server":"日服","autostart":true,"future":"drop","create_item_holding_quantity":{"unknown":9}})"},
        {"event.json",
         R"([{"func_name":"restart","enabled":false,"daily_reset":[[1,2]],"future":9}])"},
        {"switch.json", R"({"sentinel":true})"},
        {"display.json", R"({"must":"disappear"})"},
        {"assets/blob.bin", imported_binary},
    });
    bool claim_seen{};
    const auto imported = store.import_config(
        {input.data(), input.size()}, {},
        [&](const std::string_view serial, const std::string_view name) {
            claim_seen = serial == "20000" && name == "Archive Name";
            return true;
        });
    const auto target = project.root / "config" / "20000";
    const auto pulled_config = store.pull(config_key("20000"), {});
    const auto pulled_event = store.pull(event_key("20000"), {});
    const auto listed = store.config_list({});
    bool migrated_event{};
    if (pulled_event) {
        const auto event = Json::parse(pulled_event->data_json);
        const auto restart = std::find_if(
            event.begin(), event.end(), [](const Json& item) {
                return item.value("func_name", std::string{}) == "restart";
            });
        migrated_event = restart != event.end()
            && (*restart)["enabled"] == false
            && (*restart)["daily_reset"] == Json::array({Json::array({0, 0, 0})})
            && (*restart)["future"] == 9
            && restart->contains("priority");
    }
    bool migrated_config{};
    if (pulled_config) {
        const auto config = Json::parse(pulled_config->data_json);
        migrated_config = config["name"] == "  Archive Name  "
            && config["autostart"] == true && config.contains("then")
            && !config.contains("future")
            && !config["create_item_holding_quantity"].contains("unknown");
    }
    const auto migrated_switch = Json::parse(read_bytes(target / "switch.json"));
    check(imported && imported.serial == "20000"
              && imported.name == "Archive Name" && claim_seen,
          "root archive import claims and returns the trimmed Python-compatible identity");
    check(migrated_config,
          "root archive import migrates config defaults and removes unknown keys");
    check(migrated_event,
          "root archive import migrates event fields and malformed reset triples");
    check(migrated_switch.is_array() && !migrated_switch.empty()
              && migrated_switch.front().value("config", std::string{})
                     == "cafeInvite",
          "root archive import replaces archived switches with current defaults");
    check(read_bytes(target / "assets" / "blob.bin") == imported_binary
              && !std::filesystem::exists(target / "display.json"),
          "root archive import preserves binary attachments and deletes display metadata");
    check(!std::filesystem::exists(project.root / "config" / "old-a")
              && !std::filesystem::exists(project.root / "config" / "old-b")
              && std::filesystem::is_directory(project.root / "config" / "other")
              && !store.pull(config_key("old-a"), {})
              && !store.pull(config_key("old-b"), {})
              && store.pull(config_key("other"), {})
              && listed
              && Json::parse(listed->data_json)
                     == Json::array({"20000", "other"})
              && !has_private_config_artifact(project.root),
          "import atomically replaces every complete same-name pair, preserves other pairs, and invalidates caches");

    TempProject prefixed_project;
    auto prefixed_dependencies = test_defaults::with_synthetic_defaults();
    prefixed_dependencies.clock = [] { return 21'000.0; };
    adapters::FileResourceStore prefixed_store(
        prefixed_project.root, std::move(prefixed_dependencies));
    const auto prefixed = archive_bytes({
        {"bundle/config.json", R"({"name":"Prefix","server":"官服"})"},
        {"bundle/nested/keep.bin", std::string_view{"x\0y", 3}},
    });
    const auto prefixed_import = prefixed_store.import_config(
        {prefixed.data(), prefixed.size()}, {});
    check(prefixed_import && prefixed_import.serial == "21000"
              && prefixed_import.name == "Prefix"
              && read_bytes(prefixed_project.root / "config" / "21000"
                            / "nested" / "keep.bin")
                     == std::string{"x\0y", 3}
              && prefixed_store.pull(config_key("21000"), {})
              && prefixed_store.pull(event_key("21000"), {})
              && Json::parse(prefixed_store.config_list({})->data_json)
                     == Json::array({"21000"}),
          "a Python-compatible single top-level directory archive imports as one complete visible pair");
}

void test_copy_and_remove_claim_self_cancel_linearization()
{
    TempProject copy_project;
    copy_project.add_pair(
        "source", R"({"name":"Source","server":"日服"})");
    auto copy_dependencies = test_defaults::with_synthetic_defaults();
    copy_dependencies.clock = [] { return 31'000.0; };
    adapters::FileResourceStore copy_store(
        copy_project.root, std::move(copy_dependencies));

    std::stop_source copy_stop;
    bool copy_claim_called{};
    std::string claimed_copy_serial;
    std::string claimed_copy_name;
    const auto copied = copy_store.copy_config(
        "source", copy_stop.get_token(),
        [&](const std::string_view serial, const std::string_view name) {
            copy_claim_called = true;
            claimed_copy_serial = serial;
            claimed_copy_name = name;
            static_cast<void>(copy_stop.request_stop());
            return true;
        });
    check(copied && copy_claim_called && copy_stop.stop_requested()
              && copied.serial == "31000"
              && copied.serial == claimed_copy_serial
              && copied.name == claimed_copy_name
              && std::filesystem::is_directory(
                  copy_project.root / "config" / copied.serial)
              && copy_store.pull(config_key(copied.serial), {})
              && copy_store.pull(event_key(copied.serial), {})
              && !has_private_config_artifact(copy_project.root),
          "a successful copy claim can synchronously request stop without deadlock and still commits");

    std::stop_source copy_cancelled;
    copy_cancelled.request_stop();
    bool cancelled_copy_claim_called{};
    const auto uncopied = copy_store.copy_config(
        "source", copy_cancelled.get_token(),
        [&](const std::string_view, const std::string_view) {
            cancelled_copy_claim_called = true;
            return true;
        });
    check(uncopied.error == adapters::ConfigCommandError::cancelled
              && !cancelled_copy_claim_called
              && !std::filesystem::exists(
                  copy_project.root / "config" / "31001")
              && Json::parse(copy_store.config_list({})->data_json)
                     == Json::array({"31000", "source"})
              && !has_private_config_artifact(copy_project.root),
          "pre-cancelled copy wins without invoking its claim or publishing another pair");

    TempProject remove_project;
    remove_project.add_pair(
        "remove-me", R"({"name":"Remove","server":"日服"})");
    remove_project.add_pair(
        "keep-me", R"({"name":"Keep","server":"日服"})");
    adapters::FileResourceStore remove_store(remove_project.root, dependencies());
    check(remove_store.pull(config_key("remove-me"), {})
              && remove_store.pull(event_key("remove-me"), {}),
          "remove self-cancel fixture primes the retiring pair cache");
    std::stop_source remove_stop;
    bool remove_claim_called{};
    const auto removed = remove_store.remove_config(
        "remove-me", remove_stop.get_token(), [&] {
            remove_claim_called = true;
            static_cast<void>(remove_stop.request_stop());
            return true;
        });
    const auto removed_pull = remove_store.pull(config_key("remove-me"), {});
    check(removed && remove_claim_called && remove_stop.stop_requested()
              && !std::filesystem::exists(
                  remove_project.root / "config" / "remove-me")
              && !removed_pull
              && removed_pull.error == channels::ResourceStoreError::not_found
              && Json::parse(remove_store.config_list({})->data_json)
                     == Json::array({"keep-me"})
              && !has_private_config_artifact(remove_project.root),
          "a successful remove claim can synchronously request stop without deadlock and still commits");

    std::stop_source remove_cancelled;
    remove_cancelled.request_stop();
    bool cancelled_remove_claim_called{};
    const auto kept = remove_store.remove_config(
        "keep-me", remove_cancelled.get_token(), [&] {
            cancelled_remove_claim_called = true;
            return true;
        });
    check(kept.error == adapters::ConfigCommandError::cancelled
              && !cancelled_remove_claim_called
              && std::filesystem::is_directory(
                  remove_project.root / "config" / "keep-me")
              && remove_store.pull(config_key("keep-me"), {})
              && Json::parse(remove_store.config_list({})->data_json)
                     == Json::array({"keep-me"})
              && !has_private_config_artifact(remove_project.root),
          "pre-cancelled remove wins without invoking its claim or retiring the pair");
}

void test_config_archive_import_claim_cancellation_and_rollback()
{
    const auto input = archive_bytes({
        {"config.json", R"({"name":"Atomic","server":"日服"})"},
        {"payload.bin", "new"},
    });

    TempProject rejected_project;
    rejected_project.add_pair(
        "old", R"({"name":"Atomic","server":"日服","sentinel":"old"})");
    auto rejected_dependencies = test_defaults::with_synthetic_defaults();
    rejected_dependencies.clock = [] { return 22'000.0; };
    adapters::FileResourceStore rejected_store(
        rejected_project.root, std::move(rejected_dependencies));
    check(rejected_store.pull(config_key("old"), {})
              && rejected_store.config_list({}),
          "claim rejection fixture primes the old cached pair");
    bool claim_seen{};
    const auto rejected = rejected_store.import_config(
        {input.data(), input.size()}, {},
        [&](const std::string_view serial, const std::string_view name) {
            claim_seen = serial == "22000" && name == "Atomic";
            return false;
        });
    const auto old_after_rejection = rejected_store.pull(config_key("old"), {});
    check(rejected.error == adapters::ConfigCommandError::cancelled && claim_seen
              && old_after_rejection
              && Json::parse(old_after_rejection->data_json)["sentinel"] == "old"
              && Json::parse(rejected_store.config_list({})->data_json)
                     == Json::array({"old"})
              && !std::filesystem::exists(
                  rejected_project.root / "config" / "22000")
              && !has_private_config_artifact(rejected_project.root),
          "a rejected response claim publishes nothing and reclaims import staging");

    std::stop_source stopped;
    stopped.request_stop();
    bool cancelled_import_claim_called{};
    const auto cancelled = rejected_store.import_config(
        {input.data(), input.size()}, stopped.get_token(),
        [&](const std::string_view, const std::string_view) {
            cancelled_import_claim_called = true;
            return true;
        });
    check(cancelled.error == adapters::ConfigCommandError::cancelled
              && !cancelled_import_claim_called
              && Json::parse(rejected_store.config_list({})->data_json)
                     == Json::array({"old"})
              && !has_private_config_artifact(rejected_project.root),
          "pre-cancelled import does not decode, retire, or publish filesystem state");

    for (const std::string step : {"before_retire", "before_target_commit"}) {
        TempProject project;
        project.add_pair(
            "old", R"({"name":"Atomic","server":"日服","sentinel":"old"})");
        project.add_pair(
            "other", R"({"name":"Other","server":"日服","sentinel":"keep"})");
        auto fault_dependencies = test_defaults::with_synthetic_defaults();
        fault_dependencies.clock = [] { return 23'000.0; };
        fault_dependencies.config_archive_fault_injector =
            [step](const std::string_view candidate) { return candidate == step; };
        adapters::FileResourceStore store(
            project.root, std::move(fault_dependencies));
        check(store.pull(config_key("old"), {})
                  && store.pull(config_key("other"), {})
                  && store.config_list({}),
              "archive fault fixture primes all affected caches");
        const auto failed = store.import_config(
            {input.data(), input.size()}, {},
            [](const std::string_view, const std::string_view) { return true; });
        const auto old = store.pull(config_key("old"), {});
        const auto other = store.pull(config_key("other"), {});
        const auto list = store.config_list({});
        check(failed.error == adapters::ConfigCommandError::internal_error
                  && old && other
                  && Json::parse(old->data_json)["sentinel"] == "old"
                  && Json::parse(other->data_json)["sentinel"] == "keep"
                  && list
                  && Json::parse(list->data_json) == Json::array({"old", "other"})
                  && !std::filesystem::exists(project.root / "config" / "23000")
                  && !has_private_config_artifact(project.root),
              "archive fault " + step
                  + " rolls back retirement and leaves disk/list/pull caches unchanged");
    }
}

void test_config_archive_claim_self_cancel_and_crash_recovery()
{
    const auto input = archive_bytes({
        {"config.json", R"({"name":"Claim Wins","server":"日服"})"},
    });
    TempProject claim_project;
    auto claim_dependencies = test_defaults::with_synthetic_defaults();
    claim_dependencies.clock = [] { return 26'000.0; };
    adapters::FileResourceStore claim_store(
        claim_project.root, std::move(claim_dependencies));
    std::stop_source self_cancel;
    const auto claimed = claim_store.import_config(
        {input.data(), input.size()}, self_cancel.get_token(),
        [&](const std::string_view, const std::string_view) {
            static_cast<void>(self_cancel.request_stop());
            return true;
        });
    check(claimed && claimed.serial == "26000"
              && self_cancel.stop_requested()
              && std::filesystem::is_directory(
                  claim_project.root / "config" / "26000")
              && !has_private_config_artifact(claim_project.root),
          "a claim that synchronously requests stop must not self-deadlock and wins the commit gate");

    TempProject rollback_project;
    rollback_project.add_pair(
        "old-a", R"({"name":"Atomic","server":"日服"})");
    rollback_project.add_pair(
        "old-b", R"({"name":"Atomic","server":"日服"})");
    const auto rollback_root = rollback_project.root / "config";
    const std::string rollback_transaction = "123-27000-1";
    const std::string rollback_journal_name =
        ".baas-import-journal-" + rollback_transaction + ".json";
    const std::string rollback_staging_name =
        ".baas-import-" + rollback_transaction;
    const std::string rollback_tomb_a =
        ".baas-import-retired-" + rollback_transaction + "-0";
    const std::string rollback_tomb_b =
        ".baas-import-retired-" + rollback_transaction + "-1";
    std::filesystem::rename(
        rollback_root / "old-a", rollback_root / rollback_tomb_a);
    std::filesystem::create_directory(rollback_root / rollback_staging_name);
    write_bytes(
        rollback_root / rollback_staging_name / "config.json",
        R"({"name":"Atomic","server":"日服"})");
    write_bytes(
        rollback_root / rollback_staging_name / ".baas-import-commit",
        rollback_journal_name);
    write_bytes(
        rollback_root / rollback_journal_name,
        Json{{"version", 1},
             {"staging", rollback_staging_name},
             {"target", "27000"},
             {"retired",
              Json::array({
                  Json{{"source", "old-a"}, {"tombstone", rollback_tomb_a}},
                  Json{{"source", "old-b"}, {"tombstone", rollback_tomb_b}},
              })}}
            .dump());
    adapters::FileResourceStore recovered_rollback(rollback_project.root);
    const auto rolled_back_list = recovered_rollback.config_list({});
    check(rolled_back_list
              && Json::parse(rolled_back_list->data_json)
                  == Json::array({"old-a", "old-b"})
              && std::filesystem::is_directory(rollback_root / "old-a")
              && std::filesystem::is_directory(rollback_root / "old-b")
              && !std::filesystem::exists(rollback_root / "27000")
              && !has_private_config_artifact(rollback_project.root),
          "startup recovery must restore every retired profile after a pre-publication crash");

    TempProject committed_project;
    committed_project.add_pair(
        "old", R"({"name":"Atomic","server":"日服"})");
    const auto committed_root = committed_project.root / "config";
    const std::string committed_transaction = "123-28000-2";
    const std::string committed_journal_name =
        ".baas-import-journal-" + committed_transaction + ".json";
    const std::string committed_staging_name =
        ".baas-import-" + committed_transaction;
    const std::string committed_tomb =
        ".baas-import-retired-" + committed_transaction + "-0";
    std::filesystem::rename(
        committed_root / "old", committed_root / committed_tomb);
    std::filesystem::create_directory(committed_root / "28000");
    write_bytes(
        committed_root / "28000" / "config.json",
        R"({"name":"Atomic","server":"日服"})");
    write_bytes(committed_root / "28000" / "event.json", "[]");
    write_bytes(
        committed_root / "28000" / ".baas-import-commit",
        committed_journal_name);
    write_bytes(
        committed_root / committed_journal_name,
        Json{{"version", 1},
             {"staging", committed_staging_name},
             {"target", "28000"},
             {"retired",
              Json::array({
                  Json{{"source", "old"}, {"tombstone", committed_tomb}},
              })}}
            .dump());
    adapters::FileResourceStore recovered_commit(committed_project.root);
    const auto committed_list = recovered_commit.config_list({});
    check(committed_list
              && Json::parse(committed_list->data_json)
                  == Json::array({"28000"})
              && !std::filesystem::exists(committed_root / "old")
              && !std::filesystem::exists(committed_root / committed_tomb)
              && !std::filesystem::exists(
                  committed_root / "28000" / ".baas-import-commit")
              && !has_private_config_artifact(committed_project.root),
          "startup recovery must finish tombstone cleanup after a published import crash");
}

void test_config_archive_root_lock_and_journal_binding()
{
    const auto input = archive_bytes({
        {"config.json", R"({"name":"Serialized","server":"日服"})"},
    });
    TempProject live_project;
    live_project.add_pair(
        "old", R"({"name":"Serialized","server":"日服"})");
    std::mutex gate_mutex;
    std::condition_variable gate_condition;
    bool before_retire{};
    bool release_import{};
    auto dependencies = test_defaults::with_synthetic_defaults();
    dependencies.clock = [] { return 29'000.0; };
    dependencies.config_archive_fault_injector =
        [&](const std::string_view step) {
            if (step != "before_retire") return false;
            std::unique_lock lock(gate_mutex);
            before_retire = true;
            gate_condition.notify_all();
            gate_condition.wait(lock, [&] { return release_import; });
            return false;
        };
    adapters::FileResourceStore live_store(
        live_project.root, std::move(dependencies));
    std::atomic<bool> import_succeeded{};
    std::thread importer([&] {
        const auto imported = live_store.import_config(
            {input.data(), input.size()}, {});
        import_succeeded.store(
            imported && imported.serial == "29000", std::memory_order_release);
    });
    bool reached_gate{};
    {
        std::unique_lock lock(gate_mutex);
        reached_gate = gate_condition.wait_for(
            lock, 5s, [&] { return before_retire; });
    }
    bool competing_store_rejected{};
    if (reached_gate) {
        try {
            adapters::FileResourceStore competing(live_project.root);
        } catch (const std::invalid_argument&) {
            competing_store_rejected = true;
        }
    }
    const bool active_transaction_intact =
        reached_gate && has_private_config_artifact(live_project.root)
        && std::filesystem::is_directory(
            live_project.root / "config" / "old");
    {
        std::lock_guard lock(gate_mutex);
        release_import = true;
    }
    gate_condition.notify_all();
    importer.join();
    check(reached_gate && competing_store_rejected && active_transaction_intact
              && import_succeeded.load(std::memory_order_acquire)
              && std::filesystem::is_directory(
                  live_project.root / "config" / "29000")
              && !std::filesystem::exists(
                  live_project.root / "config" / "old")
              && !has_private_config_artifact(live_project.root),
          "a second store cannot recover or delete another live import transaction");

    TempProject shared_cache_project;
    shared_cache_project.add_pair(
        "old", R"({"name":"Shared Cache","server":"日服","value":"old"})");
    auto writer_dependencies = test_defaults::with_synthetic_defaults();
    writer_dependencies.clock = [] { return 29'500.0; };
    adapters::FileResourceStore writer(
        shared_cache_project.root, std::move(writer_dependencies));
    adapters::FileResourceStore reader(shared_cache_project.root);
    const auto cached_old = reader.pull(config_key("old"), {});
    const auto shared_input = archive_bytes({
        {"config.json",
         R"({"name":"Shared Cache","server":"日服","value":"new"})"},
    });
    const auto shared_import = writer.import_config(
        {shared_input.data(), shared_input.size()}, {});
    const auto stale_old = reader.pull(config_key("old"), {});
    const auto visible_new = reader.pull(config_key("29500"), {});
    check(cached_old && shared_import && shared_import.serial == "29500"
              && !stale_old
              && stale_old.error == channels::ResourceStoreError::not_found
              && visible_new
              && Json::parse(visible_new->data_json)["name"] == "Shared Cache"
              && Json::parse(reader.config_list({})->data_json)
                     == Json::array({"29500"}),
          "separately constructed stores revalidate cached pulls after a structural transaction");

    TempProject mismatch_project;
    mismatch_project.add_pair(
        "safe", R"({"name":"Safe","server":"日服"})");
    const auto mismatch_root = mismatch_project.root / "config";
    const std::string transaction = "123-30000-1";
    const std::string journal_name =
        ".baas-import-journal-" + transaction + ".json";
    const std::string staging_name = ".baas-import-" + transaction;
    const std::string unrelated_tombstone =
        ".baas-import-retired-999-30000-1-0";
    std::filesystem::create_directory(mismatch_root / staging_name);
    std::filesystem::create_directory(mismatch_root / unrelated_tombstone);
    write_bytes(
        mismatch_root / journal_name,
        Json{{"version", 1},
             {"staging", staging_name},
             {"target", "30000"},
             {"retired",
              Json::array({
                  Json{{"source", "safe"},
                       {"tombstone", unrelated_tombstone}},
              })}}
            .dump());
    bool mismatched_journal_rejected{};
    try {
        adapters::FileResourceStore rejected(mismatch_project.root);
    } catch (const std::invalid_argument&) {
        mismatched_journal_rejected = true;
    }
    check(mismatched_journal_rejected
              && std::filesystem::is_directory(
                  mismatch_root / "safe")
              && std::filesystem::is_directory(
                  mismatch_root / staging_name)
              && std::filesystem::is_directory(
                  mismatch_root / unrelated_tombstone)
              && std::filesystem::is_regular_file(
                  mismatch_root / journal_name),
          "a journal whose tombstone is not derived from its filename fails closed without touching unrelated paths");

#if defined(_WIN32)
    TempProject pending_delete_project;
    pending_delete_project.add_pair(
        "31000", R"({"name":"Committed","server":"日服"})");
    const auto pending_root = pending_delete_project.root / "config";
    const std::string pending_transaction = "777-31000-1";
    const std::string pending_journal_name =
        ".baas-import-journal-" + pending_transaction + ".json";
    const auto pending_journal = pending_root / pending_journal_name;
    write_bytes(
        pending_root / "31000" / ".baas-import-commit",
        pending_journal_name);
    write_bytes(
        pending_journal,
        Json{{"version", 1},
             {"staging", ".baas-import-" + pending_transaction},
             {"target", "31000"},
             {"retired", Json::array()}}
            .dump());
    const HANDLE held_journal = CreateFileW(
        pending_journal.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool pending_delete_rejected{};
    if (held_journal != INVALID_HANDLE_VALUE) {
        try {
            adapters::FileResourceStore blocked(pending_delete_project.root);
        } catch (const std::invalid_argument&) {
            pending_delete_rejected = true;
        }
    }
    const bool recovery_evidence_preserved =
        std::filesystem::is_regular_file(pending_journal)
        && std::filesystem::is_regular_file(
            pending_root / "31000" / ".baas-import-commit");
    if (held_journal != INVALID_HANDLE_VALUE) {
        static_cast<void>(CloseHandle(held_journal));
    }
    bool retried_recovery_succeeded{};
    try {
        adapters::FileResourceStore recovered(pending_delete_project.root);
        retried_recovery_succeeded =
            Json::parse(recovered.config_list({})->data_json)
            == Json::array({"31000"});
    } catch (...) {
    }
    check(held_journal != INVALID_HANDLE_VALUE
              && pending_delete_rejected && recovery_evidence_preserved
              && retried_recovery_succeeded
              && !std::filesystem::exists(pending_journal)
              && !std::filesystem::exists(
                  pending_root / "31000" / ".baas-import-commit"),
          "Windows recovery retains its journal and final marker while another process can delay delete-on-close");
#endif
}

void test_config_archive_cross_process_lock_and_crash_recovery()
{
    TempProject project;
    project.add_pair(
        "old", R"({"name":"Crash Lock","server":"日服"})");
    const auto ready = project.root / "child-import-ready";
    bool child_started{};
#if defined(_WIN32)
    std::array<wchar_t, 32'768> executable_buffer{};
    const auto executable_size = GetModuleFileNameW(
        nullptr, executable_buffer.data(),
        static_cast<DWORD>(executable_buffer.size()));
    PROCESS_INFORMATION process{};
    if (executable_size > 0 && executable_size < executable_buffer.size()) {
        const std::wstring executable{
            executable_buffer.data(), executable_size};
        std::wstring command = L"\"" + executable
            + L"\" --import-crash-child \""
            + project.root.native() + L"\"";
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        child_started = CreateProcessW(
            nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
        if (child_started) CloseHandle(process.hThread);
    }
#else
    const pid_t child = ::fork();
    if (child == 0) {
        ::_exit(run_import_crash_child(project.root));
    }
    child_started = child > 0;
#endif

    bool child_ready{};
    for (std::size_t attempt{}; child_started && attempt < 250; ++attempt) {
        if (std::filesystem::is_regular_file(ready)) {
            child_ready = true;
            break;
        }
        std::this_thread::sleep_for(20ms);
    }
    bool competing_store_rejected{};
    if (child_ready) {
        try {
            adapters::FileResourceStore competing(project.root);
        } catch (const std::invalid_argument&) {
            competing_store_rejected = true;
        }
    }
    const bool retired_state_observed =
        child_ready
        && !std::filesystem::exists(project.root / "config" / "old")
        && has_private_config_artifact(project.root);

#if defined(_WIN32)
    if (child_started) {
        static_cast<void>(TerminateProcess(process.hProcess, 137));
        static_cast<void>(WaitForSingleObject(process.hProcess, 10'000));
        CloseHandle(process.hProcess);
    }
#else
    if (child_started) {
        static_cast<void>(::kill(child, SIGKILL));
        int status{};
        static_cast<void>(::waitpid(child, &status, 0));
    }
#endif
    std::error_code ready_error;
    static_cast<void>(std::filesystem::remove(ready, ready_error));

    bool recovered_after_crash{};
    try {
        adapters::FileResourceStore recovered(project.root);
        const auto listed = recovered.config_list({});
        recovered_after_crash = listed
            && Json::parse(listed->data_json) == Json::array({"old"})
            && recovered.pull(config_key("old"), {});
    } catch (...) {
    }
    check(child_started && child_ready && competing_store_rejected
              && retired_state_observed && recovered_after_crash
              && std::filesystem::is_directory(
                  project.root / "config" / "old")
              && !std::filesystem::exists(
                  project.root / "config" / "32000")
              && !has_private_config_artifact(project.root),
          "a second process cannot recover a live import, and kernel lock release after a crash permits deterministic rollback");
}

void test_config_archive_import_invalid_and_capacity_inputs()
{
    TempProject project;
    auto archive_dependencies = test_defaults::with_synthetic_defaults();
    archive_dependencies.clock = [] { return 24'000.0; };
    adapters::FileResourceStore store(
        project.root, std::move(archive_dependencies));
    const auto no_config = archive_bytes({{"notes.txt", "missing"}});
    const auto bad_config = archive_bytes({{"config.json", "{"}});
    const std::array<std::byte, 4> corrupt{
        std::byte{'B'}, std::byte{'A'}, std::byte{'D'}, std::byte{'!'}};
    const auto missing = store.import_config(
        {no_config.data(), no_config.size()}, {});
    const auto malformed = store.import_config(
        {bad_config.data(), bad_config.size()}, {});
    const auto invalid_zip = store.import_config(corrupt, {});
    check(missing.error == adapters::ConfigCommandError::invalid_data
              && malformed.error == adapters::ConfigCommandError::invalid_data
              && invalid_zip.error == adapters::ConfigCommandError::invalid_data
              && Json::parse(store.config_list({})->data_json).empty()
              && !has_private_config_artifact(project.root),
          "missing config.json, bad config JSON, and invalid ZIP fail before staging");

    TempProject full_project;
    full_project.add_pair(
        "only", R"({"name":"Keep","server":"日服"})");
    channels::ResourceStoreLimits one_pair;
    one_pair.max_resources = 1;
    auto full_dependencies = test_defaults::with_synthetic_defaults();
    full_dependencies.clock = [] { return 25'000.0; };
    adapters::FileResourceStore full_store(
        full_project.root, std::move(full_dependencies), one_pair);
    const auto distinct = archive_bytes({
        {"config.json", R"({"name":"New","server":"日服"})"},
    });
    const auto full = full_store.import_config(
        {distinct.data(), distinct.size()}, {});
    check(full.error == adapters::ConfigCommandError::capacity
              && Json::parse(full_store.config_list({})->data_json)
                     == Json::array({"only"})
              && !std::filesystem::exists(full_project.root / "config" / "25000")
              && !std::filesystem::exists(full_project.root / "config" / "static.json")
              && !has_private_config_artifact(full_project.root),
          "import capacity admission rejects a new pair before static or staging side effects");
}

void test_missing_runtime_defaults_fail_mutations_closed()
{
    TempProject project;
    project.add_pair(
        "source", R"({"name":"Source","server":"日服"})", "[]");
    adapters::FileResourceStore store(project.root);
    const auto input = archive_bytes({
        {"config.json", R"({"name":"Imported","server":"日服"})"},
    });

    const auto source = store.pull(config_key("source"), {});
    const auto created = store.create_config("Created", "日服", {});
    const auto copied = store.copy_config("source", {});
    const auto imported = store.import_config(
        {input.data(), input.size()}, {});
    const auto listed = store.config_list({});

    check(source
              && created.error == adapters::ConfigCommandError::internal_error
              && copied.error == adapters::ConfigCommandError::internal_error
              && imported.error == adapters::ConfigCommandError::internal_error
              && listed
              && Json::parse(listed->data_json) == Json::array({"source"})
              && !std::filesystem::exists(
                  project.root / "config" / "static.json")
              && !has_private_config_artifact(project.root),
          "missing admitted runtime defaults preserve reads and fail every initializer mutation without side effects");
}

}  // namespace

int main(const int argc, char** argv)
{
    if (argc == 3 && std::string_view{argv[1]} == "--import-crash-child") {
        return run_import_crash_child(std::filesystem::path{argv[2]});
    }
    try {
        test_list_pull_and_resource_shapes();
        test_path_traversal_and_symlink_rejection();
#if defined(_WIN32)
        test_windows_aliases_and_anchored_directory_handles();
#else
        test_posix_ancestor_symlink_swap_fails_closed();
#endif
        test_json_validation_and_capacity();
        test_patch_conflict_and_durable_commit();
        test_atomic_writer_failure_is_invisible();
        test_post_commit_durability_failure_is_committed();
        test_default_writer_post_commit_failure_is_committed();
        test_concurrent_patch_conflict();
        test_external_change_conflicts_instead_of_overwrite();
        test_subscription_entry_barrier_and_exception_isolation();
        test_publication_queue_reentrancy_and_empty_head_enqueue();
        test_cross_unsubscribe_and_store_self_destruction();
        test_subscriber_capacity();
        test_self_unsubscribe_does_not_deadlock();
        test_setup_toml_projection_patch_and_unknown_retention();
        test_setup_toml_legacy_proxy_projection();
        test_setup_toml_eof_insertion_keeps_valid_line_boundaries();
        test_setup_toml_scalar_table_conflicts_fail_closed();
        test_create_config_transaction_concurrency_cancellation_and_cleanup();
        test_config_archive_export_and_python_compatible_import();
        test_copy_and_remove_claim_self_cancel_linearization();
        test_config_archive_import_claim_cancellation_and_rollback();
        test_config_archive_claim_self_cancel_and_crash_recovery();
        test_config_archive_root_lock_and_journal_binding();
        test_config_archive_cross_process_lock_and_crash_recovery();
        test_config_archive_import_invalid_and_capacity_inputs();
        test_missing_runtime_defaults_fail_mutations_closed();
        test_refresh_and_publish();
        test_cross_store_pull_preserves_refresh_publication_baseline();
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
    } catch (...) {
        ++failures;
        std::cerr << "UNCAUGHT: unknown exception\n";
    }
    if (failures.load() != 0) {
        std::cerr << failures.load() << " file resource store test(s) failed\n";
        return 1;
    }
    std::cout << "file resource store tests passed\n";
    return 0;
}
