#pragma once

#include "service/app/ServiceCommandLine.h"
#include "service/app/ServiceShutdown.h"
#include "service/auth/AuthOwner.h"
#include "service/http/HttpHost.h"
#include "service/router/Router.h"
#include "service/trigger/TriggerExecutor.h"

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace baas::service::app {

// Process/package identity is deliberately distinct from the protocol identity.
// baas-tauri launches BAAS_service(.exe), while its strict readiness probe
// accepts only /version.service == "BAAS Service".
inline constexpr std::string_view service_application_executable_name =
    "BAAS_service";
inline constexpr std::string_view service_application_wire_name =
    "BAAS Service";
#if !defined(BAAS_SERVICE_VERSION)
#define BAAS_SERVICE_VERSION "1.1.1"
#endif
inline constexpr std::string_view service_application_version =
    BAAS_SERVICE_VERSION;

enum class ServiceApplicationError : std::uint8_t {
    none,
    invalid_options,
    pipe_transport_unavailable,
    signal_setup_failed,
    trigger_registration_failed,
    trigger_dispatch_failed,
    remote_resource_unavailable,
    runtime_repository_invalid,
    composition_failed,
    authentication_failed,
    host_start_failed,
    readiness_failed,
    internal_failure,
};

[[nodiscard]] std::string_view service_application_error_name(
    ServiceApplicationError error) noexcept;

// Stable process exit contract. Informational dispositions and an orderly
// HTTP/signal shutdown return success.
enum class ServiceProcessExit : int {
    success = 0,
    command_line = 2,
    pipe_unavailable = 3,
    signal_setup = 4,
    composition = 5,
    host_start = 6,
    readiness = 7,
    internal_failure = 8,
};

class ServiceApplication;

struct ServiceApplicationOpenResult {
    std::unique_ptr<ServiceApplication> application;
    ServiceApplicationError error{ServiceApplicationError::none};
    auth::AuthError authentication_error{auth::AuthError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ServiceApplicationError::none && application != nullptr;
    }
};

// Production composition owner. Opening performs no socket bind, but does
// acquire the persistent auth installation lock and starts the bounded trigger
// executor and signal owner. Pipe is rejected before any of those effects.
class ServiceApplication final {
public:
    [[nodiscard]] static ServiceApplicationOpenResult open(
        ServiceRunOptions options) noexcept;
    ~ServiceApplication();

    ServiceApplication(const ServiceApplication&) = delete;
    ServiceApplication& operator=(const ServiceApplication&) = delete;
    ServiceApplication(ServiceApplication&&) = delete;
    ServiceApplication& operator=(ServiceApplication&&) = delete;

    // The two explicit stages make the readiness transition observable:
    // start_transport() exposes /health as 503 starting, then publish_ready()
    // publishes the complete AuthOwner-derived snapshot atomically.
    [[nodiscard]] http::HttpHostStartResult start_transport() noexcept;
    [[nodiscard]] bool publish_ready() noexcept;
    [[nodiscard]] ServiceShutdownReason wait_for_shutdown();
    [[nodiscard]] std::optional<ServiceShutdownReason> wait_for_shutdown(
        std::chrono::milliseconds timeout);

    // Idempotent reverse-order teardown: failed readiness, HTTP host, trigger
    // executor, then platform signal owner.
    void stop() noexcept;

    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] router::HealthReadinessSnapshot readiness_snapshot() const;

    // Embedding diagnostic capability for the exact executor owned by the
    // production trigger WebSocket factory. It does not expose a transport.
    [[nodiscard]] std::shared_ptr<trigger::TriggerExecutor>
        trigger_executor() const noexcept;

private:
    class Impl;
    explicit ServiceApplication(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

// Parses arguments excluding argv[0], prints stable help/version/diagnostics,
// runs until the first shutdown reason, and returns ServiceProcessExit.
[[nodiscard]] int run_service_application(
    std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& diagnostics) noexcept;

}  // namespace baas::service::app
