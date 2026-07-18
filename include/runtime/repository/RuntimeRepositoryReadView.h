#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace baas::runtime::repository {

enum class RuntimeRepositoryReadErrorCode : std::uint8_t {
    io,
    path_violation,
    file_limit_exceeded,
    invalid_manifest,
    manifest_mismatch,
    entry_not_found,
    payload_mismatch,
    cancelled,
    resource_exhausted,
};

class RuntimeRepositoryReadError final : public std::runtime_error {
public:
    RuntimeRepositoryReadError(RuntimeRepositoryReadErrorCode code, std::string message);
    [[nodiscard]] RuntimeRepositoryReadErrorCode code() const noexcept;

private:
    RuntimeRepositoryReadErrorCode code_;
};

struct RuntimeRepositoryReadLimits final {
    std::size_t max_manifest_bytes{16U * 1024U * 1024U};
    std::size_t max_files{16'384};
    std::size_t max_entries{32'768};
    std::uintmax_t max_total_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
    std::uintmax_t max_file_bytes{256ULL * 1024ULL * 1024ULL};
    std::size_t max_relative_path_bytes{1'024};
    std::size_t max_relative_path_depth{32};
};

struct RuntimeRepositoryReadEntry final {
    std::string path;
    std::uintmax_t size{};
    std::string sha256;
};

// A read view is a capability, not a filesystem path. It retains an anchored
// native directory handle for one snapshot-selected repository root. Only
// manifest-listed logical paths can be read, and read() returns owned bytes
// after verifying the exact file handle against the manifest entry.
class RuntimeRepositoryReadView final {
public:
    struct Impl;

    ~RuntimeRepositoryReadView();
    RuntimeRepositoryReadView(const RuntimeRepositoryReadView&) = delete;
    RuntimeRepositoryReadView& operator=(const RuntimeRepositoryReadView&) = delete;

    [[nodiscard]] const std::string& generation() const noexcept;
    [[nodiscard]] const std::string& repository_id() const noexcept;
    [[nodiscard]] const std::string& commit() const noexcept;
    [[nodiscard]] std::span<const RuntimeRepositoryReadEntry> entries() const noexcept;
    [[nodiscard]] std::vector<std::byte> read(
        std::string_view logical_path,
        std::uintmax_t max_bytes,
        std::stop_token stop_token = {}) const;

private:
    explicit RuntimeRepositoryReadView(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;

    friend struct RuntimeRepositoryReadViewFactory;
};

// Resources and scripts are opened together from one immutable metadata
// snapshot. A bundle therefore cannot combine views from different generations.
class RuntimeRepositoryReadBundle final {
public:
    ~RuntimeRepositoryReadBundle();
    RuntimeRepositoryReadBundle(const RuntimeRepositoryReadBundle&) = delete;
    RuntimeRepositoryReadBundle& operator=(const RuntimeRepositoryReadBundle&) = delete;

    [[nodiscard]] const std::string& generation() const noexcept;
    [[nodiscard]] const RuntimeRepositoryReadView& resources() const noexcept;
    [[nodiscard]] const RuntimeRepositoryReadView& scripts() const noexcept;

private:
    RuntimeRepositoryReadBundle(
        std::string generation,
        std::unique_ptr<RuntimeRepositoryReadView> resources,
        std::unique_ptr<RuntimeRepositoryReadView> scripts) noexcept;

    std::string generation_;
    std::unique_ptr<RuntimeRepositoryReadView> resources_;
    std::unique_ptr<RuntimeRepositoryReadView> scripts_;

    friend struct RuntimeRepositoryReadViewFactory;
};

#ifdef BAAS_RUNTIME_REPOSITORY_TESTING
enum class RuntimeRepositoryReadHookPoint : std::uint8_t {
    repository_root_opened,
    manifest_handle_opened,
    manifest_digest_finalizing,
    manifest_verified,
    payload_handle_opened,
    payload_digest_finalizing,
};
using RuntimeRepositoryReadViewHook = void (*)(RuntimeRepositoryReadHookPoint point,
                                               std::string_view repository_id,
                                               std::string_view logical_path);
void set_runtime_repository_read_view_hook(RuntimeRepositoryReadViewHook hook) noexcept;
#endif

}  // namespace baas::runtime::repository
