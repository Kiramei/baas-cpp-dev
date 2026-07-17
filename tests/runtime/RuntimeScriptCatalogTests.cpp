#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "runtime/script/RuntimeScriptCatalog.h"
#include "resources/ResourceSnapshot.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace repository = baas::runtime::repository;
namespace catalog = baas::runtime::script;

std::atomic<int> failures{};
std::stop_source* cancellation_source{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void cancel_catalog_read(
    const repository::RuntimeRepositoryReadHookPoint point,
    const std::string_view repository_id,
    const std::string_view logical_path)
{
    if (point == repository::RuntimeRepositoryReadHookPoint::payload_digest_finalizing
        && repository_id == "scripts"
        && logical_path == catalog::runtime_script_catalog_manifest
        && cancellation_source != nullptr) {
        cancellation_source->request_stop();
    }
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("baas-runtime-script-catalog-" + std::to_string(stamp));
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

void write_file(const std::filesystem::path& path, const std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("fixture file creation failed");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("fixture file write failed");
}

[[nodiscard]] std::string sha256(const std::string_view value)
{
    return baas::resources::sha256_hex(std::as_bytes(std::span{value.data(), value.size()}));
}

struct FixtureFile {
    std::string path;
    std::string bytes;
};

[[nodiscard]] std::string tree_manifest(std::vector<FixtureFile> files)
{
    std::ranges::sort(files, {}, &FixtureFile::path);
    std::string result =
        R"({"schema":"baas.runtime-repository.tree-manifest/v1","entries":[)";
    for (std::size_t index{}; index < files.size(); ++index) {
        if (index != 0) result.push_back(',');
        result += R"({"path":")" + files[index].path
            + R"(","size":")" + std::to_string(files[index].bytes.size())
            + R"(","sha256":")" + sha256(files[index].bytes)
            + R"(","mode":"file"})";
    }
    result += "]}";
    return result;
}

[[nodiscard]] std::string snapshot_json(
    const std::array<repository::RuntimeRepository, 2>& repositories,
    const std::string_view generation)
{
    std::string result =
        R"({"schema":"baas.runtime-repositories.snapshot/v1","generation":")"
        + std::string{generation} + R"(","repositories":[)";
    for (std::size_t index{}; index < repositories.size(); ++index) {
        if (index != 0) result.push_back(',');
        const auto& item = repositories[index];
        result += R"({"id":")" + item.id + R"(","commit":")" + item.commit
            + R"(","root":")" + item.root + R"(","manifest":")"
            + item.manifest + R"(","manifest_sha256":")"
            + item.manifest_sha256 + R"("})";
    }
    result += "]}";
    return result;
}

class RepositoryFixture final {
public:
    explicit RepositoryFixture(
        std::string catalog_bytes,
        const bool include_catalog = true,
        const bool include_package = true,
        const bool include_entry = true)
    {
        const std::vector<FixtureFile> resources{{"placeholder.bin", "resource"}};
        std::vector<FixtureFile> scripts;
        if (include_catalog) {
            scripts.push_back({std::string{catalog::runtime_script_catalog_manifest},
                               std::move(catalog_bytes)});
        }
        if (include_package) scripts.push_back({"packages/core.json", "{}"});
        if (include_entry) scripts.push_back({"tasks/entry.baas", "let ready = true;\n"});
        const auto resource_manifest = tree_manifest(resources);
        const auto scripts_manifest = tree_manifest(scripts);
        const std::string resource_commit(40, '1');
        const std::string scripts_commit(40, '2');
        repositories_ = {{
            {"resources", resource_commit, "objects/resources/" + resource_commit,
             "manifest.json", sha256(resource_manifest)},
            {"scripts", scripts_commit, "objects/scripts/" + scripts_commit,
             "scripts.json", sha256(scripts_manifest)},
        }};
        for (const auto& file : resources)
            write_file(temporary_.path() / repositories_[0].root / file.path, file.bytes);
        for (const auto& file : scripts)
            write_file(temporary_.path() / repositories_[1].root / file.path, file.bytes);
        write_file(temporary_.path() / repositories_[0].root / repositories_[0].manifest,
                   resource_manifest);
        write_file(temporary_.path() / repositories_[1].root / repositories_[1].manifest,
                   scripts_manifest);
        generation_ = repository::runtime_repository_generation(repositories_);
        write_file(temporary_.path() / "snapshots" / (generation_ + ".json"),
                   snapshot_json(repositories_, generation_));
        write_file(temporary_.path() / "current.json",
                   R"({"schema":"baas.runtime-repositories.current/v1","generation":")"
                       + generation_ + R"(","snapshot":"snapshots/)" + generation_
                       + ".json\"}");
        snapshot_ = repository::RuntimeRepositorySnapshot::activate(temporary_.path());
        bundle_ = snapshot_->open_read_bundle();
    }

    [[nodiscard]] const repository::RuntimeRepositoryReadView& scripts() const
    {
        return bundle_->scripts();
    }

    [[nodiscard]] const repository::RuntimeRepositoryReadView& resources() const
    {
        return bundle_->resources();
    }

    [[nodiscard]] catalog::RuntimeScriptCatalogPin pin() const noexcept
    {
        return {scripts().generation(), scripts().commit()};
    }

private:
    TemporaryDirectory temporary_;
    std::array<repository::RuntimeRepository, 2> repositories_;
    std::string generation_;
    std::shared_ptr<const repository::RuntimeRepositorySnapshot> snapshot_;
    std::shared_ptr<const repository::RuntimeRepositoryReadBundle> bundle_;
};

[[nodiscard]] std::string task_json(
    const std::string_view canonical,
    const std::string_view alias,
    const std::string_view run_mode = "solve")
{
    return R"({"run_mode":")" + std::string{run_mode}
        + R"(","task":")" + std::string{canonical}
        + R"(","package_manifest":"packages/core.json",)"
          R"("entry_module":"tasks/entry","entry_export":"run",)"
          R"("language_version":{"major":1,"minor":0},)"
          R"("host_modules":[{"module":"baas/resource","major":1,"min_minor":0,)"
          R"("capabilities":["resource.read","resource.decode"]},)"
          R"({"module":"baas/log","major":1,"min_minor":2,)"
          R"("capabilities":["log.write"]}],"legacy_aliases":[")"
        + std::string{alias} + R"("]})";
}

[[nodiscard]] std::string valid_catalog(const bool reverse = false)
{
    const std::array aliases{
        std::pair{"explore_hard_task", "start_hard_task"},
        std::pair{"explore_normal_task", "start_normal_task"},
        std::pair{"de_clothes", "start_fhx"},
        std::pair{"main_story", "start_main_story"},
        std::pair{"group_story", "start_group_story"},
        std::pair{"mini_story", "start_mini_story"},
        std::pair{"explore_activity_story", "start_explore_activity_story"},
        std::pair{"explore_activity_mission", "start_explore_activity_mission"},
        std::pair{"explore_activity_challenge", "start_explore_activity_challenge"},
    };
    std::string result = R"({"schema":"baas.runtime-script.catalog/v1","tasks":[)";
    for (std::size_t output_index{}; output_index < aliases.size(); ++output_index) {
        if (output_index != 0) result.push_back(',');
        const auto index = reverse ? aliases.size() - output_index - 1 : output_index;
        result += task_json(aliases[index].first, aliases[index].second);
    }
    result += "]}";
    return result;
}

void expect_error(
    const catalog::RuntimeScriptCatalogLoadResult& result,
    const catalog::RuntimeScriptCatalogError error,
    const std::string_view message)
{
    check(!result, message);
    check(result.error == error, message);
    check(!result.catalog.has_value(), message);
}

void test_success_exact_routes_versions_and_alias_data()
{
    RepositoryFixture fixture{valid_catalog(true)};
    const auto result = catalog::load_runtime_script_catalog(
        fixture.scripts(), fixture.pin());
    check(static_cast<bool>(result), "a valid catalog must load");
    if (!result) return;
    check(result.catalog->generation() == fixture.scripts().generation()
              && result.catalog->commit() == fixture.scripts().commit(),
          "the immutable output must retain the exact pin binding");
    check(result.catalog->tasks().size() == 9,
          "all nine manifest-declared legacy mappings must be retained");
    check(std::ranges::is_sorted(result.catalog->tasks(), [](const auto& left, const auto& right) {
              return std::tie(left.run_mode, left.canonical_task)
                  < std::tie(right.run_mode, right.canonical_task);
          }), "catalog task output must be deterministic");

    const std::array expected{
        std::pair{"start_hard_task", "explore_hard_task"},
        std::pair{"start_normal_task", "explore_normal_task"},
        std::pair{"start_fhx", "de_clothes"},
        std::pair{"start_main_story", "main_story"},
        std::pair{"start_group_story", "group_story"},
        std::pair{"start_mini_story", "mini_story"},
        std::pair{"start_explore_activity_story", "explore_activity_story"},
        std::pair{"start_explore_activity_mission", "explore_activity_mission"},
        std::pair{"start_explore_activity_challenge", "explore_activity_challenge"},
    };
    for (const auto& [alias, canonical] : expected) {
        const auto resolution = result.catalog->resolve("solve", alias);
        check(resolution && resolution->legacy_alias
                  && resolution->task->canonical_task == canonical,
              "every legacy alias must resolve only because catalog data declares it");
    }
    const auto canonical = result.catalog->resolve("solve", "main_story");
    check(canonical && !canonical->legacy_alias
              && canonical->task->package_manifest == "packages/core.json"
              && canonical->task->entry_module == "tasks/entry"
              && canonical->task->entry_export == "run"
              && canonical->task->language_version
                  == catalog::RuntimeScriptLanguageVersion{1, 0},
          "canonical resolution must retain exact package/language metadata");
    check(canonical && canonical->task->host_modules[0].canonical_id == "baas/log"
              && canonical->task->host_modules[1].canonical_id == "baas/resource"
              && canonical->task->host_modules[1].capabilities
                  == std::vector<std::string>{"resource.decode", "resource.read"},
          "host requirements and capabilities must have deterministic output");
    check(!result.catalog->resolve("Solve", "main_story")
              && !result.catalog->resolve("solve", "Main_Story")
              && !result.catalog->resolve("solve", "main")
              && !result.catalog->resolve("solve", "start_main_story/extra"),
          "lookup must never case-fold, prefix-match, guess, or fall back");

    RepositoryFixture ordered_fixture{valid_catalog(false)};
    const auto ordered = catalog::load_runtime_script_catalog(
        ordered_fixture.scripts(), ordered_fixture.pin());
    check(ordered
              && std::ranges::equal(result.catalog->tasks(), ordered.catalog->tasks()),
          "source task order must not change deterministic catalog output");
}

void test_pin_repository_and_reference_boundaries()
{
    RepositoryFixture fixture{valid_catalog()};
    expect_error(catalog::load_runtime_script_catalog(
                     fixture.scripts(), {"wrong-generation", fixture.scripts().commit()}),
                 catalog::RuntimeScriptCatalogError::generation_mismatch,
                 "a stale generation binding must fail closed");
    expect_error(catalog::load_runtime_script_catalog(
                     fixture.scripts(), {fixture.scripts().generation(), "wrong-commit"}),
                 catalog::RuntimeScriptCatalogError::commit_mismatch,
                 "a stale commit binding must fail closed");
    expect_error(catalog::load_runtime_script_catalog(fixture.resources(), {
                     fixture.resources().generation(), fixture.resources().commit()}),
                 catalog::RuntimeScriptCatalogError::wrong_repository,
                 "a resources view must not become a scripts catalog");

    RepositoryFixture no_catalog{valid_catalog(), false};
    expect_error(catalog::load_runtime_script_catalog(no_catalog.scripts(), no_catalog.pin()),
                 catalog::RuntimeScriptCatalogError::manifest_not_found,
                 "the exact root catalog name is mandatory");
    RepositoryFixture no_package{valid_catalog(), true, false};
    expect_error(catalog::load_runtime_script_catalog(no_package.scripts(), no_package.pin()),
                 catalog::RuntimeScriptCatalogError::missing_package_manifest,
                 "an unmanifested package descriptor must fail closed");
    RepositoryFixture no_entry{valid_catalog(), true, true, false};
    expect_error(catalog::load_runtime_script_catalog(no_entry.scripts(), no_entry.pin()),
                 catalog::RuntimeScriptCatalogError::missing_entry_module,
                 "an unmanifested exact entry source must fail closed");
}

void test_strict_json_schema_and_route_collisions()
{
    const auto invalid = [](std::string bytes, const catalog::RuntimeScriptCatalogError error,
                            const std::string_view message) {
        RepositoryFixture fixture{std::move(bytes)};
        expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin()),
                     error, message);
    };
    invalid(R"({"schema":"baas.runtime-script.catalog/v1","schema":"x","tasks":[]})",
            catalog::RuntimeScriptCatalogError::invalid_json,
            "duplicate JSON keys must be rejected during parsing");
    invalid(R"({"schema":"baas.runtime-script.catalog/v1","tasks":[],"extra":0})",
            catalog::RuntimeScriptCatalogError::invalid_field_set,
            "unknown root fields must be rejected");
    invalid(R"({"schema":"baas.runtime-script.catalog/v2","tasks":[]})",
            catalog::RuntimeScriptCatalogError::invalid_schema,
            "unknown schema versions must fail closed");
    invalid(std::string{"{\"schema\":\"baas.runtime-script.catalog/v1\",\"tasks\":[\"\xC0\xAF\"]}", 66},
            catalog::RuntimeScriptCatalogError::invalid_utf8,
            "invalid UTF-8 must be rejected before JSON publication");

    auto nul = valid_catalog();
    const auto position = nul.find("start_hard_task");
    nul.replace(position, std::string_view{"start_hard_task"}.size(), "bad\\u0000alias");
    invalid(std::move(nul), catalog::RuntimeScriptCatalogError::invalid_value,
            "decoded NUL values must fail closed");

    const auto duplicate = R"({"schema":"baas.runtime-script.catalog/v1","tasks":[)"
        + task_json("first", "same") + "," + task_json("second", "same") + "]}";
    invalid(duplicate, catalog::RuntimeScriptCatalogError::duplicate_route,
            "duplicate run-mode/request routes must be rejected");

    auto unknown_task = task_json("one", "old");
    unknown_task.insert(unknown_task.size() - 1, R"(,"extra":0)");
    invalid(R"({"schema":"baas.runtime-script.catalog/v1","tasks":[)"
                + unknown_task + "]}",
            catalog::RuntimeScriptCatalogError::invalid_field_set,
            "unknown task fields must be rejected");
}

void test_limits_and_cancellation()
{
    RepositoryFixture fixture{valid_catalog()};
    auto limits = catalog::RuntimeScriptCatalogLimits{};
    limits.max_tasks = 8;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "task count must be bounded");
    limits = {};
    limits.max_total_aliases = 8;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "aggregate alias count must be bounded");
    auto two_aliases = valid_catalog();
    constexpr std::string_view one_alias =
        "\"legacy_aliases\":[\"start_hard_task\"]";
    const auto alias_field = two_aliases.find(one_alias);
    two_aliases.replace(
        alias_field,
        one_alias.size(),
        "\"legacy_aliases\":[\"start_hard_task\",\"old_hard_task\"]");
    RepositoryFixture alias_fixture{std::move(two_aliases)};
    limits = {};
    limits.max_aliases_per_task = 1;
    expect_error(catalog::load_runtime_script_catalog(
                     alias_fixture.scripts(), alias_fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "per-task alias count must be bounded");
    limits = {};
    limits.max_host_modules_per_task = 1;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "per-task Host module count must be bounded");
    limits = {};
    limits.max_total_host_modules = 17;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "aggregate Host module count must be bounded");
    limits = {};
    limits.max_capabilities_per_module = 1;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "per-module capability count must be bounded");
    limits = {};
    limits.max_total_capabilities = 26;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "aggregate capability count must be bounded");
    limits = {};
    limits.max_json_depth = 3;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "JSON nesting must be bounded during parsing");
    limits = {};
    limits.max_json_nodes = 16;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "JSON node count must be bounded during parsing");
    limits = {};
    limits.max_string_bytes = 8;
    limits.module_specifier.max_bytes = 8;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "individual decoded JSON strings must be bounded");
    limits = {};
    limits.max_total_string_bytes = 256;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "aggregate decoded JSON strings must be bounded");
    limits = {};
    limits.module_specifier.max_bytes = 8;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::invalid_value,
                 "entry and Host modules must obey the shared byte limit");
    limits = {};
    limits.module_specifier.max_segments = 1;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::invalid_value,
                 "entry and Host modules must obey the shared segment limit");
    limits = {};
    limits.max_manifest_bytes = 16;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::manifest_too_large,
                 "manifest size must fail before payload read");
    limits = {};
    limits.max_work = 1;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::limit_exceeded,
                 "aggregate parser/validation work must be bounded");
    limits = {};
    limits.max_tasks = 0;
    expect_error(catalog::load_runtime_script_catalog(fixture.scripts(), fixture.pin(), limits),
                 catalog::RuntimeScriptCatalogError::invalid_limits,
                 "zero limits must fail closed");

    std::stop_source pre_cancelled;
    pre_cancelled.request_stop();
    expect_error(catalog::load_runtime_script_catalog(
                     fixture.scripts(), fixture.pin(), {}, pre_cancelled.get_token()),
                 catalog::RuntimeScriptCatalogError::cancelled,
                 "pre-cancellation must stop before catalog reads");
    std::stop_source during_read;
    cancellation_source = &during_read;
    repository::set_runtime_repository_read_view_hook(cancel_catalog_read);
    const auto interrupted = catalog::load_runtime_script_catalog(
        fixture.scripts(), fixture.pin(), {}, during_read.get_token());
    repository::set_runtime_repository_read_view_hook(nullptr);
    cancellation_source = nullptr;
    expect_error(interrupted, catalog::RuntimeScriptCatalogError::cancelled,
                 "cancellation during verified catalog hashing must fail closed");
}

void test_stable_error_names()
{
    using Error = catalog::RuntimeScriptCatalogError;
    constexpr std::array expected{
        std::pair{Error::none, std::string_view{"RSC000_NONE"}},
        std::pair{Error::invalid_limits, std::string_view{"RSC001_INVALID_LIMITS"}},
        std::pair{Error::wrong_repository, std::string_view{"RSC002_WRONG_REPOSITORY"}},
        std::pair{Error::manifest_not_found, std::string_view{"RSC003_MANIFEST_NOT_FOUND"}},
        std::pair{Error::manifest_too_large, std::string_view{"RSC004_MANIFEST_TOO_LARGE"}},
        std::pair{Error::repository_read_failed, std::string_view{"RSC005_REPOSITORY_READ_FAILED"}},
        std::pair{Error::invalid_utf8, std::string_view{"RSC006_INVALID_UTF8"}},
        std::pair{Error::invalid_json, std::string_view{"RSC007_INVALID_JSON"}},
        std::pair{Error::limit_exceeded, std::string_view{"RSC008_LIMIT_EXCEEDED"}},
        std::pair{Error::invalid_schema, std::string_view{"RSC009_INVALID_SCHEMA"}},
        std::pair{Error::invalid_field_set, std::string_view{"RSC010_INVALID_FIELD_SET"}},
        std::pair{Error::generation_mismatch, std::string_view{"RSC011_GENERATION_MISMATCH"}},
        std::pair{Error::commit_mismatch, std::string_view{"RSC012_COMMIT_MISMATCH"}},
        std::pair{Error::invalid_value, std::string_view{"RSC013_INVALID_VALUE"}},
        std::pair{Error::duplicate_route, std::string_view{"RSC014_DUPLICATE_ROUTE"}},
        std::pair{Error::missing_package_manifest, std::string_view{"RSC015_MISSING_PACKAGE_MANIFEST"}},
        std::pair{Error::missing_entry_module, std::string_view{"RSC016_MISSING_ENTRY_MODULE"}},
        std::pair{Error::cancelled, std::string_view{"RSC017_CANCELLED"}},
        std::pair{Error::resource_exhausted, std::string_view{"RSC018_RESOURCE_EXHAUSTED"}},
    };
    for (const auto& [error, name] : expected)
        check(catalog::runtime_script_catalog_error_name(error) == name,
              "catalog errors must expose stable machine codes");
}

}  // namespace

int main()
{
    try {
        test_success_exact_routes_versions_and_alias_data();
        test_pin_repository_and_reference_boundaries();
        test_strict_json_schema_and_route_collisions();
        test_limits_and_cancellation();
        test_stable_error_names();
    } catch (const std::exception& error) {
        std::cerr << "unexpected test exception: " << error.what() << '\n';
        return 1;
    }
    if (failures.load() != 0) {
        std::cerr << failures.load() << " runtime script catalog test(s) failed\n";
        return 1;
    }
    std::cout << "Runtime script catalog tests passed\n";
    return 0;
}
