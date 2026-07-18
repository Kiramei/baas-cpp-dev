#include "resources/ResourceSnapshot.h"
#include "runtime/procedure/CoDetectPythonCompatDefinition.h"
#include "runtime/procedure/RuntimeProcedureActivation.h"
#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "runtime/resources/RuntimeResourceSnapshotLoader.h"
#include "runtime/script/RuntimeScriptCatalog.h"
#include "runtime/script/RuntimeScriptExecutionPlan.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace procedure = baas::runtime::procedure;
namespace repository = baas::runtime::repository;
namespace runtime_resources = baas::runtime::resources;
namespace runtime_script = baas::runtime::script;
namespace resources = baas::resources;

std::atomic<int> failures{};
std::stop_source* cancellation_source{};
std::atomic<int> definition_reads{};

void check(const bool condition, const std::string_view message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic<unsigned long long> serial{};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("baas-runtime-procedure-activation-" + std::to_string(stamp) + "-" +
             std::to_string(serial.fetch_add(1)));
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

void write_file(const std::filesystem::path& path, const std::string_view value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("fixture create failed");
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output) throw std::runtime_error("fixture write failed");
}

[[nodiscard]] std::string sha256(const std::string_view value) {
    return resources::sha256_hex(std::as_bytes(std::span{value.data(), value.size()}));
}

struct File final { std::string path; std::string bytes; };

[[nodiscard]] std::string tree_manifest(std::vector<File> files) {
    std::ranges::sort(files, {}, &File::path);
    std::string result =
        R"({"schema":"baas.runtime-repository.tree-manifest/v1","entries":[)";
    for (std::size_t index{}; index < files.size(); ++index) {
        if (index) result.push_back(',');
        result += R"({"path":")" + files[index].path + R"(","size":")" +
            std::to_string(files[index].bytes.size()) + R"(","sha256":")" +
            sha256(files[index].bytes) + R"(","mode":"file"})";
    }
    return result + "]}";
}

[[nodiscard]] std::string snapshot_json(
    const std::array<repository::RuntimeRepository, 2>& repositories,
    const std::string_view generation) {
    std::string result =
        R"({"schema":"baas.runtime-repositories.snapshot/v1","generation":")" +
        std::string{generation} + R"(","repositories":[)";
    for (std::size_t index{}; index < repositories.size(); ++index) {
        if (index) result.push_back(',');
        const auto& item = repositories[index];
        result += R"({"id":")" + item.id + R"(","commit":")" + item.commit +
            R"(","root":")" + item.root + R"(","manifest":")" + item.manifest +
            R"(","manifest_sha256":")" + item.manifest_sha256 + R"("})";
    }
    return result + "]}";
}

constexpr std::string_view source =
    "import \"baas/procedure\" as procedure;\nlet ready = true;\n";

[[nodiscard]] std::string catalog_json() {
    return R"({"schema":"baas.runtime-script.catalog/v2","tasks":[{)"
        R"("run_mode":"solve","task":"main_story","package_root":"packages/core",)"
        R"("package_manifest":"packages/core/baas.package.json",)"
        R"("entry_module":"tasks/main","entry_export":"run",)"
        R"("language_version":{"major":1,"minor":2},"host_modules":[{)"
        R"("module":"baas/procedure","major":1,"min_minor":0,)"
        R"("capabilities":["procedure.run"]}],"legacy_aliases":[]}]})";
}

[[nodiscard]] std::string package_json(
    const std::vector<std::string>& procedure_ids = {"group/menu", "group/reward"}) {
    std::string procedures = "[";
    for (std::size_t index{}; index < procedure_ids.size(); ++index) {
        if (index) procedures.push_back(',');
        procedures += '"' + procedure_ids[index] + '"';
    }
    procedures += ']';
    return R"({"manifest_schema":2,"package":{"id":"bluearchive.automation.core",)"
        R"("version":"1.2.3","build":"fixture"},)"
        R"("language":{"major":1,"min_minor":1},"entrypoint":"tasks/main.baas",)"
        R"("host_modules":{"baas/procedure":{"major":1,"min_minor":0}},)"
        R"("capabilities":["procedure.run"],"procedures":)" + procedures +
        R"(,"profiles":[],"modules":[{"path":"tasks/main.baas","size":)" +
        std::to_string(source.size()) + R"(,"sha256":")" + sha256(source) +
        R"("}],"resources":[],"limits":{"source_bytes":4096,"resource_bytes":0,)"
        R"("module_count":1,"resource_count":0}})";
}

[[nodiscard]] std::string resource_manifest(const bool include_resource = true) {
    if (!include_resource)
        return R"({"schema":"baas.resources/v1","entries":[]})";
    constexpr std::string_view bytes = "menu-image";
    return R"({"schema":"baas.resources/v1","entries":[{)"
        R"("id":"image/group/menu","path":"payload/menu.bin",)"
        R"("media_type":"application/octet-stream","size":)" +
        std::to_string(bytes.size()) + R"(,"sha256":")" + sha256(bytes) + "\"}]}";
}

struct ProcedureSpec final {
    std::string id;
    std::string path;
    std::string definition;
    std::string terminals;
    std::string effects;
    std::string resource_ids;
    std::string result_schema;
};

[[nodiscard]] std::vector<ProcedureSpec> valid_specs(
    const std::string_view menu_marker = "menu-v1",
    const std::string_view menu_terminals =
        R"([{"source":"joined","id":"joined"},{"source":"exists","id":"already_joined"}])") {
    return {
        {"group/menu", "procedures/group/menu.json",
         R"({"schema":"baas.procedure-definition/v1","engine":"legacy.appear_then_click/v1","payload":{"marker":")" +
             std::string{menu_marker} + "\"}}",
         std::string{menu_terminals}, R"(["capture","vision","input","wait"])",
         R"(["image/group/menu"] )"},
        {"group/reward", "procedures/group/reward.json",
         R"({"schema":"baas.procedure-definition/v1","engine":"legacy.appear_then_click/v1","payload":{"marker":"reward-v1"}})",
         R"([{"source":"claimed","id":"claimed"}])", R"(["capture","input"])", "[]"},
    };
}

[[nodiscard]] std::string procedures_manifest(const std::vector<ProcedureSpec>& specs) {
    std::string result = R"({"schema":"baas.procedures/v1","entries":[)";
    for (std::size_t index{}; index < specs.size(); ++index) {
        if (index) result.push_back(',');
        const auto& item = specs[index];
        result += R"({"id":")" + item.id + R"(","definition":{"path":")" +
            item.path + R"(","size":)" + std::to_string(item.definition.size()) +
            R"(,"sha256":")" + sha256(item.definition) + R"("},"terminals":)" +
            item.terminals + R"(,"effects":)" + item.effects + R"(,"resources":)" +
            item.resource_ids + (item.result_schema.empty()
                ? std::string{} : R"(,"result":)" + item.result_schema) + '}';
    }
    return result + "]}";
}

[[nodiscard]] std::vector<File> procedure_files(
    const std::vector<ProcedureSpec>& specs, std::string manifest = {}) {
    if (manifest.empty()) manifest = procedures_manifest(specs);
    std::vector<File> result{{std::string{procedure::runtime_procedure_manifest_path},
                              std::move(manifest)}};
    for (const auto& item : specs)
        result.push_back({item.path, item.definition});
    return result;
}

class FixtureTrust final : public runtime_script::RuntimeScriptRepositoryTrustEvidence {
public:
    explicit FixtureTrust(const repository::RuntimeRepositoryReadView& scripts)
        : generation_(scripts.generation()), commit_(scripts.commit()) {}
    [[nodiscard]] bool covers(const std::string_view generation,
                              const std::string_view scripts_commit) const noexcept override {
        return generation == generation_ && scripts_commit == commit_;
    }
private:
    std::string generation_;
    std::string commit_;
};

class RepositoryFixture final {
public:
    explicit RepositoryFixture(
        std::vector<File> procedure_payloads = procedure_files(valid_specs()),
        std::vector<std::string> package_procedures = {"group/menu", "group/reward"},
        const bool include_resource = true) {
        std::vector<File> resource_files{
            {std::string{runtime_resources::runtime_resource_manifest_path},
             resource_manifest(include_resource)}};
        if (include_resource)
            resource_files.push_back({"payload/menu.bin", "menu-image"});
        std::vector<File> script_files{
            {std::string{runtime_script::runtime_script_catalog_manifest}, catalog_json()},
            {"packages/core/baas.package.json", package_json(package_procedures)},
            {"packages/core/tasks/main.baas", std::string{source}}};
        script_files.insert(script_files.end(),
                            std::make_move_iterator(procedure_payloads.begin()),
                            std::make_move_iterator(procedure_payloads.end()));
        const auto resource_tree = tree_manifest(resource_files);
        const auto script_tree = tree_manifest(script_files);
        const std::string resource_commit(40, '1');
        const std::string script_commit = sha256(script_tree).substr(0, 40);
        repositories_ = {{
            {"resources", resource_commit, "objects/resources/" + resource_commit,
             "manifest.json", sha256(resource_tree)},
            {"scripts", script_commit, "objects/scripts/" + script_commit,
             "manifest.json", sha256(script_tree)}}};
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
        bundle_ = snapshot_->open_read_bundle();
    }
    [[nodiscard]] const repository::RuntimeRepositoryReadView& scripts() const {
        return bundle_->scripts();
    }
    [[nodiscard]] const repository::RuntimeRepositoryReadView& resources() const {
        return bundle_->resources();
    }
    [[nodiscard]] runtime_script::RuntimeScriptExecutionPlan plan() const {
        const auto catalog = runtime_script::load_runtime_script_catalog(
            scripts(), {scripts().generation(), scripts().commit()});
        if (!catalog) throw std::runtime_error("catalog load failed");
        const auto resolution = catalog.catalog->resolve("solve", "main_story");
        if (!resolution) throw std::runtime_error("catalog resolution failed");
        const FixtureTrust trust{scripts()};
        const auto result = runtime_script::build_runtime_script_execution_plan(
            scripts(), *resolution, &trust);
        if (!result) {
            std::cerr << "plan error="
                      << runtime_script::runtime_script_execution_plan_error_name(result.error)
                      << '\n';
            throw std::runtime_error("plan build failed");
        }
        return *result.plan;
    }
    [[nodiscard]] std::shared_ptr<const runtime_resources::RuntimeResourceSnapshotActivation>
    resource_activation() const {
        const auto result = runtime_resources::load_runtime_resource_snapshot(
            resources(), {"CN", std::nullopt});
        if (!result) throw std::runtime_error("resource activation failed");
        return result.activation;
    }
    void erase_script(const std::string_view logical_path) {
        std::filesystem::remove(
            temporary_.path() / repositories_[1].root / std::string{logical_path});
    }
private:
    TemporaryDirectory temporary_;
    std::array<repository::RuntimeRepository, 2> repositories_;
    std::string generation_;
    std::shared_ptr<const repository::RuntimeRepositorySnapshot> snapshot_;
    std::shared_ptr<const repository::RuntimeRepositoryReadBundle> bundle_;
};

[[nodiscard]] procedure::RuntimeProcedureActivationLoadResult load(
    const RepositoryFixture& fixture,
    const procedure::RuntimeProcedureActivationLimits& limits = {},
    const std::stop_token stop = {}) {
    return procedure::load_runtime_procedure_activation(
        fixture.scripts(), fixture.plan(), fixture.resource_activation(), limits, stop);
}

void expect_error(const procedure::RuntimeProcedureActivationLoadResult& result,
                  const procedure::RuntimeProcedureActivationError error,
                  const std::string_view message) {
    check(!result && !result.activation && result.error == error, message);
    if (result.error != error)
        std::cerr << "  actual=" << procedure::runtime_procedure_activation_error_name(result.error)
                  << " expected=" << procedure::runtime_procedure_activation_error_name(error)
                  << " procedure=" << result.procedure_id << '\n';
}

void test_success_closure_and_owned_lifetime() {
    std::shared_ptr<const procedure::RuntimeProcedureActivation> retained;
    std::shared_ptr<const procedure::RuntimeProcedureDefinition> definition;
    std::string generation;
    {
        auto specs = valid_specs();
        specs.push_back({"unused/procedure", "procedures/unused/procedure.json",
            R"({"schema":"baas.procedure-definition/v1","engine":"legacy.appear_then_click/v1","payload":{"unused":true}})",
            R"([{"source":"ok","id":"ok"}])", "[]", "[]"});
        RepositoryFixture fixture{procedure_files(specs)};
        fixture.erase_script("procedures/unused/procedure.json");
        const auto result = load(fixture);
        check(static_cast<bool>(result), "valid closure must activate");
        if (!result) return;
        retained = result.activation;
        generation = fixture.scripts().generation();
        definition = retained->resolve_definition("group/menu");
        check(retained->generation() == generation &&
              retained->scripts_commit() == fixture.scripts().commit() &&
              retained->resources_commit() == fixture.resources().commit(),
              "activation must retain exact repository provenance");
        check(retained->procedure_count() == 2 && !retained->resolve_definition("unused/procedure"),
              "only the trusted plan closure may load definition bytes");
        check(definition && definition->engine() == procedure::runtime_procedure_legacy_engine &&
              definition->terminals().size() == 2 &&
              definition->terminals()[1].source == "exists" &&
              definition->terminals()[1].id == "already_joined",
              "definition must retain ordered source-to-terminal bindings");
        check(retained->snapshot()->resolve("group/menu")->implementation_sha256() ==
                  definition->implementation_sha256(),
              "descriptor identity must bind the loaded implementation contract");
    }
    const auto text = definition
        ? std::string(reinterpret_cast<const char*>(definition->bytes().data()),
                      definition->bytes().size())
        : std::string{};
    check(retained && retained->generation() == generation && text.find("menu-v1") != text.npos,
          "activation and exact definition bytes must outlive repository views");
}

void test_co_detect_engine_is_an_activated_production_engine() {
    auto specs = valid_specs();
    for (auto& spec : specs) {
        const auto marker = spec.definition.find("legacy.appear_then_click/v1");
        spec.definition.replace(
            marker, std::string_view{"legacy.appear_then_click/v1"}.size(),
            std::string{procedure::co_detect_python_compat_engine});
    }
    RepositoryFixture fixture{procedure_files(specs)};
    const auto result = load(fixture);
    check(result && result.activation->resolve_definition("group/menu")
              && result.activation->resolve_definition("group/menu")->engine()
                  == procedure::co_detect_python_compat_engine,
          "implemented co-detect definitions must cross the activation boundary");
}

void test_identity_binds_definition_and_terminal_mapping() {
    RepositoryFixture first{procedure_files(valid_specs("menu-v1"))};
    RepositoryFixture changed_bytes{procedure_files(valid_specs("menu-v2"))};
    const auto one = load(first);
    const auto two = load(changed_bytes);
    check(one && two && one.activation->snapshot()->snapshot_id() !=
                            two.activation->snapshot()->snapshot_id() &&
              one.activation->activation_id() != two.activation->activation_id(),
          "definition-only byte changes must change snapshot and activation identities");

    const auto swapped =
        R"([{"source":"exists","id":"already_joined"},{"source":"joined","id":"joined"}])";
    RepositoryFixture changed_mapping{procedure_files(valid_specs("menu-v1", swapped))};
    const auto three = load(changed_mapping);
    check(one && three && one.activation->snapshot()->snapshot_id() !=
                              three.activation->snapshot()->snapshot_id(),
          "ordered source-to-terminal changes must alter ProcedureSnapshot identity");
}

void test_ordered_result_schema_activation_and_identity() {
    constexpr std::string_view shop_schema =
        R"([{"name":"plan","required":true,"type":"array","items":{"type":"object","fields":[{"name":"item_id","required":true,"type":"string"},{"name":"quantity","required":true,"type":"integer"}]}},{"name":"balance","required":true,"type":"object","fields":[{"name":"credits","required":true,"type":"integer"},{"name":"gems","required":false,"type":"integer"}]},{"name":"formatted_text","required":false,"type":"string"}])";
    auto specs = valid_specs();
    specs[0].result_schema = shop_schema;
    RepositoryFixture fixture{procedure_files(specs)};
    const auto loaded = load(fixture);
    const auto descriptor = loaded
        ? loaded.activation->snapshot()->resolve("group/menu") : nullptr;
    const auto definition = loaded
        ? loaded.activation->resolve_definition("group/menu") : nullptr;
    check(loaded && descriptor && definition &&
              descriptor->result_schema().size() == 3 &&
              descriptor->result_schema()[0].name == "plan" &&
              descriptor->result_schema()[0].type ==
                  baas::script::host::ProcedureResultJsonType::Array &&
              descriptor->result_schema()[0].children.size() == 1 &&
              descriptor->result_schema()[0].children[0].children.size() == 2 &&
              descriptor->result_schema()[1].children[1].name == "gems" &&
              !descriptor->result_schema()[1].children[1].required &&
              std::ranges::equal(
                  definition->result_schema(), descriptor->result_schema()),
          "production activation and definition must retain the ordered nested result schema");

    auto changed = specs;
    auto& changed_text = changed[0].result_schema;
    const auto optional = changed_text.find(
        R"("name":"gems","required":false)");
    changed_text.replace(optional, std::string_view{
        R"("name":"gems","required":false)"}.size(),
        R"("name":"gems","required":true)");
    RepositoryFixture drift_fixture{procedure_files(changed)};
    const auto drift = load(drift_fixture);
    check(loaded && drift &&
              loaded.activation->snapshot()->snapshot_id() !=
                  drift.activation->snapshot()->snapshot_id() &&
              loaded.activation->activation_id() != drift.activation->activation_id() &&
              definition->implementation_sha256() !=
                  drift.activation->resolve_definition("group/menu")
                      ->implementation_sha256(),
          "result schema drift must alter implementation, descriptor snapshot, and activation identity");

    const std::vector<std::string> invalid_schemas{
        R"([{"name":"end","required":true,"type":"string"}])",
        R"([{"name":"value","required":true,"type":"string"},{"name":"value","required":false,"type":"string"}])",
        R"([{"name":"value","type":"string"}])",
        R"([{"name":"value","required":true,"type":"number"}])",
        R"([{"name":"value","required":true,"type":"string","extra":true}])",
        R"([{"name":"values","required":true,"type":"array"}])",
    };
    for (const auto& invalid_schema : invalid_schemas) {
        auto invalid = valid_specs();
        invalid[0].result_schema = invalid_schema;
        RepositoryFixture invalid_fixture{procedure_files(invalid)};
        expect_error(load(invalid_fixture),
                     procedure::RuntimeProcedureActivationError::invalid_manifest,
                     "unknown, missing, duplicate, reserved, and malformed result schemas must fail closed");
    }

    auto limits = procedure::RuntimeProcedureActivationLimits{};
    limits.max_result_schema_nodes_per_procedure = 2;
    expect_error(load(fixture, limits),
                 procedure::RuntimeProcedureActivationError::result_schema_limit_exceeded,
                 "production result schema node count must be independently bounded");
    limits = {};
    limits.max_result_schema_depth = 2;
    expect_error(load(fixture, limits),
                 procedure::RuntimeProcedureActivationError::result_schema_limit_exceeded,
                 "production result schema nesting must be independently bounded");
}

void test_provenance_and_closure_fail_closed() {
    RepositoryFixture first;
    RepositoryFixture second{procedure_files(valid_specs("other"))};
    const auto first_plan = first.plan();
    expect_error(procedure::load_runtime_procedure_activation(
                     second.scripts(), first_plan, second.resource_activation()),
                 procedure::RuntimeProcedureActivationError::plan_mismatch,
                 "plans cannot be mixed across script generations");
    expect_error(procedure::load_runtime_procedure_activation(
                     first.scripts(), first_plan, second.resource_activation()),
                 procedure::RuntimeProcedureActivationError::generation_mismatch,
                 "resource and script generations cannot be mixed");
    expect_error(procedure::load_runtime_procedure_activation(
                     first.resources(), first_plan, first.resource_activation()),
                 procedure::RuntimeProcedureActivationError::wrong_repository,
                 "only the scripts read capability is accepted");
    expect_error(procedure::load_runtime_procedure_activation(
                     first.scripts(), first_plan, {}),
                 procedure::RuntimeProcedureActivationError::resource_activation_required,
                 "a bare or absent ResourceSnapshot cannot activate procedures");

    RepositoryFixture missing{procedure_files(valid_specs()), {"group/menu", "missing/item"}};
    expect_error(load(missing),
                 procedure::RuntimeProcedureActivationError::required_procedure_missing,
                 "every trusted closure entry must exist");
}

void test_strict_manifests_definitions_and_paths() {
    const auto specs = valid_specs();
    const std::vector<std::pair<std::string, procedure::RuntimeProcedureActivationError>> bad{
        {R"({"schema":"baas.procedures/v1","schema":"baas.procedures/v1","entries":[]})",
         procedure::RuntimeProcedureActivationError::invalid_json},
        {R"({"schema":"baas.procedures/v1",/*comment*/"entries":[]})",
         procedure::RuntimeProcedureActivationError::invalid_json},
        {R"({"schema":"baas.procedures/v2","entries":[]})",
         procedure::RuntimeProcedureActivationError::invalid_manifest},
        {R"({"schema":"baas.procedures/v1","entries":[],"extra":true})",
         procedure::RuntimeProcedureActivationError::invalid_manifest},
    };
    for (const auto& [manifest, error] : bad) {
        RepositoryFixture fixture{procedure_files(specs, manifest)};
        expect_error(load(fixture), error,
                     "comments, duplicate keys, unknown fields, and schemas must fail");
    }

    auto invalid_utf8 = procedures_manifest(specs);
    invalid_utf8.insert(invalid_utf8.find("group/menu"), 1, static_cast<char>(0xff));
    RepositoryFixture utf8{procedure_files(specs, invalid_utf8)};
    expect_error(load(utf8), procedure::RuntimeProcedureActivationError::invalid_utf8,
                 "invalid UTF-8 must have a stable error");

    auto metadata_mismatch = procedures_manifest(specs);
    const auto size_field = R"("size":)" + std::to_string(specs.front().definition.size());
    const auto size_offset = metadata_mismatch.find(size_field);
    metadata_mismatch.replace(
        size_offset, size_field.size(),
        R"("size":)" + std::to_string(specs.front().definition.size() + 1));
    RepositoryFixture metadata_fixture{procedure_files(specs, metadata_mismatch)};
    expect_error(load(metadata_fixture),
                 procedure::RuntimeProcedureActivationError::manifest_entry_mismatch,
                 "definition size and digest pins must match the repository tree entry");

    auto bad_path = specs;
    bad_path[0].path = "procedures/../escape.json";
    auto bad_path_files = procedure_files(bad_path);
    bad_path_files.erase(bad_path_files.begin() + 1);
    RepositoryFixture path_fixture{std::move(bad_path_files)};
    expect_error(load(path_fixture),
                 procedure::RuntimeProcedureActivationError::invalid_definition_path,
                 "native or traversal paths must fail before repository reads");

    auto unsupported = specs;
    const auto marker = unsupported[0].definition.find("legacy.appear_then_click/v1");
    unsupported[0].definition.replace(marker, std::string_view{"legacy.appear_then_click/v1"}.size(),
                                      "future.engine/v2");
    RepositoryFixture unsupported_fixture{procedure_files(unsupported)};
    expect_error(load(unsupported_fixture),
                 procedure::RuntimeProcedureActivationError::unsupported_engine,
                 "v1 must reject unsupported engines");

    auto extra_field = specs;
    extra_field[0].definition.insert(extra_field[0].definition.rfind('}'), R"(,"extra":true)");
    RepositoryFixture definition_fixture{procedure_files(extra_field)};
    expect_error(load(definition_fixture),
                 procedure::RuntimeProcedureActivationError::invalid_definition,
                 "definition wrappers have an exact field set");

    auto duplicate_definition_field = specs;
    duplicate_definition_field[0].definition.replace(
        duplicate_definition_field[0].definition.find(R"("engine":)"),
        std::string_view{R"("engine":)"}.size(), R"("engine":"ignored","engine":)");
    RepositoryFixture duplicate_definition_fixture{
        procedure_files(duplicate_definition_field)};
    expect_error(load(duplicate_definition_fixture),
                 procedure::RuntimeProcedureActivationError::invalid_json,
                 "definition wrappers reject duplicate fields before engine dispatch");

    auto duplicate_id = specs;
    duplicate_id.push_back(specs.front());
    duplicate_id.back().path = "procedures/group/menu-copy.json";
    RepositoryFixture duplicate_fixture{procedure_files(duplicate_id)};
    expect_error(load(duplicate_fixture),
                 procedure::RuntimeProcedureActivationError::duplicate_entry,
                 "duplicate procedure ids fail closed");

    auto case_id = specs;
    case_id.push_back(specs.front());
    case_id.back().id = "Group/Menu";
    case_id.back().path = "procedures/group/menu-copy.json";
    RepositoryFixture case_fixture{procedure_files(case_id)};
    expect_error(load(case_fixture),
                 procedure::RuntimeProcedureActivationError::procedure_id_case_collision,
                 "ASCII case-colliding ids fail before canonical-id validation ambiguity");
}

void test_resource_digest_limits_cancellation_and_oom() {
    RepositoryFixture no_resource{procedure_files(valid_specs()),
                                  {"group/menu", "group/reward"}, false};
    expect_error(load(no_resource), procedure::RuntimeProcedureActivationError::resource_not_found,
                 "declared resources must resolve in the same-generation activation");

    RepositoryFixture fixture;
    auto limits = procedure::RuntimeProcedureActivationLimits{};
    limits.max_manifest_bytes = 1;
    expect_error(load(fixture, limits),
                 procedure::RuntimeProcedureActivationError::manifest_too_large,
                 "manifest bytes are bounded before reading");
    limits = {};
    limits.max_definition_bytes = 1;
    limits.max_total_definition_bytes = 1;
    expect_error(load(fixture, limits),
                 procedure::RuntimeProcedureActivationError::definition_too_large,
                 "individual definitions are bounded before reading");
    limits = {};
    limits.max_work = 1;
    expect_error(load(fixture, limits),
                 procedure::RuntimeProcedureActivationError::work_limit_exceeded,
                 "all parser and byte passes share a work budget");
    limits = {};
    limits.max_entries = 1;
    expect_error(load(fixture, limits),
                 procedure::RuntimeProcedureActivationError::entry_limit_exceeded,
                 "closure and manifest cardinality are bounded");
    limits = {};
    limits.max_json_depth = 0;
    expect_error(load(fixture, limits),
                 procedure::RuntimeProcedureActivationError::invalid_limits,
                 "zero limits fail closed");

    std::stop_source source_token;
    cancellation_source = &source_token;
    definition_reads.store(0);
    procedure::set_runtime_procedure_activation_hook([](const auto point) {
        if (point == procedure::RuntimeProcedureActivationHookPoint::before_definition_read &&
            definition_reads.fetch_add(1) == 1 && cancellation_source)
            cancellation_source->request_stop();
    });
    const auto cancelled = load(fixture, {}, source_token.get_token());
    procedure::set_runtime_procedure_activation_hook(nullptr);
    cancellation_source = nullptr;
    expect_error(cancelled, procedure::RuntimeProcedureActivationError::cancelled,
                 "cancellation after partial definition work publishes no activation");

    procedure::set_runtime_procedure_activation_hook([](const auto point) {
        if (point == procedure::RuntimeProcedureActivationHookPoint::before_publication)
            throw std::bad_alloc{};
    });
    const auto exhausted = load(fixture);
    procedure::set_runtime_procedure_activation_hook(nullptr);
    expect_error(exhausted, procedure::RuntimeProcedureActivationError::resource_exhausted,
                 "allocation failure before publication is typed and fail-closed");
}

void test_concurrent_immutable_activation() {
    RepositoryFixture fixture;
    const auto plan = fixture.plan();
    const auto resource_activation = fixture.resource_activation();
    std::array<std::thread, 8> workers;
    std::array<std::string, 8> identities;
    std::atomic<int> successes{};
    for (std::size_t index{}; index < workers.size(); ++index) {
        workers[index] = std::thread([&, index] {
            const auto result = procedure::load_runtime_procedure_activation(
                fixture.scripts(), plan, resource_activation);
            if (result) {
                identities[index] = result.activation->activation_id();
                ++successes;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    check(successes == static_cast<int>(workers.size()) &&
              std::ranges::all_of(identities, [&](const auto& value) {
                  return !value.empty() && value == identities.front();
              }),
          "concurrent loads from immutable views must publish identical activations");
}

static_assert(!std::is_default_constructible_v<procedure::RuntimeProcedureActivation>);
static_assert(!std::is_copy_constructible_v<procedure::RuntimeProcedureActivation>);
static_assert(!std::is_move_constructible_v<procedure::RuntimeProcedureActivation>);
static_assert(!std::is_default_constructible_v<procedure::RuntimeProcedureDefinition>);
static_assert(!std::is_copy_constructible_v<procedure::RuntimeProcedureDefinition>);

}  // namespace

int main() {
  try {
    test_success_closure_and_owned_lifetime();
    test_co_detect_engine_is_an_activated_production_engine();
    test_identity_binds_definition_and_terminal_mapping();
    test_ordered_result_schema_activation_and_identity();
    test_provenance_and_closure_fail_closed();
    test_strict_manifests_definitions_and_paths();
    test_resource_digest_limits_cancellation_and_oom();
    test_concurrent_immutable_activation();
    if (failures.load() != 0) {
        std::cerr << failures.load() << " runtime procedure activation test(s) failed\n";
        return 1;
    }
    std::cout << "Runtime procedure activation tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "UNHANDLED: " << error.what() << '\n';
    return 2;
  }
}
