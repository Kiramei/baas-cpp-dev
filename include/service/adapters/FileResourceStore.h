#pragma once

#include "service/channels/SyncHandler.h"

#include <filesystem>
#include <functional>
#include <memory>
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

// Production ResourceStore for the JSON resources owned by the BAAS project
// root. setup.toml is deliberately not interpreted here: its projected schema
// is application-owned and will be supplied by the service composition layer.
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

    // Reloads a resource changed by an external writer and publishes one root
    // replacement update. Returns false for unchanged, invalid, or unsupported
    // resources. A future filesystem watcher can call this without bypassing
    // the store's validation and subscription barrier.
    [[nodiscard]] bool refresh_and_publish(
        channels::ResourceKey key,
        std::string origin = "filesystem");

    [[nodiscard]] const std::filesystem::path& project_root() const noexcept;

    // Exact production backends for Python trigger commands copy_config and
    // remove_config*. Structural changes share the patch mutation gate and
    // invalidate cached snapshots before becoming observable.
    [[nodiscard]] ConfigCopyResult copy_config(
        std::string_view source_id,
        std::stop_token stop);
    [[nodiscard]] ConfigRemoveResult remove_config(
        std::string_view config_id,
        std::stop_token stop);

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace baas::service::adapters
