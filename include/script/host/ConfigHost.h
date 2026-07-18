#pragma once

#include "script/runtime/SynchronousHost.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace baas::script::host {

struct ConfigHostIdentity final {
    std::string config_id;
    std::string snapshot_id;
    friend bool operator==(const ConfigHostIdentity&, const ConfigHostIdentity&) = default;
};

// This value is created by the application while it pins one task request.
// ConfigHost retains the exact immutable object for the complete script run.
struct ConfigHostSnapshot final {
    ConfigHostIdentity identity;
    std::int64_t revision{};
    runtime::JsonObject values;
};

struct ConfigPatchEntry final {
    // Decoded RFC 6901 path segments. The root path is never accepted.
    std::vector<std::string> path;
    runtime::JsonValue value;
    friend bool operator==(const ConfigPatchEntry&, const ConfigPatchEntry&) = default;
};

struct ConfigCommit final {
    std::int64_t revision{};
    std::string snapshot_id;
    friend bool operator==(const ConfigCommit&, const ConfigCommit&) = default;
};

enum class ConfigPortErrorCode : std::uint8_t {
    Conflict = 1,
    InvalidPatch,
    Cancelled,
    DeadlineExceeded,
    Unavailable,
    Internal,
};

struct ConfigPortError final {
    ConfigPortErrorCode code{ConfigPortErrorCode::Internal};
    bool retryable{};
    runtime::HostEffectState effect_state{runtime::HostEffectState::NotStarted};
};

using ConfigPortTransactionResult = std::variant<ConfigCommit, ConfigPortError>;

struct ConfigTransactionRequest final {
    std::int64_t expected_revision{};
    std::vector<ConfigPatchEntry> patch;
    std::shared_ptr<const runtime::HostCancellationProbe> cancellation;

    [[nodiscard]] bool cancelled() const noexcept {
        return cancellation && cancellation->cancelled();
    }
    [[nodiscard]] bool deadline_exceeded() const noexcept {
        return cancellation && cancellation->deadline_exceeded();
    }
};

// Production implementations own persistence and the config_id strand. They
// must perform one atomic compare-and-swap transaction, must not retain a
// reference into ConfigTransactionRequest, and must not expose paths or ambient
// singleton state. The host validates JSON/path structure before this boundary;
// the port additionally performs application-schema validation atomically.
class ConfigHostPort {
public:
    virtual ~ConfigHostPort() = default;
    // The returned request-pinned identity is immutable for the port lifetime.
    [[nodiscard]] virtual const ConfigHostIdentity& identity() const noexcept = 0;
    [[nodiscard]] virtual ConfigPortTransactionResult transact(
        ConfigTransactionRequest request) = 0;

protected:
    ConfigHostPort() = default;
};

struct ConfigHostLimits final {
    std::size_t max_identity_bytes{1'024};
    std::size_t max_path_bytes{4'096};
    std::size_t max_path_segments{64};
    std::size_t max_patch_entries{256};
    std::size_t max_read_operations{4'096};
    std::size_t max_write_operations{256};
    std::size_t max_total_read_bytes{64U * 1024U * 1024U};
    std::size_t max_total_write_bytes{64U * 1024U * 1024U};
    std::size_t cooperative_check_interval{256};
    runtime::JsonBridgeLimits json{};
};

struct ConfigHostStats final {
    std::size_t snapshot_reads{};
    std::size_t value_reads{};
    std::size_t read_bytes{};
    std::size_t transaction_attempts{};
    std::size_t transaction_commits{};
    std::size_t transaction_conflicts{};
    std::size_t write_bytes{};
};

struct ConfigHostRuntime;

class ConfigHost final {
public:
    ~ConfigHost();
    ConfigHost(const ConfigHost&) = delete;
    ConfigHost& operator=(const ConfigHost&) = delete;

    [[nodiscard]] const std::shared_ptr<const ConfigHostSnapshot>&
        pinned_snapshot() const noexcept;
    [[nodiscard]] const std::shared_ptr<ConfigHostPort>& port() const noexcept;
    [[nodiscard]] ConfigHostStats stats() const noexcept;

private:
    friend struct ConfigHostRuntime;
    friend ConfigHostRuntime make_config_host_runtime(
        std::shared_ptr<const ConfigHostSnapshot>,
        std::shared_ptr<ConfigHostPort>, ConfigHostLimits);
    struct Impl;
    explicit ConfigHost(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

struct ConfigHostRuntime final {
    std::shared_ptr<ConfigHost> host;
    std::shared_ptr<const runtime::HostModuleRegistry> metadata;
    std::shared_ptr<const runtime::SynchronousNativeBindingSet> bindings;
};

[[nodiscard]] ConfigHostRuntime make_config_host_runtime(
    std::shared_ptr<const ConfigHostSnapshot> snapshot,
    std::shared_ptr<ConfigHostPort> port,
    ConfigHostLimits limits = {});

}  // namespace baas::script::host
