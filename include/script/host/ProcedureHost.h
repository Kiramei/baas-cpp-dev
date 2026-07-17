#pragma once

#include "script/host/ProcedureSnapshot.h"
#include "script/runtime/SynchronousHost.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace baas::script::host {

enum class ProcedureEffectStage : std::uint8_t { Began, Committed, Unknown };

// Executor callbacks may report from helper threads. This callback is noexcept,
// bounded, and allocation-free; false means an undeclared/invalid effect was seen.
class ProcedureEffectReporter {
public:
    virtual ~ProcedureEffectReporter() = default;
    [[nodiscard]] virtual bool report(
        ProcedureEffect effect, ProcedureEffectStage stage) noexcept = 0;
};

enum class ProcedureExecutorErrorCode : std::uint8_t {
    InvalidRequest,
    Cancelled,
    DeadlineExceeded,
    BudgetExceeded,
    Unavailable,
    ForegroundPackageMismatch,
    DeviceDisconnected,
    ResourceNotFound,
    Internal,
};

struct ProcedureExecutorError {
    ProcedureExecutorErrorCode code{ProcedureExecutorErrorCode::Internal};
    std::string message;
    bool retryable{};
    runtime::HostEffectState effect_state{runtime::HostEffectState::Unknown};
};

class ProcedureExecutorOutcome final {
public:
    [[nodiscard]] static ProcedureExecutorOutcome success(std::string terminal_id);
    [[nodiscard]] static ProcedureExecutorOutcome failure(ProcedureExecutorError error);

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const std::string& terminal_id() const;
    [[nodiscard]] const ProcedureExecutorError& error() const;

private:
    explicit ProcedureExecutorOutcome(
        std::variant<std::string, ProcedureExecutorError> value);
    std::variant<std::string, ProcedureExecutorError> value_;
};

class ProcedureExecutionRequest final {
public:
    ProcedureExecutionRequest(
        std::shared_ptr<const ProcedureSnapshot> snapshot,
        std::shared_ptr<const ProcedureDescriptor> procedure,
        std::string device_id,
        runtime::JsonObject options,
        std::shared_ptr<const runtime::HostCancellationProbe> cancellation,
        ProcedureEffectReporter& effects);

    [[nodiscard]] const std::shared_ptr<const ProcedureSnapshot>& snapshot() const noexcept;
    [[nodiscard]] const std::shared_ptr<const ProcedureDescriptor>& procedure() const noexcept;
    [[nodiscard]] const std::string& device_id() const noexcept;
    [[nodiscard]] const runtime::JsonObject& options() const noexcept;
    [[nodiscard]] bool cancelled() const noexcept;
    [[nodiscard]] bool deadline_exceeded() const noexcept;
    [[nodiscard]] ProcedureEffectReporter& effects() const noexcept;

private:
    std::shared_ptr<const ProcedureSnapshot> snapshot_;
    std::shared_ptr<const ProcedureDescriptor> procedure_;
    std::string device_id_;
    runtime::JsonObject options_;
    std::shared_ptr<const runtime::HostCancellationProbe> cancellation_;
    ProcedureEffectReporter* effects_{};
};

class ProcedureExecutor {
public:
    virtual ~ProcedureExecutor() = default;
    // The production wrapper catches every exception before the Host ABI.
    // Implementations must poll request cancellation/deadline during bounded work.
    [[nodiscard]] virtual ProcedureExecutorOutcome execute(
        const ProcedureExecutionRequest& request) = 0;
};

struct PhysicalDeviceCoordinatorLimits {
    std::size_t max_devices{1'024};
    std::size_t max_waiters{4'096};
    std::size_t max_device_id_bytes{256};
    std::chrono::milliseconds poll_interval{5};
};

enum class PhysicalDeviceAcquireCode : std::uint8_t {
    Acquired,
    Cancelled,
    DeadlineExceeded,
    Reentrant,
    Backpressure,
    Shutdown,
    InvalidDeviceId,
};

struct PhysicalDeviceAcquireResult {
    PhysicalDeviceAcquireCode code{PhysicalDeviceAcquireCode::Shutdown};
    // Opaque RAII lease. Destruction releases the physical-device strand.
    std::shared_ptr<void> lease;
};

struct PhysicalDeviceCoordinatorStats {
    std::size_t active_devices{};
    std::size_t waiters{};
    bool shutdown{};
};

class PhysicalDeviceCoordinator final
    : public std::enable_shared_from_this<PhysicalDeviceCoordinator> {
public:
    ~PhysicalDeviceCoordinator();
    PhysicalDeviceCoordinator(const PhysicalDeviceCoordinator&) = delete;
    PhysicalDeviceCoordinator& operator=(const PhysicalDeviceCoordinator&) = delete;

    [[nodiscard]] static std::shared_ptr<PhysicalDeviceCoordinator> create(
        PhysicalDeviceCoordinatorLimits limits = {});
    [[nodiscard]] PhysicalDeviceAcquireResult acquire(
        std::string_view device_id,
        const std::shared_ptr<const runtime::HostCancellationProbe>& cancellation);
    void shutdown() noexcept;
    [[nodiscard]] PhysicalDeviceCoordinatorStats stats() const noexcept;

private:
    struct Impl;
    explicit PhysicalDeviceCoordinator(std::unique_ptr<Impl> impl) noexcept;
    void release(std::string_view device_id) noexcept;
    std::unique_ptr<Impl> impl_;
};

struct ProcedureHostLimits {
    std::size_t max_device_id_bytes{256};
    std::size_t max_option_depth{32};
    std::size_t max_option_nodes{16'384};
    std::size_t max_option_bytes{1U * 1024U * 1024U};
    std::size_t max_option_work{65'536};
    std::size_t max_calls{1'000'000};
    std::size_t max_executor_message_bytes{1'024};
};

struct ProcedureHostStats {
    std::size_t calls{};
    std::size_t completed{};
    std::size_t failed{};
    std::size_t cancelled_while_waiting{};
};

class ProcedureHost final {
public:
    ~ProcedureHost();
    ProcedureHost(const ProcedureHost&) = delete;
    ProcedureHost& operator=(const ProcedureHost&) = delete;

    [[nodiscard]] const std::shared_ptr<const ProcedureSnapshot>& snapshot() const noexcept;
    [[nodiscard]] const std::string& device_id() const noexcept;
    [[nodiscard]] ProcedureHostStats stats() const noexcept;

private:
    friend struct ProcedureHostRuntime;
    friend ProcedureHostRuntime make_procedure_host_runtime(
        std::shared_ptr<const ProcedureSnapshot>, std::string,
        std::shared_ptr<ProcedureExecutor>, std::shared_ptr<PhysicalDeviceCoordinator>,
        ProcedureHostLimits);
    struct Impl;
    explicit ProcedureHost(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

struct ProcedureHostRuntime {
    std::shared_ptr<ProcedureHost> host;
    std::shared_ptr<const runtime::HostModuleRegistry> metadata;
    std::shared_ptr<const runtime::SynchronousNativeBindingSet> bindings;
};

[[nodiscard]] ProcedureHostRuntime make_procedure_host_runtime(
    std::shared_ptr<const ProcedureSnapshot> snapshot,
    std::string device_id,
    std::shared_ptr<ProcedureExecutor> executor,
    std::shared_ptr<PhysicalDeviceCoordinator> coordinator,
    ProcedureHostLimits limits = {});

[[nodiscard]] bool valid_physical_device_id(
    std::string_view value, std::size_t max_bytes = 256) noexcept;

}  // namespace baas::script::host
