#pragma once

#include "runtime/repository/RuntimeRepositorySnapshot.h"
#include "runtime/script/RuntimeScriptRepositoryTrustEvidence.h"
#include "service/app/RuntimeRepositoryTrustedPlanState.h"

#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>

namespace baas::service::app {

enum class ServiceRuntimeRepositoryPhase {
    unavailable,
    pinned,
};

enum class ServiceRuntimeRepositoryOpenError {
    none,
    invalid_expected_generation,
    generation_mismatch,
    invalid_activation,
    trusted_state_invalid,
    trusted_state_generation_mismatch,
    trusted_state_pending_recovery,
    internal_error,
};

[[nodiscard]] std::string_view service_runtime_repository_phase_name(
    ServiceRuntimeRepositoryPhase phase) noexcept;
[[nodiscard]] std::string_view service_runtime_repository_open_error_name(
    ServiceRuntimeRepositoryOpenError error) noexcept;

struct ServiceRuntimeRepositoryOpenResult;

// Owns the immutable runtime-repository generation selected during service
// composition. It deliberately has no reload operation: every consumer that
// retains pin() observes one complete generation for the service lifetime.
// Only the native open factory can attach script trust evidence; constructing
// an owner from a snapshot alone remains useful for non-production embedding
// but never grants script execution trust.
class ServiceRuntimeRepositoryOwner final {
public:
    explicit ServiceRuntimeRepositoryOwner(
        std::shared_ptr<const ::baas::runtime::repository::RuntimeRepositorySnapshot> pin);
    ~ServiceRuntimeRepositoryOwner();

    ServiceRuntimeRepositoryOwner(const ServiceRuntimeRepositoryOwner&) = delete;
    ServiceRuntimeRepositoryOwner& operator=(const ServiceRuntimeRepositoryOwner&) = delete;

    [[nodiscard]] ServiceRuntimeRepositoryPhase phase() const noexcept;
    [[nodiscard]] const std::string& generation() const noexcept;
    [[nodiscard]] std::shared_ptr<const ::baas::runtime::repository::RuntimeRepositorySnapshot>
        pin() const noexcept;
    [[nodiscard]] std::shared_ptr<const ::baas::runtime::repository::RuntimeRepositoryReadBundle>
        open_read_bundle(::baas::runtime::repository::RuntimeRepositoryReadLimits limits = {},
                         std::stop_token stop_token = {}) const;
    [[nodiscard]] std::shared_ptr<const ::baas::runtime::script::
        RuntimeScriptRepositoryTrustEvidence> script_trust_evidence() const noexcept;

private:
    ServiceRuntimeRepositoryOwner(
        std::shared_ptr<const ::baas::runtime::repository::RuntimeRepositorySnapshot> pin,
        std::shared_ptr<const ::baas::runtime::script::RuntimeScriptRepositoryTrustEvidence>
            script_trust_evidence);

    std::shared_ptr<const ::baas::runtime::repository::RuntimeRepositorySnapshot> pin_;
    std::string generation_;
    std::shared_ptr<const ::baas::runtime::script::RuntimeScriptRepositoryTrustEvidence>
        script_trust_evidence_;

    friend ServiceRuntimeRepositoryOpenResult open_service_runtime_repository_owner(
        const std::filesystem::path&, std::string_view) noexcept;
};

struct ServiceRuntimeRepositoryOpenResult {
    std::unique_ptr<ServiceRuntimeRepositoryOwner> owner;
    ServiceRuntimeRepositoryOpenError error{ServiceRuntimeRepositoryOpenError::none};
    RuntimeRepositoryTrustedPlanStateError trusted_state_error{
        RuntimeRepositoryTrustedPlanStateError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ServiceRuntimeRepositoryOpenError::none && owner != nullptr;
    }
};

// project_root is the BAAS project root. expected_generation is the exact
// generation selected by the publisher for this process start. Missing state,
// or a valid activation for any other generation, fails closed. Activation is
// performed once and the comparison uses that retained immutable pin.
[[nodiscard]] ServiceRuntimeRepositoryOpenResult open_service_runtime_repository_owner(
    const std::filesystem::path& project_root,
    std::string_view expected_generation) noexcept;

}  // namespace baas::service::app
