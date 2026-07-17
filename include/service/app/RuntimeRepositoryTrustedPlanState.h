#pragma once

#include "service/app/RuntimeRepositoryTrustedPlan.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace baas::service::app {

enum class RuntimeRepositoryTrustedPlanStateError : std::uint8_t {
  none,
  not_ready,
  invalid_root,
  invalid_state,
  inconsistent_generation,
  io,
  resource_exhausted,
  internal_error,
};

[[nodiscard]] std::string_view runtime_repository_trusted_plan_state_error_name(
    RuntimeRepositoryTrustedPlanStateError error) noexcept;

struct RuntimeRepositoryTrustedPlanStateResult {
  RuntimeRepositoryTrustedPlanStateError error{
      RuntimeRepositoryTrustedPlanStateError::internal_error};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == RuntimeRepositoryTrustedPlanStateError::none;
  }
};

// Durable crash-consistency participant for the signed-plan policy state.
// prepare() must run inside RuntimeRepositoryCommitClaim, before the updater's
// publication journal. reconcile() runs after updater.recover() at startup and
// either completes the matching policy state, discards an uncommitted intent,
// or fails closed on any generation mismatch.
class RuntimeRepositoryTrustedPlanStateStore final
    : public RuntimeRepositoryTrustedStateProvider,
      public std::enable_shared_from_this<
          RuntimeRepositoryTrustedPlanStateStore> {
public:
  explicit RuntimeRepositoryTrustedPlanStateStore(
      std::filesystem::path runtime_repository_state_root);
  ~RuntimeRepositoryTrustedPlanStateStore();
  RuntimeRepositoryTrustedPlanStateStore(
      const RuntimeRepositoryTrustedPlanStateStore &) = delete;
  RuntimeRepositoryTrustedPlanStateStore &
  operator=(const RuntimeRepositoryTrustedPlanStateStore &) = delete;

  [[nodiscard]] std::optional<RuntimeRepositoryTrustedState>
  trusted_state() const override;

  [[nodiscard]] RuntimeRepositoryTrustedPlanStateResult
  claim_ownership() noexcept;

  [[nodiscard]] RuntimeRepositoryTrustedPlanStateResult
  reconcile(std::optional<std::string_view> actual_generation) noexcept;

  [[nodiscard]] RuntimeRepositoryTrustedPlanStateResult
  prepare(std::optional<std::string_view> expected_previous_generation,
          const RuntimeRepositoryTrustedState &next_state) noexcept;

  [[nodiscard]] RuntimeRepositoryTrustedPlanStateResult
  commit(const RuntimeRepositoryTrustedState &next_state) noexcept;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] const std::filesystem::path &state_root() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace baas::service::app
