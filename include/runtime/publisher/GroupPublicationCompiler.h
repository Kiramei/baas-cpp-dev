#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace baas::runtime::publisher {

inline constexpr std::string_view group_publication_lock_schema =
    "baas.group-publication-lock/v1";
inline constexpr std::string_view group_publication_compiler_schema =
    "baas.runtime-publisher/v1";
inline constexpr std::uint32_t group_publication_compiler_version = 1;
inline constexpr std::string_view group_publication_python_baseline =
    "b8cc64705feb0067aba349892031a450d1bf8083";

enum class PublicationErrorCode : std::uint8_t {
    invalid_lock,
    unsupported_version,
    invalid_repository,
    commit_mismatch,
    source_not_found,
    source_type_mismatch,
    source_oid_mismatch,
    source_size_mismatch,
    source_digest_mismatch,
    source_content_invalid,
    incomplete_bundle,
    alias_forbidden,
    locale_fallback_forbidden,
    placeholder_forbidden,
    publication_invalid,
    publication_mismatch,
    output_io_failed,
    resource_exhausted,
    internal_failure,
};

[[nodiscard]] std::string_view publication_error_name(PublicationErrorCode code) noexcept;

class PublicationError final : public std::runtime_error {
public:
    PublicationError(PublicationErrorCode code, std::string message);
    [[nodiscard]] PublicationErrorCode code() const noexcept;
private:
    PublicationErrorCode code_;
};

struct PublicationOutput final {
    std::string relative_path;
    std::vector<std::byte> bytes;
};

// The lock is parsed as strict UTF-8 JSON with an exact schema. Its implementation
// is opaque so callers cannot construct an unvalidated publication plan.
class GroupPublicationLock final {
public:
    GroupPublicationLock(GroupPublicationLock&&) noexcept;
    GroupPublicationLock& operator=(GroupPublicationLock&&) noexcept;
    GroupPublicationLock(const GroupPublicationLock&) = delete;
    GroupPublicationLock& operator=(const GroupPublicationLock&) = delete;
    ~GroupPublicationLock();

    [[nodiscard]] const std::string& source_commit() const noexcept;
    [[nodiscard]] std::size_t bundle_count() const noexcept;

private:
    struct Impl;
    explicit GroupPublicationLock(Impl* impl) noexcept;
    Impl* impl_{};
    friend GroupPublicationLock parse_group_publication_lock(std::string_view);
    friend void verify_group_publication_sources(
        const GroupPublicationLock&, const std::filesystem::path&);
    friend void validate_group_production_lock(const GroupPublicationLock&);
    friend std::vector<PublicationOutput> compile_group_publication(
        const GroupPublicationLock&, const std::filesystem::path&);
};

[[nodiscard]] GroupPublicationLock parse_group_publication_lock(std::string_view json);

// Applies the frozen production policy. Generic library fixtures deliberately
// do not satisfy this gate; every host CLI command does.
void validate_group_production_lock(const GroupPublicationLock& lock);

// Opens exactly repository_path and resolves every source through the pinned
// commit/tree/libgit2 object database. Mutable checkout bytes are never read.
void verify_group_publication_sources(
    const GroupPublicationLock& lock,
    const std::filesystem::path& repository_path);

// Returns all deterministic support bundles followed by canonical
// baas.resources.json. PNG members are copied byte-for-byte from Git blobs.
[[nodiscard]] std::vector<PublicationOutput> compile_group_publication(
    const GroupPublicationLock& lock,
    const std::filesystem::path& repository_path);

// Verifies exact bytes against a fresh ODB compilation and also rejects
// undeclared/missing/truncated/non-canonical publication files.
void verify_group_publication(
    const GroupPublicationLock& lock,
    const std::filesystem::path& repository_path,
    const std::filesystem::path& publication_root);

// Atomically replaces individual output files and publishes the manifest last.
// check_only performs no writes and requires an exact existing publication.
void write_group_publication(
    std::span<const PublicationOutput> outputs,
    const std::filesystem::path& publication_root,
    bool check_only);

}  // namespace baas::runtime::publisher
