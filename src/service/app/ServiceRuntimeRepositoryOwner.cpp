#include "service/app/ServiceRuntimeRepositoryOwner.h"

#include <new>
#include <system_error>
#include <utility>

namespace baas::service::app {
namespace {

class ExactRuntimeScriptRepositoryTrustEvidence final
    : public runtime::script::RuntimeScriptRepositoryTrustEvidence {
public:
    ExactRuntimeScriptRepositoryTrustEvidence(
        std::string generation, std::string scripts_commit) noexcept
        : generation_(std::move(generation)),
          scripts_commit_(std::move(scripts_commit))
    {}

    [[nodiscard]] bool covers(
        const std::string_view generation,
        const std::string_view scripts_commit) const noexcept override
    {
        return generation == generation_ && scripts_commit == scripts_commit_;
    }

private:
    const std::string generation_;
    const std::string scripts_commit_;
};

[[nodiscard]] bool is_missing(const std::error_code& error) noexcept
{
    return error == std::errc::no_such_file_or_directory;
}

[[nodiscard]] bool valid_generation(const std::string_view value) noexcept
{
    if (value.size() != 64) return false;
    for (const char byte : value) {
        if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string_view service_runtime_repository_phase_name(
    const ServiceRuntimeRepositoryPhase phase) noexcept
{
    using enum ServiceRuntimeRepositoryPhase;
    switch (phase) {
        case unavailable: return "unavailable";
        case pinned: return "pinned";
    }
    return "unknown";
}

std::string_view service_runtime_repository_open_error_name(
    const ServiceRuntimeRepositoryOpenError error) noexcept
{
    using enum ServiceRuntimeRepositoryOpenError;
    switch (error) {
        case none: return "none";
        case invalid_expected_generation: return "invalid_expected_generation";
        case generation_mismatch: return "generation_mismatch";
        case invalid_activation: return "invalid_activation";
        case trusted_state_invalid: return "trusted_state_invalid";
        case trusted_state_generation_mismatch:
            return "trusted_state_generation_mismatch";
        case trusted_state_pending_recovery:
            return "trusted_state_pending_recovery";
        case internal_error: return "internal_error";
    }
    return "unknown";
}

ServiceRuntimeRepositoryOwner::ServiceRuntimeRepositoryOwner(
    std::shared_ptr<const runtime::repository::RuntimeRepositorySnapshot> pin)
    : pin_(std::move(pin)),
      generation_(pin_ ? pin_->generation() : std::string{})
{}

ServiceRuntimeRepositoryOwner::ServiceRuntimeRepositoryOwner(
    std::shared_ptr<const runtime::repository::RuntimeRepositorySnapshot> pin,
    std::shared_ptr<const runtime::script::RuntimeScriptRepositoryTrustEvidence>
        script_trust_evidence)
    : pin_(std::move(pin)),
      generation_(pin_ ? pin_->generation() : std::string{}),
      script_trust_evidence_(std::move(script_trust_evidence))
{}

ServiceRuntimeRepositoryOwner::~ServiceRuntimeRepositoryOwner() = default;

ServiceRuntimeRepositoryPhase ServiceRuntimeRepositoryOwner::phase() const noexcept
{
    return pin_ ? ServiceRuntimeRepositoryPhase::pinned
                : ServiceRuntimeRepositoryPhase::unavailable;
}

const std::string& ServiceRuntimeRepositoryOwner::generation() const noexcept
{
    return generation_;
}

std::shared_ptr<const runtime::repository::RuntimeRepositorySnapshot>
ServiceRuntimeRepositoryOwner::pin() const noexcept
{
    return pin_;
}

std::shared_ptr<const runtime::repository::RuntimeRepositoryReadBundle>
ServiceRuntimeRepositoryOwner::open_read_bundle(
    const runtime::repository::RuntimeRepositoryReadLimits limits,
    const std::stop_token stop_token) const
{
    if (!pin_) return {};
    return pin_->open_read_bundle(limits, stop_token);
}

std::shared_ptr<const runtime::script::RuntimeScriptRepositoryTrustEvidence>
ServiceRuntimeRepositoryOwner::script_trust_evidence() const noexcept
{
    return script_trust_evidence_;
}

ServiceRuntimeRepositoryOpenResult open_service_runtime_repository_owner(
    const std::filesystem::path& project_root,
    const std::string_view expected_generation) noexcept
{
    try {
        if (!valid_generation(expected_generation)) {
            return {nullptr,
                    ServiceRuntimeRepositoryOpenError::invalid_expected_generation,
                    RuntimeRepositoryTrustedPlanStateError::none};
        }
        const auto state_root =
            project_root / ".baas-updater" / "runtime-repositories";
        const auto current = state_root / "current.json";
        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(current, status_error);
        if (status_error) {
            if (is_missing(status_error)) {
                return {nullptr, ServiceRuntimeRepositoryOpenError::generation_mismatch,
                        RuntimeRepositoryTrustedPlanStateError::none};
            }
            return {nullptr, ServiceRuntimeRepositoryOpenError::invalid_activation,
                    RuntimeRepositoryTrustedPlanStateError::none};
        }
        if (status.type() == std::filesystem::file_type::not_found) {
            return {nullptr, ServiceRuntimeRepositoryOpenError::generation_mismatch,
                    RuntimeRepositoryTrustedPlanStateError::none};
        }
        auto pin = runtime::repository::RuntimeRepositorySnapshot::activate(state_root);
        if (pin->generation() != expected_generation) {
            return {nullptr, ServiceRuntimeRepositoryOpenError::generation_mismatch,
                    RuntimeRepositoryTrustedPlanStateError::none};
        }
        RuntimeRepositoryTrustedPlanStateStore trusted_state{state_root};
        const auto attestation = trusted_state.attest_exact(expected_generation);
        if (!attestation) {
            auto error = ServiceRuntimeRepositoryOpenError::trusted_state_invalid;
            if (attestation.error ==
                RuntimeRepositoryTrustedPlanStateError::inconsistent_generation) {
                error = ServiceRuntimeRepositoryOpenError::
                    trusted_state_generation_mismatch;
            } else if (attestation.error ==
                           RuntimeRepositoryTrustedPlanStateError::pending_recovery ||
                       attestation.error ==
                           RuntimeRepositoryTrustedPlanStateError::not_ready) {
                error = ServiceRuntimeRepositoryOpenError::
                    trusted_state_pending_recovery;
            } else if (attestation.error ==
                           RuntimeRepositoryTrustedPlanStateError::resource_exhausted ||
                       attestation.error ==
                           RuntimeRepositoryTrustedPlanStateError::internal_error) {
                error = ServiceRuntimeRepositoryOpenError::internal_error;
            }
            return {nullptr, error, attestation.error};
        }
        auto evidence =
            std::make_shared<const ExactRuntimeScriptRepositoryTrustEvidence>(
                pin->generation(), pin->scripts().commit);
        return {
            std::unique_ptr<ServiceRuntimeRepositoryOwner>(
                new ServiceRuntimeRepositoryOwner(
                    std::move(pin), std::move(evidence))),
            ServiceRuntimeRepositoryOpenError::none,
            RuntimeRepositoryTrustedPlanStateError::none,
        };
    } catch (const runtime::repository::RuntimeRepositoryError&) {
        return {nullptr, ServiceRuntimeRepositoryOpenError::invalid_activation,
                RuntimeRepositoryTrustedPlanStateError::none};
    } catch (const std::bad_alloc&) {
        return {nullptr, ServiceRuntimeRepositoryOpenError::internal_error,
                RuntimeRepositoryTrustedPlanStateError::resource_exhausted};
    } catch (...) {
        return {nullptr, ServiceRuntimeRepositoryOpenError::internal_error,
                RuntimeRepositoryTrustedPlanStateError::internal_error};
    }
}

}  // namespace baas::service::app
