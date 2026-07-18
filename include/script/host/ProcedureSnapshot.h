#pragma once

#include "resources/ResourceSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace baas::script::host {

enum class ProcedureEffect : std::uint8_t {
    Capture,
    Vision,
    Input,
    Wait,
    ForegroundCheck,
};

// Result payloads are JSON-only. Object field and array item schemas use the
// same recursive representation: Object children are ordered named fields;
// Array children contain exactly one unnamed, required item schema; primitive
// schemas have no children. The top-level descriptor schema is an ordered list
// of fields for the payload object that ProcedureHost merges with its own
// immutable `end` field.
enum class ProcedureResultJsonType : std::uint8_t {
    Null,
    Boolean,
    Integer,
    Float,
    String,
    Array,
    Object,
};

struct ProcedureResultFieldSchema {
    std::string name;
    bool required{};
    ProcedureResultJsonType type{ProcedureResultJsonType::Null};
    std::vector<ProcedureResultFieldSchema> children;
    friend bool operator==(
        const ProcedureResultFieldSchema&,
        const ProcedureResultFieldSchema&) = default;
};

[[nodiscard]] std::string_view procedure_result_json_type_name(
    ProcedureResultJsonType type) noexcept;

[[nodiscard]] std::string_view procedure_effect_name(ProcedureEffect effect) noexcept;

enum class ProcedureSnapshotErrorCode : std::uint8_t {
    InvalidLimits,
    ProcedureLimitExceeded,
    TerminalLimitExceeded,
    EffectLimitExceeded,
    ResourceLimitExceeded,
    StringLimitExceeded,
    WorkLimitExceeded,
    InvalidProcedureId,
    InvalidTerminalId,
    InvalidEffect,
    InvalidResourceId,
    InvalidDigest,
    DigestMismatch,
    DuplicateProcedure,
    ProcedureIdCaseCollision,
    DuplicateTerminal,
    DuplicateEffect,
    DuplicateResource,
    ResourceNotFound,
    ResourceSnapshotAbsent,
    ResultSchemaLimitExceeded,
    InvalidResultSchema,
    DuplicateResultField,
    ReservedResultField,
};

[[nodiscard]] std::string_view procedure_snapshot_error_code_name(
    ProcedureSnapshotErrorCode code) noexcept;

class ProcedureSnapshotError final : public std::runtime_error {
public:
    ProcedureSnapshotError(ProcedureSnapshotErrorCode code, std::string message);
    [[nodiscard]] ProcedureSnapshotErrorCode code() const noexcept { return code_; }

private:
    ProcedureSnapshotErrorCode code_;
};

struct ProcedureDescriptorInput {
    std::string procedure_id;
    // Ordering is semantic: the executor may return only one of these exact IDs.
    std::vector<std::string> terminal_ids;
    // These are sets. Publication sorts them into a canonical order.
    std::vector<ProcedureEffect> declared_effects;
    std::vector<std::string> resource_ids;
    // SHA-256 over the executable implementation contract. Runtime loaders bind
    // the engine, definition-file digest, and ordered source-to-terminal mapping
    // into this value before descriptor publication.
    std::string implementation_sha256;
    // SHA-256 over the descriptor's canonical logical fields, excluding this field.
    std::string sha256;
    // Ordered, strict schema for the executor-supplied result payload. Empty
    // preserves the historical terminal-only `{ "end": ... }` contract.
    std::vector<ProcedureResultFieldSchema> result_schema;
};

struct ProcedureSnapshotLimits {
    std::size_t max_procedures{4'096};
    std::size_t max_terminals_per_procedure{256};
    std::size_t max_effects_per_procedure{5};
    std::size_t max_resources_per_procedure{4'096};
    std::size_t max_string_bytes{1'024};
    std::size_t max_total_string_bytes{16U * 1024U * 1024U};
    std::size_t max_validation_work{1'000'000};
    std::size_t max_result_schema_nodes_per_procedure{16'384};
    std::size_t max_result_schema_depth{32};
};

class ProcedureDescriptor final {
public:
    [[nodiscard]] const std::string& procedure_id() const noexcept;
    [[nodiscard]] std::span<const std::string> terminal_ids() const noexcept;
    [[nodiscard]] std::span<const ProcedureEffect> declared_effects() const noexcept;
    [[nodiscard]] std::span<const std::string> resource_ids() const noexcept;
    [[nodiscard]] std::span<const ProcedureResultFieldSchema>
        result_schema() const noexcept;
    [[nodiscard]] const std::string& implementation_sha256() const noexcept;
    [[nodiscard]] const std::string& sha256() const noexcept;
    [[nodiscard]] bool accepts_terminal(std::string_view terminal_id) const noexcept;
    [[nodiscard]] bool declares_effect(ProcedureEffect effect) const noexcept;

private:
    friend class ProcedureSnapshot;
    explicit ProcedureDescriptor(ProcedureDescriptorInput input);
    ProcedureDescriptorInput input_;
};

class ProcedureSnapshot final {
public:
    ~ProcedureSnapshot();

    [[nodiscard]] static std::shared_ptr<const ProcedureSnapshot> build(
        std::vector<ProcedureDescriptorInput> descriptors,
        std::shared_ptr<const resources::ResourceSnapshot> resources,
        ProcedureSnapshotLimits limits = {});

    [[nodiscard]] std::shared_ptr<const ProcedureDescriptor> resolve(
        std::string_view procedure_id) const noexcept;
    [[nodiscard]] bool accepts_procedure_id(std::string_view procedure_id) const noexcept;
    [[nodiscard]] const std::shared_ptr<const resources::ResourceSnapshot>&
        resource_snapshot() const noexcept;
    [[nodiscard]] const std::string& snapshot_id() const noexcept;
    [[nodiscard]] std::uint64_t numeric_snapshot_id() const noexcept;
    [[nodiscard]] std::size_t procedure_count() const noexcept;

private:
    struct Impl;
    explicit ProcedureSnapshot(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool valid_procedure_id(
    std::string_view value, std::size_t max_bytes = 1'024) noexcept;
[[nodiscard]] bool valid_procedure_terminal_id(
    std::string_view value, std::size_t max_bytes = 1'024) noexcept;

// The helper canonicalizes effect/resource sets but preserves terminal order.
// It is intended for validated loader output and throws ProcedureSnapshotError
// for malformed or duplicate logical fields.
[[nodiscard]] std::string procedure_descriptor_sha256(
    const ProcedureDescriptorInput& descriptor,
    ProcedureSnapshotLimits limits = {});

}  // namespace baas::script::host
