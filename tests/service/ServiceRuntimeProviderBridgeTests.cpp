#include "service/app/ServiceRuntimeProviderBridge.h"
#include "TestConfigurationDefaults.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

using namespace std::chrono_literals;
using baas::service::adapters::FileResourceStore;
using baas::service::adapters::FileResourceWatcher;
using baas::service::app::ProductionProviderBackend;
using baas::service::app::ServiceRuntimeLogEntry;
using baas::service::app::ServiceRuntimeProviderBridge;
using baas::service::app::ServiceRuntimeProviderBridgeDependencies;
using baas::service::app::ServiceRuntimeProviderBridgeError;
using baas::service::channels::ProviderBackendError;
using baas::service::channels::ResourceKey;
using baas::service::channels::ResourceStoreError;
using baas::service::channels::ResourceUpdate;
using baas::service::channels::SyncResource;
using Json = nlohmann::json;
namespace test_defaults = baas::service::test;

int failures{};

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
        static std::atomic<std::uint64_t> sequence{};
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        root = std::filesystem::temp_directory_path()
            / ("baas-runtime-provider-"
               + std::to_string(current_process_id()) + "-" + unique + "-"
               + std::to_string(sequence.fetch_add(1)));
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
    auto store = std::make_shared<FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    auto provider = std::make_shared<ProductionProviderBackend>();
    ServiceRuntimeProviderBridge bridge(store, provider, dependencies());
    std::atomic<int> status_publications{};
    std::atomic<bool> gui_remove_published{};
    auto update_subscription = store->subscribe_updates(
        [&gui_remove_published](ResourceUpdate update) {
            if (update.key.resource != SyncResource::gui) return;
            try {
                const auto operations = Json::parse(update.operations_json);
                if (operations.size() == 1
                    && operations[0]["op"] == "remove"
                    && operations[0]["path"] == "") {
                    gui_remove_published.store(true, std::memory_order_release);
                }
            } catch (...) {
            }
        });
    auto status_subscription = provider->subscribe_status(
        [&status_publications](std::string) {
            status_publications.fetch_add(1, std::memory_order_relaxed);
        });
    check(update_subscription && status_subscription,
          "resource and status publication observers subscribe");
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
        return gui_remove_published.load(std::memory_order_acquire)
            && !gui && gui.error == ResourceStoreError::not_found;
    }), "optional gui deletion publishes its cache invalidation");

    write_bytes(project.root / "config" / "static.json", R"({"version":2})");
    check(eventually([&] {
        auto snapshot = provider->static_snapshot({});
        return snapshot && Json::parse(snapshot->data_json)["version"] == 2
            && initialized(provider) == true;
    }) && status_publications.load(std::memory_order_relaxed)
              == publications_after_start,
          "optional gui deletion completes a ready scan without status churn");

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
    auto store = std::make_shared<FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
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

void test_created_config_reaches_watcher_subscribers_and_ready_provider()
{
    TempProject project;
    project.add_config("alpha");
    project.add_globals();
    auto store = std::make_shared<FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    auto provider = std::make_shared<ProductionProviderBackend>();
    std::mutex updates_mutex;
    std::vector<baas::service::channels::ResourceUpdate> updates;
    auto subscription = store->subscribe_updates(
        [&updates_mutex, &updates](baas::service::channels::ResourceUpdate update) {
            std::lock_guard lock(updates_mutex);
            updates.push_back(std::move(update));
        });
    ServiceRuntimeProviderBridge bridge(store, provider, dependencies());
    check(subscription
              && bridge.start() == ServiceRuntimeProviderBridgeError::none,
          "create visibility fixture starts with a real watcher subscriber");
    {
        std::lock_guard lock(updates_mutex);
        updates.clear();
    }
    const auto created = store->create_config("new profile", "日服", {});
    check(static_cast<bool>(created), "production store create commits");
    if (!created) {
        bridge.stop();
        return;
    }
    check(eventually([&] {
        bool config{};
        bool event{};
        std::lock_guard lock(updates_mutex);
        for (const auto& update : updates) {
            if (!update.key.resource_id
                || *update.key.resource_id != created.serial) continue;
            const auto operations = Json::parse(update.operations_json);
            if (operations.size() != 1 || operations[0]["op"] != "replace"
                || operations[0]["path"] != "") continue;
            config = config || update.key.resource == SyncResource::config;
            event = event || update.key.resource == SyncResource::event;
        }
        return config && event;
    }), "watcher publishes new config and event within its poll interval");
    const auto config = store->pull(
        {SyncResource::config, created.serial}, {});
    const auto event = store->pull(
        {SyncResource::event, created.serial}, {});
    check(config && event && initialized(provider) == true
              && Json::parse(config->data_json)["name"] == "new profile"
              && Json::parse(event->data_json).size() == 26,
          "created pair is pull-visible while provider readiness stays true");
    bridge.stop();
}

void test_config_pair_replacement_advances_at_capacity()
{
    TempProject project;
    project.add_config("alpha");
    write_bytes(project.root / "config" / "static.json", R"({"version":1})");
    write_bytes(project.root / "setup.toml", "[general]\nchannel = 'stable'\n");
    baas::service::channels::ResourceStoreLimits limits;
    limits.max_resources = 4;
    auto store = std::make_shared<FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults(), limits);
    auto provider = std::make_shared<ProductionProviderBackend>();
    std::mutex updates_mutex;
    std::vector<baas::service::channels::ResourceUpdate> updates;
    auto subscription = store->subscribe_updates(
        [&updates_mutex, &updates](baas::service::channels::ResourceUpdate update) {
            std::lock_guard lock(updates_mutex);
            updates.push_back(std::move(update));
        });
    check(static_cast<bool>(subscription),
          "capacity replacement observer subscribes");
    ServiceRuntimeProviderBridge bridge(store, provider, dependencies());
    check(bridge.start() == ServiceRuntimeProviderBridgeError::none,
          "capacity replacement fixture fills all four cache slots");
    {
        std::lock_guard lock(updates_mutex);
        updates.clear();
    }
    const auto removed_pair_published = [&] {
        bool removed_config{};
        bool removed_event{};
        std::lock_guard lock(updates_mutex);
        for (const auto& update : updates) {
            const auto operations = Json::parse(update.operations_json);
            if (!update.key.resource_id || *update.key.resource_id != "alpha"
                || operations.empty() || operations[0]["op"] != "remove") {
                continue;
            }
            removed_config = removed_config
                || update.key.resource == SyncResource::config;
            removed_event = removed_event
                || update.key.resource == SyncResource::event;
        }
        return removed_config && removed_event;
    };
    const auto replacement_pair_published = [&] {
        bool replaced_config{};
        bool replaced_event{};
        std::lock_guard lock(updates_mutex);
        for (const auto& update : updates) {
            const auto operations = Json::parse(update.operations_json);
            if (!update.key.resource_id || *update.key.resource_id != "beta"
                || operations.empty() || operations[0]["op"] != "replace") {
                continue;
            }
            replaced_config = replaced_config
                || update.key.resource == SyncResource::config;
            replaced_event = replaced_event
                || update.key.resource == SyncResource::event;
        }
        return replaced_config && replaced_event;
    };

    // Keep alpha/config.json physically present while breaking its pair, then
    // add beta. Both retired keys must be invalidated before beta admission.
    std::filesystem::remove(project.root / "config" / "alpha" / "event.json");
    project.add_config("beta");
    check(eventually([&] {
        return removed_pair_published() && replacement_pair_published();
    }), "A-to-B scan publishes both retirement and admission pairs");

    // The update callbacks run before scan_completed(). Advance a distinct
    // provider-owned value only after all four pair updates were observed, so
    // the static snapshot is a deterministic completion barrier for this scan
    // (or the immediately following scan), not stale readiness from the prior
    // generation.
    write_bytes(project.root / "config" / "static.json", R"({"version":2})");
    check(eventually([&] {
        const auto snapshot = provider->static_snapshot({});
        return initialized(provider) == true && snapshot
            && Json::parse(snapshot->data_json)["version"] == 2;
    }), "A-to-B config replacement completes a ready provider generation");

    auto beta_config = store->pull(
        {SyncResource::config, std::string{"beta"}}, {});
    auto beta_event = store->pull(
        {SyncResource::event, std::string{"beta"}}, {});

    auto stale_alpha = store->pull(
        {SyncResource::config, std::string{"alpha"}}, {});
    check(beta_config && beta_event && removed_pair_published()
              && replacement_pair_published() && !stale_alpha
              && stale_alpha.error == ResourceStoreError::capacity,
          "removed id publishes both root removes even when one sibling remains");
    bridge.stop();
}

void test_create_and_copy_reserve_watcher_cache_capacity()
{
    TempProject project;
    project.add_config("alpha");
    write_bytes(project.root / "config" / "static.json", R"({"version":1})");
    write_bytes(project.root / "setup.toml", "[general]\nchannel = 'stable'\n");
    baas::service::channels::ResourceStoreLimits limits;
    limits.max_resources = 4;
    auto store = std::make_shared<FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults(), limits);
    auto provider = std::make_shared<ProductionProviderBackend>();
    ServiceRuntimeProviderBridge bridge(store, provider, dependencies());
    check(bridge.start() == ServiceRuntimeProviderBridgeError::none
              && initialized(provider) == true,
          "one pair plus static/setup exactly fills four watcher cache slots");

    const auto created = store->create_config("overflow", "日服", {});
    const auto copied = store->copy_config("alpha", {});
    const auto list_after_rejection = store->config_list({});
    check(created.error
                  == baas::service::adapters::ConfigCommandError::capacity
              && copied.error
                  == baas::service::adapters::ConfigCommandError::capacity
              && list_after_rejection
              && Json::parse(list_after_rejection->data_json)
                     == Json::array({"alpha"}),
          "create and copy reserve both slots for every newly admitted pair");
    // Cross multiple 20 ms watcher polls so this cannot pass only from the
    // synchronous initial snapshot retained before the rejected operations.
    std::this_thread::sleep_for(60ms);
    const auto list_after_polls = store->config_list({});
    check(initialized(provider) == true && list_after_polls
              && Json::parse(list_after_polls->data_json)
                     == Json::array({"alpha"}),
          "capacity rejection leaves the running provider ready across rescans");
    bridge.stop();
}

void test_deleted_optional_gui_frees_capacity_before_pair_refresh()
{
    TempProject project;
    project.add_config("alpha");
    project.add_globals();
    baas::service::channels::ResourceStoreLimits limits;
    // alpha + gui + static + setup consume five slots. Six slots can hold a
    // second pair only after the externally deleted optional GUI is retired.
    limits.max_resources = 6;
    auto store = std::make_shared<FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults(), limits);
    auto provider = std::make_shared<ProductionProviderBackend>();
    ServiceRuntimeProviderBridge bridge(store, provider, dependencies());
    check(bridge.start() == ServiceRuntimeProviderBridgeError::none
              && initialized(provider) == true,
          "optional-GUI capacity fixture starts ready");

    std::filesystem::remove(project.root / "config" / "gui.json");
    project.add_config("beta");
    check(eventually([&] {
        auto config = store->pull(
            {SyncResource::config, std::string{"beta"}}, {});
        auto event = store->pull(
            {SyncResource::event, std::string{"beta"}}, {});
        auto gui = store->pull({SyncResource::gui, std::nullopt}, {});
        return config && event && !gui
            && gui.error == ResourceStoreError::not_found
            && initialized(provider) == true;
    }), "deleted optional GUI is retired before a new pair uses its slot");
    // Cross several additional polls to prove the watcher did not enter a
    // permanent capacity-degraded retry loop.
    std::this_thread::sleep_for(80ms);
    check(initialized(provider) == true,
          "provider stays ready after optional-GUI capacity turnover");
    bridge.stop();
}

void test_partial_pair_capacity_failure_can_recover_after_replacement()
{
    TempProject project;
    project.add_config("alpha");
    project.add_globals();
    baas::service::channels::ResourceStoreLimits limits;
    limits.max_resources = 6;
    auto store = std::make_shared<FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults(), limits);
    auto provider = std::make_shared<ProductionProviderBackend>();
    ServiceRuntimeProviderBridge bridge(store, provider, dependencies());
    check(bridge.start() == ServiceRuntimeProviderBridgeError::none
              && initialized(provider) == true,
          "partial-pair recovery fixture starts ready");

    // Only beta/config can enter the last slot; beta/event forces this scan
    // into the capacity-degraded path after a partial cache insertion.
    project.add_config("beta");
    check(eventually([&] {
        auto beta = store->pull(
            {SyncResource::config, std::string{"beta"}}, {});
        return beta && initialized(provider) == false;
    }), "over-capacity beta leaves an observable partial cached pair");

    std::filesystem::remove_all(project.root / "config" / "beta");
    std::filesystem::remove(project.root / "config" / "gui.json");
    project.add_config("gamma");
    check(eventually([&] {
        auto gamma_config = store->pull(
            {SyncResource::config, std::string{"gamma"}}, {});
        auto gamma_event = store->pull(
            {SyncResource::event, std::string{"gamma"}}, {});
        auto beta = store->pull(
            {SyncResource::config, std::string{"beta"}}, {});
        return gamma_config && gamma_event && !beta
            && initialized(provider) == true;
    }), "next scan retires the failed pair and admits its replacement");
    std::this_thread::sleep_for(80ms);
    check(initialized(provider) == true,
          "partial-pair replacement remains ready across later polls");
    bridge.stop();
}

void test_failure_recovery_bounded_logs_and_stop_race()
{
    TempProject project;
    project.add_config("alpha");
    write_bytes(project.root / "setup.toml", "[general]\nchannel = 'stable'\n");
    auto store = std::make_shared<FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
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
    auto store = std::make_shared<FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
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

void test_start_callbacks_can_destroy_last_owner()
{
    {
        TempProject project;
        project.add_config("alpha");
        project.add_globals();
        auto store = std::make_shared<FileResourceStore>(
            project.root, test_defaults::with_synthetic_defaults());
        auto provider = std::make_shared<ProductionProviderBackend>();
        std::unique_ptr<ServiceRuntimeProviderBridge> bridge;
        auto subscription = provider->subscribe_status(
            [&bridge](std::string payload) {
                if (payload.find(R"("is_all_data_initialized":false)")
                        != std::string::npos
                    && bridge) {
                    bridge.reset();
                }
            });
        check(static_cast<bool>(subscription),
              "start-false destroy observer subscribes");
        bridge = std::make_unique<ServiceRuntimeProviderBridge>(
            store, provider, dependencies());
        auto* const raw = bridge.get();
        const auto result = raw->start();
        check(!bridge && result == ServiceRuntimeProviderBridgeError::internal_error,
              "start false publication may destroy the last bridge owner");
    }

    {
        TempProject project;
        project.add_config("alpha");
        project.add_globals();
        auto store = std::make_shared<FileResourceStore>(
            project.root, test_defaults::with_synthetic_defaults());
        auto provider = std::make_shared<ProductionProviderBackend>();
        std::unique_ptr<ServiceRuntimeProviderBridge> bridge;
        auto subscription = provider->subscribe_status(
            [&bridge](std::string payload) {
                if (payload.find(R"("is_all_data_initialized":true)")
                        != std::string::npos
                    && bridge) {
                    bridge.reset();
                }
            });
        check(static_cast<bool>(subscription),
              "start-true destroy observer subscribes");
        bridge = std::make_unique<ServiceRuntimeProviderBridge>(
            store, provider, dependencies());
        auto* const raw = bridge.get();
        const auto result = raw->start();
        check(!bridge
                  && result
                      == ServiceRuntimeProviderBridgeError::watcher_start_failed
                  && initialized(provider) == false,
              "initial true publication may destroy the last bridge owner");
    }

    {
        TempProject project;
        project.add_config("alpha");
        project.add_globals();
        auto store = std::make_shared<FileResourceStore>(
            project.root, test_defaults::with_synthetic_defaults());
        std::unique_ptr<FileResourceWatcher> watcher;
        baas::service::adapters::FileResourceWatcherConfig config;
        config.poll_interval = 20ms;
        watcher = std::make_unique<FileResourceWatcher>(
            store,
            [&watcher](const baas::service::adapters::FileResourceScanResult&) {
                watcher.reset();
            },
            config);
        auto* const raw = watcher.get();
        const auto result = raw->start();
        check(!watcher && !result.started
                  && result.error
                      == baas::service::adapters::FileResourceScanError::cancelled,
              "direct watcher initial callback may destroy its last owner");
    }
}

void test_reentrant_provider_callback_can_destroy_last_bridge()
{
    TempProject project;
    project.add_config("alpha");
    project.add_globals();
    auto store = std::make_shared<FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
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
    auto store = std::make_shared<FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
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
    test_created_config_reaches_watcher_subscribers_and_ready_provider();
    test_config_pair_replacement_advances_at_capacity();
    test_create_and_copy_reserve_watcher_cache_capacity();
    test_deleted_optional_gui_frees_capacity_before_pair_refresh();
    test_partial_pair_capacity_failure_can_recover_after_replacement();
    test_failure_recovery_bounded_logs_and_stop_race();
    test_reentrant_provider_callback_can_stop_watcher();
    test_start_callbacks_can_destroy_last_owner();
    test_reentrant_provider_callback_can_destroy_last_bridge();
    test_external_stop_waits_for_detached_reentrant_callback();
    if (failures == 0) {
        std::cout << "Service runtime provider bridge tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
