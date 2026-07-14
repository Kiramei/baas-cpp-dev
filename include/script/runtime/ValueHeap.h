#pragma once

#include "script/SourceLocation.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace baas::script::runtime {

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

struct FunctionMetadata {
    std::uint64_t function_id{};
    std::vector<Value> captures;
};

struct ModuleMetadata {
    std::string name;
    std::vector<std::pair<std::string, Value>> exports;
};

struct ErrorMetadata {
    std::string code;
    std::string message;
    std::optional<SourceSpan> span;
    std::vector<Value> details;
};

enum class TaskState { Pending, Running, Succeeded, Failed, Cancelled };

struct TaskMetadata {
    std::uint64_t task_id{};
    TaskState state{TaskState::Pending};
    std::vector<Value> retained_values;
};

struct HostHandleMetadata {
    std::uint64_t handle_id{};
    std::uint64_t adapter_id{};
    std::size_t external_bytes{};
    bool closed{false};
};

struct HostReleaseRecord {
    std::uint64_t handle_id{};
    std::uint64_t adapter_id{};
    std::size_t external_bytes{};
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
public:
    using RootId = std::uint64_t;

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

    explicit Heap(HeapLimits limits = {});
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
    [[nodiscard]] Value allocate_list(std::vector<Value> values = {});
    [[nodiscard]] Value allocate_map(std::vector<std::pair<std::string, Value>> entries = {});
    [[nodiscard]] Value allocate_function(FunctionMetadata metadata);
    [[nodiscard]] Value allocate_module(ModuleMetadata metadata);
    [[nodiscard]] Value allocate_error(ErrorMetadata metadata);
    [[nodiscard]] Value allocate_task(TaskMetadata metadata);
    [[nodiscard]] Value allocate_host_handle(HostHandleMetadata metadata);

    [[nodiscard]] ValueKind kind(Value value) const;
    [[nodiscard]] ValueKind kind(HeapRef reference) const;
    [[nodiscard]] std::string string_copy(HeapRef reference) const;
    // Returned Values are snapshots, not roots. Register them before invoking
    // any allocation-capable heap API that may collect.
    [[nodiscard]] std::vector<Value> list_values(HeapRef reference) const;
    [[nodiscard]] std::vector<std::pair<std::string, Value>> map_entries(HeapRef reference) const;
    [[nodiscard]] std::optional<Value> map_get(HeapRef reference, std::string_view key) const;
    [[nodiscard]] FunctionMetadata function_metadata(HeapRef reference) const;
    [[nodiscard]] ModuleMetadata module_metadata(HeapRef reference) const;
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
    [[nodiscard]] std::vector<HostReleaseRecord> drain_release_queue();

    void collect();
    // Invalidates every reference and prevents further allocations/mutations.
    // Leaked open handles are returned as queued release records.
    [[nodiscard]] std::vector<HostReleaseRecord> teardown();

    [[nodiscard]] HeapStats stats() const noexcept;
    [[nodiscard]] const HeapLimits& limits() const noexcept;
    [[nodiscard]] std::uint64_t identity() const noexcept;

private:
    struct Impl;
    Impl* impl_;

    void add_temporary_root(Value value);
    void pop_temporary_roots(std::size_t marker) noexcept;
};

}  // namespace baas::script::runtime
