#pragma once

#include "runtime/repository/RuntimeRepositoryGit2.h"

#include <vector>

namespace baas::runtime::repository::testing {

// This symbol exists only in the dedicated backend-test build. It changes
// thread-local transport selection and does not alter any production type.
void set_file_transport_enabled(bool enabled) noexcept;
[[nodiscard]] bool pack_is_safe(
    const std::vector<unsigned char>& pack,
    const Libgit2RuntimeRepositoryFetchLimits& limits) noexcept;
[[nodiscard]] bool smart_response_is_safe(
    const std::vector<char>& response,
    const Libgit2RuntimeRepositoryFetchLimits& limits) noexcept;
[[nodiscard]] bool pack_preflight_is_interrupted(
    const std::vector<unsigned char>& pack,
    const Libgit2RuntimeRepositoryFetchLimits& limits, bool cancel, bool expired) noexcept;

} // namespace baas::runtime::repository::testing
