#pragma once

#include "resources/ResourceSnapshot.h"
#include "runtime/repository/RuntimeRepositoryReadView.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string_view>

namespace baas::runtime::resources {

inline constexpr std::string_view runtime_resource_manifest_path = "baas.resources.json";
inline constexpr std::string_view runtime_resource_manifest_schema = "baas.resources/v1";

enum class RuntimeResourceSnapshotLoadError : std::uint8_t {
    none,
    invalid_limits,
    wrong_repository,
    manifest_not_found,
    manifest_too_large,
    repository_read_failed,
    invalid_manifest,
    entry_limit_exceeded,
    file_limit_exceeded,
    total_byte_limit_exceeded,
    string_limit_exceeded,
    work_limit_exceeded,
    path_not_manifested,
    manifest_entry_mismatch,
    duplicate_entry,
    invalid_selector,
    snapshot_validation_failed,
    cancelled,
    resource_exhausted,
    internal_failure,
};

[[nodiscard]] std::string_view
runtime_resource_snapshot_load_error_name(RuntimeResourceSnapshotLoadError error) noexcept;

struct RuntimeResourceSnapshotLoaderLimits final {
    std::size_t max_manifest_bytes{4U * 1024U * 1024U};
    std::size_t max_entries{16'384};
    std::size_t max_total_bytes{512U * 1024U * 1024U};
    std::size_t max_file_bytes{64U * 1024U * 1024U};
    std::size_t max_string_bytes{1'024};
    std::size_t max_json_depth{8};
    std::size_t max_json_nodes{131'072};
    std::size_t max_work{2ULL * 1024ULL * 1024ULL * 1024ULL};
};

struct RuntimeResourceSnapshotLoadResult final {
    std::shared_ptr<const ::baas::resources::ResourceSnapshot> snapshot;
    RuntimeResourceSnapshotLoadError error{RuntimeResourceSnapshotLoadError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == RuntimeResourceSnapshotLoadError::none && static_cast<bool>(snapshot);
    }
};

// Loads only from a pinned resources read capability. The selector is trusted
// caller input and is intentionally absent from baas.resources.json. No native
// path, environment variable, or process-global resource directory is accepted.
[[nodiscard]] RuntimeResourceSnapshotLoadResult
load_runtime_resource_snapshot(const repository::RuntimeRepositoryReadView& resources,
                               ::baas::resources::ResourceSelector selector,
                               const RuntimeResourceSnapshotLoaderLimits& limits = {},
                               std::stop_token stop_token = {}) noexcept;

enum class RuntimeResourceSnapshotLoaderHookPoint : std::uint8_t {
    before_manifest_read,
    after_manifest_parse,
    before_payload_copy,
    before_snapshot_build,
};
#ifdef BAAS_RUNTIME_RESOURCE_SNAPSHOT_LOADER_TESTING
using RuntimeResourceSnapshotLoaderHook = void (*)(RuntimeResourceSnapshotLoaderHookPoint);
void set_runtime_resource_snapshot_loader_hook(RuntimeResourceSnapshotLoaderHook hook) noexcept;
#endif

} // namespace baas::runtime::resources
