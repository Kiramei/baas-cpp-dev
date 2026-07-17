#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "runtime/script/RuntimeScriptPackageLoader.h"
#include "resources/ResourceSnapshot.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace repository = baas::runtime::repository;
namespace package_loader = baas::runtime::script;

std::atomic<int> failures{};
std::atomic<int> payload_read_starts{};
std::stop_source* cancellation_source{};

void count_payload_reads(
    const repository::RuntimeRepositoryReadHookPoint point,
    const std::string_view repository_id,
    const std::string_view)
{
    if (point == repository::RuntimeRepositoryReadHookPoint::payload_handle_opened
        && repository_id == "scripts") {
        ++payload_read_starts;
    }
}

void cancel_during_payload_digest(
    const repository::RuntimeRepositoryReadHookPoint point,
    const std::string_view repository_id,
    const std::string_view logical_path)
{
    if (point == repository::RuntimeRepositoryReadHookPoint::payload_digest_finalizing
        && repository_id == "scripts" && logical_path == "success/main.baas"
        && cancellation_source != nullptr) {
        cancellation_source->request_stop();
    }
}

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = std::filesystem::temp_directory_path()
            / ("baas-runtime-script-loader-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::string_view value)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not create fixture file");
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output) throw std::runtime_error("could not write fixture file");
}

[[nodiscard]] std::string sha256(const std::string_view value)
{
    const auto characters = std::span{value.data(), value.size()};
    return baas::resources::sha256_hex(std::as_bytes(characters));
}

struct FixtureFile {
    std::string path;
    std::string source;
};

[[nodiscard]] std::string tree_manifest(std::vector<FixtureFile> files)
{
    std::ranges::sort(files, {}, &FixtureFile::path);
    std::string result =
        R"({"schema":"baas.runtime-repository.tree-manifest/v1","entries":[)";
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (index != 0) result.push_back(',');
        result += R"({"path":")" + files[index].path
            + R"(","size":")" + std::to_string(files[index].source.size())
            + R"(","sha256":")" + sha256(files[index].source)
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
    for (std::size_t index = 0; index < repositories.size(); ++index) {
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
    explicit RepositoryFixture(std::vector<FixtureFile> scripts)
        : scripts_(std::move(scripts))
    {
        const std::vector<FixtureFile> resources{{"placeholder.bin", "resource"}};
        const std::string resource_manifest = tree_manifest(resources);
        const std::string script_manifest = tree_manifest(scripts_);

        const std::string resource_commit(40, '1');
        const std::string script_commit(64, '2');
        repositories_ = {{
            {"resources", resource_commit,
             "objects/resources/" + resource_commit,
             "manifest.json", sha256(resource_manifest)},
            {"scripts", script_commit,
             "objects/scripts/" + script_commit,
             "scripts.json", sha256(script_manifest)},
        }};

        for (const auto& file : resources) {
            write_file(
                temporary_.path() / repositories_[0].root / file.path,
                file.source);
        }
        for (const auto& file : scripts_) {
            write_file(
                temporary_.path() / repositories_[1].root / file.path,
                file.source);
        }
        write_file(
            temporary_.path() / repositories_[0].root / repositories_[0].manifest,
            resource_manifest);
        write_file(
            temporary_.path() / repositories_[1].root / repositories_[1].manifest,
            script_manifest);

        generation_ = repository::runtime_repository_generation(repositories_);
        write_file(
            temporary_.path() / "snapshots" / (generation_ + ".json"),
            snapshot_json(repositories_, generation_));
        write_file(
            temporary_.path() / "current.json",
            R"({"schema":"baas.runtime-repositories.current/v1","generation":")"
                + generation_ + R"(","snapshot":"snapshots/)" + generation_
                + ".json\"}");

        snapshot_ = repository::RuntimeRepositorySnapshot::activate(
            temporary_.path());
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

    void add_unmanifested_script(
        const std::string_view path, const std::string_view source)
    {
        write_file(
            temporary_.path() / repositories_[1].root / std::string{path},
            source);
    }

private:
    TemporaryDirectory temporary_;
    std::vector<FixtureFile> scripts_;
    std::array<repository::RuntimeRepository, 2> repositories_;
    std::string generation_;
    std::shared_ptr<const repository::RuntimeRepositorySnapshot> snapshot_;
    std::shared_ptr<const repository::RuntimeRepositoryReadBundle> bundle_;
};

[[nodiscard]] std::vector<FixtureFile> fixture_files()
{
    return {
        {"success/main.baas",
         "import \"success/dep\" as dep;\n"
         "import \"baas/log\" as log;\n"
         "let ready = true;\n"},
        {"success/dep.baas", "let answer = 42;\n"},
        {"cycle/a.baas", "import \"cycle/b\" as b;\n"},
        {"cycle/b.baas", "import \"cycle/a\" as a;\n"},
        {"missing/main.baas", "import \"missing/absent\" as absent;\n"},
        {"invalid-import/main.baas", "import \"../hidden\" as hidden;\n"},
        {"outside/main.baas", "import \"outside/hidden\" as hidden;\n"},
        {"case/main.baas", "import \"case/Target\" as target;\n"},
        {"case/target.baas", "let value = 1;\n"},
        {"depth/root.baas", "import \"depth/a\" as a;\n"},
        {"depth/a.baas", "import \"depth/b\" as b;\n"},
        {"depth/b.baas", "let value = 1;\n"},
        {"parse/bad.baas", "let = ;\n"},
        {"semantic/bad.baas", "let value = missing_name;\n"},
        {"limits/ast.baas",
         "let a = 1; let b = a; let c = b; let d = c;\n"},
        {"limits/nesting.baas",
         "{{{{{{{{{{{{let deep = 1;}}}}}}}}}}}}\n"},
    };
}

void expect_error(
    const package_loader::RuntimeScriptPackageLoadResult& result,
    const package_loader::RuntimeScriptPackageLoadError error,
    const std::string_view message)
{
    check(!result, message);
    check(result.error == error, message);
    check(!result.package.has_value(), message);
}

void test_success_host_import_and_no_execution(
    const RepositoryFixture& fixture)
{
    const auto result = package_loader::load_runtime_script_package(
        fixture.scripts(), "success/main");
    check(static_cast<bool>(result),
          "a valid recursively imported package must load");
    if (!result) return;
    check(result.package->entry_module == "success/main",
          "the canonical entry module must be retained");
    check(result.package->modules.size() == 2,
          "package imports must become owned SourceModule values");
    check(result.package->modules[0].canonical_id == "success/dep"
              && result.package->modules[1].canonical_id == "success/main",
          "SourceModule output must follow graph initialization order");
    check(result.package->host_imports
              == std::vector<std::string>{"baas/log"},
          "host imports must be recorded without repository reads");
    check(result.package->graph.package_import_edges == 1
              && result.package->graph.host_import_edges == 1,
          "validated graph edge accounting must be retained");
    check(result.package->total_source_bytes != 0
              && result.package->work != 0,
          "bounded load accounting must be observable");
}

void test_manifest_case_cycle_and_language_failures(RepositoryFixture& fixture)
{
    auto result = package_loader::load_runtime_script_package(
        fixture.scripts(), "missing/main");
    expect_error(
        result,
        package_loader::RuntimeScriptPackageLoadError::module_not_manifested,
        "a missing package import must fail at the manifest boundary");
    check(result.module == "missing/absent",
          "missing import diagnostics expose only the canonical logical ID");

    result = package_loader::load_runtime_script_package(
        fixture.scripts(), "case/main");
    expect_error(
        result,
        package_loader::RuntimeScriptPackageLoadError::module_not_manifested,
        "manifest lookup must be byte-exact and case-sensitive");

    result = package_loader::load_runtime_script_package(
        fixture.scripts(), "cycle/a");
    expect_error(
        result,
        package_loader::RuntimeScriptPackageLoadError::import_cycle,
        "the discovered package must pass validate_module_graph cycle checks");

    result = package_loader::load_runtime_script_package(
        fixture.scripts(), "parse/bad");
    expect_error(
        result,
        package_loader::RuntimeScriptPackageLoadError::parse_failed,
        "parse diagnostics must fail package discovery");
    check(!result.diagnostics.empty(),
          "parse failures must retain structured diagnostics");

    result = package_loader::load_runtime_script_package(
        fixture.scripts(), "semantic/bad");
    expect_error(
        result,
        package_loader::RuntimeScriptPackageLoadError::semantic_failed,
        "semantic diagnostics must fail package discovery");
    check(!result.diagnostics.empty(),
          "semantic failures must retain structured diagnostics");

    result = package_loader::load_runtime_script_package(
        fixture.scripts(), "success/main.baas");
    expect_error(
        result,
        package_loader::RuntimeScriptPackageLoadError::invalid_entry_module,
        "entry IDs must be canonical and extensionless");

    result = package_loader::load_runtime_script_package(
        fixture.scripts(), "baas/log");
    expect_error(
        result,
        package_loader::RuntimeScriptPackageLoadError::host_entry_module,
        "host modules are requirements and cannot be package entry sources");

    result = package_loader::load_runtime_script_package(
        fixture.scripts(), "invalid-import/main");
    expect_error(
        result,
        package_loader::RuntimeScriptPackageLoadError::invalid_import_specifier,
        "package imports must be canonical before any manifest lookup");

    result = package_loader::load_runtime_script_package(
        fixture.resources(), "success/main");
    expect_error(
        result,
        package_loader::RuntimeScriptPackageLoadError::wrong_repository,
        "a resources capability must not be accepted as the scripts repository");

    fixture.add_unmanifested_script(
        "outside/hidden.baas", "let hidden = true;\n");
    result = package_loader::load_runtime_script_package(
        fixture.scripts(), "outside/main");
    expect_error(
        result,
        package_loader::RuntimeScriptPackageLoadError::module_not_manifested,
        "a file created outside the pinned manifest must remain unreadable");
}

void test_all_loader_budgets_and_cancellation(const RepositoryFixture& fixture)
{
    package_loader::RuntimeScriptPackageLoaderLimits limits;
    limits.max_modules = 1;
    expect_error(
        package_loader::load_runtime_script_package(
            fixture.scripts(), "success/main", limits),
        package_loader::RuntimeScriptPackageLoadError::module_limit_exceeded,
        "module discovery must enforce max_modules");

    limits = {};
    limits.max_source_file_bytes = 8;
    expect_error(
        package_loader::load_runtime_script_package(
            fixture.scripts(), "success/main", limits),
        package_loader::RuntimeScriptPackageLoadError::source_file_too_large,
        "manifest size must reject a source before reading it");

    limits = {};
    limits.max_total_source_bytes = 80;
    limits.max_source_file_bytes = 80;
    expect_error(
        package_loader::load_runtime_script_package(
            fixture.scripts(), "success/main", limits),
        package_loader::RuntimeScriptPackageLoadError::total_source_bytes_exceeded,
        "aggregate source bytes must be bounded across the package");

    limits = {};
    limits.max_import_edges = 1;
    expect_error(
        package_loader::load_runtime_script_package(
            fixture.scripts(), "success/main", limits),
        package_loader::RuntimeScriptPackageLoadError::import_edge_limit_exceeded,
        "host and package imports must share the edge budget");

    limits = {};
    limits.max_import_depth = 1;
    expect_error(
        package_loader::load_runtime_script_package(
            fixture.scripts(), "depth/root", limits),
        package_loader::RuntimeScriptPackageLoadError::import_depth_exceeded,
        "the complete validated DAG must enforce longest import depth");

    limits = {};
    limits.max_work = 1;
    payload_read_starts = 0;
    repository::set_runtime_repository_read_view_hook(count_payload_reads);
    const auto no_work = package_loader::load_runtime_script_package(
        fixture.scripts(), "success/main", limits);
    repository::set_runtime_repository_read_view_hook(nullptr);
    expect_error(
        no_work,
        package_loader::RuntimeScriptPackageLoadError::work_limit_exceeded,
        "reads, parsing, semantics, discovery, and graph validation share a work budget");
    check(payload_read_starts == 0,
          "insufficient work budget must fail before opening a source payload");

    limits = {};
    limits.max_ast_nodes_per_module = 3;
    expect_error(
        package_loader::load_runtime_script_package(
            fixture.scripts(), "limits/ast", limits),
        package_loader::RuntimeScriptPackageLoadError::semantic_failed,
        "the per-module semantic AST budget must be enforced");

    limits = {};
    limits.max_semantic_nesting_depth = 4;
    expect_error(
        package_loader::load_runtime_script_package(
            fixture.scripts(), "limits/nesting", limits),
        package_loader::RuntimeScriptPackageLoadError::semantic_failed,
        "the semantic nesting budget must be enforced");

    limits = {};
    limits.specifier.max_bytes = 4;
    expect_error(
        package_loader::load_runtime_script_package(
            fixture.scripts(), "success/main", limits),
        package_loader::RuntimeScriptPackageLoadError::invalid_entry_module,
        "entry module IDs must obey the shared specifier byte budget");

    limits = {};
    limits.specifier.max_segments = 1;
    expect_error(
        package_loader::load_runtime_script_package(
            fixture.scripts(), "success/main", limits),
        package_loader::RuntimeScriptPackageLoadError::invalid_entry_module,
        "entry module IDs must obey the shared specifier segment budget");

    std::stop_source cancelled;
    cancelled.request_stop();
    expect_error(
        package_loader::load_runtime_script_package(
            fixture.scripts(), "success/main", {}, cancelled.get_token()),
        package_loader::RuntimeScriptPackageLoadError::cancelled,
        "pre-cancelled package discovery must perform no reads");

    std::stop_source during_read;
    cancellation_source = &during_read;
    repository::set_runtime_repository_read_view_hook(
        cancel_during_payload_digest);
    const auto interrupted = package_loader::load_runtime_script_package(
        fixture.scripts(), "success/main", {}, during_read.get_token());
    repository::set_runtime_repository_read_view_hook(nullptr);
    cancellation_source = nullptr;
    expect_error(
        interrupted,
        package_loader::RuntimeScriptPackageLoadError::cancelled,
        "cancellation during a verified payload read must abort discovery");

    limits = {};
    limits.max_modules = 0;
    expect_error(
        package_loader::load_runtime_script_package(
            fixture.scripts(), "success/main", limits),
        package_loader::RuntimeScriptPackageLoadError::invalid_limits,
        "zero limits must fail closed before discovery");
}

void test_stable_error_names()
{
    using Error = package_loader::RuntimeScriptPackageLoadError;
    constexpr std::array expected{
        std::pair{Error::none, std::string_view{"RSP000_NONE"}},
        std::pair{Error::invalid_limits, std::string_view{"RSP001_INVALID_LIMITS"}},
        std::pair{Error::wrong_repository, std::string_view{"RSP002_WRONG_REPOSITORY"}},
        std::pair{Error::invalid_entry_module, std::string_view{"RSP003_INVALID_ENTRY_MODULE"}},
        std::pair{Error::host_entry_module, std::string_view{"RSP004_HOST_ENTRY_MODULE"}},
        std::pair{Error::module_not_manifested, std::string_view{"RSP005_MODULE_NOT_MANIFESTED"}},
        std::pair{Error::repository_read_failed, std::string_view{"RSP006_REPOSITORY_READ_FAILED"}},
        std::pair{Error::source_file_too_large, std::string_view{"RSP007_SOURCE_FILE_TOO_LARGE"}},
        std::pair{Error::total_source_bytes_exceeded, std::string_view{"RSP008_TOTAL_SOURCE_BYTES_EXCEEDED"}},
        std::pair{Error::module_limit_exceeded, std::string_view{"RSP009_MODULE_LIMIT_EXCEEDED"}},
        std::pair{Error::import_edge_limit_exceeded, std::string_view{"RSP010_IMPORT_EDGE_LIMIT_EXCEEDED"}},
        std::pair{Error::import_depth_exceeded, std::string_view{"RSP011_IMPORT_DEPTH_EXCEEDED"}},
        std::pair{Error::work_limit_exceeded, std::string_view{"RSP012_WORK_LIMIT_EXCEEDED"}},
        std::pair{Error::parse_failed, std::string_view{"RSP013_PARSE_FAILED"}},
        std::pair{Error::semantic_failed, std::string_view{"RSP014_SEMANTIC_FAILED"}},
        std::pair{Error::invalid_import_specifier, std::string_view{"RSP015_INVALID_IMPORT_SPECIFIER"}},
        std::pair{Error::missing_package_module, std::string_view{"RSP016_MISSING_PACKAGE_MODULE"}},
        std::pair{Error::import_cycle, std::string_view{"RSP017_IMPORT_CYCLE"}},
        std::pair{Error::graph_validation_failed, std::string_view{"RSP018_GRAPH_VALIDATION_FAILED"}},
        std::pair{Error::cancelled, std::string_view{"RSP019_CANCELLED"}},
        std::pair{Error::resource_exhausted, std::string_view{"RSP020_RESOURCE_EXHAUSTED"}},
        std::pair{Error::invalid_package_manifest, std::string_view{"RSP021_INVALID_PACKAGE_MANIFEST"}},
        std::pair{Error::package_pin_mismatch, std::string_view{"RSP022_PACKAGE_PIN_MISMATCH"}},
        std::pair{Error::module_outside_package, std::string_view{"RSP023_MODULE_OUTSIDE_PACKAGE"}},
        std::pair{Error::module_manifest_mismatch, std::string_view{"RSP024_MODULE_MANIFEST_MISMATCH"}},
        std::pair{Error::unexpected_package_module, std::string_view{"RSP025_UNEXPECTED_PACKAGE_MODULE"}},
    };
    for (const auto& [error, name] : expected) {
        check(
            package_loader::runtime_script_package_load_error_name(error) == name,
            "every package loader error must expose a stable code");
    }
}

}  // namespace

int main()
{
    try {
        RepositoryFixture fixture{fixture_files()};
        test_success_host_import_and_no_execution(fixture);
        test_manifest_case_cycle_and_language_failures(fixture);
        test_all_loader_budgets_and_cancellation(fixture);
        test_stable_error_names();
    } catch (const std::exception& error) {
        std::cerr << "unexpected test exception: " << error.what() << '\n';
        return 1;
    }
    if (failures.load() != 0) {
        std::cerr << failures.load()
                  << " runtime script package loader test(s) failed\n";
        return 1;
    }
    std::cout << "Runtime script package loader tests passed\n";
    return 0;
}
