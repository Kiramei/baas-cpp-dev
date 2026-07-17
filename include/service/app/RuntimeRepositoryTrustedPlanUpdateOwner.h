#pragma once

#include "runtime/repository/RuntimeRepositoryUpdater.h"
#include "service/app/RuntimeRepositoryTrustedPlanState.h"

#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <string_view>

namespace baas::service::app {

enum class RuntimeRepositoryTrustedPlanUpdateOwnerError : std::uint8_t {
  none,
  not_recovered,
  invalid_plan,
  cancelled,
  state_failure,
  updater_failure,
  resource_exhausted,
  internal_error,
};

[[nodiscard]] std::string_view
runtime_repository_trusted_plan_update_owner_error_name(
    RuntimeRepositoryTrustedPlanUpdateOwnerError error) noexcept;

struct RuntimeRepositoryTrustedPlanUpdateOwnerResult {
  runtime::repository::RuntimeRepositoryUpdateResult update;
  RuntimeRepositoryTrustedPlanUpdateOwnerError error{
      RuntimeRepositoryTrustedPlanUpdateOwnerError::internal_error};
  RuntimeRepositoryTrustedPlanError plan_error{
      RuntimeRepositoryTrustedPlanError::none};
  RuntimeRepositoryTrustedPlanStateError state_error{
      RuntimeRepositoryTrustedPlanStateError::none};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == RuntimeRepositoryTrustedPlanUpdateOwnerError::none;
  }
};

// Standalone publisher composition boundary. recover() must succeed before
// apply(). The owner serializes verification, updater publication, and trusted
// policy-state completion. A browser-facing handler receives only
// apply(envelope) and cannot inject a trust key, clock, current generation, or
// repository plan.
class RuntimeRepositoryTrustedPlanUpdateOwner final {
public:
  RuntimeRepositoryTrustedPlanUpdateOwner(
      std::filesystem::path runtime_repository_state_root,
      std::span<const std::byte> trusted_public_key,
      std::shared_ptr<runtime::repository::RuntimeRepositoryUpdaterHooks>
          hooks = {});
  ~RuntimeRepositoryTrustedPlanUpdateOwner();

  RuntimeRepositoryTrustedPlanUpdateOwner(
      const RuntimeRepositoryTrustedPlanUpdateOwner &) = delete;
  RuntimeRepositoryTrustedPlanUpdateOwner &
  operator=(const RuntimeRepositoryTrustedPlanUpdateOwner &) = delete;

  [[nodiscard]] RuntimeRepositoryTrustedPlanUpdateOwnerResult
  recover(const runtime::repository::RuntimeRepositoryTreeValidator
              &validator) noexcept;

  [[nodiscard]] RuntimeRepositoryTrustedPlanUpdateOwnerResult apply(
      std::string_view signed_envelope,
      runtime::repository::RuntimeRepositoryFetchBackend &fetch_backend,
      const runtime::repository::RuntimeRepositoryTreeValidator &validator,
      std::stop_token stop_token = {},
      runtime::repository::RuntimeRepositoryCommitClaim *terminal_commit_claim =
          nullptr) noexcept;

  [[nodiscard]] bool recovered() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace baas::service::app
