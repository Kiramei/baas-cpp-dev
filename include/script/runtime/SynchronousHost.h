#pragma once

#include "script/runtime/HostModuleRegistry.h"
#include "script/runtime/JsonBridge.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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
};

class HostValue final {
public:
    using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string, JsonValue>;

    HostValue() noexcept = default;
    explicit HostValue(bool value) : storage_(value) {}
    explicit HostValue(std::int64_t value) : storage_(value) {}
    explicit HostValue(double value) : storage_(value) {}
    explicit HostValue(std::string value) : storage_(std::move(value)) {}
    explicit HostValue(const char* value) : storage_(std::string(value)) {}
    explicit HostValue(JsonValue value) : storage_(std::move(value)) {}

    [[nodiscard]] HostValueType type() const noexcept;
    [[nodiscard]] const Storage& storage() const noexcept { return storage_; }
    friend bool operator==(const HostValue&, const HostValue&) = default;

private:
    Storage storage_;
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
[[nodiscard]] JsonBridgeLimits effective_host_json_limits(
    const SynchronousHostLimits& limits) noexcept;

// Conversion owns every value crossing the callback boundary. No Heap, Value,
// environment, raw pointer, or native descriptor is exposed to a callback.
[[nodiscard]] HostValue heap_to_host_value(
    const Heap& heap, Value value, HostValueType expected,
    JsonBridgeLimits limits = {});
[[nodiscard]] Value host_to_heap_value(
    Heap& heap, const HostValue& value, HostValueType expected,
    JsonBridgeLimits limits = {});

// The only callback entry point. It redacts C++ exceptions, validates error
// envelopes and successful results, and never throws.
[[nodiscard]] HostResult invoke_host_callback(
    const SynchronousNativeBinding& binding,
    const HostCallContext& context,
    const HostArguments& arguments,
    const SynchronousHostLimits& limits) noexcept;

}  // namespace baas::script::runtime
