#pragma once

#include "script/SourceLocation.h"
#include "script/runtime/ModuleSpecifier.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace baas::script::runtime {

class HostReleaseDispatcher;

enum class RuntimeErrorCode {
    TypeMismatch,
    CrossHeapReference,
    StaleReference,
    CellKindMismatch,
    MemoryLimitExceeded,
    CellLimitExceeded,
    SingleAllocationExceeded,
    StringLimitExceeded,
    ExternalMemoryLimitExceeded,
    CollectionWorkLimitExceeded,
    InvalidUtf8,
    JsonCycle,
    JsonNonFinite,
    JsonUnsupported,
    HeapTornDown,
    IndexOutOfRange,
    ReleaseQueueLimitExceeded,
    JsonDepthLimitExceeded,
    JsonNodeLimitExceeded,
    JsonStringLimitExceeded,
    JsonByteLimitExceeded,
    JsonWorkLimitExceeded,
    JsonDuplicateKey,
};

[[nodiscard]] std::string_view runtime_error_code_name(RuntimeErrorCode code) noexcept;

class RuntimeError final : public std::runtime_error {
public:
    RuntimeError(RuntimeErrorCode code, std::string message);
    [[nodiscard]] RuntimeErrorCode code() const noexcept { return code_; }

private:
    RuntimeErrorCode code_;
};

struct HeapRef {
    std::uint64_t heap_identity{};
    std::uint32_t slot{};
    std::uint64_t generation{};
    friend bool operator==(const HeapRef&, const HeapRef&) = default;
};

enum class ValueKind {
    Null,
    Boolean,
    Integer,
    Float,
    HeapReference,
    String,
    List,
    OrderedMap,
    Function,
    Module,
    Error,
    Task,
    HostHandle,
    Bytes,
};

class Value {
public:
    Value() noexcept = default;
    explicit Value(bool value) noexcept : storage_(value) {}
    explicit Value(std::int64_t value) noexcept : storage_(value) {}
    explicit Value(double value) noexcept : storage_(value) {}
    explicit Value(HeapRef reference) noexcept : storage_(reference) {}

    [[nodiscard]] static Value null() noexcept { return Value(); }
    [[nodiscard]] ValueKind inline_kind() const noexcept;
    [[nodiscard]] bool as_boolean() const;
    [[nodiscard]] std::int64_t as_integer() const;
    [[nodiscard]] double as_float() const;
    [[nodiscard]] HeapRef as_heap_ref() const;

    friend bool operator==(const Value&, const Value&) = default;

private:
    std::variant<std::monostate, bool, std::int64_t, double, HeapRef> storage_;
};

enum class CallableKind : std::uint8_t { Script, Native };

struct FunctionMetadata {
    CallableKind kind{CallableKind::Script};
    std::uint64_t callable_id{};
    std::vector<Value> captures;
};

struct ModuleMetadata {
    std::string name;
    std::vector<std::pair<std::string, Value>> exports;
};

enum class LanguageErrorCode {
    ThrownValue,
    TypeMismatch,
    ArgumentInvalid,
    NameNotFound,
    UninitializedBinding,
    NotCallable,
    CallArityMismatch,
    CallArgumentDuplicate,
    CallArgumentUnknown,
    TaskCycle,
    IndexOutOfRange,
    NumericOverflow,
    DivisionByZero,
    InvalidUtf8,
    JsonCycle,
    JsonNonFinite,
    JsonUnsupported,
    JsonDuplicateKey,
    JsonLimitExceeded,
    ImportSpecifierInvalid,
    ImportCycle,
    ImportDepthLimit,
    ModuleInitializationFailed,
    ModuleMemberMissing,
    CapabilityDenied,
    HostValidationFailed,
    HostUnavailable,
    DeviceDisconnected,
    PackageMismatch,
    OcrModelUnavailable,
    ResourceMissing,
    Timeout,
    HostInternal,
    Cancelled,
    HumanTakeover,
    DeadlineExceeded,
    InstructionLimitExceeded,
    MemoryLimitExceeded,
    StackLimitExceeded,
    CleanupLimitExceeded,
    TaskLimitExceeded,
    InternalInvariant,
};

[[nodiscard]] std::string_view language_error_code_name(LanguageErrorCode code) noexcept;
[[nodiscard]] bool language_error_code_catchable(LanguageErrorCode code) noexcept;
[[nodiscard]] std::optional<LanguageErrorCode> language_error_code_from_name(
    std::string_view name) noexcept;

enum class ErrorOrigin { Script, Runtime, Host };
enum class ErrorFrameKind { Script, Host };
enum class ErrorFramePhase { Body, ModuleInit, Cleanup, Host };

struct SourceReference {
    std::string snapshot_id;
    std::string module;
    SourceSpan span;

    friend bool operator==(const SourceReference&, const SourceReference&) = default;
};

struct ErrorStackFrame {
    ErrorFrameKind kind{ErrorFrameKind::Script};
    std::string module;
    std::string function;
    ErrorFramePhase phase{ErrorFramePhase::Body};
    std::optional<SourceReference> call_source;
    std::optional<SourceReference> definition_source;
    std::optional<SourceReference> defer_source;

    friend bool operator==(const ErrorStackFrame&, const ErrorStackFrame&) = default;
};

struct ErrorContext {
    std::optional<std::string> task_id;
    std::optional<std::string> session_id;
    std::optional<std::string> package_id;
    std::optional<std::string> snapshot_id;
    std::optional<std::string> language_version;
    std::optional<std::string> correlation_id;

    friend bool operator==(const ErrorContext&, const ErrorContext&) = default;
};

struct ErrorTruncation {
    std::size_t stack_frames{};
    std::size_t cause_errors{};
    std::size_t suppressed_errors{};
    std::size_t message_bytes{};
    std::size_t detail_bytes{};
    bool details_replaced{false};
    bool fallback{false};

    friend bool operator==(const ErrorTruncation&, const ErrorTruncation&) = default;
};

struct ErrorMetadata {
    LanguageErrorCode code{LanguageErrorCode::HostInternal};
    std::string message;
    ErrorOrigin origin{ErrorOrigin::Script};
    std::optional<SourceReference> source;
    std::vector<ErrorStackFrame> stack;
    std::optional<Value> cause;
    std::vector<Value> suppressed;
    std::vector<std::pair<std::string, Value>> details;
    ErrorContext context;
    ErrorTruncation truncated;

    [[nodiscard]] std::string_view code_name() const noexcept {
        return language_error_code_name(code);
    }
    [[nodiscard]] bool catchable() const noexcept {
        return language_error_code_catchable(code);
    }
};

struct ErrorDerivation {
    std::optional<Value> cause;
    std::vector<Value> suppressed;
};

enum class TaskState { Pending, Running, Succeeded, Failed, Cancelled };

struct TaskMetadata {
    std::uint64_t task_id{};
    TaskState state{TaskState::Pending};
    std::vector<Value> retained_values;
};

// Version-1 typed Host handle ids are fixed ABI values. Invalid exists only so
// the pre-typed heap API remains source-compatible; checked Host contracts never
// accept or publish it.
enum class HostHandleTypeId : std::uint8_t {
    Invalid = 0,
    Resource = 1,
    Image = 2,
    OcrModel = 3,
    Device = 4,
};

struct HostHandleMetadata {
    std::uint64_t handle_id{};
    std::uint64_t adapter_id{};
    std::size_t external_bytes{};
    bool closed{false};
    HostHandleTypeId type_id{HostHandleTypeId::Invalid};
    std::uint64_t generation{};
    std::uint64_t context_id{};
    std::uint64_t snapshot_id{};
    std::uint64_t authentication{};
};

struct HostReleaseRecord {
    std::uint64_t handle_id{};
    std::uint64_t adapter_id{};
    std::size_t external_bytes{};
    HostHandleTypeId type_id{HostHandleTypeId::Invalid};
    std::uint64_t generation{};
    std::uint64_t context_id{};
    std::uint64_t snapshot_id{};
    std::uint64_t authentication{};
};

struct HostReleaseLease {
    std::uint64_t lease_id{};
    HostReleaseRecord record;
};

struct HeapLimits {
    std::size_t max_live_bytes{64U * 1024U * 1024U};
    std::size_t max_cells{1'000'000};
    std::size_t max_single_allocation{8U * 1024U * 1024U};
    std::size_t max_string_bytes{16U * 1024U * 1024U};
    std::size_t max_external_bytes{64U * 1024U * 1024U};
    std::size_t soft_collect_threshold{48U * 1024U * 1024U};
    std::size_t max_collection_work{2'000'000};
    std::size_t max_pending_release_records{1'000'000};
    std::size_t max_error_stack_frames{128};
    std::size_t max_error_innermost_frames{96};
    std::size_t max_error_cause_depth{16};
    std::size_t max_error_suppressed{16};
    std::size_t max_error_message_bytes{4096};
    std::size_t max_error_detail_bytes{65536};
};

struct HeapStats {
    std::size_t live_bytes{};
    std::size_t live_cells{};
    std::size_t string_bytes{};
    std::size_t external_bytes{};
    std::size_t collections{};
    std::size_t reclaimed_cells{};
    std::size_t reclaimed_bytes{};
};

class Heap {
    struct ExternalLedger;

public:
    using RootId = std::uint64_t;

    class ExternalReservation {
    public:
        ExternalReservation(const ExternalReservation&) = delete;
        ExternalReservation& operator=(const ExternalReservation&) = delete;
        ExternalReservation(ExternalReservation&& other) noexcept;
        ExternalReservation& operator=(ExternalReservation&& other) noexcept;
        ~ExternalReservation();

        [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
        [[nodiscard]] bool active() const noexcept { return ledger_ != nullptr; }

    private:
        friend class Heap;
        explicit ExternalReservation(
            std::shared_ptr<ExternalLedger> ledger, std::size_t bytes) noexcept
            : ledger_(std::move(ledger)), bytes_(bytes) {}
        void commit() noexcept { ledger_.reset(); bytes_ = 0; }
        std::shared_ptr<ExternalLedger> ledger_;
        std::size_t bytes_{};
    };

    class DetachedHostReleases final {
    public:
        DetachedHostReleases() = default;
        DetachedHostReleases(const DetachedHostReleases&) = delete;
        DetachedHostReleases& operator=(const DetachedHostReleases&) = delete;
        DetachedHostReleases(DetachedHostReleases&&) noexcept = default;
        DetachedHostReleases& operator=(DetachedHostReleases&&) noexcept = delete;

        [[nodiscard]] std::optional<HostReleaseLease> lease();
        bool acknowledge(std::uint64_t lease_id) noexcept;
        bool defer(std::uint64_t lease_id) noexcept;
        [[nodiscard]] bool empty() const noexcept { return head_ >= records_.size(); }
        [[nodiscard]] std::size_t size() const noexcept { return records_.size() - head_; }
        [[nodiscard]] std::size_t external_bytes() const noexcept;

    private:
        friend class Heap;
        std::vector<HostReleaseRecord> records_;
        std::size_t head_{};
        std::size_t cursor_{};
        std::size_t active_index_{};
        std::shared_ptr<ExternalLedger> ledger_;
        std::uint64_t next_lease_id_{1};
        std::uint64_t active_lease_id_{};
        bool lease_active_{};
    };

    class RootScope {
    public:
        RootScope(const RootScope&) = delete;
        RootScope& operator=(const RootScope&) = delete;
        RootScope(RootScope&&) = delete;
        RootScope& operator=(RootScope&&) = delete;
        ~RootScope();

        void add(Value value);

    private:
        friend class Heap;
        explicit RootScope(Heap& heap);
        Heap* heap_;
        std::size_t marker_;
    };

    // SourceReference and stack-frame module IDs use the same canonicalization
    // boundary as imports. ASCII IDs need no callback; embedders that enable
    // non-ASCII modules must inject their shared platform-independent NFC test.
    explicit Heap(HeapLimits limits = {}, NfcPredicate module_nfc = nullptr);
    ~Heap();
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;
    Heap(Heap&&) = delete;
    Heap& operator=(Heap&&) = delete;

    [[nodiscard]] RootScope root_scope() { return RootScope(*this); }
    [[nodiscard]] RootId add_root(Value value);
    bool update_root(RootId id, Value value);
    bool remove_root(RootId id) noexcept;

    [[nodiscard]] Value allocate_string(std::string value);
    [[nodiscard]] Value allocate_bytes(std::vector<std::byte> value);
    [[nodiscard]] Value allocate_list(std::vector<Value> values = {});
    [[nodiscard]] Value allocate_map(std::vector<std::pair<std::string, Value>> entries = {});
    [[nodiscard]] Value allocate_function(FunctionMetadata metadata);
    [[nodiscard]] Value allocate_module(ModuleMetadata metadata);
    [[nodiscard]] Value allocate_error(ErrorMetadata metadata);
    // Copies a published Error and returns a new immutable identity with the
    // requested causal additions. The original cell is never mutated.
    [[nodiscard]] Value derive_error(HeapRef primary, ErrorDerivation additions);
    [[nodiscard]] Value allocate_task(TaskMetadata metadata);
    [[nodiscard]] Value allocate_host_handle(HostHandleMetadata metadata);
    [[nodiscard]] ExternalReservation reserve_host_external(
        std::size_t external_bytes);
    [[nodiscard]] Value allocate_host_handle(
        HostHandleMetadata metadata, ExternalReservation&& reservation);
    void ensure_host_release_capacity(std::size_t total_records);

    [[nodiscard]] ValueKind kind(Value value) const;
    [[nodiscard]] ValueKind kind(HeapRef reference) const;
    [[nodiscard]] std::string string_copy(HeapRef reference) const;
    // Allocation-free checked views for bounded serializers. They remain valid
    // only until the owning cell is mutated, collected, or the Heap is torn
    // down; callers must stay on the owning context strand.
    [[nodiscard]] std::string_view string_view(HeapRef reference) const;
    [[nodiscard]] std::size_t string_byte_size(HeapRef reference) const;
    [[nodiscard]] std::size_t string_scalar_count(HeapRef reference) const;
    [[nodiscard]] std::vector<std::byte> bytes_copy(HeapRef reference) const;
    // Immutable allocation-free view with the same lifetime constraints as
    // string_view(). The underlying byte cell has no mutation API.
    [[nodiscard]] std::span<const std::byte> bytes_view(HeapRef reference) const;
    [[nodiscard]] std::size_t bytes_size(HeapRef reference) const;
    [[nodiscard]] std::size_t list_size(HeapRef reference) const;
    [[nodiscard]] Value list_value_at(HeapRef reference, std::size_t index) const;
    [[nodiscard]] std::size_t map_size(HeapRef reference) const;
    [[nodiscard]] std::pair<std::string_view, Value> map_entry_at(
        HeapRef reference, std::size_t index) const;
    [[nodiscard]] const ErrorMetadata& error_metadata_view(HeapRef reference) const;
    // Returned Values are snapshots, not roots. Register them before invoking
    // any allocation-capable heap API that may collect.
    [[nodiscard]] std::vector<Value> list_values(HeapRef reference) const;
    [[nodiscard]] std::vector<std::pair<std::string, Value>> map_entries(HeapRef reference) const;
    [[nodiscard]] std::optional<Value> map_get(HeapRef reference, std::string_view key) const;
    [[nodiscard]] FunctionMetadata function_metadata(HeapRef reference) const;
    [[nodiscard]] ModuleMetadata module_metadata(HeapRef reference) const;
    [[nodiscard]] std::size_t module_export_count(HeapRef reference) const;
    [[nodiscard]] ErrorMetadata error_metadata(HeapRef reference) const;
    [[nodiscard]] TaskMetadata task_metadata(HeapRef reference) const;
    [[nodiscard]] HostHandleMetadata host_handle_metadata(HeapRef reference) const;

    void list_append(HeapRef list, Value value);
    void list_set(HeapRef list, std::size_t index, Value value);
    void map_set(HeapRef map, std::string key, Value value);

    [[nodiscard]] bool truthy(Value value) const;
    [[nodiscard]] bool equals(Value left, Value right) const;
    void validate_json_safe(Value value) const;

    // Returns true only for the first close. Closing and collection only queue
    // release records; host I/O belongs to the owning context/adapter strand.
    bool close_host_handle(HeapRef reference);
    // At most one release may be leased at a time. External memory remains
    // charged until the owning dispatcher acknowledges the lease. A failed
    // ownership transfer retries the same immutable record without loss.
    [[nodiscard]] std::optional<HostReleaseLease> lease_host_release();
    bool acknowledge_host_release(std::uint64_t lease_id) noexcept;
    bool retry_host_release(std::uint64_t lease_id) noexcept;
    // Retains the immutable record and charge but rotates it behind other
    // pending releases so a pinned/broken adapter cannot starve the queue.
    bool defer_host_release(std::uint64_t lease_id) noexcept;
    // Legacy eager ownership transfer. New typed dispatchers use leases and
    // the friend-only teardown path below.
    [[nodiscard]] std::vector<HostReleaseRecord> drain_release_queue();

    void collect();
    // Invalidates every reference, prevents further allocations/mutations,
    // and destructively transfers all queued release ownership to the returned
    // vector. Typed dispatchers use the private reliable teardown path instead.
    [[nodiscard]] std::vector<HostReleaseRecord> teardown();

    [[nodiscard]] HeapStats stats() const noexcept;
    [[nodiscard]] const HeapLimits& limits() const noexcept;
    [[nodiscard]] std::uint64_t identity() const noexcept;

private:
    friend class HostReleaseDispatcher;
    struct Impl;
    Impl* impl_;

    void add_temporary_root(Value value);
    void pop_temporary_roots(std::size_t marker) noexcept;
    // Friend-only reliable ownership transfer. Application code cannot obtain
    // or silently discard a non-empty detached release token.
    [[nodiscard]] DetachedHostReleases detach_host_releases();
    void teardown_for_dispatcher();
    [[nodiscard]] std::size_t pending_host_release_count() const noexcept;
};

}  // namespace baas::script::runtime
