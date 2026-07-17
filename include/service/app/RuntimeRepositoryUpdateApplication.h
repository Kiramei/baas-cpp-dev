#pragma once

#include "runtime/repository/RuntimeRepositoryUpdater.h"
#include "service/app/RuntimeRepositoryTrustedPlanUpdateOwner.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>

namespace baas::service::app {

inline constexpr std::size_t runtime_repository_update_input_max_bytes =
    128U * 1'024U;
inline constexpr std::size_t runtime_repository_update_max_argument_count = 2;
inline constexpr std::size_t runtime_repository_update_max_argument_bytes =
    1'024U;

enum class RuntimeRepositoryUpdateApplicationError : std::uint8_t {
  none,
  invalid_project_root,
  recovery_failed,
  invalid_plan,
  update_failed,
  resource_exhausted,
  internal_error,
};

[[nodiscard]] std::string_view runtime_repository_update_application_error_name(
    RuntimeRepositoryUpdateApplicationError error) noexcept;

struct RuntimeRepositoryUpdateApplicationResult {
  RuntimeRepositoryTrustedPlanUpdateOwnerResult owner;
  RuntimeRepositoryUpdateApplicationError error{
      RuntimeRepositoryUpdateApplicationError::internal_error};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == RuntimeRepositoryUpdateApplicationError::none;
  }
};

namespace detail {

// Stable process-protocol serializer. Kept out of the browser-facing update
// API so tests can prove ambiguous commit outcomes retain their handoff data.
void write_runtime_repository_update_application_result(
    std::ostream &output,
    const RuntimeRepositoryUpdateApplicationResult &result);

} // namespace detail

// One-shot standalone/WebUI publisher composition. The caller supplies only
// the application-owned project root and an opaque publisher-signed envelope.
// Repository URLs, refs, commits, manifests, current generation, and trust key
// are selected outside the browser-facing request boundary.
[[nodiscard]] RuntimeRepositoryUpdateApplicationResult
apply_runtime_repository_update(const std::filesystem::path &project_root,
                                std::string_view signed_envelope,
                                std::stop_token stop_token = {}) noexcept;

enum class RuntimeRepositoryUpdateProcessExit : int {
  success = 0,
  command_line = 2,
  input = 3,
  recovery = 4,
  invalid_plan = 5,
  update = 6,
  internal_failure = 7,
};

// Bounded process protocol used by the product executable and deterministic
// tests. arguments exclude argv[0]. The signed envelope is read from stdin;
// stdout contains exactly one bounded JSON result and never local diagnostics.
[[nodiscard]] int run_runtime_repository_update_application(
    std::span<const std::string_view> arguments, std::istream &input,
    std::ostream &output, std::ostream &diagnostics) noexcept;

} // namespace baas::service::app
