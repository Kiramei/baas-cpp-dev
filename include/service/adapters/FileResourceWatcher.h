#pragma once

#include "service/adapters/FileResourceStore.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace baas::service::adapters {

enum class FileResourceScanError {
    none,
    cancelled,
    config_list,
    config_resource,
    static_resource,
    setup_resource,
    internal_error,
};

struct FileResourceScanSnapshot {
    channels::ResourceSnapshot config_list;
    channels::ResourceSnapshot static_data;
    channels::ResourceSnapshot setup_toml;
    std::vector<std::string> config_ids;
};

struct FileResourceScanResult {
    std::optional<FileResourceScanSnapshot> snapshot;
    FileResourceScanError error{FileResourceScanError::internal_error};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == FileResourceScanError::none && snapshot.has_value();
    }
};

struct FileResourceWatcherConfig {
    std::chrono::milliseconds poll_interval{250};
    std::string origin{"filesystem"};
};

using FileResourceScanCallback =
    std::function<void(const FileResourceScanResult& result)>;

struct FileResourceWatcherStartResult {
    bool started{};
    bool initial_scan_ready{};
    FileResourceScanError error{FileResourceScanError::none};
};

// Portable production watcher. Filesystem notification APIs differ across the
// supported platforms, so this owner performs bounded full rescans through the
// anchored FileResourceStore. Every stop joins the worker and is a callback
// barrier.
class FileResourceWatcher final {
public:
    FileResourceWatcher(
        std::shared_ptr<FileResourceStore> store,
        FileResourceScanCallback callback,
        FileResourceWatcherConfig config = {});
    ~FileResourceWatcher();

    FileResourceWatcher(const FileResourceWatcher&) = delete;
    FileResourceWatcher& operator=(const FileResourceWatcher&) = delete;

    [[nodiscard]] FileResourceWatcherStartResult start() noexcept;
    void stop() noexcept;

    [[nodiscard]] FileResourceScanResult scan_once(
        std::stop_token stop = {}) noexcept;
    [[nodiscard]] bool running() const noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace baas::service::adapters
