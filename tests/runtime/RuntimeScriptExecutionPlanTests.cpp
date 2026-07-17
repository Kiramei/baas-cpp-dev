#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "runtime/script/RuntimeScriptExecutionPlan.h"
#include "resources/ResourceSnapshot.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
namespace runtime_script = baas::runtime::script;

std::atomic<int> failures{};
std::stop_source* cancellation_source{};

void check(bool condition, std::string_view message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void cancel_manifest_read(repository::RuntimeRepositoryReadHookPoint point,
                          std::string_view repository_id,
                          std::string_view logical_path)
{
    if (point == repository::RuntimeRepositoryReadHookPoint::payload_digest_finalizing
        && repository_id == "scripts"
        && logical_path == "packages/core/baas.package.json"
        && cancellation_source) cancellation_source->request_stop();
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("baas-runtime-execution-plan-" + std::to_string(stamp));
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

void write_file(const std::filesystem::path& path, std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("fixture create failed");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("fixture write failed");
}

[[nodiscard]] std::string sha256(std::string_view value)
{
    return baas::resources::sha256_hex(std::as_bytes(std::span{value.data(), value.size()}));
}

struct File { std::string path; std::string bytes; };

[[nodiscard]] std::string tree_manifest(std::vector<File> files)
{
    std::ranges::sort(files, {}, &File::path);
    std::string result =
        R"({"schema":"baas.runtime-repository.tree-manifest/v1","entries":[)";
    for (std::size_t index{}; index < files.size(); ++index) {
        if (index) result.push_back(',');
        result += R"({"path":")" + files[index].path + R"(","size":")"
            + std::to_string(files[index].bytes.size()) + R"(","sha256":")"
            + sha256(files[index].bytes) + R"(","mode":"file"})";
    }
    return result + "]}";
}

[[nodiscard]] std::string snapshot_json(
    const std::array<repository::RuntimeRepository, 2>& repositories,
    std::string_view generation)
{
    std::string result =
        R"({"schema":"baas.runtime-repositories.snapshot/v1","generation":")"
        + std::string{generation} + R"(","repositories":[)";
    for (std::size_t index{}; index < repositories.size(); ++index) {
        if (index) result.push_back(',');
        const auto& item = repositories[index];
        result += R"({"id":")" + item.id + R"(","commit":")" + item.commit
            + R"(","root":")" + item.root + R"(","manifest":")" + item.manifest
            + R"(","manifest_sha256":")" + item.manifest_sha256 + R"("})";
    }
    return result + "]}";
}

[[nodiscard]] std::string catalog_json(std::string_view entry = "tasks/main",
                                       std::string_view language = R"({"major":1,"minor":2})",
                                       std::string_view host_minor = "0",
                                       std::string_view capabilities = R"(["log.emit"])")
{
    return R"({"schema":"baas.runtime-script.catalog/v2","tasks":[{)"
        R"("run_mode":"solve","task":"main_story",)"
        R"("package_root":"packages/core",)"
        R"("package_manifest":"packages/core/baas.package.json","entry_module":")"
        + std::string{entry} + R"(","entry_export":"run","language_version":)"
        + std::string{language}
        + R"(,"host_modules":[{"module":"baas/log","major":1,"min_minor":)"
        + std::string{host_minor} + R"(,"capabilities":)" + std::string{capabilities}
        + R"(}],"legacy_aliases":["start_main_story"]}]})";
}

[[nodiscard]] std::string valid_manifest(std::string_view main_source,
                                         std::string_view dep_source)
{
    return R"({"manifest_schema":1,"package":{"id":"bluearchive.automation.core",)"
        R"("version":"1.2.3","build":"fixture"},)"
        R"("language":{"major":1,"min_minor":1},"entrypoint":"tasks/main.baas",)"
        R"("host_modules":{"baas/log":{"major":1,"min_minor":0}},)"
        R"("capabilities":["log.emit"],"profiles":[],"modules":[)"
        R"({"path":"tasks/main.baas","size":)" + std::to_string(main_source.size())
        + R"(,"sha256":")" + sha256(main_source) + R"("},)"
        R"({"path":"tasks/dep.baas","size":)" + std::to_string(dep_source.size())
        + R"(,"sha256":")" + sha256(dep_source) + R"("}],"resources":[],)"
        R"("limits":{"source_bytes":4096,"resource_bytes":0,"module_count":2,"resource_count":0}})";
}

[[nodiscard]] std::string single_manifest(std::string_view package_id,
                                          std::string_view source)
{
    return R"({"manifest_schema":1,"package":{"id":")" + std::string{package_id}
        + R"(","version":"1.0.0"},"language":{"major":1,"min_minor":0},)"
          R"("entrypoint":"main.baas","host_modules":{"baas/log":{"major":1,"min_minor":0}},)"
          R"("capabilities":["log.emit"],"profiles":[],"modules":[{"path":"main.baas","size":)"
        + std::to_string(source.size()) + R"(,"sha256":")" + sha256(source)
        + R"("}],"resources":[],"limits":{"source_bytes":4096,"resource_bytes":0,)"
          R"("module_count":1,"resource_count":0}})";
}

[[nodiscard]] std::string two_package_catalog()
{
    auto task = [](std::string_view root, std::string_view task_name,
                   std::string_view alias) {
        return R"({"run_mode":"solve","task":")" + std::string{task_name}
            + R"(","package_root":")" + std::string{root}
            + R"(","package_manifest":")" + std::string{root}
            + R"(/baas.package.json","entry_module":"main","entry_export":"run",)"
              R"("language_version":{"major":1,"minor":0},"host_modules":[)"
              R"({"module":"baas/log","major":1,"min_minor":0,"capabilities":["log.emit"]}],)"
              R"("legacy_aliases":[")" + std::string{alias} + R"("]})";
    };
    return R"({"schema":"baas.runtime-script.catalog/v2","tasks":[)"
        + task("packages/one", "one", "start_one") + ","
        + task("packages/two", "two", "start_two") + "]}";
}

void replace_once(std::string& value, std::string_view from, std::string_view to)
{
    const auto offset = value.find(from);
    if (offset == std::string::npos) throw std::runtime_error("fixture replacement missing");
    value.replace(offset, from.size(), to);
}

class RepositoryFixture final {
public:
    RepositoryFixture(std::string manifest, std::string catalog,
                      std::string main_source, std::string dep_source,
                      std::vector<File> extras = {},
                      std::string main_path = "tasks/main.baas")
    {
        const std::vector<File> resources{{"placeholder.bin", "resource"}};
        std::vector<File> scripts{
            {std::string{runtime_script::runtime_script_catalog_manifest}, std::move(catalog)},
            {"packages/core/baas.package.json", std::move(manifest)},
            {"packages/core/" + std::move(main_path), std::move(main_source)},
            {"packages/core/tasks/dep.baas", std::move(dep_source)}};
        scripts.insert(scripts.end(), std::make_move_iterator(extras.begin()),
                       std::make_move_iterator(extras.end()));
        const auto resource_manifest = tree_manifest(resources);
        const auto script_manifest = tree_manifest(scripts);
        const std::string resource_commit(40, '1');
        const std::string script_commit(40, '2');
        repositories_ = {{
            {"resources", resource_commit, "objects/resources/" + resource_commit,
             "manifest.json", sha256(resource_manifest)},
            {"scripts", script_commit, "objects/scripts/" + script_commit,
             "scripts.json", sha256(script_manifest)}}};
        for (const auto& file : resources)
            write_file(temporary_.path() / repositories_[0].root / file.path, file.bytes);
        for (const auto& file : scripts)
            write_file(temporary_.path() / repositories_[1].root / file.path, file.bytes);
        write_file(temporary_.path() / repositories_[0].root / repositories_[0].manifest,
                   resource_manifest);
        write_file(temporary_.path() / repositories_[1].root / repositories_[1].manifest,
                   script_manifest);
        generation_ = repository::runtime_repository_generation(repositories_);
        write_file(temporary_.path() / "snapshots" / (generation_ + ".json"),
                   snapshot_json(repositories_, generation_));
        write_file(temporary_.path() / "current.json",
            R"({"schema":"baas.runtime-repositories.current/v1","generation":")"
            + generation_ + R"(","snapshot":"snapshots/)" + generation_ + ".json\"}");
        snapshot_ = repository::RuntimeRepositorySnapshot::activate(temporary_.path());
        bundle_ = snapshot_->open_read_bundle();
    }
    [[nodiscard]] const repository::RuntimeRepositoryReadView& scripts() const
    { return bundle_->scripts(); }
    [[nodiscard]] runtime_script::RuntimeScriptCatalogResolution resolution(
        std::string_view requested_task = "start_main_story") const
    {
        const auto loaded = runtime_script::load_runtime_script_catalog(
            scripts(), {scripts().generation(), scripts().commit()});
        if (!loaded) throw std::runtime_error("catalog fixture failed");
        auto resolved = loaded.catalog->resolve("solve", requested_task);
        if (!resolved) throw std::runtime_error("route fixture failed");
        return *resolved;
    }
private:
    TemporaryDirectory temporary_;
    std::array<repository::RuntimeRepository, 2> repositories_;
    std::string generation_;
    std::shared_ptr<const repository::RuntimeRepositorySnapshot> snapshot_;
    std::shared_ptr<const repository::RuntimeRepositoryReadBundle> bundle_;
};

class FixtureTrust final : public runtime_script::RuntimeScriptRepositoryTrustEvidence {
public:
    explicit FixtureTrust(const repository::RuntimeRepositoryReadView& scripts)
        : generation_(scripts.generation()), commit_(scripts.commit()) {}
    FixtureTrust(std::string generation, std::string commit)
        : generation_(std::move(generation)), commit_(std::move(commit)) {}
    [[nodiscard]] bool covers(std::string_view generation,
                              std::string_view scripts_commit) const noexcept override
    {
        return generation == generation_ && scripts_commit == commit_;
    }
private:
    std::string generation_;
    std::string commit_;
};

[[nodiscard]] runtime_script::RuntimeScriptExecutionPlanResult build_trusted(
    const RepositoryFixture& fixture,
    const runtime_script::RuntimeScriptCatalogResolution& resolution,
    const runtime_script::RuntimeScriptExecutionPlanLimits& limits = {},
    std::stop_token token = {})
{
    const FixtureTrust trust{fixture.scripts()};
    return runtime_script::build_runtime_script_execution_plan(
        fixture.scripts(), resolution, &trust, limits, token);
}

constexpr std::string_view main_source =
    "import \"tasks/dep\" as dep;\nimport \"baas/log\" as log;\nlet ready = true;\n";
constexpr std::string_view dep_source = "let answer = 42;\n";

void expect_error(const runtime_script::RuntimeScriptExecutionPlanResult& result,
                  runtime_script::RuntimeScriptExecutionPlanError error,
                  std::string_view message)
{
    check(!result && result.error == error && !result.plan, message);
}

void test_success_and_lifetime()
{
    std::optional<runtime_script::RuntimeScriptExecutionPlan> retained;
    {
        RepositoryFixture fixture{valid_manifest(main_source, dep_source), catalog_json(),
                                  std::string{main_source}, std::string{dep_source}};
        auto resolution = fixture.resolution();
        const auto result = build_trusted(fixture, resolution);
        if (!result) {
            std::cerr << "success error="
                      << runtime_script::runtime_script_execution_plan_error_name(result.error)
                      << " package="
                      << runtime_script::runtime_script_package_load_error_name(result.package_error)
                      << " module=" << result.module << '\n';
        }
        check(static_cast<bool>(result), "canonical source-only package must plan");
        if (!result) return;
        check(result.plan->legacy_alias() && result.plan->requested_task() == "start_main_story",
              "route identity must be owned");
        check(result.plan->package_id() == "bluearchive.automation.core"
              && result.plan->package_version()
                    == runtime_script::RuntimeScriptPackageVersion{1, 2, 3},
              "package identity must be retained");
        check(result.plan->modules().size() == 2
              && result.plan->package().modules[0].canonical_id == "tasks/dep"
              && result.plan->package().modules[1].canonical_id == "tasks/main",
              "all exact sources must be owned in dependency order");
        retained = *result.plan;
    }
    check(retained && retained->task().entry_export == "run"
          && retained->package().modules[1].source.find("ready") != std::string::npos,
          "plan must outlive catalog resolution, repository, and fixture");
}

void test_catalog_manifest_consistency()
{
    auto run = [](std::string manifest, std::string catalog = catalog_json(),
                  std::string main = std::string{main_source},
                  std::vector<File> extras = {}) {
        RepositoryFixture fixture{std::move(manifest), std::move(catalog), std::move(main),
                                  std::string{dep_source}, std::move(extras)};
        auto resolution = fixture.resolution();
        return build_trusted(fixture, resolution);
    };

    auto manifest = valid_manifest(main_source, dep_source);
    replace_once(manifest, R"("min_minor":1)", R"("min_minor":3)");
    expect_error(run(manifest), runtime_script::RuntimeScriptExecutionPlanError::language_mismatch,
                 "catalog language must satisfy manifest minimum");

    manifest = valid_manifest(main_source, dep_source);
    replace_once(manifest, R"("entrypoint":"tasks/main.baas")",
                 R"("entrypoint":"tasks/dep.baas")");
    expect_error(run(manifest), runtime_script::RuntimeScriptExecutionPlanError::entry_mismatch,
                 "entry module must match exactly");

    manifest = valid_manifest(main_source, dep_source);
    replace_once(manifest, R"("min_minor":0}},"capabilities")",
                 R"("min_minor":1}},"capabilities")");
    expect_error(run(manifest),
                 runtime_script::RuntimeScriptExecutionPlanError::host_requirement_mismatch,
                 "host versions must match catalog exactly");

    manifest = valid_manifest(main_source, dep_source);
    replace_once(manifest, R"(["log.emit"],"profiles")",
                 R"(["log.other"],"profiles")");
    expect_error(run(manifest), runtime_script::RuntimeScriptExecutionPlanError::capability_mismatch,
                 "capabilities cannot be unioned or narrowed silently");

    manifest = valid_manifest(main_source, dep_source);
    replace_once(manifest, R"("profiles":[])", R"("profiles":["desktop"])");
    expect_error(run(manifest), runtime_script::RuntimeScriptExecutionPlanError::unsupported_profiles,
                 "unresolved platform profiles must fail closed");

    manifest = valid_manifest(main_source, dep_source);
    replace_once(manifest, R"("resources":[])",
                 R"("resources":[{"id":"x","path":"x.bin","media_type":"x",)"
                 R"("size":1,"sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}])");
    expect_error(run(manifest), runtime_script::RuntimeScriptExecutionPlanError::unsupported_resources,
                 "resource packages must fail until a real verifier is composed");

    manifest = valid_manifest(main_source, dep_source);
    replace_once(manifest, R"({"manifest_schema":1)",
                 R"({"signature":"not-verified","manifest_schema":1)");
    expect_error(run(manifest), runtime_script::RuntimeScriptExecutionPlanError::unsupported_signature,
                 "signature metadata must never be ignored");
}

void test_module_boundary_and_pin()
{
    RepositoryFixture trust_fixture{valid_manifest(main_source, dep_source), catalog_json(),
        std::string{main_source}, std::string{dep_source}};
    auto trust_resolution = trust_fixture.resolution();
    expect_error(runtime_script::build_runtime_script_execution_plan(
                     trust_fixture.scripts(), trust_resolution, nullptr),
                 runtime_script::RuntimeScriptExecutionPlanError::trust_evidence_required,
                 "a repository read capability alone is not production trust evidence");
    const FixtureTrust wrong_trust{"wrong-generation", std::string(40, '2')};
    expect_error(runtime_script::build_runtime_script_execution_plan(
                     trust_fixture.scripts(), trust_resolution, &wrong_trust),
                 runtime_script::RuntimeScriptExecutionPlanError::trust_evidence_mismatch,
                 "trust evidence must cover the exact generation and commit");
    auto tampered_resolution = trust_resolution;
    tampered_resolution.task = nullptr;
    tampered_resolution.requested_task = "browser-forged";
    tampered_resolution.legacy_alias = false;
    const auto tamper_result = build_trusted(trust_fixture, tampered_resolution);
    check(tamper_result && tamper_result.plan->requested_task() == "start_main_story",
          "mutable compatibility views cannot replace private catalog provenance");

    RepositoryFixture signed_artifact_fixture{
        valid_manifest(main_source, dep_source), catalog_json(),
        std::string{main_source}, std::string{dep_source},
        {{"packages/core/baas.package.sig", R"({"signature_schema":1})"}}};
    auto signed_artifact_resolution = signed_artifact_fixture.resolution();
    expect_error(build_trusted(signed_artifact_fixture, signed_artifact_resolution),
                 runtime_script::RuntimeScriptExecutionPlanError::unsupported_signature,
                 "detached package signatures cannot be silently ignored in repository mode");

    const std::string escaped_main =
        "import \"other/evil\" as evil;\nimport \"baas/log\" as log;\nlet ready = true;\n";
    RepositoryFixture escape_fixture{
        valid_manifest(escaped_main, dep_source), catalog_json(), escaped_main,
        std::string{dep_source},
        {{"packages/core/other/evil.baas", "let bad = true;\n"}}};
    auto escape_resolution = escape_fixture.resolution();
    const auto escaped = build_trusted(escape_fixture, escape_resolution);
    expect_error(escaped, runtime_script::RuntimeScriptExecutionPlanError::package_load_failed,
                 "imports outside the package module set must fail");
    check(escaped.package_error == runtime_script::RuntimeScriptPackageLoadError::module_outside_package,
          "import escape must retain the narrow loader reason");
    if (escaped.package_error != runtime_script::RuntimeScriptPackageLoadError::module_outside_package)
        std::cerr << "escape package="
                  << runtime_script::runtime_script_package_load_error_name(escaped.package_error)
                  << '\n';

    auto mismatch_manifest = valid_manifest(main_source, dep_source);
    replace_once(mismatch_manifest, sha256(dep_source), std::string(64, 'a'));
    RepositoryFixture mismatch_fixture{mismatch_manifest, catalog_json(),
        std::string{main_source}, std::string{dep_source}};
    auto mismatch_resolution = mismatch_fixture.resolution();
    const auto mismatch = build_trusted(mismatch_fixture, mismatch_resolution);
    expect_error(mismatch,
                 runtime_script::RuntimeScriptExecutionPlanError::module_manifest_mismatch,
                 "module metadata must match the pinned repository entry");
    check(mismatch.package_error
              == runtime_script::RuntimeScriptPackageLoadError::module_manifest_mismatch,
          "module mismatch must retain stable loader detail");
    if (mismatch.package_error
        != runtime_script::RuntimeScriptPackageLoadError::module_manifest_mismatch)
        std::cerr << "mismatch package="
                  << runtime_script::runtime_script_package_load_error_name(mismatch.package_error)
                  << '\n';

    RepositoryFixture first{valid_manifest(main_source, dep_source), catalog_json(),
                            std::string{main_source}, std::string{dep_source}};
    RepositoryFixture second{valid_manifest(main_source, dep_source),
        catalog_json("tasks/main", R"({"major":1,"minor":3})"),
        std::string{main_source}, std::string{dep_source}};
    auto first_resolution = first.resolution();
    expect_error(build_trusted(second, first_resolution),
                 runtime_script::RuntimeScriptExecutionPlanError::generation_mismatch,
                 "resolution and repository generation cannot be mixed");

    RepositoryFixture case_fixture{valid_manifest(main_source, dep_source),
        catalog_json("tasks/Main"), std::string{main_source}, std::string{dep_source},
        {}, "tasks/Main.baas"};
    auto case_resolution = case_fixture.resolution();
    expect_error(build_trusted(case_fixture, case_resolution),
                 runtime_script::RuntimeScriptExecutionPlanError::entry_mismatch,
                 "case and path guessing must be rejected");
}

void test_json_limits_and_cancellation()
{
    auto manifest = valid_manifest(main_source, dep_source);
    replace_once(manifest, R"("manifest_schema":1)",
                 R"("manifest_schema":1,"manifest_schema":1)");
    RepositoryFixture duplicate{manifest, catalog_json(), std::string{main_source},
                                std::string{dep_source}};
    auto duplicate_resolution = duplicate.resolution();
    expect_error(build_trusted(duplicate, duplicate_resolution),
                 runtime_script::RuntimeScriptExecutionPlanError::invalid_json,
                 "duplicate JSON fields must fail");

    RepositoryFixture fixture{valid_manifest(main_source, dep_source), catalog_json(),
                              std::string{main_source}, std::string{dep_source}};
    auto resolution = fixture.resolution();
    auto limits = runtime_script::RuntimeScriptExecutionPlanLimits{};
    limits.max_work = 1;
    expect_error(build_trusted(fixture, resolution, limits),
                 runtime_script::RuntimeScriptExecutionPlanError::limit_exceeded,
                 "work limits must include JSON processing");
    limits = {};
    limits.max_modules = 1;
    expect_error(build_trusted(fixture, resolution, limits),
                 runtime_script::RuntimeScriptExecutionPlanError::limit_exceeded,
                 "module count must be bounded before source loading");
    limits = {};
    limits.max_json_depth = 0;
    expect_error(build_trusted(fixture, resolution, limits),
                 runtime_script::RuntimeScriptExecutionPlanError::invalid_limits,
                 "zero limits must fail closed");

    std::stop_source source;
    cancellation_source = &source;
    repository::set_runtime_repository_read_view_hook(cancel_manifest_read);
    const auto cancelled = build_trusted(fixture, resolution, {}, source.get_token());
    repository::set_runtime_repository_read_view_hook(nullptr);
    cancellation_source = nullptr;
    expect_error(cancelled, runtime_script::RuntimeScriptExecutionPlanError::cancelled,
                 "repository and manifest stages must share cancellation");
}

void test_explicit_package_roots_prevent_cross_package_reads()
{
    const std::string one_source =
        "import \"baas/log\" as log;\nlet package_value = 1;\n";
    const std::string two_source =
        "import \"baas/log\" as log;\nlet package_value = 2;\n";
    RepositoryFixture fixture{
        valid_manifest(main_source, dep_source), two_package_catalog(),
        std::string{main_source}, std::string{dep_source},
        {{"packages/one/baas.package.json", single_manifest("example.one", one_source)},
         {"packages/one/main.baas", one_source},
         {"packages/two/baas.package.json", single_manifest("example.two", two_source)},
         {"packages/two/main.baas", two_source}}};
    auto one_resolution = fixture.resolution("start_one");
    auto two_resolution = fixture.resolution("start_two");
    const auto one = build_trusted(fixture, one_resolution);
    const auto two = build_trusted(fixture, two_resolution);
    check(one && two && one.plan->package().modules[0].canonical_id == "main"
          && two.plan->package().modules[0].canonical_id == "main"
          && one.plan->modules()[0].logical_path == "packages/one/main.baas"
          && two.plan->modules()[0].logical_path == "packages/two/main.baas"
          && one.plan->package().modules[0].source.find("= 1") != std::string::npos
          && two.plan->package().modules[0].source.find("= 2") != std::string::npos,
          "same relative module ids in distinct explicit roots must never alias");

    const std::string escape_source =
        "import \"shared\" as shared;\nimport \"baas/log\" as log;\nlet ready = true;\n";
    RepositoryFixture escape_fixture{
        valid_manifest(main_source, dep_source), two_package_catalog(),
        std::string{main_source}, std::string{dep_source},
        {{"packages/one/baas.package.json",
          single_manifest("example.one", escape_source)},
         {"packages/one/main.baas", escape_source},
         {"packages/two/baas.package.json", single_manifest("example.two", two_source)},
         {"packages/two/main.baas", two_source},
         {"packages/two/shared.baas", "let foreign = true;\n"}}};
    auto escape_resolution = escape_fixture.resolution("start_one");
    const auto escaped = build_trusted(escape_fixture, escape_resolution);
    expect_error(escaped, runtime_script::RuntimeScriptExecutionPlanError::package_load_failed,
                 "a package import cannot cross into another explicit root");
    check(escaped.package_error
              == runtime_script::RuntimeScriptPackageLoadError::module_outside_package,
          "cross-package import must fail at the exact allowlist");
}

void test_shared_capability_is_a_manifest_set_union()
{
    const std::string source =
        "import \"tasks/dep\" as dep;\n"
        "import \"baas/log\" as log;\n"
        "import \"baas/resource\" as resource;\n"
        "let ready = true;\n";
    auto manifest = valid_manifest(source, dep_source);
    replace_once(
        manifest,
        R"("host_modules":{"baas/log":{"major":1,"min_minor":0}})",
        R"("host_modules":{"baas/log":{"major":1,"min_minor":0},)"
        R"("baas/resource":{"major":1,"min_minor":0}})");
    replace_once(
        manifest,
        R"("capabilities":["log.emit"])",
        R"("capabilities":["test.shared"])");

    auto catalog = catalog_json(
        "tasks/main", R"({"major":1,"minor":2})", "0",
        R"(["test.shared"])");
    replace_once(
        catalog,
        R"(}],"legacy_aliases")",
        R"(},{"module":"baas/resource","major":1,"min_minor":0,)"
        R"("capabilities":["test.shared"]}],"legacy_aliases")");

    RepositoryFixture fixture{
        std::move(manifest), std::move(catalog), source, std::string{dep_source}};
    auto resolution = fixture.resolution();
    const auto result = build_trusted(fixture, resolution);
    check(result && result.plan->capabilities().size() == 1
          && result.plan->capabilities()[0] == "test.shared",
          "capabilities shared by Host requirements must form one manifest set member");
}

void test_stable_error_names()
{
    using Error = runtime_script::RuntimeScriptExecutionPlanError;
    constexpr std::array expected{
        std::pair{Error::none, std::string_view{"RSE000_NONE"}},
        std::pair{Error::invalid_limits, std::string_view{"RSE001_INVALID_LIMITS"}},
        std::pair{Error::invalid_resolution, std::string_view{"RSE002_INVALID_RESOLUTION"}},
        std::pair{Error::wrong_repository, std::string_view{"RSE003_WRONG_REPOSITORY"}},
        std::pair{Error::generation_mismatch, std::string_view{"RSE004_GENERATION_MISMATCH"}},
        std::pair{Error::commit_mismatch, std::string_view{"RSE005_COMMIT_MISMATCH"}},
        std::pair{Error::manifest_not_found, std::string_view{"RSE006_MANIFEST_NOT_FOUND"}},
        std::pair{Error::manifest_too_large, std::string_view{"RSE007_MANIFEST_TOO_LARGE"}},
        std::pair{Error::repository_read_failed, std::string_view{"RSE008_REPOSITORY_READ_FAILED"}},
        std::pair{Error::invalid_utf8, std::string_view{"RSE009_INVALID_UTF8"}},
        std::pair{Error::invalid_json, std::string_view{"RSE010_INVALID_JSON"}},
        std::pair{Error::limit_exceeded, std::string_view{"RSE011_LIMIT_EXCEEDED"}},
        std::pair{Error::manifest_schema_unsupported, std::string_view{"RSE012_MANIFEST_SCHEMA_UNSUPPORTED"}},
        std::pair{Error::invalid_field_set, std::string_view{"RSE013_INVALID_FIELD_SET"}},
        std::pair{Error::invalid_value, std::string_view{"RSE014_INVALID_VALUE"}},
        std::pair{Error::trust_evidence_required, std::string_view{"RSE015_TRUST_EVIDENCE_REQUIRED"}},
        std::pair{Error::trust_evidence_mismatch, std::string_view{"RSE016_TRUST_EVIDENCE_MISMATCH"}},
        std::pair{Error::unsupported_signature, std::string_view{"RSE017_UNSUPPORTED_SIGNATURE"}},
        std::pair{Error::unsupported_resources, std::string_view{"RSE018_UNSUPPORTED_RESOURCES"}},
        std::pair{Error::unsupported_profiles, std::string_view{"RSE019_UNSUPPORTED_PROFILES"}},
        std::pair{Error::language_mismatch, std::string_view{"RSE020_LANGUAGE_MISMATCH"}},
        std::pair{Error::entry_mismatch, std::string_view{"RSE021_ENTRY_MISMATCH"}},
        std::pair{Error::host_requirement_mismatch, std::string_view{"RSE022_HOST_REQUIREMENT_MISMATCH"}},
        std::pair{Error::capability_mismatch, std::string_view{"RSE023_CAPABILITY_MISMATCH"}},
        std::pair{Error::module_manifest_mismatch, std::string_view{"RSE024_MODULE_MANIFEST_MISMATCH"}},
        std::pair{Error::package_load_failed, std::string_view{"RSE025_PACKAGE_LOAD_FAILED"}},
        std::pair{Error::cancelled, std::string_view{"RSE026_CANCELLED"}},
        std::pair{Error::resource_exhausted, std::string_view{"RSE027_RESOURCE_EXHAUSTED"}},
    };
    for (const auto& [error, name] : expected)
        check(runtime_script::runtime_script_execution_plan_error_name(error) == name,
              "every execution-plan error must expose a stable code");
}

}  // namespace

int main()
{
    try {
        test_success_and_lifetime();
        test_catalog_manifest_consistency();
        test_module_boundary_and_pin();
        test_json_limits_and_cancellation();
        test_explicit_package_roots_prevent_cross_package_reads();
        test_shared_capability_is_a_manifest_set_union();
        test_stable_error_names();
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "unexpected exception: " << error.what() << '\n';
    }
    if (failures.load() != 0) {
        std::cerr << failures.load() << " execution plan test(s) failed\n";
        return 1;
    }
    std::cout << "Runtime script execution plan tests passed\n";
    return 0;
}
