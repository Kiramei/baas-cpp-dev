#include "service/app/ConfigurationTriggerRegistration.h"
#include "service/trigger/TriggerExecutor.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;
namespace adapters = baas::service::adapters;
namespace app = baas::service::app;
namespace protocol = baas::service::protocol::trigger;
namespace trigger = baas::service::trigger;
using Json = nlohmann::json;

namespace {

int failures{};

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
        static std::atomic<unsigned int> sequence{};
        root = std::filesystem::temp_directory_path()
            / ("baas-config-trigger-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::remove_all(root);
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
    std::filesystem::path root;
};

[[nodiscard]] std::shared_ptr<const trigger::TriggerDispatcher> dispatcher(
    const std::shared_ptr<adapters::FileResourceStore>& store,
    const app::ConfigurationTriggerLimits limits = {})
{
    auto made = app::make_configuration_trigger_registrations(store, limits);
    if (!made) throw std::runtime_error("registration failed");
    auto built = trigger::TriggerDispatcher::create(std::move(made.registrations));
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
};

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
    Response response{begun.lease->batch().status(), begun.lease->batch().json()};
    if (!connection.complete_send(*begun.lease)) {
        throw std::runtime_error("complete failed");
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
    auto store = std::make_shared<adapters::FileResourceStore>(project.root);
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher(store));

    const auto copied = execute(executor, "copy_config", 7, R"({"id":"source"})");
    check(copied.status == protocol::ResponseStatus::ok,
          "copy_config must complete successfully");
    const auto envelope = Json::parse(copied.json);
    check(envelope["command"] == "copy_config" && envelope["timestamp"] == 7
              && envelope["data"]["name"] == "Alpha_copy2",
          "copy_config must preserve Python serial/name response data");
    const auto serial = envelope["data"]["serial"].get<std::string>();
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

void test_validation_bounds_and_fail_closed_paths()
{
    TempProject project;
    project.add("source", "Alpha");
    auto store = std::make_shared<adapters::FileResourceStore>(project.root);
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

    std::stop_source cancelled;
    cancelled.request_stop();
    check(store->copy_config("source", cancelled.get_token()).error
              == adapters::ConfigCommandError::cancelled
              && store->remove_config("source", cancelled.get_token()).error
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

void test_registration_scope_and_limits()
{
    check(app::make_configuration_trigger_registrations(nullptr).error
              == app::ConfigurationTriggerRegistrationError::missing_store,
          "registration must reject a missing production store");
    TempProject project;
    auto store = std::make_shared<adapters::FileResourceStore>(project.root);
    app::ConfigurationTriggerLimits invalid;
    invalid.max_payload_nodes = 0;
    check(app::make_configuration_trigger_registrations(store, invalid).error
              == app::ConfigurationTriggerRegistrationError::invalid_limits,
          "registration must reject unbounded payload limits");
    auto made = app::make_configuration_trigger_registrations(store);
    check(made && made.registrations[0].descriptor_name == "copy_config"
              && made.registrations[1].descriptor_name == "remove_config*",
          "registration must install exactly the coherent two-command slice");
}

void test_backpressure_retries_terminal_without_repeating_copy()
{
    TempProject project;
    project.add("source", "Alpha");
    auto store = std::make_shared<adapters::FileResourceStore>(project.root);
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
        test_validation_bounds_and_fail_closed_paths();
        test_registration_scope_and_limits();
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
