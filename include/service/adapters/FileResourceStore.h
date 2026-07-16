#pragma once

#include "service/adapters/ConfigurationDefaults.h"
#include "service/channels/SyncHandler.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

struct ConfigCreateResult {
    std::string serial;
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

struct ConfigArchiveExportResult {
    std::string filename;
    std::vector<std::byte> content;
    ConfigCommandError error{ConfigCommandError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ConfigCommandError::none;
    }
};

struct ConfigArchiveImportResult {
    std::string serial;
    std::string name;
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
    bool published{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return disposition == ResourceRefreshDisposition::unchanged
            || disposition == ResourceRefreshDisposition::updated;
    }
};

using ConfigCopyCommitClaim =
    std::function<bool(std::string_view serial, std::string_view name)>;
using ConfigCreateCommitClaim = std::function<bool(std::string_view serial)>;
using ConfigRemoveCommitClaim = std::function<bool()>;
using ConfigArchiveImportCommitClaim =
    std::function<bool(std::string_view serial, std::string_view name)>;

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
    // Deterministic transaction fault injection. Returning true or throwing
    // fails before the named create-config commit step; production leaves this
    // empty.
    using ConfigCreateFaultInjector = std::function<bool(std::string_view step)>;
    using ConfigArchiveFaultInjector =
        std::function<bool(std::string_view step)>;

    Clock clock;
    AtomicWriter atomic_writer;
    PostCommitDurabilityCheck post_commit_durability_check;
    ConfigCreateFaultInjector config_create_fault_injector;
    ConfigArchiveFaultInjector config_archive_fault_injector;
    std::shared_ptr<const ConfigurationDefaults> configuration_defaults;
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
    // Disk failures invalidate any cached snapshot and publish one root remove
    // so pulls and subscriber replay fail closed together.
    [[nodiscard]] ResourceRefreshResult refresh(
        channels::ResourceKey key,
        std::string origin = "filesystem");

    // Invalidates one cached key without consulting its current filesystem
    // path. A cached key publishes one root remove. This is used when the
    // config-list pair contract removes an id even if one sibling file remains.
    [[nodiscard]] ResourceRefreshResult invalidate_and_publish(
        channels::ResourceKey key,
        std::string origin = "filesystem");

    // Compatibility convenience for callers that only care whether a
    // replacement or invalidation was published.
    [[nodiscard]] bool refresh_and_publish(
        channels::ResourceKey key,
        std::string origin = "filesystem");

    [[nodiscard]] const std::filesystem::path& project_root() const noexcept;

    // Exact production backends for Python trigger commands add_config*,
    // copy_config, and remove_config*. Structural changes share the patch
    // mutation gate and invalidate cached snapshots before becoming observable.
    [[nodiscard]] ConfigCreateResult create_config(
        std::string_view name,
        std::string_view server,
        std::stop_token stop,
        ConfigCreateCommitClaim claim = {});
    [[nodiscard]] ConfigCopyResult copy_config(
        std::string_view source_id,
        std::stop_token stop,
        ConfigCopyCommitClaim claim = {});
    [[nodiscard]] ConfigRemoveResult remove_config(
        std::string_view config_id,
        std::stop_token stop,
        ConfigRemoveCommitClaim claim = {});
    [[nodiscard]] ConfigArchiveExportResult export_config(
        std::string_view config_id,
        std::stop_token stop);
    [[nodiscard]] ConfigArchiveImportResult import_config(
        std::span<const std::byte> content,
        std::stop_token stop,
        ConfigArchiveImportCommitClaim claim = {});

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace baas::service::adapters
