#pragma once

#include "service/trigger/TriggerDispatch.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::app {

inline constexpr std::size_t adb_discovery_hard_max_processes = 16'384;
inline constexpr std::size_t adb_discovery_hard_max_command_line_bytes =
    128U * 1'024U;
inline constexpr std::size_t adb_discovery_hard_max_addresses = 4'096;
inline constexpr std::size_t adb_discovery_hard_max_json_bytes =
    4U * 1'024U * 1'024U;
inline constexpr auto adb_discovery_hard_max_scan_timeout =
    std::chrono::seconds{30};
inline constexpr auto adb_discovery_hard_max_vendor_query_timeout =
    std::chrono::seconds{10};

struct AdbDiscoveryLimits {
    std::size_t max_processes{4'096};
    std::size_t max_command_line_bytes{32U * 1'024U};
    std::size_t max_addresses{1'024};
    std::size_t max_json_bytes{512U * 1'024U};
    std::chrono::milliseconds scan_timeout{5'000};
    std::chrono::milliseconds vendor_query_timeout{3'000};
};

struct EmulatorProcessInfo {
    std::uint64_t pid{};
    std::string executable_name;
    std::string command_line;
    std::string executable_path;
    // Optional vendor API result captured by the platform enumerator. Pure
    // policy tests normally leave this empty and exercise deterministic rules.
    std::string resolved_adb_address;
    // Pure policy inputs default to a known command line. Production flips
    // this false until the native process query succeeds, so access-denied
    // processes are skipped instead of being guessed as instance zero.
    bool command_line_available{true};
};

enum class AdbDiscoverySourceError : std::uint8_t {
    none,
    cancelled,
    capacity,
    unavailable,
};

[[nodiscard]] std::string_view adb_discovery_source_error_name(
    AdbDiscoverySourceError error) noexcept;

struct AdbDiscoverySourceResult {
    std::vector<std::string> addresses;
    AdbDiscoverySourceError error{AdbDiscoverySourceError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == AdbDiscoverySourceError::none;
    }
};

// Production discovery mirrors Python ServiceRuntime.detect_adb(): Android
// returns the configured/default bridge candidates, while desktop discovers
// emulator processes and maps their instance arguments to loopback ADB ports.
// Implementations are synchronous because TriggerExecutor already owns the
// bounded worker; they must observe stop while enumerating processes.
class AdbDiscoverySource {
public:
    virtual ~AdbDiscoverySource() = default;
    [[nodiscard]] virtual AdbDiscoverySourceResult detect(
        std::stop_token stop) = 0;
};

using AdbDiscoverySourceCallback =
    std::function<AdbDiscoverySourceResult(std::stop_token)>;
using BlueStacksPortResolver = std::function<std::optional<std::uint16_t>(
    std::string_view instance, bool china)>;

#if defined(_WIN32) && defined(BAAS_ADB_DISCOVERY_TEST_HOOKS)
enum class MumuAdbJsonTestState : std::uint8_t {
    resolved,
    fields_missing,
    capacity,
    malformed,
};

struct MumuAdbJsonTestResult {
    MumuAdbJsonTestState state{MumuAdbJsonTestState::malformed};
    std::string address;
};

// Private test seam for the bounded vendor JSON grammar. This symbol is not
// compiled into production builds.
[[nodiscard]] MumuAdbJsonTestResult parse_mumu_adb_json_for_test(
    std::string_view json) noexcept;
[[nodiscard]] bool local_vendor_file_is_safe_for_test(
    std::wstring_view path) noexcept;
#endif

// Pure process-to-address policy used by the production source and tests. It
// preserves process order, de-duplicates first occurrence, and never returns
// emulator-* aliases for a loopback endpoint representing the same instance.
[[nodiscard]] AdbDiscoverySourceResult discover_emulator_adb_addresses(
    const std::vector<EmulatorProcessInfo>& processes,
    BlueStacksPortResolver bluestacks_port,
    std::stop_token stop = {},
    AdbDiscoveryLimits limits = {}) noexcept;

[[nodiscard]] std::shared_ptr<AdbDiscoverySource>
make_production_adb_discovery_source(
    AdbDiscoveryLimits limits = {}) noexcept;

enum class AdbDiscoveryTriggerRegistrationError : std::uint8_t {
    none,
    missing_source,
    empty_callback,
    invalid_limits,
    resource_exhausted,
};

[[nodiscard]] std::string_view adb_discovery_trigger_registration_error_name(
    AdbDiscoveryTriggerRegistrationError error) noexcept;

struct AdbDiscoveryTriggerRegistrationResult {
    std::optional<trigger::TriggerHandlerRegistration> registration;
    AdbDiscoveryTriggerRegistrationError error{
        AdbDiscoveryTriggerRegistrationError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == AdbDiscoveryTriggerRegistrationError::none
            && registration.has_value();
    }
};

[[nodiscard]] AdbDiscoveryTriggerRegistrationResult
make_adb_discovery_trigger_registration(
    std::shared_ptr<AdbDiscoverySource> source,
    AdbDiscoveryLimits limits = {}) noexcept;

[[nodiscard]] AdbDiscoveryTriggerRegistrationResult
make_adb_discovery_trigger_registration(
    AdbDiscoverySourceCallback callback,
    AdbDiscoveryLimits limits = {}) noexcept;

}  // namespace baas::service::app
