#pragma once

#include "service/channels/SyncHandler.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace baas::service::adapters {

enum class AtomicWriteResult {
    not_committed,
    committed,
    committed_durability_uncertain,
};

enum class ConfigCommandError {
    none,
    cancelled,
    invalid_id,
    not_found,
    invalid_data,
    capacity,
    conflict,
    internal_error,
};

struct ConfigCopyResult {
    std::string serial;
    std::string name;
    ConfigCommandError error{ConfigCommandError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ConfigCommandError::none;
    }
};

struct ConfigRemoveResult {
    ConfigCommandError error{ConfigCommandError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ConfigCommandError::none;
    }
};

enum class ResourceRefreshDisposition {
    unchanged,
    updated,
    removed,
    not_found,
    invalid_data,
    capacity,
    internal_error,
};

struct ResourceRefreshResult {
    std::optional<channels::ResourceSnapshot> snapshot;
    ResourceRefreshDisposition disposition{ResourceRefreshDisposition::internal_error};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return disposition == ResourceRefreshDisposition::unchanged
            || disposition == ResourceRefreshDisposition::updated;
    }
};

using ConfigCopyCommitClaim =
    std::function<bool(std::string_view serial, std::string_view name)>;
using ConfigRemoveCommitClaim = std::function<bool()>;

struct FileResourceStoreDependencies {
    using Clock = std::function<double()>;
    // Strong commit contract: the result must describe whether replacement is
    // already externally visible. An AtomicWriter must not throw after its
    // commit point; post-commit flush/close failures return
    // committed_durability_uncertain. The store conservatively interprets any
    // exception escaping this callback as not_committed.
    using AtomicWriter = std::function<AtomicWriteResult(
        const std::filesystem::path& target,
        std::string_view bytes)>;
    using PostCommitDurabilityCheck =
        std::function<bool(const std::filesystem::path& parent_directory)>;

    Clock clock;
    AtomicWriter atomic_writer;
    PostCommitDurabilityCheck post_commit_durability_check;
};

// Production ResourceStore for resources owned by the BAAS project root.
// JSON files retain their full document. setup.toml exposes the bounded
// Python/Tauri general-settings projection while patches merge canonical keys
// back into the anchored TOML source without discarding unrelated fields.
class FileResourceStore final : public channels::ResourceStore {
public:
    explicit FileResourceStore(
        std::filesystem::path project_root,
        FileResourceStoreDependencies dependencies = {},
        channels::ResourceStoreLimits limits = {});
    ~FileResourceStore() override;

    FileResourceStore(const FileResourceStore&) = delete;
    FileResourceStore& operator=(const FileResourceStore&) = delete;

    [[nodiscard]] channels::ResourceStoreResult<channels::ResourceSnapshot>
        config_list(std::stop_token stop) override;
    [[nodiscard]] channels::ResourceStoreResult<channels::ResourceSnapshot> pull(
        const channels::ResourceKey& key,
        std::stop_token stop) override;
    [[nodiscard]] channels::ResourceStoreResult<channels::ResourcePatchResult>
        apply_patch(
            channels::ResourcePatchRequest request,
            std::stop_token stop) override;
    [[nodiscard]] channels::ResourceSubscribeResult subscribe_updates(
        UpdateCallback callback) override;

    // Reloads a resource changed by an external writer. An updated resource
    // publishes one root replacement through the normal subscription barrier.
    // Disk failures invalidate any cached snapshot so later pulls cannot serve
    // stale data after a delete or corrupt external replacement.
    [[nodiscard]] ResourceRefreshResult refresh(
        channels::ResourceKey key,
        std::string origin = "filesystem");

    // Compatibility convenience for callers that only care whether a new
    // replacement was published.
    [[nodiscard]] bool refresh_and_publish(
        channels::ResourceKey key,
        std::string origin = "filesystem");

    [[nodiscard]] const std::filesystem::path& project_root() const noexcept;

    // Exact production backends for Python trigger commands copy_config and
    // remove_config*. Structural changes share the patch mutation gate and
    // invalidate cached snapshots before becoming observable.
    [[nodiscard]] ConfigCopyResult copy_config(
        std::string_view source_id,
        std::stop_token stop,
        ConfigCopyCommitClaim claim = {});
    [[nodiscard]] ConfigRemoveResult remove_config(
        std::string_view config_id,
        std::stop_token stop,
        ConfigRemoveCommitClaim claim = {});

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace baas::service::adapters
