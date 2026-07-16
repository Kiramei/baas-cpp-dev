#include "service/app/ConfigurationTriggerRegistration.h"
#include "service/trigger/TriggerExecutor.h"
#include "TestConfigurationDefaults.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace std::chrono_literals;
namespace adapters = baas::service::adapters;
namespace app = baas::service::app;
namespace channels = baas::service::channels;
namespace protocol = baas::service::protocol::trigger;
namespace test_defaults = baas::service::test;
namespace trigger = baas::service::trigger;
using Json = nlohmann::json;

namespace {

int failures{};

std::uint64_t current_process_id() noexcept
{
#ifdef _WIN32
    return static_cast<std::uint64_t>(::_getpid());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class TempProject final {
public:
    TempProject()
    {
        static std::atomic<std::uint64_t> sequence{};
        const auto timestamp = std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count();
        root = std::filesystem::temp_directory_path()
            / ("baas-config-trigger-" + std::to_string(current_process_id())
               + "-" + std::to_string(timestamp) + "-"
               + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(root / "config");
    }
    ~TempProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    void add(const std::string& id, const std::string& name) const
    {
        const auto directory = root / "config" / id;
        std::filesystem::create_directories(directory / "nested");
        std::ofstream(directory / "config.json")
            << "{\"name\":" << Json(name).dump() << ",\"server\":\"日服\"}";
        std::ofstream(directory / "event.json") << "[]";
        std::ofstream(directory / "nested" / "kept.txt") << "kept";
    }
    void add_legacy_without_event(const std::string& id) const
    {
        const auto directory = root / "config" / id;
        std::filesystem::create_directories(directory);
        std::ofstream(directory / "config.json")
            << R"({"name":"\u3000Legacy\u3000","server":"日服","legacy_key":true,"create_item_holding_quantity":{"obsolete":7}})";
        std::ofstream(directory / "switch.json") << R"([{"legacy":true}])";
        std::ofstream(directory / "display.json") << "{}";
    }
    void add_server(const std::string& id, const std::string& server) const
    {
        const auto directory = root / "config" / id;
        std::filesystem::create_directories(directory);
        std::ofstream(directory / "config.json")
            << Json{{"name", id}, {"server", server},
                    {"create_item_holding_quantity", Json::object()}}.dump();
        std::ofstream(directory / "event.json") << "[]";
    }
    std::filesystem::path root;
};

[[nodiscard]] std::shared_ptr<const trigger::TriggerDispatcher> dispatcher(
    const std::shared_ptr<adapters::FileResourceStore>& store,
    const app::ConfigurationTriggerLimits limits = {},
    const trigger::TriggerDispatchLimits dispatch_limits = {})
{
    auto made = app::make_configuration_trigger_registrations(store, limits);
    if (!made) throw std::runtime_error("registration failed");
    auto built = trigger::TriggerDispatcher::create(
        std::move(made.registrations), dispatch_limits);
    if (!built) throw std::runtime_error("dispatcher failed");
    return std::make_shared<const trigger::TriggerDispatcher>(
        std::move(*built.dispatcher));
}

[[nodiscard]] std::optional<protocol::TriggerIngressItem> ingress(
    const std::string_view command,
    const protocol::Timestamp timestamp,
    const std::string_view payload)
{
    const auto json = std::string{"{\"type\":\"command\",\"command\":"}
        + Json(std::string{command}).dump() + ",\"timestamp\":" + std::to_string(timestamp)
        + ",\"payload\":" + std::string{payload} + "}";
    protocol::TriggerIngress owner;
    const auto received = owner.receive_json_frame(json);
    if (!received) return std::nullopt;
    return owner.take_ready();
}

template <typename Predicate>
bool wait_until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

struct Response {
    protocol::ResponseStatus status{protocol::ResponseStatus::error};
    std::string json;
    bool has_binary{};
    std::vector<std::byte> binary;
};

[[nodiscard]] bool has_private_config_transaction(
    const std::filesystem::path& config_root)
{
    std::error_code error;
    std::filesystem::directory_iterator iterator(config_root, error);
    const std::filesystem::directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
        if (iterator->path().filename().string().starts_with(".baas-")) {
            return true;
        }
    }
    return false;
}

Response execute(
    const std::shared_ptr<trigger::TriggerExecutor>& executor,
    const std::string_view command,
    const protocol::Timestamp timestamp,
    const std::string_view payload)
{
    auto session = std::make_shared<protocol::TriggerSession>();
    auto connection = executor->connect(session);
    auto item = ingress(command, timestamp, payload);
    if (!item) throw std::runtime_error("ingress failed: " + std::string{command});
    const auto submitted = connection.submit(std::move(*item));
    if (!submitted) {
        throw std::runtime_error(
            "submit failed: " + std::string{command} + ":"
            + std::string{trigger::trigger_submit_error_name(submitted.error)});
    }
    if (!wait_until([&] { return session->stats().queued_batches == 1; })) {
        throw std::runtime_error("response timeout");
    }
    auto begun = session->begin_send();
    if (!begun) throw std::runtime_error("lease failed");
    Response response{
        begun.lease->batch().status(), begun.lease->batch().json(),
        begun.lease->batch().has_binary(), begun.lease->batch().binary()};
    if (!connection.complete_send(*begun.lease)) {
        throw std::runtime_error("complete failed");
    }
    return response;
}

Response execute_binary(
    const std::shared_ptr<trigger::TriggerExecutor>& executor,
    const std::string_view command,
    const protocol::Timestamp timestamp,
    const std::string_view payload,
    const std::span<const std::byte> binary)
{
    auto session = std::make_shared<protocol::TriggerSession>();
    auto connection = executor->connect(session);
    const auto json = std::string{"{\"type\":\"command\",\"command\":"}
        + Json(std::string{command}).dump() + ",\"timestamp\":"
        + std::to_string(timestamp) + ",\"payload\":"
        + std::string{payload} + "}";
    protocol::TriggerIngress owner;
    const auto waiting = owner.receive_json_frame(json);
    if (!waiting
        || waiting.outcome != protocol::TriggerIngressOutcome::awaiting_binary) {
        throw std::runtime_error(
            "binary ingress declaration failed: " + std::string{command});
    }
    const auto received = owner.receive_binary_frame(binary);
    if (!received) {
        throw std::runtime_error(
            "binary ingress failed: " + std::string{command});
    }
    auto item = owner.take_ready();
    if (!item) {
        throw std::runtime_error(
            "binary ingress not ready: " + std::string{command});
    }
    const auto submitted = connection.submit(std::move(*item));
    if (!submitted) {
        throw std::runtime_error(
            "binary submit failed: " + std::string{command});
    }
    if (!wait_until([&] { return session->stats().queued_batches == 1; })) {
        throw std::runtime_error("binary response timeout");
    }
    auto begun = session->begin_send();
    if (!begun) throw std::runtime_error("binary response lease failed");
    Response response{
        begun.lease->batch().status(), begun.lease->batch().json(),
        begun.lease->batch().has_binary(), begun.lease->batch().binary()};
    if (!connection.complete_send(*begun.lease)) {
        throw std::runtime_error("binary response complete failed");
    }
    return response;
}

[[nodiscard]] protocol::OutboundBatch blocker_batch()
{
    protocol::CommandResponse response;
    response.command = "status";
    response.timestamp = 1;
    response.data_json = "{}";
    auto encoded = protocol::encode_command_response(std::move(response));
    if (!encoded) throw std::runtime_error("blocker encode failed");
    return std::move(encoded.batch);
}

void test_copy_and_remove_exact_python_envelopes()
{
    TempProject project;
    project.add("source", "Alpha");
    project.add("existing", "Alpha_copy");
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher(store));

    const auto copied = execute(executor, "copy_config", 7, R"({"id":"source"})");
    check(copied.status == protocol::ResponseStatus::ok,
          std::string{"copy_config must complete successfully: "} + copied.json);
    const auto envelope = Json::parse(copied.json);
    const bool valid_copy = copied.status == protocol::ResponseStatus::ok
        && envelope.contains("data") && envelope.at("data").is_object()
        && envelope.at("data").contains("serial")
        && envelope.at("data").at("serial").is_string()
        && envelope.at("data").contains("name");
    check(valid_copy && envelope.value("command", "") == "copy_config"
              && envelope.value("timestamp", 0) == 7
              && envelope.at("data").at("name") == "Alpha_copy2",
          "copy_config must preserve Python serial/name response data");
    if (!valid_copy) {
        executor->shutdown();
        return;
    }
    const auto serial = envelope.at("data").at("serial").get<std::string>();
    check(serial != "source"
              && std::filesystem::is_regular_file(
                  project.root / "config" / serial / "event.json")
              && std::filesystem::is_regular_file(
                  project.root / "config" / serial / "nested" / "kept.txt"),
          "copy_config must publish one complete recursive copy");
    const auto copied_config = Json::parse(std::ifstream(
        project.root / "config" / serial / "config.json"));
    check(copied_config["name"] == "Alpha_copy2",
          "copy_config must persist the unique copy suffix");

    const auto removed = execute(
        executor, "remove_config_remote", 8,
        std::string{"{\"id\":"} + Json(serial).dump() + "}");
    check(removed.status == protocol::ResponseStatus::ok
              && Json::parse(removed.json)["data"] == Json::object()
              && !std::filesystem::exists(project.root / "config" / serial),
          "remove_config prefix family must return Python's empty data object");
    const auto absent = execute(
        executor, "remove_configuration", 9, R"({"id":"already-absent"})");
    check(absent.status == protocol::ResponseStatus::ok,
          "removing an absent config must remain idempotent like Python");
    executor->shutdown();
}

void test_export_import_binary_round_trip_and_errors()
{
    TempProject project;
    project.add("source", "Alpha");
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher(store));

    const auto exported = execute(
        executor, "export_config", 40, R"({"id":"source"})");
    const auto export_envelope = Json::parse(exported.json);
    const bool valid_export = exported.status == protocol::ResponseStatus::ok
        && exported.has_binary && !exported.binary.empty()
        && export_envelope.value("command", "") == "export_config"
        && export_envelope.value("timestamp", 0) == 40
        && export_envelope.contains("data")
        && export_envelope.at("data").value("filename", "") == "Alpha.zip"
        && export_envelope.at("data").contains("binary")
        && export_envelope.at("data").at("binary").value("size", 0U)
            == exported.binary.size();
    check(valid_export,
          "export_config must return the Python filename and an adjacent ZIP frame");
    if (!valid_export) {
        executor->shutdown();
        return;
    }

    const auto imported = execute_binary(
        executor, "import_config", 41, R"({"binary":true})",
        exported.binary);
    const auto import_envelope = Json::parse(imported.json);
    const bool valid_import = imported.status == protocol::ResponseStatus::ok
        && !imported.has_binary
        && import_envelope.value("command", "") == "import_config"
        && import_envelope.value("timestamp", 0) == 41
        && import_envelope.contains("data")
        && import_envelope.at("data").value("name", "") == "Alpha"
        && import_envelope.at("data").contains("serial")
        && import_envelope.at("data").at("serial").is_string();
    check(valid_import,
          "import_config must consume the adjacent ZIP and return serial/name data");
    if (valid_import) {
        const auto serial = import_envelope.at("data").at("serial")
            .get<std::string>();
        check(serial != "source"
                  && !std::filesystem::exists(project.root / "config" / "source")
                  && std::filesystem::is_regular_file(
                      project.root / "config" / serial / "nested" / "kept.txt"),
              "same-name import must atomically replace the old complete profile");
    }

    const auto absent = execute(
        executor, "export_config", 42, R"({"id":"missing"})");
    check(absent.status == protocol::ResponseStatus::error
              && absent.json.find("config_not_found") != std::string::npos
              && !absent.has_binary,
          "export store failures must map through the configuration error policy");

    const std::array invalid_archive{
        std::byte{'n'}, std::byte{'o'}, std::byte{'t'}, std::byte{'-'},
        std::byte{'z'}, std::byte{'i'}, std::byte{'p'},
    };
    const auto invalid = execute_binary(
        executor, "import_config", 43, R"({"binary":true})",
        invalid_archive);
    check(invalid.status == protocol::ResponseStatus::error
              && invalid.json.find("config_invalid_data") != std::string::npos,
          "invalid import archives must fail closed with a stable mapped error");

    protocol::TriggerIngress missing_binary;
    const auto missing = missing_binary.receive_json_frame(
        R"({"type":"command","command":"import_config","timestamp":44,"payload":{}})");
    check(!missing
              && missing.error == protocol::TriggerIngressError::binary_marker_required
              && missing.command_rejection
              && missing.command_rejection->command == "import_config",
          "import_config without a declared adjacent binary must be explicitly rejected");
    executor->shutdown();
}

void test_add_config_prefix_exact_python_envelope_and_defaults()
{
    TempProject project;
    auto dependencies = test_defaults::with_synthetic_defaults();
    dependencies.clock = [] { return 1'725'000'123'456.75; };
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, std::move(dependencies));
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher(store));

    const auto added = execute(
        executor, "add_configuration", 3,
        R"({"name":"First profile","server":"日服"})");
    const auto envelope = Json::parse(added.json);
    check(added.status == protocol::ResponseStatus::ok
              && envelope.value("command", "") == "add_configuration"
              && envelope.value("timestamp", 0) == 3
              && envelope.at("data")
                     == Json{{"serial", "1725000123456"}},
          "add_config prefix must return exactly Python's serial-only data");
    const auto directory = project.root / "config" / "1725000123456";
    const auto config = Json::parse(std::ifstream(directory / "config.json"));
    const auto event = Json::parse(std::ifstream(directory / "event.json"));
    const auto switches = Json::parse(std::ifstream(directory / "switch.json"));
    check(config["name"] == "First profile" && config["server"] == "日服"
              && config["create_item_holding_quantity"].size() == 157
              && event.size() == 26 && event[0]["daily_reset"][0][0] == 19
              && switches.size() == 11
              && std::filesystem::is_regular_file(
                  project.root / "config" / "static.json"),
          "add_config must publish the complete Python initializer vector");
    const auto listed = store->config_list({});
    const auto pulled = store->pull(
        {channels::SyncResource::config, "1725000123456"}, {});
    check(listed && Json::parse(listed->data_json)
                        == Json::array({"1725000123456"})
              && pulled && Json::parse(pulled->data_json)["name"] == "First profile",
          "new config must be immediately visible to list/pull provider paths");

    for (const auto payload : {
             R"({})", R"({"name":"x"})", R"({"server":"日服"})",
             R"({"name":1,"server":"日服"})",
             R"({"name":"x","server":false})"}) {
        const auto rejected = execute(executor, "add_config_v2", 4, payload);
        check(rejected.status == protocol::ResponseStatus::error
                  && rejected.json.find(
                         "server and name are required for add_config")
                         != std::string::npos,
              "add_config must reject missing, false, and non-string fields");
    }
    const auto unsupported = execute(
        executor, "add_config", 5,
        R"({"name":"x","server":"CN"})");
    check(unsupported.status == protocol::ResponseStatus::error
              && unsupported.json.find("config_invalid_data") != std::string::npos
              && Json::parse(store->config_list({})->data_json).size() == 1
              && !has_private_config_transaction(project.root / "config"),
          "unsupported server must fail without a visible or staged config");
    executor->shutdown();
}

void test_validation_bounds_and_fail_closed_paths()
{
    TempProject project;
    project.add("source", "Alpha");
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher(store));
    for (const auto payload : {R"({})", R"({"id":1})"}) {
        const auto response = execute(executor, "copy_config", 10, payload);
        check(response.status == protocol::ResponseStatus::error,
              "missing and typed id payloads must fail closed");
    }
    check(!ingress(
              "copy_config", 10, R"({"id":"source","id":"other"})"),
          "duplicate id payloads must fail at bounded ingress");
    const auto traversal = execute(
        executor, "remove_config", 11, R"({"id":"../source"})");
    check(traversal.status == protocol::ResponseStatus::error
              && traversal.json.find("invalid_config_id") != std::string::npos
              && std::filesystem::exists(project.root / "config" / "source"),
          "config ids must not escape the owned root");

    app::ConfigurationTriggerLimits small;
    small.max_payload_bytes = 32;
    auto bounded = std::make_shared<trigger::TriggerExecutor>(dispatcher(store, small));
    const auto oversized = execute(
        bounded, "copy_config", 12,
        R"({"id":"source","padding":"01234567890123456789"})");
    check(oversized.status == protocol::ResponseStatus::error
              && oversized.json.find("config_payload_capacity") != std::string::npos,
          "configuration handlers must enforce a local payload byte ceiling");
    const auto oversized_export = execute(
        bounded, "export_config", 13,
        R"({"id":"source","padding":"01234567890123456789"})");
    check(oversized_export.status == protocol::ResponseStatus::error
              && oversized_export.json.find("config_payload_capacity")
                  != std::string::npos,
          "export_config must enforce the same local payload byte ceiling");
    const std::array one_byte{std::byte{0}};
    const auto oversized_import = execute_binary(
        bounded, "import_config", 14,
        R"({"binary":true,"padding":"01234567890123456789"})",
        one_byte);
    check(oversized_import.status == protocol::ResponseStatus::error
              && oversized_import.json.find("config_payload_capacity")
                  != std::string::npos,
          "import_config must bound its JSON payload before decoding the archive");

    std::stop_source cancelled;
    cancelled.request_stop();
    check(store->copy_config("source", cancelled.get_token()).error
              == adapters::ConfigCommandError::cancelled
              && store->create_config(
                     "name", "日服", cancelled.get_token()).error
                  == adapters::ConfigCommandError::cancelled
              && store->remove_config("source", cancelled.get_token()).error
                  == adapters::ConfigCommandError::cancelled
              && store->export_config("source", cancelled.get_token()).error
                  == adapters::ConfigCommandError::cancelled
              && store->import_config(
                     one_byte, cancelled.get_token()).error
                  == adapters::ConfigCommandError::cancelled,
          "filesystem operations must observe cancellation before mutation");

    project.add("deep", "Deep");
    auto nested = project.root / "config" / "deep";
    for (std::size_t depth = 0; depth < 34; ++depth) {
        nested /= "d";
        std::filesystem::create_directory(nested);
    }
    check(store->copy_config("deep", {}).error
              == adapters::ConfigCommandError::capacity,
          "configuration copies must enforce the file-tree depth ceiling");

    project.add("linked", "Linked");
    std::error_code link_error;
    std::filesystem::create_directory_symlink(
        project.root / "config" / "source",
        project.root / "config" / "linked" / "outside", link_error);
    if (!link_error) {
        check(store->copy_config("linked", {}).error
                  == adapters::ConfigCommandError::invalid_data,
              "configuration copies must reject reparse and symlink entries");
    }
    bounded->shutdown();
    executor->shutdown();
}

void test_wide_tree_entry_budget_and_cancellation_cleanup()
{
    TempProject project;
    project.add_server("wide", "日服");
    const auto source = project.root / "config" / "wide";
    constexpr std::size_t wide_directories = 4'097;
    for (std::size_t index = 0; index < wide_directories; ++index) {
        std::filesystem::create_directory(
            source / ("empty-" + std::to_string(index)));
    }

    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    const auto before_capacity = store->config_list({});
    const auto rejected = store->copy_config("wide", {});
    const auto after_capacity = store->config_list({});
    check(rejected.error == adapters::ConfigCommandError::capacity,
          "wide empty-directory trees must consume the 4,096-entry budget");
    check(before_capacity && after_capacity
              && before_capacity->data_json == after_capacity->data_json
              && !has_private_config_transaction(project.root / "config"),
          "entry-capacity failure must remove staging and publish no partial copy");

    // Keep the source below the entry ceiling but wide enough to ensure that
    // cancellation can be observed after private staging is created.
    for (std::size_t index = wide_directories - 16;
         index < wide_directories; ++index) {
        std::filesystem::remove(
            source / ("empty-" + std::to_string(index)));
    }
    const auto before_cancel = store->config_list({});
    std::stop_source stop;
    std::optional<adapters::ConfigCopyResult> cancelled_result;
    std::thread worker([&] {
        cancelled_result = store->copy_config("wide", stop.get_token());
    });
    const bool saw_staging = wait_until([&] {
        return has_private_config_transaction(project.root / "config");
    });
    static_cast<void>(stop.request_stop());
    worker.join();
    const auto after_cancel = store->config_list({});
    check(saw_staging && cancelled_result
              && cancelled_result->error == adapters::ConfigCommandError::cancelled,
          "a wide traversal must observe cancellation after staging begins");
    check(before_cancel && after_cancel
              && before_cancel->data_json == after_cancel->data_json
              && !has_private_config_transaction(project.root / "config"),
          "cancelled wide traversal must remove staging and publish no partial copy");
}

void test_structural_success_invalidates_cached_resources()
{
    TempProject project;
    project.add_server("source", "日服");
    project.add_server("4242", "日服");
    std::ofstream(project.root / "config" / "4242" / "config.json",
                  std::ios::trunc) << R"({"name":"stale","server":"日服"})";
    std::ofstream(project.root / "config" / "4242" / "event.json",
                  std::ios::trunc) << R"({"stale":true})";
    std::ofstream(project.root / "config" / "static.json")
        << R"({"stale":true})";

    auto dependencies = test_defaults::with_synthetic_defaults();
    dependencies.clock = [] { return 4'242.0; };
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, std::move(dependencies));
    const channels::ResourceKey config_key{
        channels::SyncResource::config, "4242"};
    const channels::ResourceKey event_key{
        channels::SyncResource::event, "4242"};
    const channels::ResourceKey static_key{
        channels::SyncResource::static_data, std::nullopt};
    check(store->pull(config_key, {}) && store->pull(event_key, {})
              && store->pull(static_key, {}),
          "cache invalidation fixture must cache old target and static resources");
    std::filesystem::remove_all(project.root / "config" / "4242");

    const auto copied = store->copy_config("source", {});
    const auto fresh_config = store->pull(config_key, {});
    const auto fresh_event = store->pull(event_key, {});
    const auto fresh_static = store->pull(static_key, {});
    check(copied && copied.serial == "4242"
              && fresh_config && fresh_event && fresh_static,
          "copy must support safe clock-id reuse and reload every affected cache");
    if (fresh_config && fresh_event && fresh_static) {
        const auto config = Json::parse(fresh_config->data_json);
        const auto event = Json::parse(fresh_event->data_json);
        const auto static_data = Json::parse(fresh_static->data_json);
        check(config.value("name", "") == "source_copy"
                  && event.is_array()
                  && static_data.contains("create_item_order")
                  && !static_data.contains("stale"),
              "post-copy pulls must not return stale config/event/static snapshots");
    }

    // Cache a value that is newer than the disk view, then restore the disk to
    // the already-correct defaults. The next copy takes update_static=false,
    // but its successful structural boundary must still invalidate the cache.
    std::ifstream default_static_stream(
        project.root / "config" / "static.json", std::ios::binary);
    const std::string default_static_bytes{
        std::istreambuf_iterator<char>{default_static_stream},
        std::istreambuf_iterator<char>{}};
    std::ofstream(project.root / "config" / "static.json", std::ios::trunc)
        << R"({"stale_cache":true})";
    check(store->refresh_and_publish(static_key, "test"),
          "static cache fixture must accept the externally changed stale value");
    std::ofstream(project.root / "config" / "static.json",
                  std::ios::binary | std::ios::trunc)
        << default_static_bytes;
    const auto no_static_write = store->copy_config("source", {});
    const auto reloaded_static = store->pull(static_key, {});
    check(no_static_write && reloaded_static,
          "copy with an already-current static file must still reload its cache");
    if (reloaded_static) {
        const auto static_data = Json::parse(reloaded_static->data_json);
        check(static_data.contains("create_item_order")
                  && !static_data.contains("stale_cache"),
              "update_static=false success must not retain an older static cache");
    }

    project.add_server("externally-removed", "日服");
    const channels::ResourceKey removed_config_key{
        channels::SyncResource::config, "externally-removed"};
    const channels::ResourceKey removed_event_key{
        channels::SyncResource::event, "externally-removed"};
    check(store->pull(removed_config_key, {})
              && store->pull(removed_event_key, {}),
          "idempotent remove fixture must cache both externally removed resources");
    std::filesystem::remove_all(
        project.root / "config" / "externally-removed");
    const auto removed = store->remove_config("externally-removed", {});
    const auto missing_config = store->pull(removed_config_key, {});
    const auto missing_event = store->pull(removed_event_key, {});
    check(removed && !missing_config && !missing_event
              && missing_config.error == channels::ResourceStoreError::not_found
              && missing_event.error == channels::ResourceStoreError::not_found,
          "idempotent successful remove must invalidate externally deleted caches");
}

void test_python_initializer_migration_vector()
{
    TempProject project;
    project.add_legacy_without_event("legacy");
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher(store));
    const auto copied = execute(executor, "copy_config", 20, R"({"id":"legacy"})");
    check(copied.status == protocol::ResponseStatus::ok,
          std::string{"copy must repair a source without event.json: "} + copied.json);
    const auto envelope = Json::parse(copied.json);
    const bool valid_copy = copied.status == protocol::ResponseStatus::ok
        && envelope.contains("data") && envelope.at("data").is_object()
        && envelope.at("data").contains("serial")
        && envelope.at("data").at("serial").is_string();
    if (!valid_copy) {
        check(false, "initializer migration response must contain a serial");
        executor->shutdown();
        return;
    }
    const auto serial = envelope.at("data").at("serial").get<std::string>();
    const auto directory = project.root / "config" / serial;
    const auto config = Json::parse(std::ifstream(directory / "config.json"));
    const auto event = Json::parse(std::ifstream(directory / "event.json"));
    const auto switches = Json::parse(std::ifstream(directory / "switch.json"));
    check(envelope.at("data").at("name") == "Legacy_copy"
              && config["name"] == "Legacy_copy"
              && config["server"] == "日服"
              && config.contains("purchase_arena_ticket_times")
              && !config.contains("legacy_key")
              && config["create_item_holding_quantity"].size() == 157
              && !config["create_item_holding_quantity"].contains("obsolete"),
          "copy must apply Unicode strip, current keys, and JP manufacturing entries");
    check(event.is_array() && !event.empty()
              && event[0]["func_name"] == "restart"
              && event[0]["daily_reset"][0][0] == 19,
          "missing event.json must use the real non-CN default migration vector");
    check(switches.is_array() && switches.size() == 11
              && !std::filesystem::exists(directory / "display.json"),
          "copy must reset switch.json and delete deprecated display.json");
    const auto static_config = Json::parse(std::ifstream(
        project.root / "config" / "static.json"));
    check(static_config.contains("create_item_order")
              && static_config["create_item_order"]["JP"]["basic"].is_object(),
          "copy initializer must atomically refresh the real global static config");

    project.add("junk", "Junk");
    std::ofstream(project.root / "config" / "junk" / "event.json",
                  std::ios::trunc) << R"([{"enabled":true}])";
    const auto repaired = execute(executor, "copy_config", 21, R"({"id":"junk"})");
    const auto repaired_envelope = Json::parse(repaired.json);
    if (repaired.status != protocol::ResponseStatus::ok
        || !repaired_envelope.contains("data")
        || !repaired_envelope.at("data").contains("serial")) {
        check(false, "junk event repair response must contain a serial");
        executor->shutdown();
        return;
    }
    const auto repaired_serial = repaired_envelope.at("data").at("serial")
        .get<std::string>();
    const auto repaired_event = Json::parse(std::ifstream(
        project.root / "config" / repaired_serial / "event.json"));
    check(repaired_event.size() == 26
              && repaired_event[0]["func_name"] == "restart",
          "an event item missing func_name must reset the whole file to defaults");

    project.add("scalar-reset", "Scalar");
    std::ofstream(project.root / "config" / "scalar-reset" / "event.json",
                  std::ios::trunc)
        << R"([{"func_name":"restart","daily_reset":[5]}])";
    const auto scalar_repaired = store->copy_config("scalar-reset", {});
    check(static_cast<bool>(scalar_repaired),
          "scalar daily_reset fixture must remain recoverable");
    if (scalar_repaired) {
        const auto scalar_event = Json::parse(std::ifstream(
            project.root / "config" / scalar_repaired.serial / "event.json"));
        check(scalar_event.size() == 26
                  && scalar_event[0]["func_name"] == "restart"
                  && scalar_event[0]["daily_reset"][0].is_array(),
              "daily_reset scalar must trigger Python's whole-file default fallback");
    }

    const auto unsupported = project.root / "config" / "unsupported";
    std::filesystem::create_directories(unsupported);
    std::ofstream(unsupported / "config.json")
        << R"({"name":"Unsupported","server":"CN"})";
    std::ofstream(unsupported / "event.json") << "[]";
    const auto rejected = execute(
        executor, "copy_config", 22, R"({"id":"unsupported"})");
    check(rejected.status == protocol::ResponseStatus::error
              && rejected.json.find("config_invalid_data") != std::string::npos,
          "literal CN and unknown servers must fail like Python ConfigSet");
    executor->shutdown();
}

void test_commit_point_wins_late_stop_and_staging_is_protected_scope()
{
    TempProject project;
    project.add("source", "Alpha");
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    std::stop_source stop;
    std::optional<std::thread> canceller;
    bool saw_private_staging{};
    bool saw_no_project_root_staging{};
    const auto copied = store->copy_config(
        "source", stop.get_token(),
        [&](const std::string_view, const std::string_view) {
            saw_no_project_root_staging = !std::filesystem::exists(
                project.root / ".baas-config-staging");
            for (const auto& child :
                 std::filesystem::directory_iterator(project.root / "config")) {
                const auto name = child.path().filename().string();
                if (name.starts_with(".baas-copy-")) saw_private_staging = true;
            }
            canceller.emplace([&] { static_cast<void>(stop.request_stop()); });
            check(wait_until([&] { return stop.stop_requested(); }),
                  "late cancellation fixture must reach the commit gate");
            return true;
        });
    if (canceller) canceller->join();
    check(copied && stop.stop_requested()
              && std::filesystem::is_directory(
                  project.root / "config" / copied.serial),
          "a commit that wins the linearization gate must never report cancelled");
    check(saw_private_staging && saw_no_project_root_staging,
          "copy staging must stay under the auth-protected config root");
}

void test_all_python_server_mappings_and_invalid_servers()
{
    TempProject project;
    const std::vector<std::pair<std::string, std::size_t>> servers{
        {"官服", 154}, {"B服", 154}, {"国际服", 157},
        {"国际服青少年", 157}, {"韩国ONE", 157},
        {"Steam国际服", 157}, {"日服", 157}, {"日服PC端", 157}};
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    for (std::size_t index = 0; index < servers.size(); ++index) {
        const auto id = "server-" + std::to_string(index);
        project.add_server(id, servers[index].first);
        const auto copied = store->copy_config(id, {});
        check(static_cast<bool>(copied),
              "every Python ConfigSet server label must copy");
        if (!copied) continue;
        const auto config = Json::parse(std::ifstream(
            project.root / "config" / copied.serial / "config.json"));
        check(config.at("create_item_holding_quantity").size()
                  == servers[index].second,
              "server mapping must install the exact manufacturing item count");
    }
    for (const auto& [id, server] :
         std::vector<std::pair<std::string, std::string>>{
             {"literal-cn", "CN"}, {"bogus", "bogus"}}) {
        project.add_server(id, server);
        const auto before = store->config_list({});
        const auto rejected = store->copy_config(id, {});
        const auto after = store->config_list({});
        check(rejected.error == adapters::ConfigCommandError::invalid_data
                  && before && after && before->data_json == after->data_json,
              "unsupported server values must fail without publishing a target");
    }

    TempProject create_project;
    auto create_dependencies = test_defaults::with_synthetic_defaults();
    create_dependencies.clock = [] { return 80'000.0; };
    adapters::FileResourceStore create_store(
        create_project.root, std::move(create_dependencies));
    std::optional<Json> canonical_cn_event;
    for (std::size_t index = 0; index < servers.size(); ++index) {
        const auto created = create_store.create_config(
            index < 2 ? "duplicate name" : "profile-" + std::to_string(index),
            servers[index].first, {});
        check(static_cast<bool>(created),
              "all eight Python server labels must support add_config");
        if (!created) continue;
        const auto directory = create_project.root / "config" / created.serial;
        const auto config = Json::parse(std::ifstream(directory / "config.json"));
        const auto event = Json::parse(std::ifstream(directory / "event.json"));
        const auto switches = Json::parse(std::ifstream(directory / "switch.json"));
        check(config.size() == 97 && event.size() == 26 && switches.size() == 11
                  && config["server"] == servers[index].first
                  && config["create_item_holding_quantity"].size()
                         == servers[index].second,
              "create output must retain complete 97/26/11 vectors and server quantities");
        const int expected_reset = index < 2 ? 20 : 19;
        check(event[0]["daily_reset"][0][0] == expected_reset,
              "create event reset must select the exact CN/non-CN projection");
        if (index == 0) canonical_cn_event = event;
        if (canonical_cn_event) {
            auto expected_event = *canonical_cn_event;
            if (index >= 2) {
                for (auto& item : expected_event) {
                    for (auto& reset : item["daily_reset"]) {
                        reset[0] = reset[0].get<int>() - 1;
                    }
                }
            }
            check(event == expected_event,
                  "all 26 events and 29 resets must equal the canonical CN/non-CN vector");
        }
    }
    const auto static_config = Json::parse(std::ifstream(
        create_project.root / "config" / "static.json"));
    check(static_config.size() == 25,
          "create must install the complete 25-key static vector");
    const auto list = Json::parse(create_store.config_list({})->data_json);
    check(list.size() == 8,
          "duplicate display names are legal and do not collapse config ids");
    check(!std::filesystem::exists(create_project.root / "error.log"),
          "successful create must not emit Python initializer error.log noise");
}

void test_registration_scope_and_limits()
{
    check(app::make_configuration_trigger_registrations(nullptr).error
              == app::ConfigurationTriggerRegistrationError::missing_store,
          "registration must reject a missing production store");
    TempProject project;
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    app::ConfigurationTriggerLimits invalid;
    invalid.max_payload_nodes = 0;
    check(app::make_configuration_trigger_registrations(store, invalid).error
              == app::ConfigurationTriggerRegistrationError::invalid_limits,
          "registration must reject unbounded payload limits");
    auto made = app::make_configuration_trigger_registrations(store);
    check(made && made.registrations[0].descriptor_name == "add_config*"
              && made.registrations[1].descriptor_name == "copy_config"
              && made.registrations[2].descriptor_name == "remove_config*"
              && made.registrations[3].descriptor_name == "export_config"
              && made.registrations[4].descriptor_name == "import_config",
          "registration must install exactly the coherent five-command slice");
}

void test_add_irrevocable_claim_is_corrected_after_commit_failure()
{
    TempProject project;
    auto dependencies = test_defaults::with_synthetic_defaults();
    dependencies.clock = [] { return 91'000.0; };
    dependencies.config_create_fault_injector =
        [](const std::string_view step) {
            return step == "before_directory_commit";
        };
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, std::move(dependencies));
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher(store));
    const auto response = execute(
        executor, "add_config", 30,
        R"({"name":"claimed","server":"日服"})");
    check(response.status == protocol::ResponseStatus::error
              && response.json.find("config_internal_error") != std::string::npos
              && !std::filesystem::exists(project.root / "config" / "91000")
              && !has_private_config_transaction(project.root / "config"),
          "post-claim directory failure must correct success to an irrevocable error");
    const auto static_data = store->pull(
        {channels::SyncResource::static_data, std::nullopt}, {});
    check(static_data && Json::parse(static_data->data_json).size() == 25,
          "independent static metadata upgrade remains provider-visible after create failure");
    executor->shutdown();
}

void test_import_irrevocable_claim_is_corrected_after_commit_failure()
{
    TempProject project;
    project.add("source", "Alpha");
    auto dependencies = test_defaults::with_synthetic_defaults();
    dependencies.clock = [] { return 93'000.0; };
    dependencies.config_archive_fault_injector =
        [](const std::string_view step) { return step == "before_retire"; };
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, std::move(dependencies));
    const auto archive = store->export_config("source", {});
    check(static_cast<bool>(archive),
          "post-claim import failure fixture must first export a valid archive");
    if (!archive) return;

    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher(store));
    const auto response = execute_binary(
        executor, "import_config", 45, R"({"binary":true})",
        archive.content);
    const auto listed = store->config_list({});
    check(response.status == protocol::ResponseStatus::error
              && response.json.find("config_internal_error") != std::string::npos
              && listed && Json::parse(listed->data_json) == Json::array({"source"})
              && std::filesystem::is_regular_file(
                  project.root / "config" / "source" / "config.json")
              && !has_private_config_transaction(project.root / "config"),
          "post-claim import failure must correct success and preserve the old profile");
    executor->shutdown();
}

void test_archive_response_rejection_remains_reversible()
{
    TempProject project;
    const std::string long_name(512, 'x');
    project.add("source", long_name);
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    const auto archive = store->export_config("source", {});
    check(static_cast<bool>(archive),
          "response rejection fixture must create a valid archive");
    if (!archive) return;

    trigger::TriggerDispatchLimits dispatch_limits;
    dispatch_limits.response_envelope.max_output_json_bytes = 256;
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        dispatcher(store, {}, dispatch_limits));
    const auto rejected_export = execute(
        executor, "export_config", 46, R"({"id":"source"})");
    check(rejected_export.status == protocol::ResponseStatus::error
              && rejected_export.json.find("config_response_rejected")
                  != std::string::npos
              && !rejected_export.has_binary,
          "an oversized export response must recover to one bounded error terminal");

    const auto rejected_import = execute_binary(
        executor, "import_config", 47, R"({"binary":true})",
        archive.content);
    const auto listed = store->config_list({});
    check(rejected_import.status == protocol::ResponseStatus::error
              && rejected_import.json.find("config_response_rejected")
                  != std::string::npos
              && listed && Json::parse(listed->data_json) == Json::array({"source"})
              && !has_private_config_transaction(project.root / "config"),
          "a rejected import success must decline the claim and publish no mutation");
    executor->shutdown();
}

void test_copy_static_upgrade_is_independent_of_directory_claim()
{
    TempProject project;
    project.add_server("source", "日服");
    std::ofstream(project.root / "config" / "static.json")
        << R"({"old_but_valid":true})";
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    const auto copied = store->copy_config(
        "source", {},
        [](const std::string_view, const std::string_view) { return false; });
    const auto static_data = store->pull(
        {channels::SyncResource::static_data, std::nullopt}, {});
    const auto listed = store->config_list({});
    check(copied.error == adapters::ConfigCommandError::cancelled
              && static_data && Json::parse(static_data->data_json).size() == 25
              && listed && Json::parse(listed->data_json)
                     == Json::array({"source"})
              && !has_private_config_transaction(project.root / "config"),
          "copy uses the same independent static upgrade before a rejected directory claim");
}

void test_directory_commit_never_replaces_concurrent_target()
{
    TempProject project;
    project.add_server("source", "日服");
    auto dependencies = test_defaults::with_synthetic_defaults();
    dependencies.clock = [] { return 92'000.0; };
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, std::move(dependencies));
    const auto target = project.root / "config" / "92000";

    const auto created = store->create_config(
        "claimed", "日服", {}, [&](const std::string_view id) {
            check(id == "92000", "create collision fixture claims expected serial");
            return std::filesystem::create_directory(target);
        });
    check(created.error == adapters::ConfigCommandError::conflict
              && std::filesystem::is_empty(target)
              && !has_private_config_transaction(project.root / "config"),
          "create no-replace commit preserves a concurrently claimed empty target");

    std::filesystem::remove(target);
    const auto copied = store->copy_config(
        "source", {}, [&](const std::string_view id, const std::string_view) {
            check(id == "92000", "copy collision fixture claims expected serial");
            return std::filesystem::create_directory(target);
        });
    check(copied.error == adapters::ConfigCommandError::conflict
              && std::filesystem::is_empty(target)
              && !has_private_config_transaction(project.root / "config"),
          "copy no-replace commit preserves a concurrently claimed empty target");
}

void test_backpressure_retries_terminal_without_repeating_copy()
{
    TempProject project;
    project.add("source", "Alpha");
    auto store = std::make_shared<adapters::FileResourceStore>(
        project.root, test_defaults::with_synthetic_defaults());
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher(store));
    protocol::TriggerSessionLimits session_limits;
    session_limits.max_queued_batches = 1;
    auto session = std::make_shared<protocol::TriggerSession>(session_limits);
    protocol::CommandAdmission admission;
    admission.command = "status";
    admission.timestamp = 1;
    admission.payload_bytes = 2;
    auto blocker = session->admit(std::move(admission));
    check(blocker && session->publish(*blocker.receipt, blocker_batch()),
          "backpressure fixture must fill the response queue");

    auto connection = executor->connect(session);
    auto item = ingress("copy_config", 2, R"({"id":"source"})");
    check(item && connection.submit(std::move(*item)),
          "backpressured copy_config must submit");
    check(wait_until([&] { return connection.stats().completed == 1; }),
          "completed copy_config terminal must be retained under backpressure");
    const auto cancelled = connection.request_cancel(2);
    check(cancelled.session_decision
              == protocol::CancelDecision::terminal_already_queued
              && !cancelled.stop_requested,
          "post-commit cancellation must not replace an irrevocable success");
    auto listed = store->config_list({});
    check(listed && Json::parse(listed->data_json).size() == 2,
          "filesystem copy must execute exactly once before response retry");

    auto first = session->begin_send();
    check(static_cast<bool>(first), "blocker lease must begin");
    if (first) {
        const auto completed = connection.complete_send(*first.lease);
        check(completed && completed.retry.attempted == 1
                  && completed.retry.published == 1,
              "capacity release must retry the retained copy terminal");
    }
    auto copied = session->begin_send();
    check(copied && copied.lease->batch().json().find("Alpha_copy")
                          != std::string::npos,
          "retried terminal must preserve the committed copy result");
    if (copied) static_cast<void>(connection.complete_send(*copied.lease));
    listed = store->config_list({});
    check(listed && Json::parse(listed->data_json).size() == 2,
          "terminal retry must not rerun the filesystem mutation");
    executor->shutdown();
}

}  // namespace

int main()
{
    try {
        test_copy_and_remove_exact_python_envelopes();
        test_export_import_binary_round_trip_and_errors();
        test_add_config_prefix_exact_python_envelope_and_defaults();
        test_validation_bounds_and_fail_closed_paths();
        test_wide_tree_entry_budget_and_cancellation_cleanup();
        test_structural_success_invalidates_cached_resources();
        test_python_initializer_migration_vector();
        test_commit_point_wins_late_stop_and_staging_is_protected_scope();
        test_all_python_server_mappings_and_invalid_servers();
        test_registration_scope_and_limits();
        test_add_irrevocable_claim_is_corrected_after_commit_failure();
        test_import_irrevocable_claim_is_corrected_after_commit_failure();
        test_archive_response_rejection_remains_reversible();
        test_copy_static_upgrade_is_independent_of_directory_claim();
        test_directory_commit_never_replaces_concurrent_target();
        test_backpressure_retries_terminal_without_repeating_copy();
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
    }
    if (failures) {
        std::cerr << failures << " configuration trigger test(s) failed\n";
        return 1;
    }
    std::cout << "configuration trigger tests passed\n";
    return 0;
}
