#pragma once

#include "runtime/repository/RuntimeRepositoryUpdater.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::app {

inline constexpr std::string_view runtime_repository_plan_envelope_schema =
    "baas.runtime-repositories.signed-plan-envelope/v1";
inline constexpr std::string_view runtime_repository_plan_payload_schema =
    "baas.runtime-repositories.update-plan/v1";
inline constexpr std::string_view runtime_repository_plan_signature_domain =
    "baas.runtime-repositories.update-plan.signature/v1\n";

struct RuntimeRepositoryTrustedPlanLimits {
  std::size_t max_envelope_bytes{128U * 1'024U};
  std::size_t max_payload_bytes{64U * 1'024U};
  std::size_t max_json_depth{16};
  std::size_t max_json_nodes{512};
  std::size_t max_previous_generations{32};
  std::uint64_t max_validity_seconds{31U * 24U * 60U * 60U};
};

enum class RuntimeRepositoryTrustedPlanError : std::uint8_t {
  none,
  invalid_limits,
  invalid_public_key,
  invalid_state,
  invalid_envelope,
  invalid_signature,
  invalid_payload,
  noncanonical_payload,
  not_yet_valid,
  expired,
  replay_rejected,
  bootstrap_rejected,
  resource_exhausted,
  internal_error,
};

[[nodiscard]] std::string_view runtime_repository_trusted_plan_error_name(
    RuntimeRepositoryTrustedPlanError error) noexcept;

class VerifiedRuntimeRepositoryPlan final
    : public runtime::repository::RuntimeRepositoryUpdatePlanProvider {
public:
  [[nodiscard]] runtime::repository::RuntimeRepositoryUpdatePlan
  trusted_plan() const override;

  [[nodiscard]] const std::string &target_generation() const noexcept;
  [[nodiscard]] std::uint64_t sequence() const noexcept;
  [[nodiscard]] std::uint64_t not_before_unix() const noexcept;
  [[nodiscard]] std::uint64_t expires_unix() const noexcept;
  [[nodiscard]] const std::string &payload_sha256() const noexcept;

private:
  VerifiedRuntimeRepositoryPlan(
      runtime::repository::RuntimeRepositoryUpdatePlan plan,
      std::string target_generation, std::uint64_t sequence,
      std::uint64_t not_before_unix, std::uint64_t expires_unix,
      std::string payload_sha256) noexcept;

  runtime::repository::RuntimeRepositoryUpdatePlan plan_;
  std::string target_generation_;
  std::uint64_t sequence_{};
  std::uint64_t not_before_unix_{};
  std::uint64_t expires_unix_{};
  std::string payload_sha256_;

  friend class RuntimeRepositoryTrustedPlanVerifier;
};

struct RuntimeRepositoryTrustedPlanResult;

// Persist this state atomically with the successfully published generation.
// payload_sha256 binds an idempotent same-sequence retry to the exact accepted
// publisher payload.
struct RuntimeRepositoryTrustedState {
  std::string generation;
  std::uint64_t sequence{};
  std::string payload_sha256;

  [[nodiscard]] bool
  operator==(const RuntimeRepositoryTrustedState &) const = default;
};

class RuntimeRepositoryTrustedStateProvider {
public:
  virtual ~RuntimeRepositoryTrustedStateProvider() = default;
  [[nodiscard]] virtual std::optional<RuntimeRepositoryTrustedState>
  trusted_state() const = 0;
};

// A product composition root constructs one long-lived verifier before any
// untrusted listener starts. The verifier owns the trust root and reads the
// system clock itself; request handlers submit only signed envelope bytes.
class RuntimeRepositoryTrustedPlanVerifier final {
public:
  explicit RuntimeRepositoryTrustedPlanVerifier(
      std::span<const std::byte> trusted_public_key,
      std::shared_ptr<const RuntimeRepositoryTrustedStateProvider>
          state_provider,
      RuntimeRepositoryTrustedPlanLimits limits = {});

  [[nodiscard]] RuntimeRepositoryTrustedPlanResult
  verify(std::string_view envelope) const noexcept;

private:
  [[nodiscard]] RuntimeRepositoryTrustedPlanResult
  verify_at(std::string_view envelope,
            std::optional<RuntimeRepositoryTrustedState> current_state,
            std::uint64_t now_unix) const noexcept;

  std::vector<std::byte> trusted_public_key_;
  std::shared_ptr<const RuntimeRepositoryTrustedStateProvider> state_provider_;
  RuntimeRepositoryTrustedPlanLimits limits_;
};

struct RuntimeRepositoryTrustedPlanResult {
  std::shared_ptr<const VerifiedRuntimeRepositoryPlan> plan;
  RuntimeRepositoryTrustedPlanError error{
      RuntimeRepositoryTrustedPlanError::internal_error};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == RuntimeRepositoryTrustedPlanError::none && plan != nullptr;
  }
};

// The envelope contains exactly schema, payload, and signature. payload is
// canonical padded base64url of canonical UTF-8 JSON; signature is Ed25519 over
// runtime_repository_plan_signature_domain followed by the decoded payload.
// current_state is absent only for first bootstrap. A higher-sequence successor
// must authorize the current generation. An equal sequence is accepted only as
// an exact-payload, same-generation idempotent retry.

} // namespace baas::service::app
