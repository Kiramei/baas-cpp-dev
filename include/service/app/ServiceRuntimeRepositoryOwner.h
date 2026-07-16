#pragma once

#include "runtime/repository/RuntimeRepositorySnapshot.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace baas::service::app {

enum class ServiceRuntimeRepositoryPhase {
    unavailable,
    pinned,
};

enum class ServiceRuntimeRepositoryOpenError {
    none,
    invalid_activation,
    internal_error,
};

[[nodiscard]] std::string_view service_runtime_repository_phase_name(
    ServiceRuntimeRepositoryPhase phase) noexcept;
[[nodiscard]] std::string_view service_runtime_repository_open_error_name(
    ServiceRuntimeRepositoryOpenError error) noexcept;

// Owns the immutable runtime-repository generation selected during service
// composition. It deliberately has no reload operation: every consumer that
// retains pin() observes one complete generation for the service lifetime.
class ServiceRuntimeRepositoryOwner final {
public:
    explicit ServiceRuntimeRepositoryOwner(
        std::shared_ptr<const runtime::repository::RuntimeRepositorySnapshot> pin);
    ~ServiceRuntimeRepositoryOwner();

    ServiceRuntimeRepositoryOwner(const ServiceRuntimeRepositoryOwner&) = delete;
    ServiceRuntimeRepositoryOwner& operator=(const ServiceRuntimeRepositoryOwner&) = delete;

    [[nodiscard]] ServiceRuntimeRepositoryPhase phase() const noexcept;
    [[nodiscard]] const std::string& generation() const noexcept;
    [[nodiscard]] std::shared_ptr<const runtime::repository::RuntimeRepositorySnapshot>
        pin() const noexcept;

private:
    std::shared_ptr<const runtime::repository::RuntimeRepositorySnapshot> pin_;
    std::string generation_;
};

struct ServiceRuntimeRepositoryOpenResult {
    std::unique_ptr<ServiceRuntimeRepositoryOwner> owner;
    ServiceRuntimeRepositoryOpenError error{ServiceRuntimeRepositoryOpenError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ServiceRuntimeRepositoryOpenError::none && owner != nullptr;
    }
};

// project_root is the BAAS project root. A missing current.json creates an
// unavailable owner. Once any current entry exists, all activation failures
// are fatal so malformed or tampered state can never silently fall back.
[[nodiscard]] ServiceRuntimeRepositoryOpenResult open_service_runtime_repository_owner(
    const std::filesystem::path& project_root) noexcept;

}  // namespace baas::service::app
