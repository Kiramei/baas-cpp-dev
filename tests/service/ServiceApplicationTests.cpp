#include "service/app/ServiceApplication.h"

#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "service/http/HttpHost.h"
#include "service/protocol/TriggerIngress.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
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

#if defined(BAAS_SECRETSTREAM_TEST_HOOKS) \
 || defined(BAAS_BUSINESS_SESSION_TEST_HOOKS) \
 || defined(BAAS_SERVICE_SHUTDOWN_TEST_HOOKS) \
 || defined(BAAS_SERVICE_TRIGGER_EXECUTOR_TEST_HOOKS) \
 || defined(BAAS_SERVICE_TRIGGER_HANDLER_TEST_HOOKS) \
 || defined(BAAS_SERVICE_WEBSOCKET_TEST_HOOKS)
#error "BAAS_service application tests must link the production, hook-free targets"
#endif

namespace app = baas::service::app;
namespace http = baas::service::http;
namespace protocol = baas::service::protocol::trigger;
namespace trigger = baas::service::trigger;
namespace router = baas::service::router;
namespace repository = baas::runtime::repository;
using namespace std::chrono_literals;

namespace {

int failures = 0;

std::uint64_t current_process_id() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint64_t>(::_getpid());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void set_environment(const char* name, const char* value)
{
#if defined(_WIN32)
    if (_putenv_s(name, value == nullptr ? "" : value) != 0) {
        throw std::runtime_error("environment update failed");
    }
#else
    const int result = value == nullptr ? unsetenv(name) : setenv(name, value, 1);
    if (result != 0) throw std::runtime_error("environment update failed");
#endif
}

[[nodiscard]] std::optional<std::string> read_environment(const char* name)
{
#if defined(_WIN32)
    char* value{};
    std::size_t length{};
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        std::free(value);
        return std::nullopt;
    }
    std::string copied(value, length == 0 ? 0 : length - 1);
    std::free(value);
    return copied;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::nullopt
                            : std::optional<std::string>{value};
#endif
}

class EnvironmentGuard final {
public:
    explicit EnvironmentGuard(const char* name) : name_(name), value_(read_environment(name)) {}
    ~EnvironmentGuard()
    {
        try {
            set_environment(name_.c_str(), value_ ? value_->c_str() : nullptr);
        } catch (...) {
        }
    }

private:
    std::string name_;
    std::optional<std::string> value_;
};

class TemporaryRoot final {
public:
    explicit TemporaryRoot(const std::string_view suffix)
    {
        static std::atomic<std::uint64_t> sequence{};
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        path = std::filesystem::temp_directory_path()
            / ("baas-service-app-" + std::string{suffix} + "-"
               + std::to_string(current_process_id()) + "-" + unique + "-"
               + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }
    ~TemporaryRoot()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
};

void add_remote_server_resource(const std::filesystem::path& root)
{
    const auto directory = root / "service" / "remote";
    std::filesystem::create_directories(directory);
    std::ofstream jar(
        directory / "scrcpy-server.jar", std::ios::binary | std::ios::trunc);
    jar << "deterministic application-test ws-scrcpy fixture";
    jar.close();
    if (!jar) throw std::runtime_error("remote server fixture write failed");
}

void write_runtime_repository_file(
    const std::filesystem::path& path, const std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::runtime_error("runtime repository fixture write failed");
}

constexpr std::string_view runtime_repository_manifest =
    R"({"schema":"baas.runtime-repository.tree-manifest/v1","entries":[{"path":"nested/payload.bin","size":"7","sha256":"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5","mode":"file"}]})";

constexpr std::string_view runtime_repository_manifest_sha256 =
    "737a1b1bcb0b033b9d8a969ac87271ee6dc4ad2f1659a49488413b5a2986b270";

[[nodiscard]] std::string add_runtime_repository_activation(
    const std::filesystem::path& project_root,
    const char resource_digit = '1',
    const char script_digit = '2')
{
    const std::string resource_commit(40, resource_digit);
    const std::string script_commit(40, script_digit);
    const std::array<repository::RuntimeRepository, 2> values{{
        {"resources", resource_commit, "objects/resources/" + resource_commit,
         "manifest.json", std::string(runtime_repository_manifest_sha256)},
        {"scripts", script_commit, "objects/scripts/" + script_commit,
         "manifest.json", std::string(runtime_repository_manifest_sha256)},
    }};
    const auto generation = repository::runtime_repository_generation(values);
    const auto state_root =
        project_root / ".baas-updater" / "runtime-repositories";
    const nlohmann::json snapshot{
        {"schema", "baas.runtime-repositories.snapshot/v1"},
        {"generation", generation},
        {"repositories", nlohmann::json::array({
            {
                {"id", values[0].id},
                {"commit", values[0].commit},
                {"root", values[0].root},
                {"manifest", values[0].manifest},
                {"manifest_sha256", values[0].manifest_sha256},
            },
            {
                {"id", values[1].id},
                {"commit", values[1].commit},
                {"root", values[1].root},
                {"manifest", values[1].manifest},
                {"manifest_sha256", values[1].manifest_sha256},
            },
        })},
    };
    const nlohmann::json current{
        {"schema", "baas.runtime-repositories.current/v1"},
        {"generation", generation},
        {"snapshot", "snapshots/" + generation + ".json"},
    };
    write_runtime_repository_file(
        state_root / "snapshots" / (generation + ".json"), snapshot.dump());
    write_runtime_repository_file(state_root / "current.json", current.dump());
    for (const auto& value : values) {
        const auto repository_root = state_root / value.root;
        write_runtime_repository_file(
            repository_root / value.manifest, runtime_repository_manifest);
        write_runtime_repository_file(
            repository_root / "nested" / "payload.bin", "payload");
    }
    return generation;
}

void publish_runtime_repository_generation(
    const std::filesystem::path& project_root,
    const std::string_view generation)
{
    const nlohmann::json current{
        {"schema", "baas.runtime-repositories.current/v1"},
        {"generation", generation},
        {"snapshot", "snapshots/" + std::string{generation} + ".json"},
    };
    write_runtime_repository_file(
        project_root / ".baas-updater" / "runtime-repositories"
            / "current.json",
        current.dump());
}

[[nodiscard]] std::uint16_t unused_loopback_port()
{
    http::HttpHostRouterConfig router_config;
    router_config.service = {"port probe", "test"};
    router_config.health_snapshot = router::HealthSnapshot{};
    http::HttpHostConfig host_config;
    host_config.websocket.enabled = false;
    host_config.worker_count = 2;
    host_config.max_queued_requests = 4;
    host_config.ready_timeout = 1s;
    http::HttpHost probe{std::move(router_config), {}, host_config};
    const auto started = probe.start();
    const auto selected = started.port;
    probe.stop();
    if (!started.started || selected == 0) {
        throw std::runtime_error("failed to reserve an ephemeral port");
    }
    return selected;
}

[[nodiscard]] app::ServiceRunOptions options(
    const std::filesystem::path& root, const std::uint16_t port,
    std::string runtime_repository_generation = std::string(64, 'a'))
{
    return {
        root, "127.0.0.1", port, std::nullopt,
        std::move(runtime_repository_generation)};
}

[[nodiscard]] std::optional<protocol::TriggerIngressItem> ingress_item(
    const std::string_view command, const protocol::Timestamp timestamp,
    const std::string_view payload = "{}")
{
    const std::string json = std::string{"{\"type\":\"command\",\"command\":\""}
        + std::string{command} + "\",\"timestamp\":"
        + std::to_string(timestamp) + ",\"payload\":" + std::string{payload} + "}";
    protocol::TriggerIngress ingress;
    if (!ingress.receive_json_frame(json)) return std::nullopt;
    return ingress.take_ready();
}

template <typename Predicate>
[[nodiscard]] bool wait_until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

void test_information_and_pipe_fail_before_side_effect()
{
    check(app::service_application_executable_name == "BAAS_service"
              && app::service_application_wire_name == "BAAS Service"
              && app::service_application_executable_name
                  != app::service_application_wire_name,
          "executable and wire identities must remain explicitly separated");

    std::ostringstream output;
    std::ostringstream diagnostics;
    const std::string_view version_arguments[]{"--version"};
    check(app::run_service_application(version_arguments, output, diagnostics) == 0
              && output.str() == std::string{"BAAS_service "}
                    + std::string{app::service_application_version} + "\n"
              && diagnostics.str().empty(),
          "--version must be successful and stable");

    output.str("");
    output.clear();
    const std::string_view help_arguments[]{"--help"};
    check(app::run_service_application(help_arguments, output, diagnostics) == 0
              && output.str().find("Usage: BAAS_service") != std::string::npos
              && output.str().find("not enabled") != std::string::npos,
          "--help must describe only the implemented transports");

    TemporaryRoot root{"pipe"};
#if defined(_WIN32)
    const std::string pipe = R"(\\.\pipe\baas-service-app-test)";
#else
    const std::string pipe = (root.path / "service.sock").string();
#endif
    auto direct = options(root.path, unused_loopback_port());
    direct.pipe_name = pipe;
    const auto rejected = app::ServiceApplication::open(direct);
    check(rejected.error == app::ServiceApplicationError::pipe_transport_unavailable,
          "ServiceApplication must reject the incomplete pipe transport");
    check(!std::filesystem::exists(root.path / "config"),
          "pipe rejection must happen before auth files or lock directories exist");

    const std::vector<std::string> owned{
        "--project-root", root.path.string(), "--host", "127.0.0.1",
        "--port", std::to_string(unused_loopback_port()),
        "--runtime-repository-generation", std::string(64, 'a'),
        "--pipe-name", pipe,
    };
    std::vector<std::string_view> arguments;
    for (const auto& value : owned) arguments.emplace_back(value);
    output.str("");
    output.clear();
    diagnostics.str("");
    diagnostics.clear();
    check(app::run_service_application(arguments, output, diagnostics)
              == static_cast<int>(app::ServiceProcessExit::pipe_unavailable)
              && diagnostics.str().find("pipe_transport_unavailable")
                  != std::string::npos,
          "process boundary must return the stable non-zero pipe exit");
    check(!std::filesystem::exists(root.path / "config"),
          "pipe process rejection must remain side-effect free");
}

void test_real_loopback_lifecycle_trigger_and_persistence()
{
    TemporaryRoot root{"lifecycle"};
    add_remote_server_resource(root.path);
    const auto runtime_repository_generation =
        add_runtime_repository_activation(root.path);
    std::filesystem::create_directories(root.path / "config" / "source");
    {
        std::ofstream config(
            root.path / "config" / "source" / "config.json",
            std::ios::binary | std::ios::trunc);
        config << R"({"name":"Alpha","server":"日服"})";
        config.close();
        if (!config) throw std::runtime_error("config fixture write failed");
    }
    {
        std::ofstream event(
            root.path / "config" / "source" / "event.json",
            std::ios::binary | std::ios::trunc);
        event << "[]";
        event.close();
        if (!event) throw std::runtime_error("event fixture write failed");
    }
    {
        std::ofstream static_data(
            root.path / "config" / "static.json",
            std::ios::binary | std::ios::trunc);
        static_data << R"({"version":1,"source":"application-test"})";
        static_data.close();
        if (!static_data) throw std::runtime_error("static fixture write failed");
    }
    {
        std::ofstream setup(
            root.path / "setup.toml", std::ios::binary | std::ios::trunc);
        setup << "[general]\nchannel = 'stable'\n";
        setup.close();
        if (!setup) throw std::runtime_error("setup fixture write failed");
    }
    const auto port = unused_loopback_port();
    auto opened = app::ServiceApplication::open(
        options(root.path, port, runtime_repository_generation));
    check(static_cast<bool>(opened),
          "real production dependencies must compose ServiceApplication");
    if (!opened) return;
    auto application = std::move(opened.application);
    const auto runtime_repository_read_bundle =
        application->runtime_repository_read_bundle();
    check(runtime_repository_read_bundle
              && runtime_repository_read_bundle.get()
                  == application->runtime_repository_read_bundle().get()
              && runtime_repository_read_bundle->generation()
                  == runtime_repository_generation
              && runtime_repository_read_bundle->resources().generation()
                  == runtime_repository_generation
              && runtime_repository_read_bundle->scripts().generation()
                  == runtime_repository_generation
              && runtime_repository_read_bundle->resources().entries().size() == 1
              && runtime_repository_read_bundle->scripts().entries().size() == 1,
          "application must retain one validated resources/scripts bundle for its startup generation");
    if (runtime_repository_read_bundle) {
        const auto payload = runtime_repository_read_bundle->resources().read(
            "nested/payload.bin", 7);
        check(std::string{
                  reinterpret_cast<const char*>(payload.data()), payload.size()}
                  == "payload",
              "application repository capability must return manifest-verified owned bytes");
    }
    check(application->readiness_snapshot().state
              == router::HealthReadinessState::starting,
          "application must begin in explicit starting readiness");

    auto contender = app::ServiceApplication::open(
        options(root.path, unused_loopback_port(), runtime_repository_generation));
    check(contender.error == app::ServiceApplicationError::authentication_failed
              && contender.authentication_error
                  == baas::service::auth::AuthError::storage_failure,
          "second instance must fail on the persistent auth installation lock");

    const auto advanced_generation = add_runtime_repository_activation(
        root.path, '3', '4');
    check(advanced_generation != runtime_repository_generation
              && application->runtime_repository_read_bundle().get()
                  == runtime_repository_read_bundle.get()
              && application->runtime_repository_read_bundle()->generation()
                  == runtime_repository_generation,
          "advancing current must not replace the application's retained startup bundle");
    publish_runtime_repository_generation(
        root.path, runtime_repository_generation);

    const auto started = application->start_transport();
    check(started.started && started.port == port && application->port() == port,
          "production host must bind the exact CLI-selected port");
    if (!started.started) return;

    httplib::Client client{"127.0.0.1", port};
    client.set_connection_timeout(1s);
    client.set_read_timeout(2s);
    client.set_write_timeout(2s);
    const auto version = client.Get("/version");
    const std::string expected_version =
        std::string{R"({"api_version":1,"ok":true,"service":"BAAS Service","version":")"}
        + std::string{app::service_application_version} + R"("})";
    check(version && version->status == 200
              && version->body == expected_version,
          "real loopback /version must expose the Tauri wire identity");
    const auto starting = client.Get("/health");
    check(starting && starting->status == 503
              && starting->body.find("health_starting") != std::string::npos,
          "listener must expose 503 until the AuthOwner snapshot is ready");
    const auto auth_route = client.Post(
        "/auth/remember", "{}", "application/json");
    check(auth_route && auth_route->status == 400
              && auth_route->body.find("invalid_remember_request")
                  != std::string::npos,
          "real AuthHttpAdapter endpoint must be installed");

    check(application->publish_ready(),
          "AuthOwner-derived public state must publish readiness");
    const auto ready = client.Get("/health");
    const auto ready_json = ready ? nlohmann::json::parse(ready->body)
                                  : nlohmann::json{};
    const auto repository_pointer =
        nlohmann::json::json_pointer{"/statuses/runtime/repository"};
    const auto* repository_health =
        ready && ready_json.contains(repository_pointer)
        ? &ready_json.at(repository_pointer) : nullptr;
    check(ready && ready->status == 200
              && ready->body.find(R"("phase":"ready")") != std::string::npos
              && ready->body.find(R"("remote":"desktop_only")") != std::string::npos
              && ready->body.find(R"("server_sign_public_key":")")
                  != std::string::npos,
          "ready health must contain runtime policy and real auth public state");
    check(repository_health && repository_health->is_object()
              && repository_health->size() == 2
              && repository_health->value("phase", "") == "pinned"
              && repository_health->value("generation", "")
                  == runtime_repository_generation
              && ready->body.find(".baas-updater") == std::string::npos
              && ready->body.find("objects/resources") == std::string::npos
              && ready->body.find("manifest.json") == std::string::npos,
          "health must expose only pinned phase and generation, never paths or repository metadata");
    const auto public_key = application->readiness_snapshot()
                                .health.auth.server_sign_public_key;
    check(!public_key.empty(), "sodium AuthOwner must publish a signing key");

    auto session = std::make_shared<protocol::TriggerSession>();
    auto connection = application->trigger_executor()->connect(session);
    auto copy = ingress_item("copy_config", 1, R"({"id":"source"})");
    check(copy.has_value(), "copy_config production ingress fixture must parse");
    if (copy) {
        const auto submitted = connection.submit(std::move(*copy));
        check(static_cast<bool>(submitted),
              "application dispatcher must install the real copy_config handler");
        std::optional<protocol::SendLease> lease;
        check(wait_until([&] {
                  auto begun = session->begin_send();
                  if (!begun) return false;
                  lease = std::move(*begun.lease);
                  return true;
              }),
              "copy_config must complete through the production executor");
        if (lease) {
            const auto envelope = nlohmann::json::parse(lease->batch().json());
            const bool valid_copy_response = envelope.value("status", "") == "ok"
                && envelope.contains("data") && envelope.at("data").is_object()
                && envelope.at("data").contains("serial")
                && envelope.at("data").at("serial").is_string()
                && envelope.at("data").contains("name")
                && envelope.at("data").at("name").is_string();
            check(valid_copy_response,
                  "copy_config production response must be successful");
            if (valid_copy_response) {
                const auto& data = envelope.at("data");
                const auto serial = data.at("serial").get<std::string>();
                check(data.at("name") == "Alpha_copy"
                      && std::filesystem::is_regular_file(
                          root.path / "config" / serial / "event.json"),
                      "BAAS_service must execute a real durable configuration copy");
            }
            check(static_cast<bool>(connection.complete_send(*lease)),
                  "copy_config terminal must complete through executor ownership");
        }
    }

    auto add = ingress_item(
        "add_config_remote", 4,
        R"({"name":"Application profile","server":"日服"})");
    check(add.has_value(), "add_config production ingress fixture must parse");
    if (add) {
        const auto submitted = connection.submit(std::move(*add));
        check(static_cast<bool>(submitted),
              "application dispatcher must install the real add_config handler");
        std::optional<protocol::SendLease> lease;
        check(wait_until([&] {
                  auto begun = session->begin_send();
                  if (!begun) return false;
                  lease = std::move(*begun.lease);
                  return true;
              }),
              "add_config must complete through the production executor");
        if (lease) {
            const auto envelope = nlohmann::json::parse(lease->batch().json());
            const bool valid_add = envelope.value("status", "") == "ok"
                && envelope.value("command", "") == "add_config_remote"
                && envelope.at("data").is_object()
                && envelope.at("data").size() == 1
                && envelope.at("data").at("serial").is_string();
            check(valid_add,
                  "add_config production response must contain only serial");
            if (valid_add) {
                const auto serial = envelope.at("data").at("serial")
                                        .get<std::string>();
                const auto directory = root.path / "config" / serial;
                const auto config = nlohmann::json::parse(std::ifstream(
                    directory / "config.json"));
                check(config["name"] == "Application profile"
                          && config["server"] == "日服"
                          && std::filesystem::is_regular_file(
                              directory / "event.json")
                          && std::filesystem::is_regular_file(
                              directory / "switch.json"),
                      "BAAS_service must execute a real durable config create");
            }
            check(static_cast<bool>(connection.complete_send(*lease)),
                  "add_config terminal must complete through executor ownership");
        }
    }

    auto status = ingress_item("status", 2);
    check(status.has_value(), "status ingress fixture must parse");
    if (status) {
        const auto submitted = connection.submit(std::move(*status));
        check(static_cast<bool>(submitted),
              "application must route status into its real executor");
        std::optional<protocol::SendLease> lease;
        check(wait_until([&] {
                  auto begun = session->begin_send();
                  if (!begun) return false;
                  lease = std::move(*begun.lease);
                  return true;
              }),
              "status trigger must produce a bounded terminal response");
        if (lease) {
            check(lease->batch().json()
                      == R"({"type":"command_response","command":"status","status":"ok","data":{},"timestamp":2})",
                  "status trigger must expose the production provider snapshot");
            check(static_cast<bool>(connection.complete_send(*lease)),
                  "status terminal must complete through executor ownership");
        }
    }

    {
        EnvironmentGuard android("BAAS_ANDROID");
        EnvironmentGuard serial("BAAS_ANDROID_ADB_SERIAL");
        set_environment("BAAS_ANDROID", "true");
        set_environment("BAAS_ANDROID_ADB_SERIAL", "");
        auto detect = ingress_item("detect_adb", 3);
        check(detect.has_value(), "detect_adb production ingress fixture must parse");
        if (detect) {
            const auto submitted = connection.submit(std::move(*detect));
            check(static_cast<bool>(submitted),
                  "application dispatcher must install the real detect_adb handler");
            std::optional<protocol::SendLease> lease;
            check(wait_until([&] {
                      auto begun = session->begin_send();
                      if (!begun) return false;
                      lease = std::move(*begun.lease);
                      return true;
                  }),
                  "detect_adb must complete through the production executor");
            if (lease) {
                check(lease->batch().json()
                          == R"({"type":"command_response","command":"detect_adb","status":"ok","data":{"addresses":["127.0.0.1:5555","localhost:5555"]},"timestamp":3})",
                      "production detect_adb must preserve the Python Android envelope");
                check(static_cast<bool>(connection.complete_send(*lease)),
                      "detect_adb terminal must complete through executor ownership");
            }
        }
    }
    static_cast<void>(connection.close());

    const auto shutdown = client.Post("/shutdown", "", "application/json");
    check(shutdown && shutdown->status == 202,
          "HTTP shutdown must only publish main-thread intent");
    const auto reason = application->wait_for_shutdown(2s);
    check(reason == app::ServiceShutdownReason::http_request,
          "main owner must observe the HTTP shutdown reason");
    application->stop();
    check(application->port() == 0
              && application->readiness_snapshot().state
                  == router::HealthReadinessState::failed
              && application->trigger_executor()->stats().stopping,
          "reverse teardown must withdraw readiness, stop host, and drain executor");
    client.stop();
    application.reset();

    auto restarted = app::ServiceApplication::open(
        options(root.path, unused_loopback_port(), runtime_repository_generation));
    check(static_cast<bool>(restarted),
          "auth installation lock must release after orderly teardown");
    if (restarted) {
        const auto restart = restarted.application->start_transport();
        check(restart.started && restarted.application->publish_ready(),
              "persisted auth state must reopen and become ready");
        if (restart.started) {
            check(restarted.application->readiness_snapshot()
                          .health.auth.server_sign_public_key == public_key,
                  "restart must preserve the persisted signing identity");
        }
        restarted.application->stop();
    }
}

void test_port_conflict_reports_nonzero_start_failure()
{
    http::HttpHostRouterConfig blocker_router;
    blocker_router.service = {"port blocker", "test"};
    blocker_router.health_snapshot = router::HealthSnapshot{};
    http::HttpHostConfig blocker_config;
    blocker_config.websocket.enabled = false;
    blocker_config.worker_count = 2;
    blocker_config.max_queued_requests = 4;
    blocker_config.ready_timeout = 1s;
    http::HttpHost blocker{std::move(blocker_router), {}, blocker_config};
    const auto occupied = blocker.start();
    check(occupied.started && occupied.port != 0,
          "port-conflict fixture must occupy a real loopback port");
    if (!occupied.started) return;

    TemporaryRoot root{"conflict"};
    add_remote_server_resource(root.path);
    const auto generation = add_runtime_repository_activation(root.path);
    auto opened = app::ServiceApplication::open(
        options(root.path, occupied.port, generation));
    check(static_cast<bool>(opened),
          "port selection must not bind during composition");
    if (opened) {
        const auto failed = opened.application->start_transport();
        check(!failed.started
                  && failed.error == http::HttpHostStartError::bind_failed
                  && opened.application->readiness_snapshot().state
                      == router::HealthReadinessState::failed,
              "occupied CLI port must fail non-zero and publish failed readiness");
    }
    opened.application.reset();

    const std::vector<std::string> owned{
        "--project-root", root.path.string(), "--host", "127.0.0.1",
        "--port", std::to_string(occupied.port),
        "--runtime-repository-generation", generation,
    };
    std::vector<std::string_view> arguments;
    for (const auto& value : owned) arguments.emplace_back(value);
    std::ostringstream output;
    std::ostringstream diagnostics;
    check(app::run_service_application(arguments, output, diagnostics)
              == static_cast<int>(app::ServiceProcessExit::host_start)
              && diagnostics.str().find("start:bind_failed") != std::string::npos,
          "process boundary must map fixed-port conflict to stable exit 6");
    blocker.stop();
}

void test_readiness_requires_real_runtime_resources()
{
    TemporaryRoot root{"missing-resources"};
    add_remote_server_resource(root.path);
    const auto generation = add_runtime_repository_activation(root.path);
    auto opened = app::ServiceApplication::open(
        options(root.path, unused_loopback_port(), generation));
    check(static_cast<bool>(opened),
          "missing resources do not cause optimistic failure during composition");
    if (!opened) return;
    const auto started = opened.application->start_transport();
    check(started.started, "missing-resource fixture starts transport");
    if (!started.started) return;
    check(!opened.application->publish_ready()
              && opened.application->readiness_snapshot().state
                  == router::HealthReadinessState::failed,
          "application readiness fails closed without real static and setup data");
}

void test_missing_repository_fails_before_composition()
{
    TemporaryRoot root{"repository-missing"};
    add_remote_server_resource(root.path);
    const auto opened = app::ServiceApplication::open(
        options(root.path, unused_loopback_port()));
    check(opened.error
              == app::ServiceApplicationError::runtime_repository_generation_mismatch
              && app::service_application_error_name(opened.error)
                  == "runtime_repository_generation_mismatch"
              && !opened.application,
          "missing current must fail the expected generation contract");
    check(!std::filesystem::exists(root.path / "config"),
          "missing current must fail before auth and resource-store side effects");

    const std::vector<std::string> owned{
        "--project-root", root.path.string(), "--host", "127.0.0.1",
        "--port", std::to_string(unused_loopback_port()),
        "--runtime-repository-generation", std::string(64, 'a'),
    };
    std::vector<std::string_view> arguments;
    for (const auto& value : owned) arguments.emplace_back(value);
    std::ostringstream output;
    std::ostringstream diagnostics;
    check(app::run_service_application(arguments, output, diagnostics)
              == static_cast<int>(app::ServiceProcessExit::composition)
              && diagnostics.str().find("runtime_repository_generation_mismatch")
                  != std::string::npos,
          "process boundary must report missing expected generation as composition exit 5");
}

void test_repository_generation_mismatch_fails_before_composition()
{
    TemporaryRoot root{"repository-mismatch"};
    add_remote_server_resource(root.path);
    const auto installed = add_runtime_repository_activation(root.path);
    const std::string expected(64, installed == std::string(64, 'f') ? 'e' : 'f');
    const auto opened = app::ServiceApplication::open(
        options(root.path, unused_loopback_port(), expected));
    check(opened.error
              == app::ServiceApplicationError::runtime_repository_generation_mismatch
              && !opened.application,
          "a valid activation for another generation must fail service composition");
    check(!std::filesystem::exists(root.path / "config"),
          "generation mismatch must fail before auth and resource-store side effects");

    const auto invalid = app::ServiceApplication::open(
        options(root.path, unused_loopback_port(), std::string(64, 'A')));
    check(invalid.error == app::ServiceApplicationError::invalid_options
              && !invalid.application,
          "direct application callers must supply a canonical expected generation");
}

void test_invalid_runtime_repository_fails_before_composition()
{
    TemporaryRoot root{"invalid-runtime-repository"};
    add_remote_server_resource(root.path);
    write_runtime_repository_file(
        root.path / ".baas-updater" / "runtime-repositories" / "current.json",
        R"({"schema":"baas.runtime-repositories.current/v1","generation":"tampered"})");
    const auto opened = app::ServiceApplication::open(
        options(root.path, unused_loopback_port()));
    check(opened.error == app::ServiceApplicationError::runtime_repository_invalid
              && app::service_application_error_name(opened.error)
                  == "runtime_repository_invalid"
              && !opened.application,
          "malformed or tampered activation must fail service composition");
    check(!std::filesystem::exists(root.path / "config"),
          "invalid activation must fail before auth and resource-store side effects");

    const std::vector<std::string> owned{
        "--project-root", root.path.string(), "--host", "127.0.0.1",
        "--port", std::to_string(unused_loopback_port()),
        "--runtime-repository-generation", std::string(64, 'a'),
    };
    std::vector<std::string_view> arguments;
    for (const auto& value : owned) arguments.emplace_back(value);
    std::ostringstream output;
    std::ostringstream diagnostics;
    check(app::run_service_application(arguments, output, diagnostics)
              == static_cast<int>(app::ServiceProcessExit::composition)
              && diagnostics.str().find("runtime_repository_invalid")
                  != std::string::npos,
          "process boundary must report invalid activation as composition exit 5");
}

void test_missing_remote_resource_fails_before_composition_side_effects()
{
    TemporaryRoot root{"missing-remote"};
    const auto generation = add_runtime_repository_activation(root.path);
    const auto opened = app::ServiceApplication::open(
        options(root.path, unused_loopback_port(), generation));
    check(opened.error == app::ServiceApplicationError::remote_resource_unavailable,
          "desktop composition must fail closed without the ws-scrcpy jar");
    check(!std::filesystem::exists(root.path / "config"),
          "missing remote resource must fail before auth or resource-store effects");
}

void test_invalid_repository_payload_fails_before_remote_and_composition()
{
    TemporaryRoot root{"invalid-runtime-payload"};
    const auto generation = add_runtime_repository_activation(root.path);
    write_runtime_repository_file(
        root.path / ".baas-updater" / "runtime-repositories" / "objects"
            / "scripts" / std::string(40, '2') / "nested" / "payload.bin",
        "tamper!");
    const auto opened = app::ServiceApplication::open(
        options(root.path, unused_loopback_port(), generation));
    check(opened.error == app::ServiceApplicationError::runtime_repository_invalid
              && !opened.application,
          "payload digest mismatch must fail before the later missing remote-resource check");
    check(!std::filesystem::exists(root.path / "config"),
          "payload admission failure must precede auth, resource-store, worker, and socket effects");
}

}  // namespace

int main()
{
    try {
        test_information_and_pipe_fail_before_side_effect();
        test_real_loopback_lifecycle_trigger_and_persistence();
        test_port_conflict_reports_nonzero_start_failure();
        test_readiness_requires_real_runtime_resources();
        test_missing_repository_fails_before_composition();
        test_repository_generation_mismatch_fails_before_composition();
        test_invalid_runtime_repository_fails_before_composition();
        test_invalid_repository_payload_fails_before_remote_and_composition();
        test_missing_remote_resource_fails_before_composition_side_effects();
    } catch (const std::exception& error) {
        std::cerr << "UNEXPECTED: " << error.what() << '\n';
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << " service application test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "service application tests passed\n";
    return EXIT_SUCCESS;
}
