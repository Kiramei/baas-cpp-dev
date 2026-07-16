#include "service/app/AdbDiscoveryTriggerRegistration.h"
#include "service/trigger/TriggerExecutor.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
namespace app = baas::service::app;
namespace trigger = baas::service::trigger;
namespace protocol = baas::service::protocol::trigger;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
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

[[nodiscard]] std::optional<protocol::TriggerIngressItem> ingress_item(
    const std::string_view command, const protocol::Timestamp timestamp)
{
    std::string json = R"({"type":"command","command":")";
    json.append(command);
    json.append(R"(","timestamp":)");
    json.append(std::to_string(timestamp));
    json.append(R"(,"payload":{}})");
    protocol::TriggerIngress ingress;
    if (!ingress.receive_json_frame(json)) return std::nullopt;
    return ingress.take_ready();
}

[[nodiscard]] std::shared_ptr<protocol::TriggerSession> borrowed_session(
    protocol::TriggerSession& session)
{
    return {&session, [](protocol::TriggerSession*) noexcept {}};
}

struct ObservedBatch {
    protocol::ResponseStatus status{protocol::ResponseStatus::error};
    std::string json;
    bool terminal{};
};

[[nodiscard]] ObservedBatch execute_one(
    app::AdbDiscoverySourceCallback callback)
{
    auto made = app::make_adb_discovery_trigger_registration(
        std::move(callback));
    if (!made) throw std::runtime_error("registration build failed");
    std::vector<trigger::TriggerHandlerRegistration> registrations;
    registrations.emplace_back(std::move(*made.registration));
    auto built = trigger::TriggerDispatcher::create(std::move(registrations));
    if (!built) throw std::runtime_error("dispatcher build failed");
    auto dispatcher = std::make_shared<const trigger::TriggerDispatcher>(
        std::move(*built.dispatcher));
    trigger::TriggerExecutor executor(dispatcher, {1, 1, 1, 1});
    protocol::TriggerSession session;
    auto connection = executor.connect(borrowed_session(session));
    auto item = ingress_item("detect_adb", 7);
    if (!item || !connection.submit(std::move(*item))) {
        throw std::runtime_error("submit failed");
    }
    if (!wait_until([&] { return session.stats().queued_batches == 1; })) {
        throw std::runtime_error("response timeout");
    }
    auto begun = session.begin_send();
    if (!begun) throw std::runtime_error("response lease failed");
    ObservedBatch observed{
        begun.lease->batch().status(),
        begun.lease->batch().json(),
        begun.lease->batch().terminal(),
    };
    if (!connection.complete_send(*begun.lease)) {
        throw std::runtime_error("response completion failed");
    }
    return observed;
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
    explicit EnvironmentGuard(const char* name) : name_(name)
    {
        value_ = read_environment(name);
    }
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

void test_factory_is_exact_and_fail_closed()
{
    auto missing = app::make_adb_discovery_trigger_registration(
        std::shared_ptr<app::AdbDiscoverySource>{});
    check(!missing
              && missing.error
                  == app::AdbDiscoveryTriggerRegistrationError::missing_source,
          "factory must reject a missing source");
    auto empty = app::make_adb_discovery_trigger_registration(
        app::AdbDiscoverySourceCallback{});
    check(!empty
              && empty.error
                  == app::AdbDiscoveryTriggerRegistrationError::empty_callback,
          "factory must reject an empty callback");
    app::AdbDiscoveryLimits invalid;
    invalid.max_addresses = 0;
    check(app::make_adb_discovery_trigger_registration(
              [](std::stop_token) { return app::AdbDiscoverySourceResult{}; },
              invalid)
              .error
              == app::AdbDiscoveryTriggerRegistrationError::invalid_limits,
          "factory must reject invalid limits");
    invalid = {};
    invalid.scan_timeout = std::chrono::milliseconds::zero();
    check(app::make_adb_discovery_trigger_registration(
              [](std::stop_token) { return app::AdbDiscoverySourceResult{}; },
              invalid)
              .error
              == app::AdbDiscoveryTriggerRegistrationError::invalid_limits,
          "factory must reject an unbounded or disabled scan deadline");
    auto made = app::make_adb_discovery_trigger_registration(
        [](std::stop_token) { return app::AdbDiscoverySourceResult{}; });
    check(made && made.registration->descriptor_name == "detect_adb",
          "factory must return only the canonical detect_adb descriptor");
}

void test_exact_python_success_envelope_and_json_escaping()
{
    auto observed = execute_one([](std::stop_token) {
        return app::AdbDiscoverySourceResult{
            {"127.0.0.1:5557", "quoted\"serial", "line\nserial"}};
    });
    const std::string expected =
        R"({"type":"command_response","command":"detect_adb","status":"ok","data":{"addresses":["127.0.0.1:5557","quoted\"serial","line\nserial"]},"timestamp":7})";
    check(observed.status == protocol::ResponseStatus::ok && observed.terminal
              && observed.json == expected,
          "success must preserve the Python addresses data envelope");

    auto empty = execute_one([](std::stop_token) {
        return app::AdbDiscoverySourceResult{};
    });
    check(empty.json
              == R"({"type":"command_response","command":"detect_adb","status":"ok","data":{"addresses":[]},"timestamp":7})",
          "no supported emulator must return a stable empty array");
}

void test_source_errors_and_exceptions_fail_closed()
{
    auto capacity = execute_one([](std::stop_token) {
        return app::AdbDiscoverySourceResult{
            {}, app::AdbDiscoverySourceError::capacity};
    });
    check(capacity.status == protocol::ResponseStatus::error
              && capacity.json.find("adb_discovery_source_capacity")
                  != std::string::npos,
          "source capacity must be an error terminal");
    auto unavailable = execute_one([](std::stop_token) {
        return app::AdbDiscoverySourceResult{
            {}, app::AdbDiscoverySourceError::unavailable};
    });
    check(unavailable.status == protocol::ResponseStatus::error
              && unavailable.json.find("adb_discovery_source_unavailable")
                  != std::string::npos,
          "source unavailability must be an error terminal");
    auto exception = execute_one([](std::stop_token) -> app::AdbDiscoverySourceResult {
        throw std::runtime_error("sensitive detail");
    });
    check(exception.status == protocol::ResponseStatus::error
              && exception.json.find("adb_discovery_source_exception")
                  != std::string::npos
              && exception.json.find("sensitive detail") == std::string::npos,
          "exceptions must fail closed without leaking details");
    auto oversized = execute_one([](std::stop_token) {
        return app::AdbDiscoverySourceResult{
            {std::string(600U * 1'024U, 'x')}};
    });
    check(oversized.status == protocol::ResponseStatus::error
              && oversized.json.find("adb_discovery_source_capacity")
                  != std::string::npos,
          "encoded address data must remain inside its independent byte budget");
}

void test_vendor_process_mapping_order_and_deduplication()
{
    std::vector<app::EmulatorProcessInfo> processes{
        {10, "dnplayer.exe", R"("C:\LDPlayer\dnplayer.exe" index=1|)", {}},
        {20, "MEmu.exe", R"("C:\MEmu\MEmu.exe" MEmu_2)", {}},
        {30, "dnplayer.exe", "dnplayer.exe index=1", {}},
        {40, "MuMuPlayer.exe", "MuMuPlayer.exe -v 3", {}},
        {50, "HD-Player.exe", "HD-Player.exe --instance Pie64", "BlueStacks_nxt"},
        {60, "nox.exe", "Nox.exe -clone:Nox_4", {}},
        {70, "unrelated.exe", "unrelated.exe", {}},
    };
    auto result = app::discover_emulator_adb_addresses(
        processes,
        [](const std::string_view instance, bool) -> std::optional<std::uint16_t> {
            return instance == "Pie64" ? std::optional<std::uint16_t>{
                                             static_cast<std::uint16_t>(5'559)}
                                       : std::nullopt;
        });
    const std::vector<std::string> expected{
        "127.0.0.1:5557",
        "127.0.0.1:21523",
        "127.0.0.1:16480",
        "127.0.0.1:5559",
        "127.0.0.1:62027",
    };
    check(result && result.addresses == expected,
          "process discovery must preserve first-seen type groups and deduplicate");
}

void test_process_policy_is_bounded_and_cancellable()
{
    app::AdbDiscoveryLimits limits;
    limits.max_processes = 1;
    std::vector<app::EmulatorProcessInfo> too_many{
        {1, "dnplayer.exe", "dnplayer.exe", {}},
        {2, "dnplayer.exe", "dnplayer.exe", {}},
    };
    check(app::discover_emulator_adb_addresses(too_many, {}, {}, limits).error
              == app::AdbDiscoverySourceError::capacity,
          "process count must be bounded");
    std::stop_source stopped;
    stopped.request_stop();
    check(app::discover_emulator_adb_addresses(
              {too_many.front()}, {}, stopped.get_token())
              .error == app::AdbDiscoverySourceError::cancelled,
          "process mapping must observe cancellation");

    const auto unreadable = app::discover_emulator_adb_addresses(
        {{3, "dnplayer.exe", {}, {}, {}, false},
         {4, "MuMuPlayer.exe", {}, {}, {}, false}},
        {});
    check(unreadable && unreadable.addresses.empty(),
          "unreadable production command lines must not guess instance-zero ports");
}

void test_vendor_resolved_mumu_address_precedes_formula_fallback()
{
    const std::vector<app::EmulatorProcessInfo> processes{
        {1, "MuMuPlayer.exe", "MuMuPlayer.exe -v 3", {}, "127.0.0.1:16481"},
    };
    const auto result = app::discover_emulator_adb_addresses(processes, {});
    check(result
              && result.addresses
                  == std::vector<std::string>{"127.0.0.1:16481"},
          "MuMuManager adb JSON must override the deterministic fallback port");
}

void test_bluestacks_region_selection_fails_closed()
{
    const auto resolve = [](const std::string_view instance, const bool china)
        -> std::optional<std::uint16_t> {
        if (instance != "Pie64") return std::nullopt;
        return china
            ? std::optional<std::uint16_t>{static_cast<std::uint16_t>(5'560)}
            : std::optional<std::uint16_t>{static_cast<std::uint16_t>(5'559)};
    };
    const auto global = app::discover_emulator_adb_addresses(
        {{1, "HD-Player.exe", "HD-Player.exe --instance Pie64",
          R"(C:\Program Files\BlueStacks_nxt\HD-Player.exe)"}},
        resolve);
    check(global && global.addresses
              == std::vector<std::string>{"127.0.0.1:5559"},
          "known global BlueStacks path must not fall back to the CN config");

    const auto china = app::discover_emulator_adb_addresses(
        {{1, "HD-Player.exe", "HD-Player.exe --instance Pie64",
          R"(C:\Program Files\BlueStacks_nxt_cn\HD-Player.exe)"}},
        resolve);
    check(china && china.addresses
              == std::vector<std::string>{"127.0.0.1:5560"},
          "known CN BlueStacks path must use only the CN config");

    const auto hidden_path = app::discover_emulator_adb_addresses(
        {{1, "HD-Player.exe", "HD-Player.exe --instance Pie64", {}}},
        resolve);
    check(hidden_path && hidden_path.addresses.empty(),
          "unknown BlueStacks region with conflicting ports must fail closed");

    const auto unambiguous = app::discover_emulator_adb_addresses(
        {{1, "HD-Player.exe", "HD-Player.exe --instance Pie64", {}}},
        [](const std::string_view, const bool china)
            -> std::optional<std::uint16_t> {
            return china
                ? std::optional<std::uint16_t>{
                      static_cast<std::uint16_t>(5'560)}
                : std::nullopt;
        });
    check(unambiguous && unambiguous.addresses
              == std::vector<std::string>{"127.0.0.1:5560"},
          "unknown BlueStacks region may use one unambiguous installed result");
}

#if defined(_WIN32) && defined(BAAS_ADB_DISCOVERY_TEST_HOOKS)
void test_mumu_json_failure_classification()
{
    const auto resolved = app::parse_mumu_adb_json_for_test(
        R"({"adb_host":"127.0.0.1","adb_port":16481,"extra":[true,null]})");
    check(resolved.state == app::MumuAdbJsonTestState::resolved
              && resolved.address == "127.0.0.1:16481",
          "valid MuMu JSON with both fields must resolve the manager address");

    const auto missing = app::parse_mumu_adb_json_for_test(
        R"({"adb_host":"127.0.0.1"})");
    check(missing.state == app::MumuAdbJsonTestState::fields_missing,
          "valid MuMu JSON missing one field must permit formula fallback");
    const auto null_field = app::parse_mumu_adb_json_for_test(
        R"({"adb_host":null,"adb_port":16481})");
    check(null_field.state == app::MumuAdbJsonTestState::fields_missing,
          "MuMu null fields must match Python's missing-value fallback");

    for (const std::string_view malformed : {
             std::string_view{"{garbage}"},
             std::string_view{R"({"adb_host":"127.0.0.1","adb_port":0})"},
             std::string_view{R"({"adb_host":"127.0.0.1","adb_port":1.5})"},
             std::string_view{R"({"adb_host":{},"adb_port":16481})"},
         }) {
        check(app::parse_mumu_adb_json_for_test(malformed).state
                  == app::MumuAdbJsonTestState::malformed,
              "invalid MuMu output must fail closed instead of using fallback");
    }
}
#endif

void test_production_android_environment_parity()
{
    EnvironmentGuard android("BAAS_ANDROID");
    EnvironmentGuard serial("BAAS_ANDROID_ADB_SERIAL");
    set_environment("BAAS_ANDROID", "yes");
    set_environment("BAAS_ANDROID_ADB_SERIAL", " localhost:5555 ");
    auto source = app::make_production_adb_discovery_source();
    check(static_cast<bool>(source), "production source must be constructible");
    auto result = source->detect({});
    const std::vector<std::string> expected{
        "localhost:5555", "127.0.0.1:5555"};
    check(result && result.addresses == expected,
          "Android candidates must trim configured serial and preserve exact order");

    set_environment("BAAS_ANDROID_ADB_SERIAL", "");
    result = source->detect({});
    check(result.addresses
              == std::vector<std::string>{
                  "127.0.0.1:5555", "localhost:5555"},
          "Android defaults must match Python");
}

}  // namespace

int main(const int argc, char** argv)
{
    try {
        if (argc == 2 && std::string_view{argv[1]} == "--live") {
            auto source = app::make_production_adb_discovery_source();
            if (!source) throw std::runtime_error("production source unavailable");
            const auto result = source->detect({});
            if (!result) {
                std::cerr << app::adb_discovery_source_error_name(result.error)
                          << '\n';
                return 1;
            }
            for (const auto& address : result.addresses) {
                std::cout << address << '\n';
            }
            return 0;
        }
#if defined(_WIN32) && defined(BAAS_ADB_DISCOVERY_TEST_HOOKS)
        if (argc == 3 && std::string_view{argv[1]} == "--path-safe") {
            const auto path = std::filesystem::path{argv[2]}.wstring();
            std::cout << (app::local_vendor_file_is_safe_for_test(path)
                              ? "safe" : "unsafe")
                      << '\n';
            return 0;
        }
#endif
        test_factory_is_exact_and_fail_closed();
        test_exact_python_success_envelope_and_json_escaping();
        test_source_errors_and_exceptions_fail_closed();
        test_vendor_process_mapping_order_and_deduplication();
        test_process_policy_is_bounded_and_cancellable();
        test_vendor_resolved_mumu_address_precedes_formula_fallback();
        test_bluestacks_region_selection_fails_closed();
#if defined(_WIN32) && defined(BAAS_ADB_DISCOVERY_TEST_HOOKS)
        test_mumu_json_failure_classification();
#endif
        test_production_android_environment_parity();
        if (failures != 0) return 1;
        std::cout << "AdbDiscoveryTriggerRegistration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "AdbDiscoveryTriggerRegistration test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
