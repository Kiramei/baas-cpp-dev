#pragma once

#include "runtime/repository/RuntimeRepositorySnapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace baas::runtime::repository {

enum class RuntimeRepositoryId { Resources, Scripts };

struct RepositoryFetchSpec final {
    RuntimeRepositoryId id{RuntimeRepositoryId::Resources};
    std::string remote_url;
    // Trusted advertised ref used by transport backends to fetch a reachable
    // object before peeling and comparing it with exact_commit.
    std::string advertised_reference;
    std::string exact_commit;
    std::string manifest;
    std::string expected_manifest_sha256;
};

struct RuntimeRepositoryUpdatePlan final {
    std::array<RepositoryFetchSpec, 2> repositories;
};

// This provider is the trusted policy boundary. The updater deliberately does
// not read configuration files or embed repository URLs.
class RuntimeRepositoryUpdatePlanProvider {
  public:
    virtual ~RuntimeRepositoryUpdatePlanProvider() = default;
    [[nodiscard]] virtual RuntimeRepositoryUpdatePlan trusted_plan() const = 0;
};

struct RepositoryStageResult final {
    std::string resolved_commit;
};

// A backend receives only an updater-allocated private directory. It must not
// publish state or select a different revision.
class RuntimeRepositoryFetchBackend {
  public:
    virtual ~RuntimeRepositoryFetchBackend() = default;
    [[nodiscard]] virtual RepositoryStageResult
    stage_exact(const RepositoryFetchSpec& spec,
                const std::filesystem::path& updater_owned_staging_directory,
                std::stop_token stop_token) = 0;
};

struct RepositoryTreeSeal final {
    std::string manifest_sha256;
    std::string payload_sha256;
    std::size_t file_count{};
    std::uintmax_t total_bytes{};

    [[nodiscard]] bool operator==(const RepositoryTreeSeal&) const = default;
};

struct RepositoryValidationLimits final {
    std::size_t max_files{16'384};
    std::size_t max_entries{32'768};
    std::uintmax_t max_total_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
    std::uintmax_t max_file_bytes{256ULL * 1024ULL * 1024ULL};
    std::size_t max_manifest_bytes{16U * 1024U * 1024U};
    std::size_t max_relative_path_bytes{1'024};
    std::size_t max_relative_path_depth{32};
};

// A successful result certifies that the manifest hash is trusted and that the
// manifest binds every payload byte to spec.exact_commit. The core refuses an
// empty seal and invokes the same validator again for deduplicated objects.
class RuntimeRepositoryTreeValidator {
  public:
    virtual ~RuntimeRepositoryTreeValidator() = default;
    [[nodiscard]] virtual RepositoryTreeSeal
    validate_and_seal(const RepositoryFetchSpec& spec, const std::filesystem::path& repository_root,
                      std::stop_token stop_token) const = 0;
};

// Portable strict implementation used by production callers and tests. The
// manifest schema is baas.runtime-repository.tree-manifest/v1. Its entries
// array lists every non-manifest file with exact path, decimal-string size,
// sha256, and mode="file". Unknown, missing, duplicate, non-portable, linked,
// reparse, special, and empty-directory entries are rejected. Transport
// backends map mode="file" only to Git 100644; executable Git entries are not
// accepted because Windows cannot faithfully revalidate that mode.
class StrictRuntimeRepositoryTreeValidator final : public RuntimeRepositoryTreeValidator {
  public:
    explicit StrictRuntimeRepositoryTreeValidator(RepositoryValidationLimits limits = {});
    [[nodiscard]] RepositoryTreeSeal validate_and_seal(const RepositoryFetchSpec& spec,
                                                       const std::filesystem::path& repository_root,
                                                       std::stop_token stop_token) const override;

  private:
    RepositoryValidationLimits limits_;
};

#ifdef BAAS_RUNTIME_REPOSITORY_UPDATER_TESTING
using RuntimeRepositoryValidationReadHook = void (*)(const std::filesystem::path&,
                                                     const std::filesystem::path&);
void set_runtime_repository_validation_read_hook_for_testing(
    RuntimeRepositoryValidationReadHook hook) noexcept;
#endif

enum class RuntimeRepositoryUpdateErrorCode {
    Busy,
    Cancelled,
    InvalidPlan,
    FetchFailed,
    CommitMismatch,
    ValidationFailed,
    CurrentConflict,
    NoPrevious,
    Io,
    RecoveryFailed,
};

// Stable, non-sensitive text suitable for untrusted API consumers. Detailed
// provider, backend, validator, and filesystem diagnostics are deliberately
// excluded from RuntimeRepositoryUpdateResult.
[[nodiscard]] std::string_view runtime_repository_update_error_message(
    RuntimeRepositoryUpdateErrorCode code) noexcept;

struct RuntimeRepositoryUpdateError final {
    RuntimeRepositoryUpdateErrorCode code{RuntimeRepositoryUpdateErrorCode::Io};
    std::string message;
};

enum class PublishDisposition {
    NotCommitted,
    Committed,
    CommittedDurabilityUncertain,
};

struct RuntimeRepositoryUpdateResult final {
    std::optional<RuntimeRepositoryUpdateError> error;
    PublishDisposition disposition{PublishDisposition::NotCommitted};
    std::string pinned_generation;
    std::shared_ptr<const RuntimeRepositorySnapshot> pinned_bundle;

    [[nodiscard]] explicit operator bool() const noexcept { return !error.has_value(); }
};

enum class ExpectedCurrentKind { Any, Absent, Exact };

struct ExpectedCurrent final {
    ExpectedCurrentKind kind{ExpectedCurrentKind::Any};
    std::string generation;

    [[nodiscard]] static ExpectedCurrent any();
    [[nodiscard]] static ExpectedCurrent absent();
    [[nodiscard]] static ExpectedCurrent exact(std::string generation);
};

enum class RuntimeRepositoryUpdaterCheckpoint {
    PlanValidated,
    ResourcesStaged,
    ScriptsStaged,
    CandidatesSealed,
    ObjectsCommitted,
    SnapshotInstalled,
    JournalPrepared,
    PreviousReplaced,
    BeforeCurrentReplace,
    CurrentReplaced,
    JournalRemoved,
};

enum class RuntimeRepositoryFileOperation {
    PreparedJournalReplace,
    PreviousPointerReplace,
    PreviousPhaseJournalReplace,
    CurrentPointerReplace,
    CurrentPhaseJournalReplace,
    JournalRemove,
};

// Intended for diagnostics and deterministic fault injection. Exceptions from
// pre-current checkpoints are reported as not committed; CurrentReplaced is an
// observation only and cannot turn a committed update into ordinary failure.
class RuntimeRepositoryUpdaterHooks {
  public:
    virtual ~RuntimeRepositoryUpdaterHooks() = default;
    virtual void checkpoint(RuntimeRepositoryUpdaterCheckpoint checkpoint) = 0;
    virtual void before_file_operation(RuntimeRepositoryFileOperation) {}
    virtual void committed(RuntimeRepositoryUpdaterCheckpoint) noexcept {}
    // Receives the original local diagnostic before the public result is
    // redacted. Implementations are trusted local sinks and must not forward
    // this text to users or external telemetry without additional redaction.
    virtual void diagnostic(RuntimeRepositoryUpdateErrorCode, std::string_view) noexcept {}
};

class RuntimeRepositoryUpdater final {
  public:
    // state_root must be the caller-selected
    // <BAAS_ROOT>/.baas-updater/runtime-repositories directory.
    explicit RuntimeRepositoryUpdater(std::filesystem::path state_root,
                                      std::shared_ptr<RuntimeRepositoryUpdaterHooks> hooks = {});
    ~RuntimeRepositoryUpdater();
    RuntimeRepositoryUpdater(const RuntimeRepositoryUpdater&) = delete;
    RuntimeRepositoryUpdater& operator=(const RuntimeRepositoryUpdater&) = delete;

    [[nodiscard]] RuntimeRepositoryUpdateResult
    update(const RuntimeRepositoryUpdatePlanProvider& plan_provider,
           RuntimeRepositoryFetchBackend& fetch_backend,
           const RuntimeRepositoryTreeValidator& validator,
           ExpectedCurrent expected_current = ExpectedCurrent::any(),
           std::stop_token stop_token = {});

    [[nodiscard]] RuntimeRepositoryUpdateResult
    rollback(ExpectedCurrent expected_current = ExpectedCurrent::any(),
             std::stop_token stop_token = {});

    [[nodiscard]] std::shared_ptr<const RuntimeRepositorySnapshot> pin_current() const;
    [[nodiscard]] const std::filesystem::path& state_root() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace baas::runtime::repository
