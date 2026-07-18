#include "script/host/ProcedureHost.h"

#include <algorithm>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <thread>
#include <utility>

namespace baas::script::host {
namespace {

using runtime::HostArguments;
using runtime::HostCallContext;
using runtime::HostEffectState;
using runtime::HostErrorCode;
using runtime::HostResult;
using runtime::HostValue;
using runtime::HostValueType;
using runtime::JsonArray;
using runtime::JsonKind;
using runtime::JsonObject;
using runtime::JsonValue;

struct AdmissionCoordinatorIdentity final {};

struct ProcedureAdmissionNode final {
    std::shared_ptr<const AdmissionCoordinatorIdentity> coordinator;
    std::string device_id;
    std::atomic<bool> active{};
};

class ProcedureAdmissionChain final : public runtime::HostAdmissionToken {
public:
    explicit ProcedureAdmissionChain(
        std::vector<std::shared_ptr<ProcedureAdmissionNode>> lineage_value)
        : lineage(std::move(lineage_value))
    {
    }

    std::vector<std::shared_ptr<ProcedureAdmissionNode>> lineage;
};

#ifdef BAAS_SCRIPT_PROCEDURE_HOST_TEST_HOOKS
std::atomic<std::size_t> queued_acquisition_failure_checkpoint{};
std::atomic<std::size_t> result_processing_failure_checkpoint{};
#endif

void queued_acquisition_allocation_checkpoint(
    const bool queued, const std::size_t checkpoint)
{
#ifdef BAAS_SCRIPT_PROCEDURE_HOST_TEST_HOOKS
    if (!queued) return;
    auto expected = checkpoint;
    if (queued_acquisition_failure_checkpoint.compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel))
        throw std::bad_alloc();
#else
    (void)queued;
    (void)checkpoint;
#endif
}

void result_processing_allocation_checkpoint(const std::size_t checkpoint)
{
#ifdef BAAS_SCRIPT_PROCEDURE_HOST_TEST_HOOKS
    auto expected = checkpoint;
    if (result_processing_failure_checkpoint.compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel))
        throw std::bad_alloc();
#else
    (void)checkpoint;
#endif
}

[[nodiscard]] JsonValue detail(const char* key, const char* value)
{
    return JsonValue(JsonObject{{key, JsonValue(value)}});
}

[[nodiscard]] HostResult error(
    const HostErrorCode code, const char* message, const bool retryable,
    const HostEffectState effect_state,
    std::optional<JsonValue> details = std::nullopt)
{
    return HostResult::failure(
        {code, message, retryable, effect_state, std::move(details)});
}

[[nodiscard]] HostResult cancelled(const HostEffectState effect_state)
{
    return error(HostErrorCode::Cancelled, "procedure execution cancelled", false,
                 effect_state);
}

[[nodiscard]] HostResult deadline(
    const HostEffectState effect_state,
    const ProcedureDeadlineScope scope = ProcedureDeadlineScope::Context)
{
    return error(HostErrorCode::DeadlineExceeded, "procedure deadline exceeded", false,
                 effect_state, detail("deadline_scope",
                                      scope == ProcedureDeadlineScope::Context
                                          ? "context" : "call"));
}

[[nodiscard]] bool valid_host_limits(const ProcedureHostLimits& limits) noexcept
{
    return limits.max_device_id_bytes != 0 && limits.max_option_depth != 0 &&
        limits.max_option_nodes != 0 && limits.max_option_bytes != 0 &&
        limits.max_option_work != 0 && limits.max_result_depth != 0 &&
        limits.max_result_depth <= 256 && limits.max_result_nodes != 0 &&
        limits.max_result_string_bytes != 0 &&
        limits.max_result_total_bytes != 0 &&
        limits.max_result_validation_work != 0 && limits.max_calls != 0;
}

[[nodiscard]] bool add_bounded(
    std::size_t& target, std::size_t amount, std::size_t limit) noexcept;

enum class ResultValidationCode : std::uint8_t {
    Valid,
    SchemaMismatch,
    LimitExceeded,
};

struct ResultMetrics {
    std::size_t nodes{};
    std::size_t string_bytes{};
    std::size_t total_bytes{};
    std::size_t work{};
};

[[nodiscard]] ResultValidationCode validate_result_value(
    const JsonValue& value, const ProcedureResultFieldSchema& schema,
    const ProcedureHostLimits& limits, std::size_t depth,
    ResultMetrics& metrics) noexcept;

[[nodiscard]] ResultValidationCode validate_result_object(
    const JsonObject& object,
    const std::span<const ProcedureResultFieldSchema> schemas,
    const ProcedureHostLimits& limits, const std::size_t depth,
    ResultMetrics& metrics) noexcept
{
    if (depth > limits.max_result_depth) return ResultValidationCode::LimitExceeded;
    std::size_t schema_index{};
    for (std::size_t object_index{}; object_index < object.size(); ++object_index) {
        if (!add_bounded(metrics.work, 1, limits.max_result_validation_work))
            return ResultValidationCode::LimitExceeded;
        const auto& [key, child] = object[object_index];
        if (key.empty() || key == "end" ||
            !add_bounded(metrics.string_bytes, key.size(),
                         limits.max_result_string_bytes) ||
            !add_bounded(metrics.total_bytes, key.size(),
                         limits.max_result_total_bytes) ||
            !add_bounded(metrics.total_bytes, sizeof(std::size_t),
                         limits.max_result_total_bytes))
            return key.empty() || key == "end"
                ? ResultValidationCode::SchemaMismatch
                : ResultValidationCode::LimitExceeded;
        // schema_index advances monotonically through a descriptor whose field
        // names are unique. A repeated payload key therefore cannot match the
        // next schema position and is rejected below without rescanning every
        // prior attacker-controlled key.
        while (schema_index < schemas.size() && schemas[schema_index].name != key) {
            if (schemas[schema_index].required)
                return ResultValidationCode::SchemaMismatch;
            if (!add_bounded(metrics.work, 1, limits.max_result_validation_work))
                return ResultValidationCode::LimitExceeded;
            ++schema_index;
        }
        if (schema_index == schemas.size())
            return ResultValidationCode::SchemaMismatch;
        const auto status = validate_result_value(
            child, schemas[schema_index], limits, depth, metrics);
        if (status != ResultValidationCode::Valid) return status;
        ++schema_index;
    }
    while (schema_index < schemas.size()) {
        if (schemas[schema_index].required)
            return ResultValidationCode::SchemaMismatch;
        if (!add_bounded(metrics.work, 1, limits.max_result_validation_work))
            return ResultValidationCode::LimitExceeded;
        ++schema_index;
    }
    return ResultValidationCode::Valid;
}

[[nodiscard]] ResultValidationCode validate_result_value(
    const JsonValue& value, const ProcedureResultFieldSchema& schema,
    const ProcedureHostLimits& limits, const std::size_t depth,
    ResultMetrics& metrics) noexcept
{
    if (depth > limits.max_result_depth ||
        !add_bounded(metrics.nodes, 1, limits.max_result_nodes) ||
        !add_bounded(metrics.total_bytes, 1, limits.max_result_total_bytes) ||
        !add_bounded(metrics.work, 1, limits.max_result_validation_work))
        return ResultValidationCode::LimitExceeded;
    const auto expected = [&]() noexcept {
        switch (schema.type) {
            case ProcedureResultJsonType::Null: return JsonKind::Null;
            case ProcedureResultJsonType::Boolean: return JsonKind::Boolean;
            case ProcedureResultJsonType::Integer: return JsonKind::Integer;
            case ProcedureResultJsonType::Float: return JsonKind::Float;
            case ProcedureResultJsonType::String: return JsonKind::String;
            case ProcedureResultJsonType::Array: return JsonKind::Array;
            case ProcedureResultJsonType::Object: return JsonKind::Object;
        }
        return JsonKind::Null;
    }();
    if (value.kind() != expected) return ResultValidationCode::SchemaMismatch;
    switch (value.kind()) {
        case JsonKind::Null: return ResultValidationCode::Valid;
        case JsonKind::Boolean:
            return add_bounded(metrics.total_bytes, 1, limits.max_result_total_bytes)
                ? ResultValidationCode::Valid : ResultValidationCode::LimitExceeded;
        case JsonKind::Integer:
            return add_bounded(metrics.total_bytes, sizeof(std::int64_t),
                               limits.max_result_total_bytes)
                ? ResultValidationCode::Valid : ResultValidationCode::LimitExceeded;
        case JsonKind::Float:
            if (!std::isfinite(std::get<double>(value.value())))
                return ResultValidationCode::SchemaMismatch;
            return add_bounded(metrics.total_bytes, sizeof(double),
                               limits.max_result_total_bytes)
                ? ResultValidationCode::Valid : ResultValidationCode::LimitExceeded;
        case JsonKind::String: {
            const auto bytes = std::get<std::string>(value.value()).size();
            return add_bounded(metrics.string_bytes, bytes,
                               limits.max_result_string_bytes) &&
                    add_bounded(metrics.total_bytes, bytes,
                                limits.max_result_total_bytes)
                ? ResultValidationCode::Valid : ResultValidationCode::LimitExceeded;
        }
        case JsonKind::Array: {
            if (schema.children.size() != 1)
                return ResultValidationCode::SchemaMismatch;
            const auto& array = std::get<JsonArray>(value.value());
            for (const auto& child : array) {
                if (!add_bounded(metrics.total_bytes, sizeof(std::size_t),
                                 limits.max_result_total_bytes) ||
                    !add_bounded(metrics.work, 1, limits.max_result_validation_work))
                    return ResultValidationCode::LimitExceeded;
                const auto status = validate_result_value(
                    child, schema.children.front(), limits, depth + 1, metrics);
                if (status != ResultValidationCode::Valid) return status;
            }
            return ResultValidationCode::Valid;
        }
        case JsonKind::Object:
            return validate_result_object(
                std::get<JsonObject>(value.value()), schema.children,
                limits, depth + 1, metrics);
    }
    return ResultValidationCode::SchemaMismatch;
}

[[nodiscard]] ResultValidationCode validate_result_payload(
    const JsonObject& payload, const ProcedureDescriptor& descriptor,
    const ProcedureHostLimits& limits) noexcept
{
    ResultMetrics metrics;
    if (!add_bounded(metrics.nodes, 1, limits.max_result_nodes) ||
        !add_bounded(metrics.total_bytes, 1, limits.max_result_total_bytes) ||
        !add_bounded(metrics.work, 1, limits.max_result_validation_work))
        return ResultValidationCode::LimitExceeded;
    return validate_result_object(
        payload, descriptor.result_schema(), limits, 1, metrics);
}

struct OptionMetrics {
    std::size_t nodes{};
    std::size_t bytes{};
    std::size_t work{};
};

[[nodiscard]] bool add_bounded(
    std::size_t& target, const std::size_t amount, const std::size_t limit) noexcept
{
    if (amount > limit || target > limit - amount) return false;
    target += amount;
    return true;
}

[[nodiscard]] bool validate_option_value(
    const JsonValue& value, const ProcedureHostLimits& limits,
    const std::size_t depth, OptionMetrics& metrics) noexcept
{
    if (depth > limits.max_option_depth ||
        !add_bounded(metrics.nodes, 1, limits.max_option_nodes) ||
        !add_bounded(metrics.work, 1, limits.max_option_work)) return false;
    switch (value.kind()) {
        case JsonKind::Null: return true;
        case JsonKind::Boolean: return add_bounded(metrics.bytes, 1, limits.max_option_bytes);
        case JsonKind::Integer:
        case JsonKind::Float:
            return add_bounded(metrics.bytes, sizeof(std::uint64_t), limits.max_option_bytes);
        case JsonKind::String:
            return add_bounded(metrics.bytes,
                std::get<std::string>(value.value()).size(), limits.max_option_bytes);
        case JsonKind::Array: {
            const auto& array = std::get<JsonArray>(value.value());
            for (const auto& child : array) {
                if (!add_bounded(metrics.work, 1, limits.max_option_work) ||
                    !validate_option_value(child, limits, depth + 1, metrics)) return false;
            }
            return true;
        }
        case JsonKind::Object: {
            const auto& object = std::get<JsonObject>(value.value());
            for (std::size_t index{}; index < object.size(); ++index) {
                const auto& [key, child] = object[index];
                if (key.empty() ||
                    !add_bounded(metrics.bytes, key.size(), limits.max_option_bytes) ||
                    !add_bounded(metrics.work, 1, limits.max_option_work)) return false;
                for (std::size_t previous{}; previous < index; ++previous) {
                    if (!add_bounded(metrics.work, 1, limits.max_option_work) ||
                        object[previous].first == key) return false;
                }
                if (!validate_option_value(child, limits, depth + 1, metrics)) return false;
            }
            return true;
        }
    }
    return false;
}

class BoundedEffectTrace final : public ProcedureEffectReporter {
public:
    explicit BoundedEffectTrace(const ProcedureDescriptor& descriptor) noexcept
    {
        for (const auto effect : descriptor.declared_effects())
            declared_ |= bit(effect);
    }

    bool report(const ProcedureEffect effect, const ProcedureEffectStage stage) noexcept override
    {
        const auto mask = bit(effect);
        if (mask == 0 || (declared_ & mask) == 0) {
            invalid_.store(true, std::memory_order_release);
            return false;
        }
        const auto index = static_cast<std::size_t>(effect);
        auto expected = static_cast<std::uint8_t>(EffectState::NotStarted);
        std::uint8_t desired{};
        switch (stage) {
            case ProcedureEffectStage::Began: {
                desired = static_cast<std::uint8_t>(EffectState::Began);
                auto current = states_[index].load(std::memory_order_acquire);
                while (current == static_cast<std::uint8_t>(EffectState::NotStarted) ||
                       current == static_cast<std::uint8_t>(EffectState::Committed)) {
                    if (states_[index].compare_exchange_weak(
                            current, desired, std::memory_order_acq_rel,
                            std::memory_order_acquire)) return true;
                }
                invalid_.store(true, std::memory_order_release);
                return false;
            }
            case ProcedureEffectStage::Committed:
                expected = static_cast<std::uint8_t>(EffectState::Began);
                desired = static_cast<std::uint8_t>(EffectState::Committed);
                break;
            case ProcedureEffectStage::Unknown:
                expected = static_cast<std::uint8_t>(EffectState::Began);
                desired = static_cast<std::uint8_t>(EffectState::Unknown);
                break;
            default:
                invalid_.store(true, std::memory_order_release);
                return false;
        }
        if (states_[index].compare_exchange_strong(
                expected, desired, std::memory_order_acq_rel, std::memory_order_acquire))
            return true;
        invalid_.store(true, std::memory_order_release);
        return false;
    }

    [[nodiscard]] bool invalid() const noexcept
    {
        return invalid_.load(std::memory_order_acquire);
    }

    [[nodiscard]] HostEffectState effect_state() const noexcept
    {
        bool committed{};
        for (const auto& state : states_) {
            const auto value = static_cast<EffectState>(state.load(std::memory_order_acquire));
            if (value == EffectState::Began || value == EffectState::Unknown)
                return HostEffectState::Unknown;
            committed = committed || value == EffectState::Committed;
        }
        return committed ? HostEffectState::Committed : HostEffectState::NotStarted;
    }

    [[nodiscard]] HostEffectState input_effect_state() const noexcept
    {
        const auto value = static_cast<EffectState>(
            states_[static_cast<std::size_t>(ProcedureEffect::Input)].load(
                std::memory_order_acquire));
        if (value == EffectState::Began || value == EffectState::Unknown)
            return HostEffectState::Unknown;
        return value == EffectState::Committed
            ? HostEffectState::Committed : HostEffectState::NotStarted;
    }

private:
    enum class EffectState : std::uint8_t { NotStarted, Began, Committed, Unknown };

    [[nodiscard]] static std::uint32_t bit(const ProcedureEffect effect) noexcept
    {
        const auto value = static_cast<unsigned int>(effect);
        return value <= static_cast<unsigned int>(ProcedureEffect::ForegroundCheck)
            ? (UINT32_C(1) << value) : 0;
    }

    std::uint32_t declared_{};
    std::array<std::atomic<std::uint8_t>, 5> states_{};
    std::atomic<bool> invalid_{};
};

[[nodiscard]] HostEffectState merge_effect_state(
    const HostEffectState reported, const HostEffectState supplied) noexcept
{
    if (reported == HostEffectState::Unknown || supplied == HostEffectState::Unknown)
        return HostEffectState::Unknown;
    if (reported == HostEffectState::Committed || supplied == HostEffectState::Committed)
        return HostEffectState::Committed;
    return HostEffectState::NotStarted;
}

[[nodiscard]] HostResult map_executor_error(
    const ProcedureExecutorError& supplied, const HostEffectState reported,
    const HostEffectState reported_input)
{
    const auto effect = merge_effect_state(reported, supplied.effect_state);
    switch (supplied.code) {
        case ProcedureExecutorErrorCode::InvalidRequest:
            return error(HostErrorCode::InvalidArgument,
                         "procedure executor rejected the request", false, effect);
        case ProcedureExecutorErrorCode::Cancelled: return cancelled(effect);
        case ProcedureExecutorErrorCode::DeadlineExceeded:
            return deadline(effect, supplied.deadline_scope);
        case ProcedureExecutorErrorCode::BudgetExceeded:
            return error(HostErrorCode::BudgetExceeded,
                         "procedure executor budget exceeded", supplied.retryable, effect,
                         detail("budget_scope", "host_operation"));
        case ProcedureExecutorErrorCode::ResourceExhausted:
            return error(HostErrorCode::BudgetExceeded,
                         "procedure executor resource exhausted", false, effect,
                         detail("budget_scope", "external_memory"));
        case ProcedureExecutorErrorCode::Unavailable:
            if (supplied.unavailable_reason ==
                ProcedureUnavailableReason::RecentFrameUnavailable)
                return error(HostErrorCode::Unavailable,
                             "recent device frame is unavailable",
                             supplied.retryable, effect,
                             detail("unavailable_reason", "recent_frame_unavailable"));
            return error(HostErrorCode::Unavailable,
                         "procedure executor unavailable", supplied.retryable, effect);
        case ProcedureExecutorErrorCode::ForegroundPackageMismatch:
            return error(HostErrorCode::Unavailable,
                         "device foreground package does not match", true,
                         reported_input,
                         detail("unavailable_reason", "foreground_package_mismatch"));
        case ProcedureExecutorErrorCode::DeviceDisconnected:
            return error(HostErrorCode::DeviceDisconnected,
                         "procedure device disconnected", supplied.retryable, effect);
        case ProcedureExecutorErrorCode::ResourceNotFound:
            return error(HostErrorCode::ResourceNotFound,
                         "procedure resource unavailable", supplied.retryable, effect);
        case ProcedureExecutorErrorCode::Internal:
            return error(HostErrorCode::Internal,
                         "procedure executor internal failure", false, effect);
    }
    return error(HostErrorCode::Internal, "invalid procedure executor error", false,
                 HostEffectState::Unknown);
}

}  // namespace

#ifdef BAAS_SCRIPT_PROCEDURE_HOST_TEST_HOOKS
namespace testing {

void fail_queued_acquisition_at_allocation(
    const std::size_t checkpoint) noexcept
{
    queued_acquisition_failure_checkpoint.store(
        checkpoint, std::memory_order_release);
}

void fail_result_processing_at_allocation(
    const std::size_t checkpoint) noexcept
{
    result_processing_failure_checkpoint.store(
        checkpoint, std::memory_order_release);
}

}  // namespace testing
#endif

ProcedureExecutorOutcome::ProcedureExecutorOutcome(
    std::variant<Success, ProcedureExecutorError> value)
    : value_(std::move(value))
{
}

ProcedureExecutorOutcome ProcedureExecutorOutcome::success(std::string terminal_id)
{
    return ProcedureExecutorOutcome(
        std::variant<Success, ProcedureExecutorError>(
            std::in_place_index<0>, Success{std::move(terminal_id), {}}));
}

ProcedureExecutorOutcome ProcedureExecutorOutcome::success(
    std::string terminal_id, JsonObject payload)
{
    return ProcedureExecutorOutcome(
        std::variant<Success, ProcedureExecutorError>(
            std::in_place_index<0>,
            Success{std::move(terminal_id), std::move(payload)}));
}

ProcedureExecutorOutcome ProcedureExecutorOutcome::failure(ProcedureExecutorError error)
{
    return ProcedureExecutorOutcome(
        std::variant<Success, ProcedureExecutorError>(
            std::in_place_index<1>, std::move(error)));
}

bool ProcedureExecutorOutcome::ok() const noexcept { return value_.index() == 0; }
const std::string& ProcedureExecutorOutcome::terminal_id() const
{
    return std::get<Success>(value_).terminal_id;
}
const JsonObject& ProcedureExecutorOutcome::payload() const
{
    return std::get<Success>(value_).payload;
}
const ProcedureExecutorError& ProcedureExecutorOutcome::error() const
{
    return std::get<ProcedureExecutorError>(value_);
}

ProcedureExecutionRequest::ProcedureExecutionRequest(
    std::shared_ptr<const ProcedureSnapshot> snapshot,
    std::shared_ptr<const ProcedureDescriptor> procedure,
    std::string device_id, JsonObject options,
    std::shared_ptr<const runtime::HostCancellationProbe> cancellation,
    ProcedureEffectReporter& effects)
    : ProcedureExecutionRequest(
          std::move(snapshot), std::move(procedure), std::move(device_id),
          std::move(options), std::move(cancellation), {}, effects)
{
}

ProcedureExecutionRequest::ProcedureExecutionRequest(
    std::shared_ptr<const ProcedureSnapshot> snapshot,
    std::shared_ptr<const ProcedureDescriptor> procedure,
    std::string device_id, JsonObject options,
    std::shared_ptr<const runtime::HostCancellationProbe> cancellation,
    std::shared_ptr<const runtime::HostAdmissionToken> admission,
    ProcedureEffectReporter& effects)
    : snapshot_(std::move(snapshot)), procedure_(std::move(procedure)),
      device_id_(std::move(device_id)), options_(std::move(options)),
      cancellation_(std::move(cancellation)), admission_(std::move(admission)),
      effects_(&effects)
{
}

const std::shared_ptr<const ProcedureSnapshot>&
ProcedureExecutionRequest::snapshot() const noexcept { return snapshot_; }
const std::shared_ptr<const ProcedureDescriptor>&
ProcedureExecutionRequest::procedure() const noexcept { return procedure_; }
const std::string& ProcedureExecutionRequest::device_id() const noexcept { return device_id_; }
const JsonObject& ProcedureExecutionRequest::options() const noexcept { return options_; }
bool ProcedureExecutionRequest::cancelled() const noexcept
{
    return cancellation_ && cancellation_->cancelled();
}
bool ProcedureExecutionRequest::deadline_exceeded() const noexcept
{
    return cancellation_ && cancellation_->deadline_exceeded();
}
const std::shared_ptr<const runtime::HostAdmissionToken>&
ProcedureExecutionRequest::admission_token() const noexcept { return admission_; }
ProcedureEffectReporter& ProcedureExecutionRequest::effects() const noexcept { return *effects_; }

struct PhysicalDeviceCoordinator::Impl {
    struct LeaseToken {
        std::shared_ptr<PhysicalDeviceCoordinator> owner;
        std::string device_id;
        std::shared_ptr<ProcedureAdmissionNode> admission;
        bool armed{};
        ~LeaseToken()
        {
            if (armed) {
                admission->active.store(false, std::memory_order_release);
                owner->release(device_id);
            }
        }
    };

    struct DeviceState {
        bool busy{};
        std::thread::id owner;
        std::deque<std::uint64_t> waiters;
    };

    explicit Impl(const PhysicalDeviceCoordinatorLimits configured)
        : limits(configured), identity(std::make_shared<AdmissionCoordinatorIdentity>())
    {
    }

    PhysicalDeviceCoordinatorLimits limits;
    std::shared_ptr<const AdmissionCoordinatorIdentity> identity;
    mutable std::timed_mutex mutex;
    std::condition_variable_any changed;
    std::map<std::string, DeviceState, std::less<>> devices;
    std::uint64_t next_ticket{1};
    std::size_t waiter_count{};
    bool stopping{};
};

PhysicalDeviceCoordinator::PhysicalDeviceCoordinator(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

PhysicalDeviceCoordinator::~PhysicalDeviceCoordinator() { shutdown(); }

std::shared_ptr<PhysicalDeviceCoordinator> PhysicalDeviceCoordinator::create(
    const PhysicalDeviceCoordinatorLimits limits)
{
    if (limits.max_devices == 0 || limits.max_waiters == 0 ||
        limits.max_device_id_bytes == 0 || limits.poll_interval.count() <= 0 ||
        limits.max_admission_depth == 0 ||
        limits.poll_interval > std::chrono::seconds(1))
        throw std::invalid_argument("physical-device coordinator limits are invalid");
    return std::shared_ptr<PhysicalDeviceCoordinator>(
        new PhysicalDeviceCoordinator(std::make_unique<Impl>(limits)));
}

PhysicalDeviceAcquireResult PhysicalDeviceCoordinator::acquire(
    const std::string_view device_id,
    const std::shared_ptr<const runtime::HostCancellationProbe>& cancellation,
    const std::shared_ptr<const runtime::HostAdmissionToken>& parent_admission)
{
    if (!valid_physical_device_id(device_id, impl_->limits.max_device_id_bytes))
        return {PhysicalDeviceAcquireCode::InvalidDeviceId, {}, {}};
    std::unique_lock<std::timed_mutex> lock(impl_->mutex, std::defer_lock);
    for (;;) {
        if (cancellation && cancellation->deadline_exceeded())
            return {PhysicalDeviceAcquireCode::DeadlineExceeded, {}, {}};
        if (cancellation && cancellation->cancelled())
            return {PhysicalDeviceAcquireCode::Cancelled, {}, {}};
        if (lock.try_lock_for(impl_->limits.poll_interval)) break;
    }
    if (impl_->stopping) return {PhysicalDeviceAcquireCode::Shutdown, {}, {}};
    auto found = impl_->devices.find(device_id);
    if (found == impl_->devices.end()) {
        if (impl_->devices.size() >= impl_->limits.max_devices)
            return {PhysicalDeviceAcquireCode::Backpressure, {}, {}};
        found = impl_->devices.emplace(std::string(device_id), Impl::DeviceState{}).first;
    }
    auto& state = found->second;
    const auto parent =
        std::dynamic_pointer_cast<const ProcedureAdmissionChain>(parent_admission);
    std::size_t active_depth{};
    if (parent) {
        for (const auto& node : parent->lineage) {
            if (!node->active.load(std::memory_order_acquire)) continue;
            ++active_depth;
            if (node->coordinator.get() == impl_->identity.get() &&
                node->device_id == device_id)
                return {PhysicalDeviceAcquireCode::Reentrant, {}, {}};
        }
    }
    if (state.busy && state.owner == std::this_thread::get_id())
        return {PhysicalDeviceAcquireCode::Reentrant, {}, {}};
    if (active_depth >= impl_->limits.max_admission_depth)
        return {PhysicalDeviceAcquireCode::Backpressure, {}, {}};

    const auto owner = shared_from_this();
    auto make_acquisition = [&](const bool queued) -> PhysicalDeviceAcquireResult {
        std::size_t allocation_checkpoint{};
        const auto checkpoint = [&] {
            queued_acquisition_allocation_checkpoint(
                queued, ++allocation_checkpoint);
        };
        std::vector<std::shared_ptr<ProcedureAdmissionNode>> lineage;
        checkpoint();
        lineage.reserve(active_depth + 1);
        if (parent) {
            for (const auto& node : parent->lineage)
                if (node->active.load(std::memory_order_acquire))
                    lineage.push_back(node);
        }
        checkpoint();
        auto node = std::make_shared<ProcedureAdmissionNode>();
        node->coordinator = impl_->identity;
        checkpoint();
        node->device_id = device_id;
        lineage.push_back(node);
        checkpoint();
        auto admission = std::make_shared<const ProcedureAdmissionChain>(
            std::move(lineage));
        checkpoint();
        auto lease = std::make_shared<Impl::LeaseToken>();
        lease->owner = owner;
        checkpoint();
        lease->device_id = device_id;
        lease->admission = node;
        state.busy = true;
        state.owner = std::this_thread::get_id();
        node->active.store(true, std::memory_order_release);
        lease->armed = true;
        return {PhysicalDeviceAcquireCode::Acquired, std::move(lease),
                std::move(admission)};
    };
    if (!state.busy && state.waiters.empty())
        return make_acquisition(false);
    if (impl_->waiter_count >= impl_->limits.max_waiters)
        return {PhysicalDeviceAcquireCode::Backpressure, {}, {}};
    auto ticket = impl_->next_ticket++;
    if (ticket == 0) ticket = impl_->next_ticket++;
    state.waiters.push_back(ticket);
    ++impl_->waiter_count;

    auto cancel_wait = [&]() noexcept {
        const auto current = impl_->devices.find(device_id);
        if (current == impl_->devices.end()) return;
        auto& waiters = current->second.waiters;
        const auto position = std::find(waiters.begin(), waiters.end(), ticket);
        if (position != waiters.end()) {
            waiters.erase(position);
            --impl_->waiter_count;
        }
        if (!current->second.busy && waiters.empty()) impl_->devices.erase(current);
        impl_->changed.notify_all();
    };
    for (;;) {
        // Deadline wins when both become observable at the same checkpoint.
        if (cancellation && cancellation->deadline_exceeded()) {
            cancel_wait();
            return {PhysicalDeviceAcquireCode::DeadlineExceeded, {}, {}};
        }
        if (cancellation && cancellation->cancelled()) {
            cancel_wait();
            return {PhysicalDeviceAcquireCode::Cancelled, {}, {}};
        }
        if (impl_->stopping) {
            cancel_wait();
            return {PhysicalDeviceAcquireCode::Shutdown, {}, {}};
        }
        const auto current = impl_->devices.find(device_id);
        if (current != impl_->devices.end() && !current->second.busy &&
            !current->second.waiters.empty() && current->second.waiters.front() == ticket) {
            try {
                // Every potentially-throwing operation occurs before busy is
                // published. If construction fails, remove this front ticket
                // so the device queue cannot remain permanently poisoned.
                auto acquisition = make_acquisition(true);
                current->second.waiters.pop_front();
                --impl_->waiter_count;
                return acquisition;
            } catch (...) {
                cancel_wait();
                throw;
            }
        }
        impl_->changed.wait_for(lock, impl_->limits.poll_interval);
    }
}

void PhysicalDeviceCoordinator::release(const std::string_view device_id) noexcept
{
    try {
        const std::scoped_lock lock(impl_->mutex);
        const auto found = impl_->devices.find(device_id);
        if (found == impl_->devices.end()) return;
        found->second.busy = false;
        found->second.owner = {};
        if (found->second.waiters.empty()) impl_->devices.erase(found);
        impl_->changed.notify_all();
    } catch (...) {
        // All lookup keys were allocated by acquire; std::map::find/erase and
        // notification do not allocate. This is a final no-throw safety net.
    }
}

void PhysicalDeviceCoordinator::shutdown() noexcept
{
    try {
        const std::scoped_lock lock(impl_->mutex);
        impl_->stopping = true;
        impl_->changed.notify_all();
    } catch (...) {
    }
}

PhysicalDeviceCoordinatorStats PhysicalDeviceCoordinator::stats() const noexcept
{
    try {
        const std::scoped_lock lock(impl_->mutex);
        std::size_t active{};
        for (const auto& [ignored, state] : impl_->devices) {
            (void)ignored;
            active += state.busy ? 1U : 0U;
        }
        return {active, impl_->waiter_count, impl_->stopping};
    } catch (...) {
        return {};
    }
}

bool valid_physical_device_id(const std::string_view value, const std::size_t max_bytes) noexcept
{
    if (value.empty() || value.size() > max_bytes || value == "." || value == "..")
        return false;
    return std::all_of(value.begin(), value.end(), [](const char character) {
        return (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '-' ||
            character == '_' || character == '.' || character == ':';
    });
}

struct ProcedureHost::Impl {
    Impl(std::shared_ptr<const ProcedureSnapshot> snapshot_value,
         std::string device_id_value, std::shared_ptr<ProcedureExecutor> executor_value,
         std::shared_ptr<PhysicalDeviceCoordinator> coordinator_value,
         const ProcedureHostLimits configured)
        : snapshot(std::move(snapshot_value)), device_id(std::move(device_id_value)),
          executor(std::move(executor_value)), coordinator(std::move(coordinator_value)),
          limits(configured)
    {
    }

    [[nodiscard]] HostResult run(
        const HostCallContext& context, const HostArguments& arguments)
    {
        const auto call = calls.fetch_add(1, std::memory_order_acq_rel);
        if (call >= limits.max_calls) {
            calls.fetch_sub(1, std::memory_order_acq_rel);
            ++failed;
            return error(HostErrorCode::BudgetExceeded,
                         "procedure call budget exceeded", true,
                         HostEffectState::NotStarted,
                         detail("budget_scope", "host_operation"));
        }
        if (arguments.size() != 2 || !arguments[0] ||
            arguments[0]->type() != HostValueType::String ||
            (arguments[1] && arguments[1]->type() != HostValueType::Json)) {
            ++failed;
            return error(HostErrorCode::InvalidArgument,
                         "invalid procedure run arguments", false,
                         HostEffectState::NotStarted);
        }
        const auto& procedure_id = std::get<std::string>(arguments[0]->storage());
        if (!snapshot->accepts_procedure_id(procedure_id)) {
            ++failed;
            return error(HostErrorCode::InvalidArgument,
                         "procedure id is not canonical", false,
                         HostEffectState::NotStarted);
        }
        const auto procedure = snapshot->resolve(procedure_id);
        if (!procedure) {
            ++failed;
            return error(HostErrorCode::ResourceNotFound,
                         "procedure is absent from the immutable snapshot", false,
                         HostEffectState::NotStarted);
        }
        JsonObject options;
        if (arguments[1]) {
            const auto& json = std::get<JsonValue>(arguments[1]->storage());
            if (json.kind() != JsonKind::Object) {
                ++failed;
                return error(HostErrorCode::InvalidArgument,
                             "procedure options must be an ordered map", false,
                             HostEffectState::NotStarted);
            }
            OptionMetrics metrics;
            if (!validate_option_value(json, limits, 1, metrics)) {
                ++failed;
                return error(HostErrorCode::InvalidArgument,
                             "procedure options exceed bounded canonical shape", false,
                             HostEffectState::NotStarted);
            }
            options = std::get<JsonObject>(json.value());
        }
        if (context.deadline_exceeded()) {
            ++failed;
            return deadline(HostEffectState::NotStarted);
        }
        if (context.cancelled()) {
            ++failed;
            return cancelled(HostEffectState::NotStarted);
        }

        auto acquired = coordinator->acquire(
            device_id, context.cancellation, context.admission);
        if (acquired.code != PhysicalDeviceAcquireCode::Acquired) {
            ++failed;
            switch (acquired.code) {
                case PhysicalDeviceAcquireCode::Cancelled:
                    ++cancelled_waiting;
                    return cancelled(HostEffectState::NotStarted);
                case PhysicalDeviceAcquireCode::DeadlineExceeded:
                    ++cancelled_waiting;
                    return deadline(HostEffectState::NotStarted);
                case PhysicalDeviceAcquireCode::Reentrant:
                    return error(HostErrorCode::Unavailable,
                                 "physical-device strand reentry rejected", false,
                                 HostEffectState::NotStarted);
                case PhysicalDeviceAcquireCode::Backpressure:
                    return error(HostErrorCode::Backpressure,
                                 "physical-device strand is saturated", true,
                                 HostEffectState::NotStarted);
                case PhysicalDeviceAcquireCode::Shutdown:
                    return error(HostErrorCode::Unavailable,
                                 "physical-device coordinator is shut down", false,
                                 HostEffectState::NotStarted);
                case PhysicalDeviceAcquireCode::InvalidDeviceId:
                    return error(HostErrorCode::Internal,
                                 "frozen physical-device id became invalid", false,
                                 HostEffectState::NotStarted);
                case PhysicalDeviceAcquireCode::Acquired: break;
            }
        }
        // Keep the lease, snapshot, descriptor, executor, options, and probe
        // owned through the complete adapter call and postflight checks.
        auto lease = std::move(acquired.lease);
        BoundedEffectTrace trace(*procedure);
        if (context.deadline_exceeded()) {
            ++failed;
            return deadline(HostEffectState::NotStarted);
        }
        if (context.cancelled()) {
            ++failed;
            return cancelled(HostEffectState::NotStarted);
        }

        ProcedureExecutorOutcome outcome = ProcedureExecutorOutcome::failure(
            {ProcedureExecutorErrorCode::Internal, false, HostEffectState::Unknown});
        try {
            ProcedureExecutionRequest request(
                snapshot, procedure, device_id, std::move(options),
                context.cancellation, acquired.admission, trace);
            outcome = executor->execute(request);
        } catch (const std::bad_alloc&) {
            ++failed;
            return error(HostErrorCode::BudgetExceeded,
                         "procedure executor allocation failed", false,
                         trace.effect_state(),
                         detail("budget_scope", "external_memory"));
        } catch (const std::exception&) {
            ++failed;
            return error(HostErrorCode::Internal,
                         "procedure executor threw an exception", false,
                         trace.effect_state());
        } catch (...) {
            ++failed;
            return error(HostErrorCode::Internal,
                         "procedure executor threw a non-standard exception", false,
                         trace.effect_state());
        }
        if (trace.invalid()) {
            ++failed;
            return error(HostErrorCode::Internal,
                         "procedure executor reported an undeclared effect", false,
                         trace.effect_state());
        }
        // Deadline has deterministic precedence over cancellation and adapter
        // completion whenever they become observable at the same safe point.
        if (context.deadline_exceeded()) {
            ++failed;
            return deadline(trace.effect_state());
        }
        if (context.cancelled()) {
            ++failed;
            return cancelled(trace.effect_state());
        }
        if (!outcome.ok()) {
            ++failed;
            return map_executor_error(
                outcome.error(), trace.effect_state(), trace.input_effect_state());
        }
        if (!procedure->accepts_terminal(outcome.terminal_id())) {
            ++failed;
            return error(HostErrorCode::Internal,
                         "procedure executor returned an undeclared terminal", false,
                         trace.effect_state());
        }
        const auto payload_status = validate_result_payload(
            outcome.payload(), *procedure, limits);
        std::optional<HostResult> prepared;
        bool allocation_failed{};
        bool processing_failed{};
        try {
            if (payload_status == ResultValidationCode::Valid) {
                result_processing_allocation_checkpoint(1);
                JsonObject object;
                object.reserve(outcome.payload().size() + 1);
                result_processing_allocation_checkpoint(2);
                object.emplace_back("end", JsonValue(outcome.terminal_id()));
                object.insert(
                    object.end(), outcome.payload().begin(), outcome.payload().end());
                result_processing_allocation_checkpoint(3);
                prepared.emplace(HostResult::success(
                    HostValue(JsonValue(std::move(object)))));
            }
        } catch (const std::bad_alloc&) {
            allocation_failed = true;
        } catch (...) {
            processing_failed = true;
        }
        // Result validation/canonical publication is bounded Host work. Repeat
        // postflight after it so a deadline still wins over cancellation and
        // either malformed adapter output or allocation failure.
        if (context.deadline_exceeded()) {
            ++failed;
            return deadline(trace.effect_state());
        }
        if (context.cancelled()) {
            ++failed;
            return cancelled(trace.effect_state());
        }
        if (allocation_failed) {
            ++failed;
            return error(HostErrorCode::BudgetExceeded,
                         "procedure result allocation failed", false,
                         trace.effect_state(),
                         detail("budget_scope", "external_memory"));
        }
        if (processing_failed) {
            ++failed;
            return error(HostErrorCode::Internal,
                         "procedure result processing failed", false,
                         trace.effect_state());
        }
        if (payload_status == ResultValidationCode::LimitExceeded) {
            ++failed;
            return error(HostErrorCode::BudgetExceeded,
                         "procedure result exceeds bounded validation limits", false,
                         trace.effect_state(),
                         detail("budget_scope", "host_operation"));
        }
        if (payload_status != ResultValidationCode::Valid || !prepared) {
            ++failed;
            return error(HostErrorCode::Internal,
                         "procedure executor returned an invalid result payload", false,
                         trace.effect_state());
        }
        ++completed;
        return std::move(*prepared);
    }

    std::shared_ptr<const ProcedureSnapshot> snapshot;
    std::string device_id;
    std::shared_ptr<ProcedureExecutor> executor;
    std::shared_ptr<PhysicalDeviceCoordinator> coordinator;
    ProcedureHostLimits limits;
    std::atomic<std::size_t> calls{};
    std::atomic<std::size_t> completed{};
    std::atomic<std::size_t> failed{};
    std::atomic<std::size_t> cancelled_waiting{};
};

ProcedureHost::ProcedureHost(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
ProcedureHost::~ProcedureHost() = default;
const std::shared_ptr<const ProcedureSnapshot>& ProcedureHost::snapshot() const noexcept
{
    return impl_->snapshot;
}
const std::string& ProcedureHost::device_id() const noexcept { return impl_->device_id; }
ProcedureHostStats ProcedureHost::stats() const noexcept
{
    return {impl_->calls.load(std::memory_order_relaxed),
            impl_->completed.load(std::memory_order_relaxed),
            impl_->failed.load(std::memory_order_relaxed),
            impl_->cancelled_waiting.load(std::memory_order_relaxed)};
}

ProcedureHostRuntime make_procedure_host_runtime(
    std::shared_ptr<const ProcedureSnapshot> snapshot, std::string device_id,
    std::shared_ptr<ProcedureExecutor> executor,
    std::shared_ptr<PhysicalDeviceCoordinator> coordinator,
    const ProcedureHostLimits limits)
{
    if (!snapshot) throw std::invalid_argument("procedure snapshot is absent");
    if (!executor) throw std::invalid_argument("procedure executor is absent");
    if (!coordinator) throw std::invalid_argument("physical-device coordinator is absent");
    if (!valid_host_limits(limits))
        throw std::invalid_argument("procedure Host limits are invalid");
    if (!valid_physical_device_id(device_id, limits.max_device_id_bytes))
        throw std::invalid_argument("procedure Host device id is invalid");

    auto host = std::shared_ptr<ProcedureHost>(new ProcedureHost(
        std::make_unique<ProcedureHost::Impl>(
            snapshot, std::move(device_id), std::move(executor),
            std::move(coordinator), limits)));
    runtime::SynchronousNativeBinding run;
    run.binding_id = "host.procedure.run.v1";
    run.contract = {
        {{"procedure_id", HostValueType::String, true},
         {"options", HostValueType::OrderedStringJsonMap, false}},
        HostValueType::OrderedStringJsonMap, "procedure_steps",
        runtime::HostExecutionMode::ThreadSafe,
        runtime::HostCancellationMode::Cooperative};
    run.callback = [host](const HostCallContext& context, const HostArguments& arguments) {
        try {
            return host->impl_->run(context, arguments);
        } catch (const std::bad_alloc&) {
            return error(HostErrorCode::BudgetExceeded,
                         "procedure Host allocation failed", true,
                         HostEffectState::NotStarted,
                         detail("budget_scope", "external_memory"));
        } catch (const std::exception&) {
            return error(HostErrorCode::Internal,
                         "procedure Host callback failed", false,
                         HostEffectState::NotStarted);
        } catch (...) {
            return error(HostErrorCode::Internal,
                         "procedure Host callback failed", false,
                         HostEffectState::NotStarted);
        }
    };
    auto metadata = std::make_shared<const runtime::HostModuleRegistry>(
        std::vector<runtime::HostModuleDescriptor>{{
            "baas/procedure", {1, 0},
            {{"run", "host.procedure.run.v1", "procedure.execute"}}}});
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{std::move(run)});
    return {std::move(host), std::move(metadata), std::move(bindings)};
}

}  // namespace baas::script::host
