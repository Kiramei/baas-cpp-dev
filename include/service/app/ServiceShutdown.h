#pragma once

#include "service/router/Router.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

namespace baas::service::app {

struct ServiceSignalBlockResult;
struct ServiceSignalOwnerOpenResult;

enum class ServiceShutdownReason {
    none,
    http_request,
    interrupt,
    terminate,
    signal_failure,
};

[[nodiscard]] std::string_view service_shutdown_reason_name(
    ServiceShutdownReason reason) noexcept;

// Process-wide first-wins shutdown intent. Request paths perform only one
// atomic compare/exchange and a waiter notification. The application main
// thread remains the sole owner allowed to stop hosts or join runtime workers.
class ServiceShutdownCoordinator final : public router::ShutdownIntent {
public:
    ServiceShutdownCoordinator() noexcept = default;

    ServiceShutdownCoordinator(const ServiceShutdownCoordinator&) = delete;
    ServiceShutdownCoordinator& operator=(const ServiceShutdownCoordinator&) = delete;
    ServiceShutdownCoordinator(ServiceShutdownCoordinator&&) = delete;
    ServiceShutdownCoordinator& operator=(ServiceShutdownCoordinator&&) = delete;

    [[nodiscard]] router::ShutdownDecision request_shutdown() noexcept override;
    [[nodiscard]] router::ShutdownDecision request(
        ServiceShutdownReason reason) noexcept;

    // Blocks until the first request and returns its immutable reason.
    [[nodiscard]] ServiceShutdownReason wait();
    [[nodiscard]] std::optional<ServiceShutdownReason> wait_for(
        std::chrono::milliseconds timeout);
    [[nodiscard]] ServiceShutdownReason reason() const noexcept;

private:
    std::atomic<ServiceShutdownReason> reason_{ServiceShutdownReason::none};
    mutable std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
};

enum class ServiceSignalError {
    none,
    signal_block_failed,
    invalid_coordinator,
    invalid_signal_block,
    already_active,
    event_creation_failed,
    handler_registration_failed,
    thread_start_failed,
};

[[nodiscard]] std::string_view service_signal_error_name(
    ServiceSignalError error) noexcept;

// RAII token proving that SIGINT/SIGTERM were blocked on the current POSIX
// thread before application worker creation. It is a no-op ownership token on
// Windows. Destroy it on the same thread that called block_service_shutdown_signals().
class ServiceSignalBlock final {
public:
    ~ServiceSignalBlock();

    ServiceSignalBlock(const ServiceSignalBlock&) = delete;
    ServiceSignalBlock& operator=(const ServiceSignalBlock&) = delete;
    ServiceSignalBlock(ServiceSignalBlock&&) = delete;
    ServiceSignalBlock& operator=(ServiceSignalBlock&&) = delete;

private:
    class Impl;
    explicit ServiceSignalBlock(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;

    friend struct ServiceSignalBlockResult;
    friend struct ServiceSignalOwnerOpenResult;
    friend ServiceSignalBlockResult block_service_shutdown_signals() noexcept;
    friend ServiceSignalOwnerOpenResult open_service_signal_owner(
        std::shared_ptr<ServiceShutdownCoordinator>,
        std::unique_ptr<ServiceSignalBlock>) noexcept;
};

struct ServiceSignalBlockResult {
    std::unique_ptr<ServiceSignalBlock> block;
    ServiceSignalError error{ServiceSignalError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ServiceSignalError::none && block != nullptr;
    }
};

// Must be called on the future application-main thread before any application
// thread is created. Later threads inherit the POSIX signal mask.
[[nodiscard]] ServiceSignalBlockResult block_service_shutdown_signals() noexcept;

class ServiceSignalOwner final {
public:
    ~ServiceSignalOwner();

    ServiceSignalOwner(const ServiceSignalOwner&) = delete;
    ServiceSignalOwner& operator=(const ServiceSignalOwner&) = delete;
    ServiceSignalOwner(ServiceSignalOwner&&) = delete;
    ServiceSignalOwner& operator=(ServiceSignalOwner&&) = delete;

    // Idempotent. Returns only after the platform waiter thread has joined and
    // no later platform event can reach the coordinator through this owner.
    void stop() noexcept;

#if defined(BAAS_SERVICE_SHUTDOWN_TEST_HOOKS)
    [[nodiscard]] bool notify_for_test(ServiceShutdownReason reason) noexcept;
#endif

private:
    class Impl;
    explicit ServiceSignalOwner(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;

    friend struct ServiceSignalOwnerOpenResult;
    friend ServiceSignalOwnerOpenResult open_service_signal_owner(
        std::shared_ptr<ServiceShutdownCoordinator>,
        std::unique_ptr<ServiceSignalBlock>) noexcept;
};

struct ServiceSignalOwnerOpenResult {
    std::unique_ptr<ServiceSignalOwner> owner;
    ServiceSignalError error{ServiceSignalError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ServiceSignalError::none && owner != nullptr;
    }
};

// Consumes the signal-block token so its original mask remains installed for
// the complete lifetime of the platform waiter.
[[nodiscard]] ServiceSignalOwnerOpenResult open_service_signal_owner(
    std::shared_ptr<ServiceShutdownCoordinator> coordinator,
    std::unique_ptr<ServiceSignalBlock> signal_block) noexcept;

}  // namespace baas::service::app
