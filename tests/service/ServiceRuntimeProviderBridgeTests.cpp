#include "service/app/ServiceRuntimeProviderBridge.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;
using baas::service::adapters::FileResourceStore;
using baas::service::app::ProductionProviderBackend;
using baas::service::app::ServiceRuntimeLogEntry;
using baas::service::app::ServiceRuntimeProviderBridge;
using baas::service::app::ServiceRuntimeProviderBridgeDependencies;
using baas::service::app::ServiceRuntimeProviderBridgeError;
using baas::service::channels::ProviderBackendError;
using baas::service::channels::ResourceKey;
using baas::service::channels::ResourceStoreError;
using baas::service::channels::SyncResource;
using Json = nlohmann::json;

int failures{};

void check(const bool condition, const std::string& message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << bytes;
    output.close();
    if (!output) throw std::runtime_error("fixture write failed");
}

struct TempProject {
    TempProject()
    {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        root = std::filesystem::temp_directory_path()
            / ("baas-runtime-provider-" + unique);
        std::filesystem::create_directories(root);
    }

    ~TempProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    void add_config(const std::string& id) const
    {
        write_bytes(root / "config" / id / "config.json",
                    R"({"name":"Alpha","enabled":true})");
        write_bytes(root / "config" / id / "event.json",
                    R"({"events":["start"]})");
    }

    void add_globals(const int version = 1) const
    {
        write_bytes(root / "config" / "gui.json", R"({"theme":"dark"})");
        write_bytes(root / "config" / "static.json",
                    "{\"version\":" + std::to_string(version) + "}");
        write_bytes(root / "setup.toml", "[general]\nchannel = 'stable'\n");
    }

    std::filesystem::path root;
};

ServiceRuntimeProviderBridgeDependencies dependencies()
{
    ServiceRuntimeProviderBridgeDependencies result;
    result.clock = [] { return 1234.5; };
    result.watcher.poll_interval = 20ms;
    return result;
}

template <class Predicate>
bool eventually(Predicate predicate, const std::chrono::milliseconds timeout = 2s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return predicate();
}

std::optional<bool> initialized(const std::shared_ptr<ProductionProviderBackend>& provider)
{
    auto result = provider->all_data_initialized({});
    return result ? *result.value : std::optional<bool>{};
}

void test_real_initialization_and_external_refresh()
{
    TempProject project;
    project.add_config("alpha");
    project.add_globals();
    auto store = std::make_shared<FileResourceStore>(project.root);
    auto provider = std::make_shared<ProductionProviderBackend>();
    ServiceRuntimeProviderBridge bridge(store, provider, dependencies());
    std::atomic<int> status_publications{};
    auto status_subscription = provider->subscribe_status(
        [&status_publications](std::string) {
            status_publications.fetch_add(1, std::memory_order_relaxed);
        });
    check(static_cast<bool>(status_subscription),
          "status publication observer subscribes");
    check(bridge.publish_log({"global", "info", "before start"})
              == ProviderBackendError::internal_error,
          "runtime log admission is closed before start");

    check(bridge.start() == ServiceRuntimeProviderBridgeError::none,
          "complete real project initializes synchronously");
    check(bridge.running() && bridge.initialized() && initialized(provider) == true,
          "watcher and provider expose initialized only after the full scan");
    auto initial_static = provider->static_snapshot({});
    check(initial_static
              && Json::parse(initial_static->data_json)["version"] == 1,
          "provider static snapshot is the real config/static.json document");
    const auto publications_after_start =
        status_publications.load(std::memory_order_relaxed);
    std::this_thread::sleep_for(80ms);
    check(status_publications.load(std::memory_order_relaxed)
              == publications_after_start,
          "unchanged watcher passes do not rebroadcast initialized state");
    auto setup = store->pull({SyncResource::setup_toml, std::nullopt}, {});
    check(setup && Json::parse(setup->data_json)["channel"] == "stable",
          "initial scan loads and validates the real setup projection");

    std::filesystem::remove(project.root / "config" / "gui.json");
    check(eventually([&] {
        auto gui = store->pull({SyncResource::gui, std::nullopt}, {});
        return !gui && gui.error == ResourceStoreError::not_found;
    }) && initialized(provider) == true,
          "optional gui deletion invalidates cache without failing readiness");

    write_bytes(project.root / "config" / "static.json", R"({"version":2})");
    check(eventually([&] {
        auto snapshot = provider->static_snapshot({});
        return snapshot && Json::parse(snapshot->data_json)["version"] == 2;
    }), "watcher refreshes changed static data into provider");

    std::filesystem::remove(project.root / "config" / "static.json");
    check(eventually([&] { return initialized(provider) == false; }),
          "required resource deletion fails closed");
    project.add_globals(3);
    check(eventually([&] {
        auto snapshot = provider->static_snapshot({});
        return initialized(provider) == true && snapshot
            && Json::parse(snapshot->data_json)["version"] == 3;
    }), "a subsequent complete real scan recovers initialization");

    bridge.stop();
    check(!bridge.running() && !bridge.initialized() && initialized(provider) == false,
          "stop joins watcher and resets initialized state");
}

void test_config_rescan_invalidates_removed_cache()
{
    TempProject project;
    project.add_config("alpha");
    project.add_globals();
    auto store = std::make_shared<FileResourceStore>(project.root);
    auto provider = std::make_shared<ProductionProviderBackend>();
    ServiceRuntimeProviderBridge bridge(store, provider, dependencies());
    check(bridge.start() == ServiceRuntimeProviderBridgeError::none,
          "config rescan fixture starts");
    auto cached = store->pull({SyncResource::config, std::string{"alpha"}}, {});
    check(static_cast<bool>(cached), "config is cached before external removal");

    std::filesystem::remove_all(project.root / "config" / "alpha");
    check(eventually([&] {
        auto list = store->config_list({});
        return list && Json::parse(list->data_json).empty();
    }), "config-list rescan observes external directory removal");
    check(eventually([&] {
        auto pull = store->pull({SyncResource::config, std::string{"alpha"}}, {});
        return !pull && pull.error == ResourceStoreError::not_found;
    }), "removed config cannot survive through the resource cache");
    check(initialized(provider) == true,
          "a valid empty config list remains a fully initialized project");
    bridge.stop();
}

void test_failure_recovery_bounded_logs_and_stop_race()
{
    TempProject project;
    project.add_config("alpha");
    write_bytes(project.root / "setup.toml", "[general]\nchannel = 'stable'\n");
    auto store = std::make_shared<FileResourceStore>(project.root);
    auto provider = std::make_shared<ProductionProviderBackend>();
    ServiceRuntimeProviderBridge bridge(store, provider, dependencies());

    check(bridge.start() == ServiceRuntimeProviderBridgeError::initial_data_unavailable,
          "missing required static data is reported without fake initialization");
    check(bridge.running() && initialized(provider) == false,
          "watcher remains active to recover from an incomplete startup");
    write_bytes(project.root / "config" / "static.json", R"({"version":7})");
    check(eventually([&] { return initialized(provider) == true; }),
          "watcher recovers when the missing real data arrives");

    check(bridge.publish_log({"config:alpha", "debug", "runtime message"})
              == ProviderBackendError::none,
          "bounded runtime logs enter the shared provider backend");
    check(bridge.publish_log(ServiceRuntimeLogEntry{
              "global", "info", std::string(4'097, 'x')})
              == ProviderBackendError::capacity,
          "runtime log message capacity is enforced before publication");
    auto logs = provider->logs_full({});
    check(logs && Json::parse(logs->entries_json).size() >= 2,
          "provider retains bridge lifecycle and runtime log entries");

    std::atomic<bool> stop_writer{false};
    std::thread writer([&] {
        int version = 8;
        while (!stop_writer.load(std::memory_order_acquire)) {
            write_bytes(project.root / "config" / "static.json",
                        "{\"version\":" + std::to_string(version++) + "}");
            std::this_thread::yield();
        }
    });
    std::this_thread::sleep_for(30ms);
    bridge.stop();
    stop_writer.store(true, std::memory_order_release);
    writer.join();
    check(initialized(provider) == false,
          "concurrent filesystem churn cannot republish initialized after stop");
    check(bridge.publish_log({"global", "info", "after stop"})
              == ProviderBackendError::internal_error,
          "stop closes runtime log admission");
}

void test_reentrant_provider_callback_can_stop_watcher()
{
    TempProject project;
    project.add_config("alpha");
    project.add_globals();
    auto store = std::make_shared<FileResourceStore>(project.root);
    auto provider = std::make_shared<ProductionProviderBackend>();
    ServiceRuntimeProviderBridge bridge(store, provider, dependencies());
    check(bridge.start() == ServiceRuntimeProviderBridgeError::none,
          "reentrant-stop fixture starts");

    std::atomic<bool> armed{false};
    auto subscription = provider->subscribe_status(
        [&bridge, &armed](std::string) {
            if (armed.load(std::memory_order_acquire)) bridge.stop();
        });
    check(static_cast<bool>(subscription),
          "reentrant-stop observer subscribes");
    armed.store(true, std::memory_order_release);
    std::filesystem::remove(project.root / "setup.toml");
    check(eventually([&] { return !bridge.running(); }),
          "synchronous Provider callback may stop its watcher thread safely");
    check(initialized(provider) == false,
          "reentrant watcher stop leaves final initialized state false");
}

void test_reentrant_provider_callback_can_destroy_last_bridge()
{
    TempProject project;
    project.add_config("alpha");
    project.add_globals();
    auto store = std::make_shared<FileResourceStore>(project.root);
    auto provider = std::make_shared<ProductionProviderBackend>();
    auto bridge = std::make_unique<ServiceRuntimeProviderBridge>(
        store, provider, dependencies());
    check(bridge->start() == ServiceRuntimeProviderBridgeError::none,
          "reentrant-destroy fixture starts");

    std::atomic<bool> armed{false};
    auto subscription = provider->subscribe_status(
        [&bridge, &armed](std::string) {
            if (armed.load(std::memory_order_acquire) && bridge) bridge.reset();
        });
    check(static_cast<bool>(subscription),
          "reentrant-destroy observer subscribes");
    armed.store(true, std::memory_order_release);
    std::filesystem::remove(project.root / "setup.toml");
    check(eventually([&] { return !bridge; }),
          "worker callback may destroy the last bridge owner safely");
    check(eventually([&] { return initialized(provider) == false; }),
          "reentrant bridge destruction leaves initialized false");
}

void test_external_stop_waits_for_detached_reentrant_callback()
{
    TempProject project;
    project.add_config("alpha");
    project.add_globals();
    auto store = std::make_shared<FileResourceStore>(project.root);
    auto provider = std::make_shared<ProductionProviderBackend>();
    ServiceRuntimeProviderBridge bridge(store, provider, dependencies());
    check(bridge.start() == ServiceRuntimeProviderBridgeError::none,
          "detached-barrier fixture starts");

    std::atomic<bool> armed{false};
    std::atomic<bool> callback_entered{false};
    std::atomic<bool> release_callback{false};
    auto subscription = provider->subscribe_status(
        [&bridge, &armed, &callback_entered, &release_callback](std::string) {
            if (!armed.load(std::memory_order_acquire)) return;
            bridge.stop();
            callback_entered.store(true, std::memory_order_release);
            while (!release_callback.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        });
    check(static_cast<bool>(subscription),
          "detached-barrier observer subscribes");
    armed.store(true, std::memory_order_release);
    std::filesystem::remove(project.root / "setup.toml");
    if (!eventually([&] {
            return callback_entered.load(std::memory_order_acquire);
        })) {
        check(false, "reentrant callback enters before barrier check");
        release_callback.store(true, std::memory_order_release);
        bridge.stop();
        return;
    }

    std::atomic<bool> external_stop_returned{false};
    std::thread external_stop([&] {
        bridge.stop();
        external_stop_returned.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(50ms);
    check(!external_stop_returned.load(std::memory_order_acquire),
          "external stop remains a barrier while detached callback is active");
    release_callback.store(true, std::memory_order_release);
    external_stop.join();
    check(external_stop_returned.load(std::memory_order_acquire)
              && !bridge.running(),
          "external stop returns only after detached worker completion");
}

}  // namespace

int main()
{
    test_real_initialization_and_external_refresh();
    test_config_rescan_invalidates_removed_cache();
    test_failure_recovery_bounded_logs_and_stop_race();
    test_reentrant_provider_callback_can_stop_watcher();
    test_reentrant_provider_callback_can_destroy_last_bridge();
    test_external_stop_waits_for_detached_reentrant_callback();
    if (failures == 0) {
        std::cout << "Service runtime provider bridge tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
