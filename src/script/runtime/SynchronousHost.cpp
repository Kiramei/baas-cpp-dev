#include "script/runtime/SynchronousHost.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace baas::script::runtime {
namespace {

[[nodiscard]] bool valid_enum(const HostErrorCode code) noexcept
{
    const auto raw = static_cast<unsigned>(code);
    return raw >= 1 && raw <= 16;
}

[[nodiscard]] bool valid_effect(const HostEffectState state) noexcept
{
    return state == HostEffectState::NotStarted || state == HostEffectState::Committed ||
        state == HostEffectState::Unknown;
}

[[nodiscard]] bool valid_value_type(const HostValueType type) noexcept
{
    return type >= HostValueType::Null && type <= HostValueType::OrderedStringJsonMap;
}

[[nodiscard]] bool valid_utf8(const std::string_view value) noexcept
{
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<unsigned char>(value[offset]);
        std::size_t width{};
        std::uint32_t scalar{};
        if (first <= 0x7F) { width = 1; scalar = first; }
        else if (first >= 0xC2 && first <= 0xDF) { width = 2; scalar = first & 0x1FU; }
        else if (first >= 0xE0 && first <= 0xEF) { width = 3; scalar = first & 0x0FU; }
        else if (first >= 0xF0 && first <= 0xF4) { width = 4; scalar = first & 0x07U; }
        else return false;
        if (width > value.size() - offset) return false;
        for (std::size_t index = 1; index < width; ++index) {
            const auto next = static_cast<unsigned char>(value[offset + index]);
            if ((next & 0xC0U) != 0x80U) return false;
            scalar = (scalar << 6U) | (next & 0x3FU);
        }
        if ((width == 2 && scalar < 0x80U) || (width == 3 && scalar < 0x800U) ||
            (width == 4 && scalar < 0x10000U) || scalar > 0x10FFFFU ||
            (scalar >= 0xD800U && scalar <= 0xDFFFU)) return false;
        offset += width;
    }
    return true;
}

[[nodiscard]] bool valid_identifier(const std::string_view value) noexcept
{
    if (value.empty()) return false;
    for (const unsigned char character : value) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '.' ||
              character == '_' || character == '-')) return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::string_view> detail_string(
    const HostError& error, const std::string_view key) noexcept
{
    if (!error.details || error.details->kind() != JsonKind::Object) return std::nullopt;
    const auto& entries = std::get<JsonObject>(error.details->value());
    for (const auto& [name, value] : entries) {
        if (name == key && value.kind() == JsonKind::String)
            return std::get<std::string>(value.value());
    }
    return std::nullopt;
}

[[nodiscard]] bool validate_json(const JsonValue& value, const JsonBridgeLimits limits) noexcept
{
    try {
        Heap heap;
        (void)json_to_heap_value(heap, value, limits);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool validate_host_value(
    const HostValue& value, const HostValueType expected,
    const JsonBridgeLimits limits) noexcept
{
    if (expected == HostValueType::OrderedStringJsonMap) {
        return value.type() == HostValueType::Json &&
            std::get<JsonValue>(value.storage()).kind() == JsonKind::Object &&
            validate_json(std::get<JsonValue>(value.storage()), limits);
    }
    if (value.type() != expected) return false;
    if (expected == HostValueType::Float)
        return std::isfinite(std::get<double>(value.storage()));
    if (expected == HostValueType::String)
        return std::get<std::string>(value.storage()).size() <= limits.max_string_bytes &&
            valid_utf8(std::get<std::string>(value.storage()));
    if (expected == HostValueType::Json)
        return validate_json(std::get<JsonValue>(value.storage()), limits);
    return true;
}

}  // namespace

std::string_view host_error_code_name(const HostErrorCode code) noexcept
{
    using enum HostErrorCode;
    switch (code) {
        case CapabilityDenied: return "HOST001_CAPABILITY_DENIED";
        case InvalidArgument: return "HOST002_INVALID_ARGUMENT";
        case Cancelled: return "HOST003_CANCELLED";
        case DeadlineExceeded: return "HOST004_DEADLINE_EXCEEDED";
        case BudgetExceeded: return "HOST005_BUDGET_EXCEEDED";
        case Unavailable: return "HOST006_UNAVAILABLE";
        case IoError: return "HOST007_IO_ERROR";
        case DeviceDisconnected: return "HOST008_DEVICE_DISCONNECTED";
        case ConfigConflict: return "HOST009_CONFIG_CONFLICT";
        case ResourceNotFound: return "HOST010_RESOURCE_NOT_FOUND";
        case ModelUnavailable: return "HOST011_MODEL_UNAVAILABLE";
        case PolicyDenied: return "HOST012_POLICY_DENIED";
        case ProtocolError: return "HOST013_PROTOCOL_ERROR";
        case Internal: return "HOST014_INTERNAL";
        case HandleClosed: return "HOST015_HANDLE_CLOSED";
        case Backpressure: return "HOST016_BACKPRESSURE";
    }
    return "HOST014_INTERNAL";
}

HostValueType HostValue::type() const noexcept
{
    return static_cast<HostValueType>(storage_.index());
}

HostResult HostResult::success(HostValue value)
{
    return HostResult(std::variant<HostValue, HostError, BoundaryFailure>(
        std::in_place_index<0>, std::move(value)));
}

HostResult HostResult::failure(HostError error) noexcept
{
    return HostResult(std::variant<HostValue, HostError, BoundaryFailure>(
        std::in_place_index<1>, std::move(error)));
}

HostResult HostResult::boundary_failure(const BoundaryFailure failure) noexcept
{
    return HostResult(std::variant<HostValue, HostError, BoundaryFailure>(
        std::in_place_index<2>, failure));
}

bool HostResult::ok() const noexcept { return state_.index() == 0; }
bool HostResult::has_error() const noexcept { return state_.index() == 1; }
const HostValue& HostResult::value() const { return std::get<HostValue>(state_); }
const HostError& HostResult::error() const { return std::get<HostError>(state_); }
HostResult::BoundaryFailure HostResult::boundary_failure() const noexcept
{
    return state_.index() == 2 ? std::get<BoundaryFailure>(state_) : BoundaryFailure::None;
}

SynchronousNativeBindingSet::SynchronousNativeBindingSet(
    std::vector<SynchronousNativeBinding> bindings, const SynchronousHostLimits limits)
    : limits_(limits)
{
    if (limits.max_bindings == 0 || limits.max_parameters_per_binding == 0 ||
        limits.max_total_parameters == 0 || limits.max_string_bytes == 0 ||
        limits.max_total_string_bytes == 0 || limits.max_validation_work == 0 ||
        limits.max_safe_message_bytes == 0)
        throw HostBindingError(HostBindingErrorCode::InvalidLimits, "synchronous Host limits must be non-zero");
    if (bindings.size() > limits.max_bindings)
        throw HostBindingError(HostBindingErrorCode::BindingLimitExceeded, "synchronous Host binding limit exceeded");

    std::size_t total_parameters{};
    std::size_t total_strings{};
    std::size_t work{};
    for (const auto& binding : bindings) {
        if (++work > limits.max_validation_work)
            throw HostBindingError(HostBindingErrorCode::WorkLimitExceeded, "synchronous Host validation work exceeded");
        if (!valid_identifier(binding.binding_id))
            throw HostBindingError(HostBindingErrorCode::InvalidBindingId, "invalid synchronous Host binding id");
        if (!binding.callback)
            throw HostBindingError(HostBindingErrorCode::MissingCallback, "synchronous Host callback is absent");
        if (binding.contract.execution != HostExecutionMode::ThreadSafe)
            throw HostBindingError(HostBindingErrorCode::UnsupportedExecutionMode, "synchronous Host supports thread_safe bindings only");
        if (binding.contract.cancellation != HostCancellationMode::Preflight)
            throw HostBindingError(HostBindingErrorCode::UnsupportedCancellationMode, "synchronous Host supports preflight cancellation only");
        if (!valid_value_type(binding.contract.result))
            throw HostBindingError(HostBindingErrorCode::InvalidParameter, "invalid synchronous Host result type");
        if (binding.contract.budget_scope.empty())
            throw HostBindingError(HostBindingErrorCode::MissingBudgetScope, "synchronous Host budget scope is absent");
        if (binding.contract.parameters.size() > limits.max_parameters_per_binding ||
            binding.contract.parameters.size() > limits.max_total_parameters -
                std::min(total_parameters, limits.max_total_parameters))
            throw HostBindingError(HostBindingErrorCode::ParameterLimitExceeded, "synchronous Host parameter limit exceeded");
        total_parameters += binding.contract.parameters.size();

        std::unordered_set<std::string_view> names;
        bool saw_optional = false;
        auto add_string = [&](const std::string_view value) {
            if (value.size() > limits.max_string_bytes ||
                value.size() > limits.max_total_string_bytes -
                    std::min(total_strings, limits.max_total_string_bytes))
                throw HostBindingError(HostBindingErrorCode::StringLimitExceeded, "synchronous Host string limit exceeded");
            total_strings += value.size();
        };
        add_string(binding.binding_id);
        add_string(binding.contract.budget_scope);
        for (const auto& parameter : binding.contract.parameters) {
            if (++work > limits.max_validation_work)
                throw HostBindingError(HostBindingErrorCode::WorkLimitExceeded, "synchronous Host validation work exceeded");
            if (!valid_identifier(parameter.name) || !valid_value_type(parameter.type))
                throw HostBindingError(HostBindingErrorCode::InvalidParameter, "invalid synchronous Host parameter");
            if (saw_optional && parameter.required)
                throw HostBindingError(HostBindingErrorCode::InvalidParameter, "required Host parameter follows an optional parameter");
            saw_optional = saw_optional || !parameter.required;
            if (!names.insert(parameter.name).second)
                throw HostBindingError(HostBindingErrorCode::DuplicateParameter, "duplicate synchronous Host parameter");
            add_string(parameter.name);
        }
    }

    std::sort(bindings.begin(), bindings.end(), [](const auto& left, const auto& right) {
        return left.binding_id < right.binding_id;
    });
    for (std::size_t index = 1; index < bindings.size(); ++index) {
        if (bindings[index - 1].binding_id == bindings[index].binding_id)
            throw HostBindingError(HostBindingErrorCode::DuplicateBinding, "duplicate synchronous Host binding");
    }
    bindings_ = std::move(bindings);
}

const SynchronousNativeBinding* SynchronousNativeBindingSet::find(
    const std::string_view binding_id) const noexcept
{
    const auto found = std::lower_bound(bindings_.begin(), bindings_.end(), binding_id,
        [](const SynchronousNativeBinding& binding, const std::string_view id) {
            return binding.binding_id < id;
        });
    return found != bindings_.end() && found->binding_id == binding_id ? &*found : nullptr;
}

HostErrorTranslation translate_host_error(const HostError& error) noexcept
{
    if (!valid_enum(error.code) || !valid_effect(error.effect_state)) return {};
    using enum HostErrorCode;
    switch (error.code) {
        case CapabilityDenied:
        case PolicyDenied: return {LanguageErrorCode::CapabilityDenied, true};
        case InvalidArgument:
        case ConfigConflict:
        case HandleClosed: return {LanguageErrorCode::HostValidationFailed, true};
        case Cancelled: return {LanguageErrorCode::Cancelled, true};
        case DeadlineExceeded: {
            const auto scope = detail_string(error, "deadline_scope");
            if (!scope) return {};
            if (*scope == "context") return {LanguageErrorCode::DeadlineExceeded, true};
            if (*scope == "call") return {LanguageErrorCode::Timeout, true};
            return {};
        }
        case BudgetExceeded: {
            const auto scope = detail_string(error, "budget_scope");
            if (!scope) return {};
            if (*scope == "external_memory") return {LanguageErrorCode::MemoryLimitExceeded, true};
            if (*scope == "host_operation") return {LanguageErrorCode::TaskLimitExceeded, true};
            return {};
        }
        case Unavailable:
        case IoError:
        case ProtocolError:
        case Backpressure: return {LanguageErrorCode::HostUnavailable, true};
        case DeviceDisconnected: return {LanguageErrorCode::DeviceDisconnected, true};
        case ResourceNotFound: return {LanguageErrorCode::ResourceMissing, true};
        case ModelUnavailable: return {LanguageErrorCode::OcrModelUnavailable, true};
        case Internal: return {LanguageErrorCode::HostInternal, true};
    }
    return {};
}

LanguageErrorCode translate_host_boundary_failure(
    const HostResult::BoundaryFailure failure) noexcept
{
    return failure == HostResult::BoundaryFailure::Allocation
        ? LanguageErrorCode::MemoryLimitExceeded
        : LanguageErrorCode::HostInternal;
}

JsonBridgeLimits effective_host_json_limits(const SynchronousHostLimits& limits) noexcept
{
    auto result = limits.json_limits;
    result.max_string_bytes = std::min(result.max_string_bytes, limits.max_string_bytes);
    return result;
}

HostValue heap_to_host_value(
    const Heap& heap, const Value value, const HostValueType expected,
    const JsonBridgeLimits limits)
{
    const auto kind = heap.kind(value);
    switch (expected) {
        case HostValueType::Null:
            if (kind == ValueKind::Null) return HostValue();
            break;
        case HostValueType::Boolean:
            if (kind == ValueKind::Boolean) return HostValue(value.as_boolean());
            break;
        case HostValueType::Integer:
            if (kind == ValueKind::Integer) return HostValue(value.as_integer());
            break;
        case HostValueType::Float:
            if (kind == ValueKind::Float && std::isfinite(value.as_float())) return HostValue(value.as_float());
            break;
        case HostValueType::String:
            if (kind == ValueKind::String) {
                auto string = heap.string_copy(value.as_heap_ref());
                if (string.size() > limits.max_string_bytes)
                    throw RuntimeError(RuntimeErrorCode::JsonStringLimitExceeded, "Host string limit exceeded");
                return HostValue(std::move(string));
            }
            break;
        case HostValueType::Json:
            return HostValue(heap_value_to_json(heap, value, limits));
        case HostValueType::OrderedStringJsonMap: {
            auto converted = heap_value_to_json(heap, value, limits);
            if (converted.kind() != JsonKind::Object)
                throw RuntimeError(RuntimeErrorCode::TypeMismatch, "Host argument must be an ordered map");
            return HostValue(std::move(converted));
        }
    }
    throw RuntimeError(RuntimeErrorCode::TypeMismatch, "Host argument type does not match the binding contract");
}

Value host_to_heap_value(
    Heap& heap, const HostValue& value, const HostValueType expected,
    const JsonBridgeLimits limits)
{
    if (!validate_host_value(value, expected, limits))
        throw RuntimeError(RuntimeErrorCode::TypeMismatch, "Host result does not match the binding contract");
    switch (expected) {
        case HostValueType::Null: return Value::null();
        case HostValueType::Boolean: return Value(std::get<bool>(value.storage()));
        case HostValueType::Integer: return Value(std::get<std::int64_t>(value.storage()));
        case HostValueType::Float: return Value(std::get<double>(value.storage()));
        case HostValueType::String: return heap.allocate_string(std::get<std::string>(value.storage()));
        case HostValueType::Json: return json_to_heap_value(heap, std::get<JsonValue>(value.storage()), limits);
        case HostValueType::OrderedStringJsonMap:
            return json_to_heap_value(heap, std::get<JsonValue>(value.storage()), limits);
    }
    throw RuntimeError(RuntimeErrorCode::TypeMismatch, "invalid Host result contract");
}

HostResult invoke_host_callback(
    const SynchronousNativeBinding& binding, const HostCallContext& context,
    const HostArguments& arguments, const SynchronousHostLimits& limits) noexcept
{
    try {
        if (binding.contract.execution != HostExecutionMode::ThreadSafe ||
            binding.contract.cancellation != HostCancellationMode::Preflight)
            return HostResult::boundary_failure(HostResult::BoundaryFailure::CallbackException);
        auto result = binding.callback(context, arguments);
        if (result.ok()) {
            if (!validate_host_value(
                    result.value(), binding.contract.result, effective_host_json_limits(limits)))
                return HostResult::boundary_failure(HostResult::BoundaryFailure::CallbackException);
            return result;
        }
        if (!result.has_error()) return result;
        const auto& error = result.error();
        if (!valid_enum(error.code) || !valid_effect(error.effect_state) ||
            error.message.size() > limits.max_safe_message_bytes ||
            !valid_utf8(error.message) ||
            (error.details && !validate_json(
                *error.details, effective_host_json_limits(limits))))
            return HostResult::boundary_failure(HostResult::BoundaryFailure::CallbackException);
        return result;
    } catch (const std::bad_alloc&) {
        return HostResult::boundary_failure(HostResult::BoundaryFailure::Allocation);
    } catch (const std::exception&) {
        return HostResult::boundary_failure(HostResult::BoundaryFailure::CallbackException);
    } catch (...) {
        return HostResult::boundary_failure(HostResult::BoundaryFailure::CallbackException);
    }
}

}  // namespace baas::script::runtime
