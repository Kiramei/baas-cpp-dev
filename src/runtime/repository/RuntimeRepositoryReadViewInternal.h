#pragma once

#include "runtime/repository/RuntimeRepositoryReadView.h"
#include "runtime/repository/RuntimeRepositorySnapshot.h"

#include <filesystem>
#include <memory>

namespace baas::runtime::repository {

struct RuntimeRepositoryStateRootAnchor;

[[nodiscard]] std::shared_ptr<RuntimeRepositoryStateRootAnchor>
open_runtime_repository_state_root_anchor(const std::filesystem::path& state_root);

struct RuntimeRepositoryReadViewFactory final {
    [[nodiscard]] static std::shared_ptr<const RuntimeRepositoryReadBundle> open_bundle(
        std::shared_ptr<RuntimeRepositoryStateRootAnchor> state_root,
        const std::string& generation,
        const std::array<RuntimeRepository, 2>& repositories,
        RuntimeRepositoryReadLimits limits,
        std::stop_token stop_token);

    [[nodiscard]] static std::unique_ptr<RuntimeRepositoryReadView> open_view(
        const std::shared_ptr<RuntimeRepositoryStateRootAnchor>& state_root,
        const std::string& generation,
        const RuntimeRepository& repository,
        const RuntimeRepositoryReadLimits& limits,
        std::stop_token stop_token);
};

}  // namespace baas::runtime::repository
