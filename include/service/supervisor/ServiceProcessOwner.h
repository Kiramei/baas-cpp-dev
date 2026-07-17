#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace baas::service::supervisor {

inline constexpr std::string_view service_process_loopback_host = "127.0.0.1";

struct ServiceProcessConfig {
    std::filesystem::path service_executable;
    std::filesystem::path trusted_application_root;
    std::filesystem::path safe_working_directory;
    std::filesystem::path project_root;
    std::string host{service_process_loopback_host};
    std::uint16_t port = 0;
    std::string runtime_repository_generation;
};

enum class ServiceProcessState : std::uint8_t {
    stopped,
    running,
    exited,
};

enum class ServiceProcessError : std::uint8_t {
    none,
    invalid_configuration,
    already_active,
    launch_failed,
    wait_failed,
    terminate_failed,
};

[[nodiscard]] std::string_view service_process_error_name(
    ServiceProcessError error) noexcept;

struct ServiceProcessExit {
    int exit_code = 0;
    bool signaled = false;
    int signal = 0;
};

struct ServiceProcessStartResult {
    ServiceProcessError error = ServiceProcessError::launch_failed;
    std::uint64_t process_id = 0;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ServiceProcessError::none && process_id != 0;
    }
};

enum class ServiceProcessWaitDisposition : std::uint8_t {
    exited,
    timed_out,
    not_running,
    failed,
};

struct ServiceProcessWaitResult {
    ServiceProcessWaitDisposition disposition =
        ServiceProcessWaitDisposition::failed;
    ServiceProcessError error = ServiceProcessError::wait_failed;
    std::optional<ServiceProcessExit> exit;
};

// Owns exactly one BAAS_service child at a time. A trusted native host (never a
// browser payload) resolves the application root, executable and working
// directory before start. The executable and working directory must be inside
// that root. This class never performs PATH lookup, inherits the parent
// environment, or constructs a shell command. All public operations are
// thread-safe.
class ServiceProcessOwner final {
public:
    ServiceProcessOwner();
    ~ServiceProcessOwner();

    ServiceProcessOwner(const ServiceProcessOwner&) = delete;
    ServiceProcessOwner& operator=(const ServiceProcessOwner&) = delete;
    ServiceProcessOwner(ServiceProcessOwner&&) = delete;
    ServiceProcessOwner& operator=(ServiceProcessOwner&&) = delete;

    [[nodiscard]] ServiceProcessStartResult start(ServiceProcessConfig config);
    [[nodiscard]] ServiceProcessWaitResult wait_for(
        std::chrono::milliseconds timeout) noexcept;

    // Force-terminates the owned process group/job and reaps it. Repeated calls
    // after a completed stop are successful no-ops.
    [[nodiscard]] ServiceProcessError stop(
        std::chrono::milliseconds timeout = std::chrono::seconds{5}) noexcept;

    [[nodiscard]] ServiceProcessState state() const noexcept;
    [[nodiscard]] std::uint64_t process_id() const noexcept;

#if defined(BAAS_SERVICE_PROCESS_OWNER_TEST_HOOKS)
    [[nodiscard]] bool emergency_reaper_ready_for_tests() const noexcept;
#endif

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace baas::service::supervisor
