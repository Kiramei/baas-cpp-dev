#pragma once

#include "runtime/repository/RuntimeRepositoryGit2.h"

#include <string_view>
#include <string>
#include <vector>

namespace baas::runtime::repository::testing {

// This symbol exists only in the dedicated backend-test build. It changes
// thread-local transport selection and does not alter any production type.
void set_file_transport_enabled(bool enabled) noexcept;
[[nodiscard]] std::string git_path_bytes(const std::filesystem::path& path);
[[nodiscard]] bool advertisement_is_safe(const std::vector<char>& advertisement,
                                         std::size_t maximum_refs) noexcept;
[[nodiscard]] bool https_endpoint_is_valid(std::string_view url) noexcept;
[[nodiscard]] bool trusted_https_advertisement_is_reachable(
    std::string_view url, const Libgit2RuntimeRepositoryFetchLimits& limits) noexcept;
[[nodiscard]] bool trusted_https_roundtrip_resolves(
    std::string_view url, std::string_view advertised_reference,
    std::string_view exact_commit, const std::filesystem::path& scratch_directory,
    const Libgit2RuntimeRepositoryFetchLimits& limits) noexcept;
[[nodiscard]] bool pack_is_safe(
    const std::vector<unsigned char>& pack,
    const Libgit2RuntimeRepositoryFetchLimits& limits) noexcept;
[[nodiscard]] bool smart_response_is_safe(
    const std::vector<char>& response,
    const Libgit2RuntimeRepositoryFetchLimits& limits) noexcept;
[[nodiscard]] bool negotiation_response_is_safe(const std::vector<char>& response) noexcept;
[[nodiscard]] bool upload_pack_budget_accepts(
    const std::vector<std::size_t>& response_sizes, std::size_t byte_limit,
    std::size_t round_limit) noexcept;
[[nodiscard]] bool pack_preflight_is_interrupted(
    const std::vector<unsigned char>& pack,
    const Libgit2RuntimeRepositoryFetchLimits& limits, bool cancel, bool expired) noexcept;

} // namespace baas::runtime::repository::testing
