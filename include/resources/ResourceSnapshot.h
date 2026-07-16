#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace baas::resources {

enum class ResourceErrorCode : std::uint8_t {
    InvalidLimits,
    EntryLimitExceeded,
    ByteLimitExceeded,
    InvalidResourceId,
    InvalidLocale,
    InvalidActivity,
    InvalidMediaType,
    InvalidDigest,
    SizeMismatch,
    DigestMismatch,
    DuplicateVariant,
};

[[nodiscard]] std::string_view resource_error_code_name(
    ResourceErrorCode code) noexcept;

class ResourceError final : public std::runtime_error {
public:
    ResourceError(ResourceErrorCode code, std::string message);
    [[nodiscard]] ResourceErrorCode code() const noexcept { return code_; }

private:
    ResourceErrorCode code_;
};

struct ResourceSelector {
    std::string locale;
    std::optional<std::string> current_activity;
    friend bool operator==(const ResourceSelector&, const ResourceSelector&) = default;
};

// This is the fully materialized output of package activation. Filesystem and
// archive paths are deliberately absent: ResourceSnapshot never probes ambient
// storage and defensively copies every validated byte payload before publication.
struct ResourcePayload {
    std::string resource_id;
    std::optional<std::string> locale;
    std::optional<std::string> activity;
    std::string media_type;
    std::size_t declared_size{};
    std::string sha256;
    std::shared_ptr<const std::vector<std::byte>> bytes;
};

struct ResourceSnapshotLimits {
    std::size_t max_entries{65'536};
    std::size_t max_total_bytes{512U * 1024U * 1024U};
    std::size_t max_entry_bytes{64U * 1024U * 1024U};
    std::size_t max_resource_id_bytes{1'024};
    std::size_t max_selector_bytes{1'024};
    std::size_t max_media_type_bytes{256};
};

class ResourceEntry final {
public:
    [[nodiscard]] const std::string& resource_id() const noexcept;
    [[nodiscard]] const std::optional<std::string>& locale() const noexcept;
    [[nodiscard]] const std::optional<std::string>& activity() const noexcept;
    [[nodiscard]] const std::string& media_type() const noexcept;
    [[nodiscard]] const std::string& sha256() const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] std::size_t retained_bytes() const noexcept;

private:
    friend class ResourceSnapshot;
    explicit ResourceEntry(ResourcePayload payload);
    ResourcePayload payload_;
};

class ResourceSnapshot final {
public:
    ~ResourceSnapshot();
    [[nodiscard]] static std::shared_ptr<const ResourceSnapshot> build(
        ResourceSelector selector,
        std::vector<ResourcePayload> payloads,
        ResourceSnapshotLimits limits = {});

    // Resolution precedence is exact selector locale+activity, locale-only,
    // activity-only, then locale/activity-neutral. A locale override never
    // changes the snapshot's frozen current activity.
    [[nodiscard]] std::shared_ptr<const ResourceEntry> resolve(
        std::string_view resource_id,
        std::optional<std::string_view> locale_override = std::nullopt) const;

    [[nodiscard]] bool accepts_resource_id(std::string_view resource_id) const noexcept;
    [[nodiscard]] bool accepts_locale(std::string_view locale) const noexcept;

    [[nodiscard]] const ResourceSelector& selector() const noexcept;
    [[nodiscard]] const std::string& snapshot_id() const noexcept;
    [[nodiscard]] std::uint64_t numeric_snapshot_id() const noexcept;
    [[nodiscard]] std::size_t entry_count() const noexcept;
    [[nodiscard]] std::size_t retained_bytes() const noexcept;

private:
    struct Impl;
    explicit ResourceSnapshot(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool valid_resource_id(
    std::string_view value, std::size_t max_bytes = 1'024) noexcept;
[[nodiscard]] bool valid_resource_locale(
    std::string_view value, std::size_t max_bytes = 1'024) noexcept;
[[nodiscard]] bool valid_resource_activity(
    std::string_view value, std::size_t max_bytes = 1'024) noexcept;
[[nodiscard]] std::string sha256_hex(std::span<const std::byte> bytes);

}  // namespace baas::resources
