#include "service/adapters/FileResourceStore.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
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
#endif

namespace {

using namespace std::chrono_literals;
using Json = nlohmann::json;
namespace adapters = baas::service::adapters;
namespace channels = baas::service::channels;

std::atomic<int> failures{};

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

class TempProject {
public:
    TempProject()
    {
        static std::atomic<std::uint64_t> sequence{};
        root = std::filesystem::temp_directory_path()
            / ("baas-file-resource-store-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::remove_all(root);
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
    return {[] { return 9'000'000'000'000.0; }, std::move(writer), std::move(check)};
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

    auto setup = store.pull({channels::SyncResource::setup_toml, std::nullopt}, {});
    check(!setup && setup.error == channels::ResourceStoreError::not_found,
          "setup TOML is explicitly unsupported rather than misparsed as JSON");
    auto no_id = store.pull({channels::SyncResource::config, std::nullopt}, {});
    check(!no_id && no_id.error == channels::ResourceStoreError::invalid_data,
          "config pull requires a resource id");
    auto global_id = store.pull(
        {channels::SyncResource::gui, std::string{"alpha"}}, {});
    check(!global_id && global_id.error == channels::ResourceStoreError::invalid_data,
          "global resources reject a resource id");
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
    check(!store.refresh_and_publish(config_key(), "filesystem"),
          "POSIX anchored refresh refuses a swapped config ancestor symlink");
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
    auto setup = store.apply_patch(
        {{channels::SyncResource::setup_toml, std::nullopt}, "0", {}}, {});
    check(!setup && setup.error == channels::ResourceStoreError::not_found,
          "setup TOML patch is explicitly not found");

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

void test_refresh_and_publish()
{
    TempProject project;
    project.add_pair("alpha", "{}");
    project.add_globals();
    adapters::FileResourceStore store(project.root, dependencies());
    auto gui = store.pull({channels::SyncResource::gui, std::nullopt}, {});
    check(static_cast<bool>(gui), "refresh baseline is cached");

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
    check(!store.refresh_and_publish(
              {channels::SyncResource::gui, std::nullopt}, "filesystem"),
          "invalid external JSON is rejected");
    auto preserved = store.pull({channels::SyncResource::gui, std::nullopt}, {});
    check(preserved && Json::parse(preserved->data_json)["theme"] == "light"
              && updates.size() == 1,
          "invalid refresh leaves visible snapshot and publications unchanged");
    check(!store.refresh_and_publish(
              {channels::SyncResource::setup_toml, std::nullopt}, "filesystem"),
          "setup TOML refresh remains unsupported");
    check(!store.refresh_and_publish(
              {channels::SyncResource::gui, std::nullopt}, std::string(65, 'o')),
          "refresh origin is bounded");
}

}  // namespace

int main()
{
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
        test_refresh_and_publish();
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
