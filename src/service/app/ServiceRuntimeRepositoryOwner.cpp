#include "service/app/ServiceRuntimeRepositoryOwner.h"

#include <new>
#include <system_error>
#include <utility>

namespace baas::service::app {
namespace {

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
        case internal_error: return "internal_error";
    }
    return "unknown";
}

ServiceRuntimeRepositoryOwner::ServiceRuntimeRepositoryOwner(
    std::shared_ptr<const runtime::repository::RuntimeRepositorySnapshot> pin)
    : pin_(std::move(pin)),
      generation_(pin_ ? pin_->generation() : std::string{})
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

ServiceRuntimeRepositoryOpenResult open_service_runtime_repository_owner(
    const std::filesystem::path& project_root,
    const std::string_view expected_generation) noexcept
{
    try {
        if (!valid_generation(expected_generation)) {
            return {
                nullptr,
                ServiceRuntimeRepositoryOpenError::invalid_expected_generation,
            };
        }
        const auto state_root =
            project_root / ".baas-updater" / "runtime-repositories";
        const auto current = state_root / "current.json";
        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(current, status_error);
        if (status_error) {
            if (is_missing(status_error)) {
                return {nullptr, ServiceRuntimeRepositoryOpenError::generation_mismatch};
            }
            return {nullptr, ServiceRuntimeRepositoryOpenError::invalid_activation};
        }
        if (status.type() == std::filesystem::file_type::not_found) {
            return {nullptr, ServiceRuntimeRepositoryOpenError::generation_mismatch};
        }
        auto pin = runtime::repository::RuntimeRepositorySnapshot::activate(state_root);
        if (pin->generation() != expected_generation) {
            return {nullptr, ServiceRuntimeRepositoryOpenError::generation_mismatch};
        }
        return {
            std::make_unique<ServiceRuntimeRepositoryOwner>(std::move(pin)),
            ServiceRuntimeRepositoryOpenError::none,
        };
    } catch (const runtime::repository::RuntimeRepositoryError&) {
        return {nullptr, ServiceRuntimeRepositoryOpenError::invalid_activation};
    } catch (const std::bad_alloc&) {
        return {nullptr, ServiceRuntimeRepositoryOpenError::internal_error};
    } catch (...) {
        return {nullptr, ServiceRuntimeRepositoryOpenError::internal_error};
    }
}

}  // namespace baas::service::app
