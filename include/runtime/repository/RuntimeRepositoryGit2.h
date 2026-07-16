#pragma once

#include "runtime/repository/RuntimeRepositoryUpdater.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace baas::runtime::repository {

struct Libgit2RuntimeRepositoryFetchLimits final {
    std::chrono::milliseconds connect_timeout{10'000};
    std::chrono::milliseconds stall_timeout{20'000};
    std::chrono::milliseconds absolute_timeout{900'000};
    std::size_t max_advertised_refs{4'096};
    std::size_t max_advertisement_bytes{2U * 1024U * 1024U};
    std::size_t max_pack_bytes{256U * 1024U * 1024U};
    std::size_t max_received_objects{131'072};
    std::size_t max_odb_objects{131'072};
    std::uintmax_t max_odb_bytes{3ULL * 1024ULL * 1024ULL * 1024ULL};
    std::uintmax_t max_odb_object_bytes{256ULL * 1024ULL * 1024ULL};
    std::size_t max_commit_bytes{1U * 1024U * 1024U};
    std::size_t max_tag_bytes{1U * 1024U * 1024U};
    std::size_t max_tree_bytes{16U * 1024U * 1024U};
    std::size_t max_fallback_blob_bytes{32U * 1024U * 1024U};
    std::size_t max_delta_instruction_bytes{16U * 1024U * 1024U};
    std::size_t max_peel_depth{8};
    RepositoryValidationLimits tree{};
};

struct Libgit2RuntimeRepositoryFetchOptions final {
    Libgit2RuntimeRepositoryFetchLimits limits{};
};

// Optional production backend. It fetches only one trusted advertised heads or
// tags ref, proves its peeled SHA-1 commit, materializes only portable 100644
// blobs, and runs the strict repository-tree validator before returning.
class Libgit2RuntimeRepositoryFetchBackend final : public RuntimeRepositoryFetchBackend {
  public:
    explicit Libgit2RuntimeRepositoryFetchBackend(
        Libgit2RuntimeRepositoryFetchOptions options = {});
    ~Libgit2RuntimeRepositoryFetchBackend() override;
    Libgit2RuntimeRepositoryFetchBackend(const Libgit2RuntimeRepositoryFetchBackend&) = delete;
    Libgit2RuntimeRepositoryFetchBackend&
    operator=(const Libgit2RuntimeRepositoryFetchBackend&) = delete;

    [[nodiscard]] RepositoryStageResult
    stage_exact(const RepositoryFetchSpec& spec,
                const std::filesystem::path& updater_owned_staging_directory,
                std::stop_token stop_token) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace baas::runtime::repository
