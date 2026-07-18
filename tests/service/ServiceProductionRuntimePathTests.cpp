#include "service/app/ServiceApplication.h"
#include "service/app/ProductionRuntimeTaskControl.h"

#include "resources/ResourceSnapshot.h"
#include "runtime/procedure/CoDetectProductionAdapter.h"
#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "runtime/script/RuntimeScriptCatalog.h"
#include "runtime/script/RuntimeScriptRepositoryTrustEvidence.h"
#include "script/runtime/LogHost.h"
#include "service/protocol/TriggerIngress.h"
#include "service/protocol/TriggerSession.h"
#include "service/runtime/ProductionRuntimeScriptTaskFactory.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace app = baas::service::app;
namespace procedure = baas::runtime::procedure;
namespace protocol = baas::service::protocol::trigger;
namespace repository = baas::runtime::repository;
namespace runtime_script = baas::runtime::script;
namespace service_runtime = baas::service::runtime;
namespace script_runtime = baas::script::runtime;
namespace trigger = baas::service::trigger;
using namespace std::chrono_literals;

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

[[nodiscard]] std::uint64_t process_id() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint64_t>(::_getpid());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

void write_file(
    const std::filesystem::path& path,
    const std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::runtime_error{"fixture write failed"};
}

[[nodiscard]] std::string sha256(const std::string_view value)
{
    return baas::resources::sha256_hex(
        std::as_bytes(std::span{value.data(), value.size()}));
}

struct File final {
    std::string path;
    std::string bytes;
};

[[nodiscard]] std::string tree_manifest(std::vector<File> files)
{
    std::ranges::sort(files, {}, &File::path);
    nlohmann::ordered_json entries = nlohmann::ordered_json::array();
    for (const auto& file : files) {
        entries.push_back({
            {"path", file.path},
            {"size", std::to_string(file.bytes.size())},
            {"sha256", sha256(file.bytes)},
            {"mode", "file"},
        });
    }
    nlohmann::ordered_json root;
    root["schema"] = "baas.runtime-repository.tree-manifest/v1";
    root["entries"] = std::move(entries);
    return root.dump();
}

class ProjectFixture final {
public:
    explicit ProjectFixture(const bool alternate = false)
    {
        static std::atomic<std::uint64_t> sequence{};
        root = std::filesystem::temp_directory_path()
            / ("baas-service-production-runtime-"
               + std::to_string(process_id()) + "-"
               + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(root);
#if !defined(__ANDROID__)
        write_file(
            root / "service/remote/scrcpy-server.jar",
            "production runtime service-path fixture");
#endif

        const std::string source =
            "import \"baas/log\" as log;\n"
            "fn run() { log.emit(\"info\", \"service path\"); return true; }\n";
        const std::string package =
            R"({"manifest_schema":1,"package":{"id":"baas.test.service-path","version":"1.0.0"},"language":{"major":1,"min_minor":0},"entrypoint":"main.baas","host_modules":{"baas/log":{"major":1,"min_minor":0}},"capabilities":["log.emit"],"profiles":[],"modules":[{"path":"main.baas","size":)"
            + std::to_string(source.size()) + R"(,"sha256":")"
            + sha256(source)
            + R"("}],"resources":[],"limits":{"source_bytes":4096,"resource_bytes":0,"module_count":1,"resource_count":0}})";
        const std::string catalog =
            R"({"schema":"baas.runtime-script.catalog/v2","tasks":[{"run_mode":"solve","task":"one","package_root":"packages/one","package_manifest":"packages/one/baas.package.json","entry_module":"main","entry_export":"run","language_version":{"major":1,"minor":0},"host_modules":[{"module":"baas/log","major":1,"min_minor":0,"capabilities":["log.emit"]}],"legacy_aliases":["start_one"]},{"run_mode":"solve","task":"explore_hard_task","package_root":"packages/one","package_manifest":"packages/one/baas.package.json","entry_module":"main","entry_export":"run","language_version":{"major":1,"minor":0},"host_modules":[{"module":"baas/log","major":1,"min_minor":0,"capabilities":["log.emit"]}],"legacy_aliases":[]}]})";

        std::vector<File> resource_files{
            {"baas.resources.json",
             R"({"schema":"baas.resources/v1","entries":[]})"},
            {"service/configuration/defaults/event.json", "[]"},
            {"service/configuration/defaults/static.json",
             R"({"create_item_order":{"CN":{"basic":{}},"Global":{"basic":{}},"JP":{"basic":{}}}})"},
            {"service/configuration/defaults/switch.json", "[]"},
            {"service/configuration/defaults/user.json",
             R"({"name":"Default","server":"CN","create_item_holding_quantity":{}})"},
        };
        std::vector<File> script_files{
            {std::string{runtime_script::runtime_script_catalog_manifest},
             catalog},
            {"packages/one/baas.package.json", package},
            {"packages/one/main.baas", source},
        };

        const auto resource_manifest = tree_manifest(resource_files);
        const auto script_manifest = tree_manifest(script_files);
        const std::string resource_commit(40, alternate ? '3' : '1');
        const std::string script_commit(40, alternate ? '4' : '2');
        repositories = {{
            {"resources", resource_commit,
             "objects/resources/" + resource_commit,
             "manifest.json", sha256(resource_manifest)},
            {"scripts", script_commit,
             "objects/scripts/" + script_commit,
             "manifest.json", sha256(script_manifest)},
        }};
        generation = repository::runtime_repository_generation(repositories);
        const auto state = root / ".baas-updater/runtime-repositories";
        for (const auto& file : resource_files) {
            write_file(state / repositories[0].root / file.path, file.bytes);
        }
        for (const auto& file : script_files) {
            write_file(state / repositories[1].root / file.path, file.bytes);
        }
        write_file(
            state / repositories[0].root / repositories[0].manifest,
            resource_manifest);
        write_file(
            state / repositories[1].root / repositories[1].manifest,
            script_manifest);

        nlohmann::ordered_json repository_entries =
            nlohmann::ordered_json::array();
        for (const auto& item : repositories) {
            repository_entries.push_back({
                {"id", item.id},
                {"commit", item.commit},
                {"root", item.root},
                {"manifest", item.manifest},
                {"manifest_sha256", item.manifest_sha256},
            });
        }
        nlohmann::ordered_json snapshot;
        snapshot["schema"] = "baas.runtime-repositories.snapshot/v1";
        snapshot["generation"] = generation;
        snapshot["repositories"] = std::move(repository_entries);
        write_file(
            state / "snapshots" / (generation + ".json"), snapshot.dump());
        nlohmann::ordered_json current;
        current["schema"] = "baas.runtime-repositories.current/v1";
        current["generation"] = generation;
        current["snapshot"] = "snapshots/" + generation + ".json";
        write_file(state / "current.json", current.dump());
        write_file(state / ".trusted-plan-writer.lock", "");
        write_file(
            state / ".trusted-plan-owner",
            "baas.runtime-repositories.trusted-plan-owner/v1\ninitialized\n");
        const nlohmann::json trusted{
            {"schema", "baas.runtime-repositories.trusted-plan-state/v1"},
            {"generation", generation},
            {"sequence", "1"},
            {"payload_sha256", std::string(64, 'c')},
        };
        write_file(state / ".trusted-plan-state.json", trusted.dump());

        auto snapshot_owner = repository::RuntimeRepositorySnapshot::activate(
            state);
        bundle = snapshot_owner->open_read_bundle();
    }

    ~ProjectFixture()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    [[nodiscard]] app::ServiceRunOptions options() const
    {
        return {
            root, "127.0.0.1", 1, std::nullopt, generation};
    }

    std::filesystem::path root;
    std::array<repository::RuntimeRepository, 2> repositories;
    std::string generation;
    std::shared_ptr<const repository::RuntimeRepositoryReadBundle> bundle;
};

class Trust final
    : public runtime_script::RuntimeScriptRepositoryTrustEvidence {
public:
    Trust(std::string generation, std::string commit)
        : generation_(std::move(generation)), commit_(std::move(commit))
    {
    }

    [[nodiscard]] bool covers(
        const std::string_view generation,
        const std::string_view scripts_commit) const noexcept override
    {
        return generation == generation_ && scripts_commit == commit_;
    }

private:
    std::string generation_;
    std::string commit_;
};

class Sink final : public script_runtime::StructuredLogSink {
public:
    void write(const script_runtime::StructuredLogEvent&) override
    {
        ++events;
    }

    std::atomic<unsigned int> events{};
};

class Device final : public procedure::CoDetectProductionDevicePort {
public:
    explicit Device(
        std::shared_ptr<const procedure::CoDetectProductionDeviceIdentity>
            identity)
        : identity_(std::move(identity))
    {
    }

    [[nodiscard]] std::shared_ptr<const procedure::CoDetectProductionDeviceIdentity>
    current_identity() const noexcept override { return identity_; }
    [[nodiscard]] std::uint64_t monotonic_ms() const noexcept override
    { return 1; }
    [[nodiscard]] std::uint64_t screenshot_interval_ms() const noexcept override
    { return 1; }
    [[nodiscard]] std::shared_ptr<const procedure::CoDetectProductionBgrFrame>
    latest_frame() const noexcept override { return {}; }
    [[nodiscard]] bool publish_latest_frame(
        std::shared_ptr<const procedure::CoDetectProductionBgrFrame>)
        noexcept override { return false; }
    [[nodiscard]] procedure::CoDetectResult<procedure::CoDetectProductionBgrFrame>
    capture(const procedure::CoDetectControl&) override
    { return procedure::CoDetectOperationError::Unavailable; }
    [[nodiscard]] procedure::CoDetectResult<std::monostate> click(
        procedure::CoDetectClick,
        const procedure::CoDetectControl&) override
    { return procedure::CoDetectOperationError::Unavailable; }
    [[nodiscard]] procedure::CoDetectResult<std::monostate> wait(
        std::uint64_t,
        const procedure::CoDetectControl&) override
    { return procedure::CoDetectOperationError::Unavailable; }
    [[nodiscard]] procedure::CoDetectResult<bool> foreground_matches(
        const procedure::CoDetectControl&) override
    { return procedure::CoDetectOperationError::Unavailable; }

private:
    std::shared_ptr<const procedure::CoDetectProductionDeviceIdentity> identity_;
};

enum class ProviderMode { pass, fail, wait_for_control };

class Provider final
    : public service_runtime::ProductionRuntimeScriptTaskProvider {
public:
    Provider(
        std::shared_ptr<const repository::RuntimeRepositoryReadBundle> bundle,
        const ProviderMode mode)
        : bundle_(std::move(bundle)), mode_(mode),
          trust_(std::make_shared<Trust>(
              bundle_->generation(), bundle_->scripts().commit())),
          identity_(std::make_shared<const
              procedure::CoDetectProductionDeviceIdentity>(
                  procedure::CoDetectProductionDeviceIdentity{
                      "test-device", procedure::CoDetectProfile::cn, 1, true})),
          device_(std::make_shared<Device>(identity_)),
          sink_(std::make_shared<Sink>())
    {
    }

    [[nodiscard]] std::optional<service_runtime::ProductionRuntimeScriptTaskInputs>
    pin(
        const service_runtime::RuntimeTaskRequest& request,
        std::span<const std::string>,
        const service_runtime::RuntimeScriptTaskExecutionControl& control)
        const override
    {
        ++pins;
        if (mode_ == ProviderMode::fail) return std::nullopt;
        if (mode_ == ProviderMode::wait_for_control) {
            while (!control.stop_requested() && !control.deadline_exceeded()) {
                std::this_thread::sleep_for(1ms);
            }
            return std::nullopt;
        }
        auto config = std::make_shared<const
            service_runtime::ProductionRuntimeScriptConfigSnapshot>(
                service_runtime::ProductionRuntimeScriptConfigSnapshot{
                    request.config_id,
                    request.config_id + "@snapshot",
                    identity_->device_id,
                    "CN",
                    identity_->profile,
                    {"CN", std::nullopt},
                    {"log.emit"},
                    {"log.emit"},
                    {},
                    {}});
        return service_runtime::ProductionRuntimeScriptTaskInputs{
            std::move(config), bundle_, trust_, device_, identity_, sink_, {}};
    }

    mutable std::atomic<unsigned int> pins{};
    std::shared_ptr<Sink> sink() const noexcept { return sink_; }

private:
    std::shared_ptr<const repository::RuntimeRepositoryReadBundle> bundle_;
    ProviderMode mode_;
    std::shared_ptr<Trust> trust_;
    std::shared_ptr<const procedure::CoDetectProductionDeviceIdentity> identity_;
    std::shared_ptr<Device> device_;
    std::shared_ptr<Sink> sink_;
};

class ControlledRuntime final
    : public service_runtime::RuntimeScriptTaskRuntime {
public:
    ControlledRuntime(
        service_runtime::RuntimeScriptTaskIdentity identity,
        std::shared_ptr<std::atomic<unsigned int>> entered,
        const bool fail)
        : identity_(std::move(identity)), entered_(std::move(entered)),
          fail_(fail)
    {}

    const service_runtime::RuntimeScriptTaskIdentity& identity()
        const noexcept override { return identity_; }

    service_runtime::RuntimeTaskTerminal execute(
        const service_runtime::RuntimeScriptTaskExecutionControl& control,
        const service_runtime::RuntimeTaskProgressReporter&) override
    {
        ++*entered_;
        if (fail_) return {false, 1};
        while (!control.stop_requested() && !control.deadline_exceeded()) {
            std::this_thread::sleep_for(1ms);
        }
        if (control.deadline_exceeded()) return {false, 124};
        // Keep the owner's stopping phase observable for the restart race.
        std::this_thread::sleep_for(25ms);
        return {false, 130};
    }

private:
    service_runtime::RuntimeScriptTaskIdentity identity_;
    std::shared_ptr<std::atomic<unsigned int>> entered_;
    bool fail_{};
};

class ControlledFactory final
    : public service_runtime::RuntimeScriptTaskRuntimeFactory {
public:
    std::string generation;
    std::string scripts_commit;
    std::string resources_commit;
    bool fail_execution{};
    bool exhaust_prepare{};
    bool invalid_identity{};
    std::shared_ptr<std::atomic<unsigned int>> creates =
        std::make_shared<std::atomic<unsigned int>>();
    std::shared_ptr<std::atomic<unsigned int>> entered =
        std::make_shared<std::atomic<unsigned int>>();

    std::unique_ptr<service_runtime::RuntimeScriptTaskRuntime> create(
        const service_runtime::RuntimeTaskRequest& request,
        const std::span<const std::string> requested,
        const service_runtime::RuntimeScriptTaskExecutionControl&) const override
    {
        ++*creates;
        if (exhaust_prepare) throw std::bad_alloc{};
        if (requested.empty() || requested.front() == "missing") return {};
        service_runtime::RuntimeScriptTaskIdentity identity;
        identity.config_id = request.config_id;
        if (invalid_identity) identity.config_id = "wrong-config";
        identity.config_snapshot_id = request.config_id + "@controlled";
        identity.profile = "CN";
        identity.device_id = "controlled-device";
        identity.runtime_generation = generation;
        identity.scripts_commit = scripts_commit;
        identity.resources_commit = resources_commit;
        identity.run_mode = request.run_mode;
        identity.requested_task_plan.assign(requested.begin(), requested.end());
        identity.canonical_task_plan = identity.requested_task_plan;
        return std::make_unique<ControlledRuntime>(
            std::move(identity), entered, fail_execution);
    }
};

class ControlledCompositionFactory final
    : public app::ServiceRuntimeTaskCompositionFactory {
public:
    ControlledCompositionFactory(
        std::shared_ptr<ControlledFactory> factory,
        const std::chrono::milliseconds deadline)
        : factory_(std::move(factory)), deadline_(deadline)
    {}

    app::ServiceRuntimeTaskComposition compose(
        std::shared_ptr<const repository::RuntimeRepositoryReadBundle>
            bundle) override
    {
        if (factory_->generation.empty()) {
            factory_->generation = bundle->generation();
            factory_->scripts_commit = bundle->scripts().commit();
            factory_->resources_commit = bundle->resources().commit();
        }
        service_runtime::RuntimeTaskBackend fallback = [](
            const service_runtime::RuntimeTaskRequest&, std::stop_token,
            const service_runtime::RuntimeTaskProgressReporter&) noexcept {
            return service_runtime::RuntimeTaskTerminal{false, 1};
        };
        auto owner = std::make_shared<service_runtime::RuntimeTaskOwner>(
            std::move(fallback));
        service_runtime::RuntimeScriptTaskBackendOptions options;
        options.task_deadline = deadline_;
        auto control = std::make_shared<app::ProductionRuntimeTaskControl>(
            owner, factory_,
            service_runtime::RuntimeScriptTaskRepositoryBinding{
                bundle->generation(), bundle->scripts().commit(),
                bundle->resources().commit()},
            options);
        return {std::move(owner), std::move(control)};
    }

private:
    std::shared_ptr<ControlledFactory> factory_;
    std::chrono::milliseconds deadline_;
};

[[nodiscard]] std::optional<protocol::TriggerIngressItem> ingress(
    const std::string_view command,
    const protocol::Timestamp timestamp,
    const std::string_view config_id = {},
    const std::string_view payload = "{}")
{
    nlohmann::ordered_json value;
    value["type"] = "command";
    value["command"] = command;
    value["timestamp"] = timestamp;
    if (!config_id.empty()) value["config_id"] = config_id;
    value["payload"] = nlohmann::json::parse(payload);
    protocol::TriggerIngress parser;
    if (!parser.receive_json_frame(value.dump())) return std::nullopt;
    return parser.take_ready();
}

template <typename Predicate>
[[nodiscard]] bool wait_until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

class TriggerClient final {
public:
    explicit TriggerClient(app::ServiceApplication& application)
        : session(std::make_shared<protocol::TriggerSession>()),
          connection(application.trigger_executor()->connect(session))
    {
    }

    [[nodiscard]] trigger::TriggerSubmitResult submit(
        const std::string_view command,
        const protocol::Timestamp timestamp,
        const std::string_view config_id = {},
        const std::string_view payload = "{}")
    {
        auto item = ingress(command, timestamp, config_id, payload);
        if (!item) throw std::runtime_error{"ingress fixture failed"};
        return connection.submit(std::move(*item));
    }

    [[nodiscard]] std::optional<nlohmann::json> response()
    {
        std::optional<protocol::SendLease> lease;
        if (!wait_until([&] {
                auto begun = session->begin_send();
                if (!begun) return false;
                lease = std::move(*begun.lease);
                return true;
            })) {
            return std::nullopt;
        }
        auto value = nlohmann::json::parse(lease->batch().json());
        if (!connection.complete_send(*lease)) return std::nullopt;
        return value;
    }

    std::shared_ptr<protocol::TriggerSession> session;
    trigger::TriggerConnectionOwner connection;
};

[[nodiscard]] app::ServiceApplicationDependencies dependencies(
    std::shared_ptr<Provider> provider,
    const std::chrono::milliseconds deadline = 30min)
{
    app::ServiceApplicationDependencies result;
    service_runtime::RuntimeScriptTaskBackendOptions options;
    options.task_deadline = deadline;
    result.runtime_task_composition_factory =
        app::make_production_runtime_task_composition_factory(
            std::move(provider), options);
    return result;
}

void test_absent_opt_in_preserves_unregistered_runtime_commands()
{
    ProjectFixture project;
    auto opened = app::ServiceApplication::open(project.options());
    check(static_cast<bool>(opened), "default application composition succeeds");
    if (!opened) return;
    TriggerClient client{*opened.application};
    const auto submitted = client.submit(
        "solve", 1, "alpha", R"({"task":"one"})");
    check(submitted.error == trigger::TriggerSubmitError::unregistered_command,
          "provider absence must not install a placeholder solve handler");
    check(!opened.application->runtime_task_snapshot("alpha"),
          "provider absence must not create a runtime task owner generation");
    opened.application->stop();
}

void test_success_runs_factory_backend_owner_and_control()
{
    ProjectFixture project;
    auto provider = std::make_shared<Provider>(
        project.bundle, ProviderMode::pass);
    auto opened = app::ServiceApplication::open(
        project.options(), dependencies(provider));
    check(static_cast<bool>(opened), "opt-in application composition succeeds");
    if (!opened) return;
    TriggerClient client{*opened.application};
    check(static_cast<bool>(
              client.submit("solve", 2, "alpha", R"({"task":"start_one"})")),
          "solve traverses real trigger admission");
    const auto response = client.response();
    check(response && response->value("status", "") == "ok"
              && response->at("data").value("task", "") == "start_one",
          "control claims Python-compatible one-shot response");
    check(wait_until([&] {
              const auto snapshot =
                  opened.application->runtime_task_snapshot("alpha");
              return snapshot && !snapshot->running;
          }),
          "factory-backed runtime reaches a terminal owner snapshot");
    const auto snapshot = opened.application->runtime_task_snapshot("alpha");
    check(snapshot && snapshot->exit_code == 0 && provider->pins == 1
              && provider->sink()->events == 1,
          "real factory evaluates the pinned package and flushes its log host");
    check(static_cast<bool>(
              client.submit("start_hard_task", 3, "legacy")),
          "start wildcard descriptor reaches the production control");
    const auto legacy = client.response();
    check(legacy
              && legacy->at("data").value("task", "")
                  == "explore_hard_task",
          "production control normalizes the Python command alias");
    check(wait_until([&] {
              const auto value =
                  opened.application->runtime_task_snapshot("legacy");
              return value && !value->running;
          }),
          "normalized start wildcard executes through the real factory");
    check(provider->pins == 2 && provider->sink()->events == 2,
          "start wildcard pins and executes one independent generation");
    check(static_cast<bool>(
              client.submit("start_scheduler", 4, "scheduler")),
          "scheduler descriptor remains registered in opt-in composition");
    const auto scheduler = client.response();
    check(scheduler && scheduler->value("status", "") == "error"
              && scheduler->value("error", "")
                  == "runtime_task_control_unavailable",
          "missing native scheduler plan fails closed without fake work");
    opened.application->stop();
}

void test_provider_failure_and_deadline_fail_closed()
{
    {
        ProjectFixture project;
        auto provider = std::make_shared<Provider>(
            project.bundle, ProviderMode::fail);
        auto opened = app::ServiceApplication::open(
            project.options(), dependencies(provider));
        check(static_cast<bool>(opened), "failing-provider application opens");
        if (opened) {
            TriggerClient client{*opened.application};
            check(static_cast<bool>(
                      client.submit("solve", 3, "fail", R"({"task":"one"})")),
                  "provider failure task is service-admitted");
            const auto failed = client.response();
            check(failed && failed->value("status", "") == "error"
                      && failed->value("error", "")
                          == "runtime_task_control_unavailable",
                  "null provider fails before claim with a stable wire error");
            check(!opened.application->runtime_task_snapshot("fail"),
                  "provider failure must not publish a fake successful generation");
            opened.application->stop();
        }
    }
    {
        ProjectFixture project;
        auto factory = std::make_shared<ControlledFactory>();
        app::ServiceApplicationDependencies controlled;
        controlled.runtime_task_composition_factory =
            std::make_shared<ControlledCompositionFactory>(factory, 25ms);
        auto opened = app::ServiceApplication::open(
            project.options(), std::move(controlled));
        check(static_cast<bool>(opened), "deadline-provider application opens");
        if (opened) {
            TriggerClient client{*opened.application};
            check(static_cast<bool>(client.submit(
                      "solve", 4, "deadline", R"({"task":"one"})")),
                  "deadline task is service-admitted");
            const auto started = client.response();
            check(started && started->value("status", "") == "ok",
                  "deadline task ownership response is delivered");
            check(wait_until([&] {
                      const auto value = opened.application
                                             ->runtime_task_snapshot("deadline");
                      return value && !value->running;
                  }),
                  "deadline reaches terminal state");
            const auto value =
                opened.application->runtime_task_snapshot("deadline");
            check(value && value->exit_code
                      == service_runtime::runtime_script_task_deadline_exit_code,
                  "factory/provider deadline wins with stable exit 124");
            check(static_cast<bool>(client.submit("status", 5)),
                  "deadline status query submits");
            const auto status = client.response();
            check(status && status->at("data").at("deadline")
                      .value("exit_code", 0) == 124,
                  "real status wire publishes deadline exit 124");
            opened.application->stop();
        }
    }
    {
        ProjectFixture project;
        auto factory = std::make_shared<ControlledFactory>();
        factory->fail_execution = true;
        app::ServiceApplicationDependencies controlled;
        controlled.runtime_task_composition_factory =
            std::make_shared<ControlledCompositionFactory>(factory, 30min);
        auto opened = app::ServiceApplication::open(
            project.options(), std::move(controlled));
        check(static_cast<bool>(opened), "failure-runtime application opens");
        if (opened) {
            TriggerClient client{*opened.application};
            check(static_cast<bool>(client.submit(
                      "solve", 5, "exit-one", R"({"task":"one"})")),
                  "failure runtime submits");
            const auto started = client.response();
            check(started && started->value("status", "") == "ok",
                  "prepared runtime is acknowledged before execution");
            check(wait_until([&] {
                      const auto value = opened.application
                                             ->runtime_task_snapshot("exit-one");
                      return value && !value->running;
                  }),
                  "failure runtime reaches terminal state");
            check(static_cast<bool>(client.submit("status", 6)),
                  "failure status query submits");
            const auto status = client.response();
            check(status && status->at("data").at("exit-one")
                      .value("exit_code", 0) == 1,
                  "real status wire publishes execution failure exit 1");
            check(static_cast<bool>(client.submit(
                      "solve", 7, "missing", R"({"task":"missing"})")),
                  "missing task reaches prepare");
            const auto missing = client.response();
            check(missing && missing->value("status", "") == "error"
                      && missing->value("error", "")
                          == "runtime_task_control_unavailable"
                      && !opened.application->runtime_task_snapshot("missing"),
                  "missing task fails before claim without a fake snapshot");
            factory->exhaust_prepare = true;
            check(static_cast<bool>(client.submit(
                      "solve", 8, "allocation", R"({"task":"one"})")),
                  "allocation failure reaches prepare");
            const auto allocation = client.response();
            check(allocation && allocation->value("status", "") == "error"
                      && allocation->value("error", "")
                          == "runtime_task_control_capacity"
                      && !opened.application
                              ->runtime_task_snapshot("allocation"),
                  "prepare allocation failure is stable and rolls back");
            factory->exhaust_prepare = false;
            factory->invalid_identity = true;
            check(static_cast<bool>(client.submit(
                      "solve", 9, "identity", R"({"task":"one"})")),
                  "invalid identity reaches prepare");
            const auto identity = client.response();
            check(identity && identity->value("status", "") == "error"
                      && identity->value("error", "")
                          == "runtime_task_invalid_task"
                      && !opened.application
                              ->runtime_task_snapshot("identity"),
                  "invalid runtime identity fails before claim without snapshot");
            opened.application->stop();
        }
    }
    {
        ProjectFixture admitted;
        ProjectFixture stale{true};
        auto provider = std::make_shared<Provider>(
            stale.bundle, ProviderMode::pass);
        auto opened = app::ServiceApplication::open(
            admitted.options(), dependencies(provider));
        check(static_cast<bool>(opened), "stale-provider application opens");
        if (opened) {
            TriggerClient client{*opened.application};
            check(static_cast<bool>(client.submit(
                      "solve", 8, "stale", R"({"task":"one"})")),
                  "stale bundle reaches prepare");
            const auto response = client.response();
            check(response && response->value("status", "") == "error"
                      && response->value("error", "")
                          == "runtime_task_repository_mismatch"
                      && !opened.application->runtime_task_snapshot("stale"),
                  "valid but non-admitted bundle is rejected before claim");
            opened.application->stop();
        }
    }
    {
        ProjectFixture project;
        auto provider = std::make_shared<Provider>(
            project.bundle, ProviderMode::wait_for_control);
        auto opened = app::ServiceApplication::open(
            project.options(), dependencies(provider));
        check(static_cast<bool>(opened), "prepare-shutdown application opens");
        if (opened) {
            TriggerClient client{*opened.application};
            check(static_cast<bool>(client.submit(
                      "solve", 9, "preparing", R"({"task":"one"})")),
                  "blocking provider prepare submits");
            check(wait_until([&] { return provider->pins.load() == 1; }),
                  "provider prepare enters before shutdown");
            const auto before = std::chrono::steady_clock::now();
            opened.application->stop();
            check(std::chrono::steady_clock::now() - before < 2s
                      && !opened.application
                              ->runtime_task_snapshot("preparing"),
                  "shutdown cancels in-flight prepare and rolls back admission");
        }
    }
}

void test_duplicate_config_cancel_and_concurrent_shutdown()
{
    ProjectFixture project;
    auto factory = std::make_shared<ControlledFactory>();
    app::ServiceApplicationDependencies controlled;
    controlled.runtime_task_composition_factory =
        std::make_shared<ControlledCompositionFactory>(factory, 30min);
    auto opened = app::ServiceApplication::open(
        project.options(), std::move(controlled));
    check(static_cast<bool>(opened), "cancellation application opens");
    if (!opened) return;
    TriggerClient client{*opened.application};
    check(static_cast<bool>(
              client.submit("solve", 5, "same", R"({"task":"one"})")),
          "first keyed task submits");
    check(client.response().has_value(), "first keyed task is acknowledged");
    check(wait_until([&] { return factory->entered->load() == 1; }),
          "first keyed task enters the production provider");

    check(static_cast<bool>(client.submit("status", 6)),
          "native status query submits while the task is running");
    const auto running_status = client.response();
    check(running_status && running_status->at("data").at("same")
              .value("running", false)
              && running_status->at("data").at("same")
                  .value("current_task", "") == "one",
          "RuntimeTaskOwner state is merged into the real status wire");
    check(static_cast<bool>(
              client.submit("solve", 7, "same", R"({"task":"one"})")),
          "duplicate keyed command reaches the concrete control");
    const auto duplicate = client.response();
    check(duplicate
              && duplicate->at("data").value("status", "")
                  == "already-running"
              && factory->creates->load() == 1,
          "RuntimeTaskOwner keyed gate prevents a second factory invocation");
    check(static_cast<bool>(client.submit("stop_scheduler", 8, "same")),
          "keyed compatibility stop submits");
    const auto stopped = client.response();
    check(stopped
              && stopped->at("data").value("status", "") == "stopped",
          "stop_scheduler claims then commits the keyed cancellation");
    check(static_cast<bool>(
              client.submit("solve", 9, "same", R"({"task":"one"})")),
          "restart during stopping reaches the production control");
    const auto stopping = client.response();
    check(stopping && stopping->value("status", "") == "error"
              && stopping->value("error", "") == "runtime_task_conflict",
          "stopping is an explicit conflict, never already-running success");
    check(wait_until([&] {
              const auto value =
                  opened.application->runtime_task_snapshot("same");
              return value && !value->running;
          }),
          "keyed cancellation drains the provider and worker");
    const auto cancelled = opened.application->runtime_task_snapshot("same");
    check(cancelled && cancelled->exit_code
              == service_runtime::runtime_script_task_cancelled_exit_code,
          "manual cancellation publishes stable exit 130");
    check(static_cast<bool>(client.submit("status", 10)),
          "terminal native status query submits");
    const auto terminal_status = client.response();
    check(terminal_status && terminal_status->at("data").at("same")
              .value("exit_code", 0) == 130,
          "real status wire publishes the terminal cancellation exit code");

    // The current application remains usable after one completed generation;
    // exercise both the global stop and concurrent idempotent application stop.
    check(static_cast<bool>(
              client.submit("solve", 11, "shutdown", R"({"task":"one"})")),
          "shutdown fixture task submits");
    check(client.response().has_value(), "global-stop task is acknowledged");
    check(wait_until([&] { return factory->entered->load() == 2; }),
          "global-stop fixture enters the retained provider");
    check(static_cast<bool>(client.submit("stop_all_tasks", 12)),
          "global stop descriptor submits");
    const auto global_stop = client.response();
    check(global_stop
              && global_stop->at("data").value("status", "") == "stopped",
          "global stop commits the owner reservation");
    check(wait_until([&] {
              const auto value =
                  opened.application->runtime_task_snapshot("shutdown");
              return value && !value->running;
          }),
          "global stop drains the retained provider");

    check(static_cast<bool>(
              client.submit("solve", 13, "application-stop",
                            R"({"task":"one"})")),
          "application shutdown fixture submits");
    check(client.response().has_value(),
          "application shutdown fixture is acknowledged");
    check(wait_until([&] { return factory->entered->load() == 3; }),
          "application shutdown fixture enters the retained provider");
    std::thread first{[&] { opened.application->stop(); }};
    std::thread second{[&] { opened.application->stop(); }};
    first.join();
    second.join();
    const auto shutdown =
        opened.application->runtime_task_snapshot("application-stop");
    check(shutdown && !shutdown->running
              && shutdown->exit_code
                  == service_runtime::runtime_script_task_cancelled_exit_code
              && opened.application->trigger_executor()->stats().stopping,
          "concurrent stop closes trigger admission then drains native tasks");
}

}  // namespace

int main()
{
    try {
        test_absent_opt_in_preserves_unregistered_runtime_commands();
        test_success_runs_factory_backend_owner_and_control();
        test_provider_failure_and_deadline_fail_closed();
        test_duplicate_config_cancel_and_concurrent_shutdown();
    } catch (const std::exception& error) {
        std::cerr << "UNEXPECTED: " << error.what() << '\n';
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << " production runtime service-path test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "production runtime service-path tests passed\n";
    return EXIT_SUCCESS;
}
