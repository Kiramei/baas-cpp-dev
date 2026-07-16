#pragma once

#include "script/runtime/HostModuleRegistry.h"
#include "script/runtime/JsonBridge.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace baas::script::runtime {

enum class HostErrorCode : std::uint8_t {
    CapabilityDenied = 1,
    InvalidArgument,
    Cancelled,
    DeadlineExceeded,
    BudgetExceeded,
    Unavailable,
    IoError,
    DeviceDisconnected,
    ConfigConflict,
    ResourceNotFound,
    ModelUnavailable,
    PolicyDenied,
    ProtocolError,
    Internal,
    HandleClosed,
    Backpressure,
};

[[nodiscard]] std::string_view host_error_code_name(HostErrorCode code) noexcept;

enum class HostEffectState : std::uint8_t { NotStarted, Committed, Unknown };

struct HostError {
    HostErrorCode code{HostErrorCode::Internal};
    std::string message;
    bool retryable{};
    HostEffectState effect_state{HostEffectState::Unknown};
    std::optional<JsonValue> details;
};

enum class HostValueType : std::uint8_t {
    Null,
    Boolean,
    Integer,
    Float,
    String,
    Json,
    OrderedStringJsonMap,
    HostResource,
    HostImage,
    HostOcrModel,
    HostDevice,
};

class HostReleaseDispatcher;
class HostHandleCapability;

enum class HostHandleTransferKind : std::uint8_t {
    ProducedGrant,
    BorrowedReference,
};

class HostHandleReservation final {
public:
    HostHandleReservation(const HostHandleReservation&) = delete;
    HostHandleReservation& operator=(const HostHandleReservation&) = delete;
    HostHandleReservation(HostHandleReservation&&) noexcept = default;
    HostHandleReservation& operator=(HostHandleReservation&&) noexcept = default;
    ~HostHandleReservation() = default;

    [[nodiscard]] bool active() const noexcept { return control_ != nullptr; }

private:
    friend class HostReleaseDispatcher;
    explicit HostHandleReservation(std::shared_ptr<void> control)
        : control_(std::move(control)) {}
    std::shared_ptr<void> control_;
};

// An owning ABI token for one checked host<T> borrow or result handoff. Only a
// context-owned HostReleaseDispatcher can mint one; callbacks can inspect its
// opaque key but cannot construct or alter it.
class HostHandleValue final {
public:
    HostHandleValue(const HostHandleValue&) = default;
    HostHandleValue& operator=(const HostHandleValue&) = default;
    HostHandleValue(HostHandleValue&&) noexcept = default;
    HostHandleValue& operator=(HostHandleValue&&) noexcept = default;

    [[nodiscard]] HostHandleTypeId type_id() const;
    [[nodiscard]] std::uint64_t handle_id() const;
    [[nodiscard]] std::uint64_t adapter_id() const;
    [[nodiscard]] std::uint64_t generation() const;
    [[nodiscard]] std::uint64_t context_id() const;
    [[nodiscard]] std::uint64_t snapshot_id() const;
    [[nodiscard]] std::size_t external_bytes() const;
    [[nodiscard]] HostHandleTransferKind transfer_kind() const noexcept { return transfer_kind_; }
    [[nodiscard]] bool usable() const noexcept;
    // Callback boundary cleanup. Produced grants ignore this operation.
    void revoke_callback_borrow() const noexcept;

    friend bool operator==(const HostHandleValue& left, const HostHandleValue& right) noexcept
    {
        return left.usable() && right.usable() &&
            left.metadata_.handle_id == right.metadata_.handle_id &&
            left.metadata_.adapter_id == right.metadata_.adapter_id &&
            left.metadata_.external_bytes == right.metadata_.external_bytes &&
            left.metadata_.closed == right.metadata_.closed &&
            left.metadata_.type_id == right.metadata_.type_id &&
            left.metadata_.generation == right.metadata_.generation &&
            left.metadata_.context_id == right.metadata_.context_id &&
            left.metadata_.snapshot_id == right.metadata_.snapshot_id &&
            left.metadata_.authentication == right.metadata_.authentication &&
            left.transfer_kind_ == right.transfer_kind_;
    }

private:
    friend class HostReleaseDispatcher;
    friend class HostValue;
    explicit HostHandleValue(
        HostHandleMetadata metadata, HostHandleTransferKind transfer_kind,
        std::shared_ptr<HostHandleCapability> capability = {})
        : metadata_(std::move(metadata)), transfer_kind_(transfer_kind),
          capability_(std::move(capability)) {}

    void require_usable() const;
    void consume() const noexcept;

    HostHandleMetadata metadata_;
    HostHandleTransferKind transfer_kind_{HostHandleTransferKind::ProducedGrant};
    std::shared_ptr<HostHandleCapability> capability_;
};

class HostValue final {
public:
    using Storage = std::variant<
        std::monostate, bool, std::int64_t, double, std::string, JsonValue,
        HostHandleValue>;

    HostValue() noexcept = default;
    explicit HostValue(bool value) : storage_(value) {}
    explicit HostValue(std::int64_t value) : storage_(value) {}
    explicit HostValue(double value) : storage_(value) {}
    explicit HostValue(std::string value) : storage_(std::move(value)) {}
    explicit HostValue(const char* value) : storage_(std::string(value)) {}
    explicit HostValue(JsonValue value) : storage_(std::move(value)) {}
    explicit HostValue(HostHandleValue value) : storage_(std::move(value)) {}

    [[nodiscard]] HostValueType type() const noexcept;
    [[nodiscard]] const Storage& storage() const noexcept { return storage_; }
    friend bool operator==(const HostValue&, const HostValue&) = default;

private:
    Storage storage_;
};

[[nodiscard]] bool is_host_handle_type(HostValueType type) noexcept;
[[nodiscard]] HostValueType host_value_type(HostHandleTypeId type);
[[nodiscard]] HostHandleTypeId host_handle_type(HostValueType type);

using HostReleaseCallback = std::function<bool(const HostHandleValue&)>;

struct HostReleaseAdapter {
    std::uint64_t adapter_id{};
    HostReleaseCallback release;
};

struct HostReleaseDispatcherStats {
    std::size_t issued{};
    std::size_t borrowed{};
    std::size_t released{};
    std::size_t retried{};
    std::size_t rejected_records{};
    std::size_t pending_releases{};
    std::size_t pending_external_bytes{};
    bool teardown_complete{};
};

// One dispatcher belongs to exactly one execution context and immutable
// snapshot. It authenticates native keys, pins borrows across callback entry,
// and owns reliable Heap release lease/ACK/retry processing. Adapter callbacks
// run only here, never in Heap collection.
class HostReleaseDispatcher final {
public:
    HostReleaseDispatcher(
        std::uint64_t snapshot_id, std::vector<HostReleaseAdapter> adapters);
    ~HostReleaseDispatcher();

    HostReleaseDispatcher(const HostReleaseDispatcher&) = delete;
    HostReleaseDispatcher& operator=(const HostReleaseDispatcher&) = delete;
    HostReleaseDispatcher(HostReleaseDispatcher&&) = delete;
    HostReleaseDispatcher& operator=(HostReleaseDispatcher&&) = delete;

    void attach_context(Heap& heap);
    // Reserve the Heap external-memory charge and a generational native key
    // before adapter-side allocation. adopt() is allocation-free with respect
    // to the handle table; abandoning a reservation rolls both reservations back.
    [[nodiscard]] HostHandleReservation reserve(
        HostHandleTypeId type_id, std::uint64_t adapter_id,
        std::size_t external_bytes);
    [[nodiscard]] HostHandleValue adopt(HostHandleReservation&& reservation);
    [[nodiscard]] HostHandleValue borrow(
        const Heap& heap, Value value, HostHandleTypeId expected_type);
    [[nodiscard]] Value publish(
        Heap& heap, const HostHandleValue& value, HostHandleTypeId expected_type);
    // Marks an unpublished produced grant for reliable release at the next
    // dispatcher safe point. It is idempotent and never performs adapter I/O.
    void abandon(const HostHandleValue& value) noexcept;

    // The checked callback entry point brackets every invocation with these
    // methods so copied borrows are revoked and unreturned grants are abandoned
    // even if callback code stores token copies or throws.
    [[nodiscard]] std::uint64_t begin_callback_scope();
    void finish_callback_scope(
        std::uint64_t scope_id, const HostHandleValue* retained_grant) noexcept;

    // Returns false when no record can make progress. Invalid or forged queue
    // records are retained for diagnosis/retry and never debit memory.
    bool dispatch_one(Heap& heap) noexcept;
    void dispatch_all(Heap& heap) noexcept;
    // Retries records detached from a torn-down Heap. The dispatcher owns both
    // records and their external-memory ledger, so no Heap lifetime is needed.
    void retry_detached_releases() noexcept;
    // Destruction fallback: performs only VM teardown and reliable ownership
    // transfer. It never invokes an adapter and is safe without the owner strand
    // when the evaluator is no longer executing.
    [[nodiscard]] bool detach_context_for_destruction(Heap& heap) noexcept;
    // False is explicit retry evidence: the owner must retain the dispatcher
    // and call retry_detached_releases after the adapter becomes available;
    // teardown has already transferred ownership away from the Heap.
    [[nodiscard]] bool teardown(Heap& heap) noexcept;

    [[nodiscard]] std::uint64_t context_id() const noexcept;
    [[nodiscard]] std::uint64_t snapshot_id() const noexcept;
    [[nodiscard]] HostReleaseDispatcherStats stats() const noexcept;
    // Exposes the destructor invariant for embedders/tests. Destroying while
    // false is a fail-fast programming error because native ownership remains.
    [[nodiscard]] bool destruction_safe() const noexcept;

private:
    struct Impl;
    Impl* impl_;
};

class HostResult final {
public:
    enum class BoundaryFailure : std::uint8_t { None, Allocation, CallbackException };

    static HostResult success(HostValue value = {});
    static HostResult failure(HostError error) noexcept;
    static HostResult boundary_failure(BoundaryFailure failure) noexcept;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] bool has_error() const noexcept;
    [[nodiscard]] const HostValue& value() const;
    [[nodiscard]] const HostError& error() const;
    [[nodiscard]] BoundaryFailure boundary_failure() const noexcept;

private:
    explicit HostResult(std::variant<HostValue, HostError, BoundaryFailure> state)
        : state_(std::move(state)) {}
    std::variant<HostValue, HostError, BoundaryFailure> state_;
};

struct HostParameterContract {
    std::string name;
    HostValueType type{HostValueType::Json};
    bool required{true};
};

enum class HostExecutionMode : std::uint8_t {
    ThreadSafe,
    ContextStrand,
    BoundedCpuPool,
    BoundedIoPool,
    OtherStrand,
};

enum class HostCancellationMode : std::uint8_t { Preflight, Cooperative };

struct HostCallContract {
    std::vector<HostParameterContract> parameters;
    HostValueType result{HostValueType::Null};
    std::string budget_scope;
    HostExecutionMode execution{HostExecutionMode::ThreadSafe};
    HostCancellationMode cancellation{HostCancellationMode::Preflight};
};

struct HostCallContext {
    std::string_view module_id;
    std::string_view export_name;
    std::string_view binding_id;
    HostApiVersion selected_version;
    std::size_t call_index{};
};

using HostArguments = std::vector<std::optional<HostValue>>;
using SynchronousHostCallback = std::function<HostResult(const HostCallContext&, const HostArguments&)>;

struct SynchronousNativeBinding {
    std::string binding_id;
    HostCallContract contract;
    SynchronousHostCallback callback;
};

struct SynchronousHostLimits {
    std::size_t max_bindings{4'096};
    std::size_t max_parameters_per_binding{64};
    std::size_t max_total_parameters{65'536};
    std::size_t max_string_bytes{1'024};
    std::size_t max_total_string_bytes{4U * 1024U * 1024U};
    std::size_t max_validation_work{100'000};
    std::size_t max_safe_message_bytes{4'096};
    JsonBridgeLimits json_limits{};
};

enum class HostBindingErrorCode : std::uint8_t {
    InvalidLimits,
    BindingLimitExceeded,
    ParameterLimitExceeded,
    StringLimitExceeded,
    WorkLimitExceeded,
    InvalidBindingId,
    DuplicateBinding,
    InvalidParameter,
    DuplicateParameter,
    MissingBudgetScope,
    MissingCallback,
    UnsupportedExecutionMode,
    UnsupportedCancellationMode,
};

class HostBindingError final : public std::runtime_error {
public:
    HostBindingError(HostBindingErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    [[nodiscard]] HostBindingErrorCode code() const noexcept { return code_; }

private:
    HostBindingErrorCode code_;
};

class SynchronousNativeBindingSet final {
public:
    explicit SynchronousNativeBindingSet(
        std::vector<SynchronousNativeBinding> bindings,
        SynchronousHostLimits limits = {});

    [[nodiscard]] const SynchronousNativeBinding* find(std::string_view binding_id) const noexcept;
    [[nodiscard]] const SynchronousHostLimits& limits() const noexcept { return limits_; }
    [[nodiscard]] std::size_t size() const noexcept { return bindings_.size(); }

private:
    SynchronousHostLimits limits_;
    std::vector<SynchronousNativeBinding> bindings_;
};

struct HostErrorTranslation {
    LanguageErrorCode code{LanguageErrorCode::HostInternal};
    bool declared_status{};
};

[[nodiscard]] HostErrorTranslation translate_host_error(const HostError& error) noexcept;
[[nodiscard]] LanguageErrorCode translate_host_boundary_failure(
    HostResult::BoundaryFailure failure) noexcept;
[[nodiscard]] LanguageErrorCode translate_host_result_runtime_error(
    RuntimeErrorCode code) noexcept;
[[nodiscard]] JsonBridgeLimits effective_host_json_limits(
    const SynchronousHostLimits& limits) noexcept;

struct HostValueMetrics {
    std::size_t nodes{};
    std::size_t string_bytes{};
    std::size_t total_bytes{};
    std::size_t work{};
};

[[nodiscard]] HostValueMetrics measure_host_value(
    const HostValue& value, JsonBridgeLimits limits = {});

// Conversion owns every value crossing the callback boundary. No Heap, Value,
// environment, raw pointer, or native descriptor is exposed to a callback.
[[nodiscard]] HostValue heap_to_host_value(
    const Heap& heap, Value value, HostValueType expected,
    JsonBridgeLimits limits = {}, HostReleaseDispatcher* handles = nullptr);
[[nodiscard]] Value host_to_heap_value(
    Heap& heap, const HostValue& value, HostValueType expected,
    JsonBridgeLimits limits = {}, HostReleaseDispatcher* handles = nullptr);

// The only callback entry point. It redacts C++ exceptions, validates error
// envelopes and successful results, and never throws.
[[nodiscard]] HostResult invoke_host_callback(
    const SynchronousNativeBinding& binding,
    const HostCallContext& context,
    const HostArguments& arguments,
    const SynchronousHostLimits& limits,
    HostReleaseDispatcher* handles = nullptr) noexcept;

struct InMemoryLogEvent {
    std::string level;
    std::string message;
    std::optional<JsonObject> fields;
    friend bool operator==(const InMemoryLogEvent&, const InMemoryLogEvent&) = default;
};

// A deterministic test/embedder adapter for the catalog's baas/log.emit v1
// contract. This is intentionally not the production LogHost.
class InMemoryLogHost final {
public:
    [[nodiscard]] std::vector<InMemoryLogEvent> events() const;

private:
    friend SynchronousNativeBinding make_in_memory_log_binding(
        std::shared_ptr<InMemoryLogHost> host);
    [[nodiscard]] HostResult emit(const HostCallContext&, const HostArguments&);
    mutable std::mutex mutex_;
    std::vector<InMemoryLogEvent> events_;
};

[[nodiscard]] SynchronousNativeBinding make_in_memory_log_binding(
    std::shared_ptr<InMemoryLogHost> host);

}  // namespace baas::script::runtime
