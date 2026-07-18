#include "resources/ResourceSnapshot.h"
#include "CoDetectProductionAdapterFixture.h"
#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "service/runtime/ProductionRuntimeScriptTaskFactory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace service_runtime = ::baas::service::runtime;
namespace repository = ::baas::runtime::repository;
namespace runtime_procedure = ::baas::runtime::procedure;
namespace runtime_script = ::baas::runtime::script;
namespace script_host = ::baas::script::host;
namespace script_runtime = ::baas::script::runtime;

namespace {

int failures{};

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
                               .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("baas-production-runtime-factory-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }
private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error{"fixture create failed"};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error{"fixture write failed"};
}

std::string sha256(const std::string_view value)
{
    return ::baas::resources::sha256_hex(
        std::as_bytes(std::span{value.data(), value.size()}));
}

struct File final {
    std::string path;
    std::string bytes;
};

std::string tree_manifest(std::vector<File> files)
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

std::string snapshot_json(
    const std::array<repository::RuntimeRepository, 2>& repositories,
    const std::string_view generation)
{
    std::string result =
        R"({"schema":"baas.runtime-repositories.snapshot/v1","generation":")"
        + std::string{generation} + R"(","repositories":[)";
    for (std::size_t index{}; index < repositories.size(); ++index) {
        if (index) result.push_back(',');
        const auto& item = repositories[index];
        result += R"({"id":")" + item.id + R"(","commit":")" + item.commit
            + R"(","root":")" + item.root + R"(","manifest":")"
            + item.manifest + R"(","manifest_sha256":")"
            + item.manifest_sha256 + R"("})";
    }
    return result + "]}";
}

std::string package_manifest(
    const std::string_view package_id, const std::string_view source,
    const bool procedure = false)
{
    const auto host = procedure ? "baas/procedure" : "baas/log";
    const auto capability = procedure ? "procedure.execute" : "log.emit";
    return std::string{R"({"manifest_schema":)"} + (procedure ? "2" : "1")
        + R"(,"package":{"id":")"
        + std::string{package_id}
        + R"(","version":"1.0.0"},"language":{"major":1,"min_minor":0},)"
          R"("entrypoint":"main.baas","host_modules":{")" + host
        + R"(":{"major":1,"min_minor":0}},"capabilities":[")" + capability
        + R"("],)" + (procedure
            ? R"("procedures":["navigation.to-main-page"],)" : "")
        + R"("profiles":[],"modules":[{"path":"main.baas","size":)"
        + std::to_string(source.size()) + R"(,"sha256":")" + sha256(source)
        + R"("}],"resources":[],"limits":{"source_bytes":4096,"resource_bytes":0,)"
          R"("module_count":1,"resource_count":0}})";
}

std::string catalog_json(const bool procedure = false)
{
    const auto task = [](const std::string_view root,
                         const std::string_view task_name,
                         const std::string_view alias,
                         const bool procedure_task) {
        const auto host = procedure_task ? "baas/procedure" : "baas/log";
        const auto capability = procedure_task ? "procedure.execute" : "log.emit";
        return R"({"run_mode":"solve","task":")" + std::string{task_name}
            + R"(","package_root":")" + std::string{root}
            + R"(","package_manifest":")" + std::string{root}
            + R"(/baas.package.json","entry_module":"main","entry_export":"run",)"
              R"("language_version":{"major":1,"minor":0},"host_modules":[)"
              R"({"module":")" + host
            + R"(","major":1,"min_minor":0,"capabilities":[")" + capability
            + R"("]}],)"
              R"("legacy_aliases":[")" + std::string{alias} + R"("]})";
    };
    return R"({"schema":"baas.runtime-script.catalog/v2","tasks":[)"
        + task("packages/one", "one", "start_one", procedure) + ","
        + task("packages/two", "two", "start_two", false) + "]}";
}

std::string procedure_definition(
    const std::string_view engine =
        runtime_procedure::co_detect_python_compat_engine)
{
    return R"({"schema":"baas.procedure-definition/v1","engine":")"
        + std::string{engine}
        + R"(","payload":{"profile_source":"device.server-and-locale/v1","ends":{"rgb":["rgb-hit"],"image":[]},"reactions":{"rgb":[],"rgb_profiled":[],"image":[],"image_profiled":[]},"popups":{"rgb":[],"profiled_image":[]},"loading":{"all_rgb":["rgb-miss"]},"foreground_check":{"android_only":true,"interval_ms":100,"idle_feature_ms":100},"loop":{"skip_first_screenshot":false,"timeout_ms":1000,"duplicate_click_window_ms":2000,"tentative":{"enabled":false}}}})";
}

std::string procedure_entry(
    const std::string_view id,
    const std::string_view path,
    const std::string_view resource_id,
    const std::string_view definition)
{
    return R"({"id":")" + std::string{id}
        + R"(","definition":{"path":")" + std::string{path}
        + R"(","size":)"
        + std::to_string(definition.size()) + R"(,"sha256":")"
        + sha256(definition)
        + R"("},"terminals":[{"source":"rgb-hit","id":"done"}],"effects":["capture","vision","input","wait","foreground_check"],"resources":[")"
        + std::string{resource_id} + R"("]})";
}

std::string procedure_manifest(
    const std::string_view main_definition,
    const std::string_view other_definition)
{
    return R"({"schema":"baas.procedures/v1","entries":[)"
        + procedure_entry(
            "navigation.other", "procedures/navigation.other.json",
            "procedure-support/navigation.other/v1", other_definition)
        + ","
        + procedure_entry(
            "navigation.to-main-page",
            "procedures/navigation.to-main-page.json",
            "procedure-support/navigation.to-main-page/v1", main_definition)
        + "]}";
}

class RepositoryFixture final {
public:
    explicit RepositoryFixture(
        const std::string_view first_result = "true",
        const std::string_view second_result = "true",
        const bool procedure = false,
        const bool corrupt_second_package = false,
        const bool legacy_procedure = false)
    {
        const std::string first = procedure
            ? std::string{
                "import \"baas/procedure\" as procedure;\n"
                "fn run() { return procedure.run(\"navigation.to-main-page\").end == \"done\"; }\n"}
            : std::string{
                "import \"baas/log\" as log;\n"
                "fn run() { log.emit(\"info\", \"one\"); return "}
                + std::string{first_result} + "; }\n";
        const std::string second = std::string{
            "import \"baas/log\" as log;\n"
            "fn run() { log.emit(\"info\", \"two\"); return "}
            + std::string{second_result} + "; }\n";
        std::vector<File> resources;
        if (procedure) {
            const auto archive = make_co_detect_production_test_archive(
                "procedure-support/navigation.to-main-page/v1");
            const auto other_archive = make_co_detect_production_test_archive(
                "procedure-support/navigation.other/v1");
            const std::string archive_bytes{
                reinterpret_cast<const char*>(archive.data()), archive.size()};
            const std::string other_archive_bytes{
                reinterpret_cast<const char*>(other_archive.data()),
                other_archive.size()};
            resources = {
                {"baas.resources.json",
                 R"({"schema":"baas.resources/v1","entries":[{"id":"procedure-support/navigation.other/v1","path":"payload/other-support.bundle","media_type":")"
                    + std::string{runtime_procedure::co_detect_support_bundle_media_type}
                    + R"(","size":)" + std::to_string(other_archive.size())
                    + R"(,"sha256":")" + sha256(other_archive_bytes)
                    + R"(","locale":"JP"},{"id":"procedure-support/navigation.to-main-page/v1","path":"payload/support.bundle","media_type":")"
                    + std::string{runtime_procedure::co_detect_support_bundle_media_type}
                    + R"(","size":)" + std::to_string(archive.size())
                    + R"(,"sha256":")" + sha256(archive_bytes)
                    + R"(","locale":"JP"}]})"},
                {"payload/other-support.bundle", other_archive_bytes},
                {"payload/support.bundle", archive_bytes},
            };
        }
        else {
            resources = {{"baas.resources.json",
                          R"({"schema":"baas.resources/v1","entries":[]})"}};
        }
        std::vector<File> scripts{
            {std::string{runtime_script::runtime_script_catalog_manifest},
             catalog_json(procedure)},
            {"packages/one/baas.package.json",
             package_manifest("bluearchive.one", first, procedure)},
            {"packages/one/main.baas", first},
            {"packages/two/baas.package.json",
             package_manifest(
                 "bluearchive.two",
                 corrupt_second_package ? second + "x" : second)},
            {"packages/two/main.baas", second},
        };
        if (procedure) {
            const auto definition = procedure_definition(
                legacy_procedure
                    ? runtime_procedure::runtime_procedure_legacy_engine
                    : runtime_procedure::co_detect_python_compat_engine);
            const auto other_definition = procedure_definition();
            scripts.push_back({"baas.procedures.json",
                               procedure_manifest(
                                   definition, other_definition)});
            scripts.push_back({"procedures/navigation.other.json",
                               other_definition});
            scripts.push_back({"procedures/navigation.to-main-page.json",
                               definition});
        }
        const auto resource_manifest = tree_manifest(resources);
        const auto script_manifest = tree_manifest(scripts);
        const std::string resource_commit(40, '1');
        const std::string script_commit(40, '2');
        repositories_ = {{
            {"resources", resource_commit,
             "objects/resources/" + resource_commit, "manifest.json",
             sha256(resource_manifest)},
            {"scripts", script_commit,
             "objects/scripts/" + script_commit, "scripts.json",
             sha256(script_manifest)},
        }};
        for (const auto& file : resources)
            write_file(temporary_.path() / repositories_[0].root / file.path,
                       file.bytes);
        for (const auto& file : scripts)
            write_file(temporary_.path() / repositories_[1].root / file.path,
                       file.bytes);
        write_file(
            temporary_.path() / repositories_[0].root
                / repositories_[0].manifest,
            resource_manifest);
        write_file(
            temporary_.path() / repositories_[1].root
                / repositories_[1].manifest,
            script_manifest);
        generation_ = repository::runtime_repository_generation(repositories_);
        write_file(
            temporary_.path() / "snapshots" / (generation_ + ".json"),
            snapshot_json(repositories_, generation_));
        write_file(
            temporary_.path() / "current.json",
            R"({"schema":"baas.runtime-repositories.current/v1","generation":")"
                + generation_ + R"(","snapshot":"snapshots/)"
                + generation_ + ".json\"}");
        snapshot_ = repository::RuntimeRepositorySnapshot::activate(
            temporary_.path());
        bundle_ = snapshot_->open_read_bundle();
    }

    [[nodiscard]] const std::shared_ptr<const repository::RuntimeRepositoryReadBundle>&
    bundle() const noexcept
    {
        return bundle_;
    }

private:
    TemporaryDirectory temporary_;
    std::array<repository::RuntimeRepository, 2> repositories_;
    std::string generation_;
    std::shared_ptr<const repository::RuntimeRepositorySnapshot> snapshot_;
    std::shared_ptr<const repository::RuntimeRepositoryReadBundle> bundle_;
};

class Trust final : public runtime_script::RuntimeScriptRepositoryTrustEvidence {
public:
    Trust(std::string generation, std::string commit)
        : generation_(std::move(generation)), commit_(std::move(commit)) {}
    [[nodiscard]] bool covers(
        const std::string_view generation,
        const std::string_view scripts_commit) const noexcept override
    {
        return allowed_.load() && generation == generation_
            && scripts_commit == commit_;
    }
    void deny() noexcept { allowed_ = false; }
private:
    std::string generation_;
    std::string commit_;
    std::atomic<bool> allowed_{true};
};

class RecordingSink final : public script_runtime::StructuredLogSink {
public:
    void write(const script_runtime::StructuredLogEvent& event) override
    {
        std::lock_guard lock{mutex_};
        events_.push_back(event);
    }
    [[nodiscard]] std::vector<script_runtime::StructuredLogEvent> events() const
    {
        std::lock_guard lock{mutex_};
        return events_;
    }
private:
    mutable std::mutex mutex_;
    std::vector<script_runtime::StructuredLogEvent> events_;
};

class Device final : public runtime_procedure::CoDetectProductionDevicePort {
public:
    explicit Device(
        std::shared_ptr<const runtime_procedure::CoDetectProductionDeviceIdentity>
            identity)
        : identity_(std::move(identity)),
          pixels_(std::make_shared<std::vector<std::byte>>(
              1'280U * 720U * 3U))
    {
        (*pixels_)[0] = std::byte{10};
        (*pixels_)[1] = std::byte{20};
        (*pixels_)[2] = std::byte{30};
    }

    [[nodiscard]] std::shared_ptr<const runtime_procedure::CoDetectProductionDeviceIdentity>
    current_identity() const noexcept override
    {
        std::lock_guard lock{mutex_};
        return identity_;
    }
    void publish_identity(
        std::shared_ptr<const runtime_procedure::CoDetectProductionDeviceIdentity>
            identity)
    {
        std::lock_guard lock{mutex_};
        identity_ = std::move(identity);
    }
    [[nodiscard]] std::uint64_t monotonic_ms() const noexcept override { return 1; }
    [[nodiscard]] std::uint64_t screenshot_interval_ms() const noexcept override
    { return 1; }
    [[nodiscard]] std::shared_ptr<const runtime_procedure::CoDetectProductionBgrFrame>
    latest_frame() const noexcept override
    {
        std::lock_guard lock{mutex_};
        return latest_;
    }
    [[nodiscard]] bool publish_latest_frame(
        std::shared_ptr<const runtime_procedure::CoDetectProductionBgrFrame> frame)
        noexcept override
    {
        std::lock_guard lock{mutex_};
        if (!frame || frame->identity != identity_) return false;
        latest_ = std::move(frame);
        return true;
    }
    [[nodiscard]] runtime_procedure::CoDetectResult<
        runtime_procedure::CoDetectProductionBgrFrame>
    capture(const runtime_procedure::CoDetectControl&) override
    {
        std::lock_guard lock{mutex_};
        return runtime_procedure::CoDetectProductionBgrFrame{
            identity_, 1'280, 720, 1'280U * 3U, pixels_};
    }
    [[nodiscard]] runtime_procedure::CoDetectResult<std::monostate> click(
        runtime_procedure::CoDetectClick,
        const runtime_procedure::CoDetectControl&) override
    { return std::monostate{}; }
    [[nodiscard]] runtime_procedure::CoDetectResult<std::monostate> wait(
        std::uint64_t, const runtime_procedure::CoDetectControl&) override
    { return std::monostate{}; }
    [[nodiscard]] runtime_procedure::CoDetectResult<bool> foreground_matches(
        const runtime_procedure::CoDetectControl&) override
    { return true; }
private:
    mutable std::mutex mutex_;
    std::shared_ptr<const runtime_procedure::CoDetectProductionDeviceIdentity>
        identity_;
    std::shared_ptr<std::vector<std::byte>> pixels_;
    std::shared_ptr<const runtime_procedure::CoDetectProductionBgrFrame> latest_;
};

class Provider final : public service_runtime::ProductionRuntimeScriptTaskProvider {
public:
    Provider(
        std::shared_ptr<const repository::RuntimeRepositoryReadBundle> repositories,
        std::shared_ptr<Trust> trust,
        std::shared_ptr<Device> device,
        std::shared_ptr<const runtime_procedure::CoDetectProductionDeviceIdentity>
            identity,
        std::shared_ptr<RecordingSink> sink,
        std::string locale = "CN",
        std::vector<service_runtime::ProductionRuntimeProcedureSupport> support = {})
        : repositories_(std::move(repositories)), trust_(std::move(trust)),
          device_(std::move(device)), identity_(std::move(identity)),
          sink_(std::move(sink)), locale_(std::move(locale)),
          support_(std::move(support)) {}

    [[nodiscard]] std::optional<service_runtime::ProductionRuntimeScriptTaskInputs>
    pin(const service_runtime::RuntimeTaskRequest& request,
        std::span<const std::string>,
        const service_runtime::RuntimeScriptTaskExecutionControl&) const override
    {
        ++pins_;
        if (throw_on_pin_.load()) throw std::bad_alloc{};
        if (disabled_.load()) return std::nullopt;
        auto config = std::make_shared<
            const service_runtime::ProductionRuntimeScriptConfigSnapshot>(
            service_runtime::ProductionRuntimeScriptConfigSnapshot{
                request.config_id,
                request.config_id + "@snapshot-7",
                identity_->device_id,
                locale_,
                identity_->profile,
                {locale_, std::nullopt},
                {"log.emit", "procedure.execute", "test.ping"},
                {"log.emit", "procedure.execute", "test.ping"},
                {},
                support_});
        std::shared_ptr<const service_runtime::ProductionRuntimeScriptExtensions>
            extensions;
        {
            std::lock_guard lock{extensions_mutex_};
            extensions = extensions_;
        }
        return service_runtime::ProductionRuntimeScriptTaskInputs{
            std::move(config), repositories_, trust_, device_, identity_, sink_,
            std::move(extensions)};
    }
    void set_extensions(
        std::shared_ptr<const service_runtime::ProductionRuntimeScriptExtensions>
            extensions)
    {
        std::lock_guard lock{extensions_mutex_};
        extensions_ = std::move(extensions);
    }
    void disable() noexcept { disabled_ = true; }
    void throw_on_pin() noexcept { throw_on_pin_ = true; }
    [[nodiscard]] int pins() const noexcept { return pins_.load(); }
private:
    std::shared_ptr<const repository::RuntimeRepositoryReadBundle> repositories_;
    std::shared_ptr<Trust> trust_;
    std::shared_ptr<Device> device_;
    std::shared_ptr<const runtime_procedure::CoDetectProductionDeviceIdentity>
        identity_;
    std::shared_ptr<RecordingSink> sink_;
    std::string locale_;
    std::vector<service_runtime::ProductionRuntimeProcedureSupport> support_;
    mutable std::mutex extensions_mutex_;
    std::shared_ptr<const service_runtime::ProductionRuntimeScriptExtensions>
        extensions_;
    mutable std::atomic<int> pins_{};
    std::atomic<bool> disabled_{};
    std::atomic<bool> throw_on_pin_{};
};

struct Harness final {
    explicit Harness(
        const std::string_view first_result = "true",
        const std::string_view second_result = "true",
        const bool procedure = false,
        const bool corrupt_second_package = false,
        const bool wrong_procedure_support = false,
        const bool legacy_procedure = false)
        : repositories(
              first_result, second_result, procedure, corrupt_second_package,
              legacy_procedure),
          trust(std::make_shared<Trust>(
              repositories.bundle()->generation(),
              repositories.bundle()->scripts().commit())),
          identity(std::make_shared<const
              runtime_procedure::CoDetectProductionDeviceIdentity>(
                  runtime_procedure::CoDetectProductionDeviceIdentity{
                      "emulator-5556", procedure
                          ? runtime_procedure::CoDetectProfile::jp
                          : runtime_procedure::CoDetectProfile::cn,
                      7, true})),
          device(std::make_shared<Device>(identity)),
          sink(std::make_shared<RecordingSink>()),
          provider(std::make_shared<Provider>(
              repositories.bundle(), trust, device, identity, sink,
              procedure ? "JP" : "CN",
              procedure
                  ? std::vector<service_runtime::ProductionRuntimeProcedureSupport>{
                        {"navigation.to-main-page",
                         wrong_procedure_support
                             ? "procedure-support/navigation.other/v1"
                             : "procedure-support/navigation.to-main-page/v1"}}
                  : std::vector<service_runtime::ProductionRuntimeProcedureSupport>{})),
          factory(service_runtime::make_production_runtime_script_task_factory(
              provider))
    {
    }

    RepositoryFixture repositories;
    std::shared_ptr<Trust> trust;
    std::shared_ptr<const runtime_procedure::CoDetectProductionDeviceIdentity>
        identity;
    std::shared_ptr<Device> device;
    std::shared_ptr<RecordingSink> sink;
    std::shared_ptr<Provider> provider;
    std::shared_ptr<const service_runtime::RuntimeScriptTaskRuntimeFactory>
        factory;
};

enum class ExtensionMode {
    Valid,
    DuplicateHost,
    ThrowOnSecond,
    CancelOnFirst,
    DriftOnSecond,
};

struct ExtensionCounters final {
    std::atomic<int> calls{};
    std::atomic<int> legacy_executor_calls{};
    std::atomic<int> legacy_executions{};
    std::atomic<int> created{};
    std::atomic<int> destroyed{};
    std::atomic<int> live{};
};

class LegacyTestExecutor final : public script_host::ProcedureExecutor {
public:
    explicit LegacyTestExecutor(std::shared_ptr<ExtensionCounters> counters)
        : counters_(std::move(counters))
    {
    }

    [[nodiscard]] script_host::ProcedureExecutorOutcome execute(
        const script_host::ProcedureExecutionRequest&) override
    {
        ++counters_->legacy_executions;
        return script_host::ProcedureExecutorOutcome::success("done");
    }

private:
    std::shared_ptr<ExtensionCounters> counters_;
};

class ExtensionOwner final {
public:
    explicit ExtensionOwner(std::shared_ptr<ExtensionCounters> counters)
        : counters_(std::move(counters))
    {
        ++counters_->created;
        ++counters_->live;
    }
    ~ExtensionOwner()
    {
        ++counters_->destroyed;
        --counters_->live;
    }
private:
    std::shared_ptr<ExtensionCounters> counters_;
};

class TestExtensions final
    : public service_runtime::ProductionRuntimeScriptExtensions {
public:
    TestExtensions(
        service_runtime::ProductionRuntimeScriptExtensionIdentity identity,
        ExtensionMode mode,
        std::shared_ptr<ExtensionCounters> counters,
        std::stop_source* stop = nullptr,
        std::shared_ptr<Device> device = {},
        std::shared_ptr<const
            runtime_procedure::CoDetectProductionDeviceIdentity> identity_token = {})
        : identity_(std::move(identity)), mode_(mode),
          counters_(std::move(counters)), stop_(stop),
          device_(std::move(device)), identity_token_(std::move(identity_token))
    {
    }

    [[nodiscard]] const service_runtime::
        ProductionRuntimeScriptExtensionIdentity&
    identity() const noexcept override
    {
        return identity_;
    }

    [[nodiscard]] std::vector<script_host::HostRuntimeContribution>
    make_host_contributions(
        const runtime_script::RuntimeScriptExecutionPlan&,
        std::shared_ptr<const script_runtime::HostCancellationProbe>)
        const override
    {
        const auto call = ++counters_->calls;
        if (mode_ == ExtensionMode::ThrowOnSecond && call == 2)
            throw std::bad_alloc{};
        if (mode_ == ExtensionMode::CancelOnFirst && call == 1 && stop_)
            stop_->request_stop();
        if (mode_ == ExtensionMode::DriftOnSecond && call == 2 && device_
            && identity_token_) {
            device_->publish_identity(std::make_shared<const
                runtime_procedure::CoDetectProductionDeviceIdentity>(
                runtime_procedure::CoDetectProductionDeviceIdentity{
                    identity_token_->device_id, identity_token_->profile,
                    identity_token_->session_epoch + 1,
                    identity_token_->android}));
        }

        auto owner = std::make_shared<ExtensionOwner>(counters_);
        const bool duplicate = mode_ == ExtensionMode::DuplicateHost;
        script_runtime::HostModuleDescriptor module{
            duplicate ? "baas/log" : "baas/test", {1, 0},
            {{duplicate ? "emit" : "ping",
              duplicate ? "host.log.emit.v1"
                        : "host.test.ping.v1",
              duplicate ? "log.emit" : "test.ping"}}};
        script_runtime::SynchronousNativeBinding binding{
            duplicate ? "host.log.emit.v1"
                      : "host.test.ping.v1",
            {{}, script_runtime::HostValueType::Null, "test_extension",
             script_runtime::HostExecutionMode::ThreadSafe,
             script_runtime::HostCancellationMode::Preflight},
            [](const script_runtime::HostCallContext&,
               const script_runtime::HostArguments&) {
                return script_runtime::HostResult::success();
            }};
        return {{
            {std::move(module)}, {std::move(binding)}, {std::move(owner)}, {}}};
    }

    [[nodiscard]] std::shared_ptr<script_host::ProcedureExecutor>
    make_activated_legacy_procedure_executor(
        std::shared_ptr<const runtime_procedure::RuntimeProcedureActivation>
            activation,
        const std::string_view procedure_id,
        const service_runtime::RuntimeScriptTaskExecutionControl&) const override
    {
        const auto definition = activation
            ? activation->resolve_definition(procedure_id) : nullptr;
        if (!definition || definition->engine()
                != runtime_procedure::runtime_procedure_legacy_engine)
            return {};
        ++counters_->legacy_executor_calls;
        return std::make_shared<LegacyTestExecutor>(counters_);
    }

private:
    service_runtime::ProductionRuntimeScriptExtensionIdentity identity_;
    ExtensionMode mode_;
    std::shared_ptr<ExtensionCounters> counters_;
    std::stop_source* stop_{};
    std::shared_ptr<Device> device_;
    std::shared_ptr<const runtime_procedure::CoDetectProductionDeviceIdentity>
        identity_token_;
};

service_runtime::ProductionRuntimeScriptExtensionIdentity extension_identity(
    const Harness& harness)
{
    return {
        "primary", "primary@snapshot-7",
        harness.repositories.bundle()->generation(),
        harness.repositories.bundle()->scripts().commit(),
        harness.repositories.bundle()->resources().commit()};
}

service_runtime::RuntimeTaskRequest request(std::string config_id = "primary")
{
    service_runtime::RuntimeTaskRequest result;
    result.config_id = std::move(config_id);
    result.run_mode = "solve";
    result.current_task = "start_one";
    result.waiting_tasks = {"start_two"};
    return result;
}

void test_real_catalog_plan_hosts_and_ordered_evaluation()
{
    Harness harness;
    const auto backend = service_runtime::make_runtime_script_task_backend(
        harness.factory);
    std::vector<service_runtime::RuntimeTaskProgress> progress;
    const auto terminal = backend(request(), {}, [&](auto update) {
        progress.push_back(std::move(update));
        return true;
    });
    const auto events = harness.sink->events();
    check(!terminal.is_flag_run && terminal.exit_code == 0,
          "two real packages must evaluate to exact success terminal");
    check(progress.size() == 2
              && progress[0].current_task == "start_one"
              && progress[0].waiting_tasks
                  == std::vector<std::string>{"start_two"}
              && progress[1].current_task == "start_two"
              && progress[1].waiting_tasks.empty(),
          "the complete requested plan must publish ordered progress");
    check(events.size() == 2 && events[0].message == "one"
              && events[0].identity.task_id == "one"
              && events[1].message == "two"
              && events[1].identity.task_id == "two",
          "each exact entry export must execute once with its canonical log identity");
    check(harness.provider->pins() == 1,
          "one task-plan create must pin provider state exactly once");
}

void test_entry_export_requires_exact_boolean_true()
{
    for (const auto value : {std::string_view{"false"},
                             std::string_view{"null"},
                             std::string_view{"1"}}) {
        Harness harness{value};
        auto task = request();
        task.waiting_tasks.clear();
        const auto terminal =
            service_runtime::make_runtime_script_task_backend(harness.factory)(
                task, {}, [](auto) { return true; });
        check(terminal.exit_code
                  == service_runtime::runtime_script_task_failure_exit_code,
              "false/null/number task results must fail exact Boolean contract");
        check(harness.sink->events().size() == 1,
              "non-true task results must still close and drain their LogHost");
    }

    Harness accepted{"true"};
    auto task = request();
    task.waiting_tasks.clear();
    const auto terminal =
        service_runtime::make_runtime_script_task_backend(accepted.factory)(
            task, {}, [](auto) { return true; });
    check(terminal.exit_code == 0,
          "only exact Boolean true may produce production success");
}

void test_real_co_detect_activation_bundle_device_and_evaluator()
{
    Harness harness{"true", "true", true};
    auto task = request();
    task.waiting_tasks.clear();
    const std::vector<std::string> requested{"start_one"};
    const service_runtime::RuntimeScriptTaskExecutionControl control{
        {}, std::chrono::steady_clock::now() + std::chrono::minutes{1}};
    std::unique_ptr<service_runtime::RuntimeScriptTaskRuntime> runtime;
    try {
        runtime = harness.factory->create(task, requested, control);
    } catch (const std::exception& error) {
        std::cerr << "co-detect factory create failed: " << error.what() << '\n';
    }
    check(runtime != nullptr,
          "real co-detect fixture must construct a production runtime");
    if (!runtime) return;
    const auto terminal = runtime->execute(control, [](auto) { return true; });
    check(terminal.exit_code == 0,
          "scripts view through activation/support/device/evaluator must execute co-detect");
    check(harness.device->latest_frame() != nullptr,
          "production co-detect execution must capture and publish an owned frame");
}

void test_co_detect_support_must_belong_to_the_activated_procedure()
{
    Harness wrong_mapping{"true", "true", true, false, true};
    auto task = request();
    task.waiting_tasks.clear();
    const std::vector<std::string> requested{"start_one"};
    const service_runtime::RuntimeScriptTaskExecutionControl control{
        {}, std::chrono::steady_clock::now() + std::chrono::minutes{1}};
    check(!wrong_mapping.factory->create(task, requested, control)
              && wrong_mapping.device->latest_frame() == nullptr,
          "a valid bundle declared by another procedure must not be substituted"
          " through config support mapping");
}

void test_activation_supported_legacy_executor_extension_is_reachable()
{
    Harness harness{"true", "true", true, false, false, true};
    auto counters = std::make_shared<ExtensionCounters>();
    harness.provider->set_extensions(std::make_shared<TestExtensions>(
        extension_identity(harness), ExtensionMode::Valid, counters));
    auto task = request();
    task.waiting_tasks.clear();
    const auto terminal =
        service_runtime::make_runtime_script_task_backend(harness.factory)(
            task, {}, [](auto) { return true; });
    check(terminal.exit_code == 0
              && counters->legacy_executor_calls == 1
              && counters->legacy_executions == 1,
          "the extension seam must be reachable for the activation-supported"
          " legacy engine only");
}

void test_runtime_retains_pins_and_never_rereads_provider()
{
    Harness harness;
    const auto value = request();
    const std::vector<std::string> requested{"start_one", "start_two"};
    const service_runtime::RuntimeScriptTaskExecutionControl control{
        {}, std::chrono::steady_clock::now() + std::chrono::minutes{1}};
    auto runtime = harness.factory->create(value, requested, control);
    check(runtime && runtime->identity().requested_task_plan == requested
              && runtime->identity().canonical_task_plan
                  == std::vector<std::string>{"one", "two"}
              && runtime->identity().runtime_generation
                  == harness.repositories.bundle()->generation()
              && runtime->identity().scripts_commit
                  == harness.repositories.bundle()->scripts().commit()
              && runtime->identity().resources_commit
                  == harness.repositories.bundle()->resources().commit(),
          "create must retain exact config/repository and ordered canonical pins");
    harness.provider->disable();
    const auto terminal = runtime->execute(control, [](auto) { return true; });
    check(terminal.exit_code == 0 && harness.provider->pins() == 1,
          "execution must use retained state without rereading disabled provider state");
}

void test_identity_changes_and_trust_fail_closed()
{
    Harness harness;
    const auto value = request();
    const std::vector<std::string> requested{"start_one", "start_two"};
    const service_runtime::RuntimeScriptTaskExecutionControl control{
        {}, std::chrono::steady_clock::now() + std::chrono::minutes{1}};
    auto runtime = harness.factory->create(value, requested, control);
    auto replacement = std::make_shared<const
        runtime_procedure::CoDetectProductionDeviceIdentity>(
            runtime_procedure::CoDetectProductionDeviceIdentity{
                harness.identity->device_id, harness.identity->profile,
                harness.identity->session_epoch + 1, true});
    harness.device->publish_identity(std::move(replacement));
    const auto terminal = runtime->execute(control, [](auto) { return true; });
    check(terminal.exit_code
              == service_runtime::runtime_script_task_failure_exit_code
              && harness.sink->events().empty(),
          "device session changes after create must fail before script execution");

    Harness denied;
    denied.trust->deny();
    check(!denied.factory->create(value, requested, control),
          "trust evidence not covering exact generation/commit must fail closed");
}

void test_extension_identity_composition_and_lifetime()
{
    Harness harness;
    auto counters = std::make_shared<ExtensionCounters>();
    auto extension = std::make_shared<TestExtensions>(
        extension_identity(harness), ExtensionMode::Valid, counters);
    harness.provider->set_extensions(extension);
    const service_runtime::RuntimeScriptTaskExecutionControl control{
        {}, std::chrono::steady_clock::now() + std::chrono::minutes{1}};
    const std::vector<std::string> requested{"start_one", "start_two"};
    std::unique_ptr<service_runtime::RuntimeScriptTaskRuntime> runtime;
    try {
        runtime = harness.factory->create(request(), requested, control);
    }
    catch (const std::exception& error) {
        std::cerr << "valid extension create failed: " << error.what() << '\n';
    }
    check(runtime && counters->calls == 2 && counters->live == 2,
          "extensions must create one fresh request-local Host owner per task");
    runtime.reset();
    check(counters->live == 0 && counters->created == 2
              && counters->destroyed == 2,
          "runtime destruction must release every extension Host lifetime owner");
    Harness mismatch;
    auto mismatch_counters = std::make_shared<ExtensionCounters>();
    auto wrong_identity = extension_identity(mismatch);
    wrong_identity.generation.assign(64, '0');
    mismatch.provider->set_extensions(std::make_shared<TestExtensions>(
        std::move(wrong_identity), ExtensionMode::Valid, mismatch_counters));
    check(!mismatch.factory->create(request(), requested, control)
              && mismatch_counters->calls == 0,
          "extension publications not covering the exact pin must fail before use");

    Harness same_snapshot_other_config;
    auto cross_config_counters = std::make_shared<ExtensionCounters>();
    auto cross_config_identity = extension_identity(same_snapshot_other_config);
    cross_config_identity.config_id = "secondary";
    same_snapshot_other_config.provider->set_extensions(
        std::make_shared<TestExtensions>(
            std::move(cross_config_identity), ExtensionMode::Valid,
            cross_config_counters));
    check(!same_snapshot_other_config.factory->create(
              request(), requested, control) &&
              cross_config_counters->calls == 0,
          "config A/B sharing one snapshot id must reject cross-config extensions");

    Harness duplicate;
    auto duplicate_counters = std::make_shared<ExtensionCounters>();
    duplicate.provider->set_extensions(std::make_shared<TestExtensions>(
        extension_identity(duplicate), ExtensionMode::DuplicateHost,
        duplicate_counters));
    const auto duplicate_terminal =
        service_runtime::make_runtime_script_task_backend(duplicate.factory)(
            request(), {}, [](auto) { return true; });
    check(duplicate_terminal.exit_code
                  == service_runtime::runtime_script_task_failure_exit_code
              && duplicate_counters->live == 0,
          "duplicate extension modules/bindings must fail closed and release owners");
}

void test_partial_create_failures_raii_close_completed_tasks()
{
    const service_runtime::RuntimeScriptTaskExecutionControl control{
        {}, std::chrono::steady_clock::now() + std::chrono::minutes{1}};
    const std::vector<std::string> requested{"start_one", "start_two"};

    Harness invalid_second{"true", "true", false, true};
    auto invalid_counters = std::make_shared<ExtensionCounters>();
    invalid_second.provider->set_extensions(std::make_shared<TestExtensions>(
        extension_identity(invalid_second), ExtensionMode::Valid,
        invalid_counters));
    check(!invalid_second.factory->create(request(), requested, control)
              && invalid_counters->created == 1
              && invalid_counters->destroyed == 1
              && invalid_counters->live == 0,
          "second-plan failure must RAII-close the already-built first task");

    Harness throwing;
    auto throwing_counters = std::make_shared<ExtensionCounters>();
    throwing.provider->set_extensions(std::make_shared<TestExtensions>(
        extension_identity(throwing), ExtensionMode::ThrowOnSecond,
        throwing_counters));
    const auto throw_terminal =
        service_runtime::make_runtime_script_task_backend(throwing.factory)(
            request(), {}, [](auto) { return true; });
    check(throw_terminal.exit_code
                  == service_runtime::runtime_script_task_failure_exit_code
              && throwing_counters->calls == 2
              && throwing_counters->created == 1
              && throwing_counters->destroyed == 1
              && throwing_counters->live == 0,
          "second extension exception must not leak the first evaluator/Host owners");

    Harness cancelled;
    std::stop_source stop;
    auto cancelled_counters = std::make_shared<ExtensionCounters>();
    cancelled.provider->set_extensions(std::make_shared<TestExtensions>(
        extension_identity(cancelled), ExtensionMode::CancelOnFirst,
        cancelled_counters, &stop));
    const service_runtime::RuntimeScriptTaskExecutionControl cancelled_control{
        stop.get_token(),
        std::chrono::steady_clock::now() + std::chrono::minutes{1}};
    check(!cancelled.factory->create(
              request(), requested, cancelled_control)
              && stop.stop_requested() && cancelled_counters->created == 1
              && cancelled_counters->destroyed == 1
              && cancelled_counters->live == 0,
          "cancellation after first task construction must RAII-close it");

    Harness drifting;
    auto drifting_counters = std::make_shared<ExtensionCounters>();
    drifting.provider->set_extensions(std::make_shared<TestExtensions>(
        extension_identity(drifting), ExtensionMode::DriftOnSecond,
        drifting_counters, nullptr, drifting.device, drifting.identity));
    check(!drifting.factory->create(request(), requested, control)
              && drifting_counters->created == 2
              && drifting_counters->destroyed == 2
              && drifting_counters->live == 0,
          "final device identity drift must close every fully-built task");
}

void test_boundaries_exceptions_and_fresh_concurrent_runtimes()
{
    Harness throwing;
    throwing.provider->throw_on_pin();
    const auto terminal = service_runtime::make_runtime_script_task_backend(
        throwing.factory)(request(), {}, [](auto) { return true; });
    check(terminal.exit_code
              == service_runtime::runtime_script_task_failure_exit_code,
          "provider allocation failure must not escape the backend boundary");

    Harness concurrent;
    const auto backend = service_runtime::make_runtime_script_task_backend(
        concurrent.factory);
    std::atomic<int> first{-1};
    std::atomic<int> second{-1};
    std::jthread a([&] {
        first = backend(request("a"), {}, [](auto) { return true; })
                    .exit_code.value_or(-1);
    });
    std::jthread b([&] {
        second = backend(request("b"), {}, [](auto) { return true; })
                     .exit_code.value_or(-1);
    });
    a.join();
    b.join();
    check(first.load() == 0 && second.load() == 0
              && concurrent.provider->pins() == 2
              && concurrent.sink->events().size() == 4,
          "concurrent creates must return isolated fresh evaluator/Host runtimes");
}

} // namespace

int main()
{
    test_real_catalog_plan_hosts_and_ordered_evaluation();
    test_entry_export_requires_exact_boolean_true();
    test_real_co_detect_activation_bundle_device_and_evaluator();
    test_co_detect_support_must_belong_to_the_activated_procedure();
    test_activation_supported_legacy_executor_extension_is_reachable();
    test_runtime_retains_pins_and_never_rereads_provider();
    test_identity_changes_and_trust_fail_closed();
    test_extension_identity_composition_and_lifetime();
    test_partial_create_failures_raii_close_completed_tasks();
    test_boundaries_exceptions_and_fresh_concurrent_runtimes();
    if (failures != 0) {
        std::cerr << failures << " production runtime factory test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "production runtime factory tests passed\n";
    return EXIT_SUCCESS;
}
