#include "procedure/LegacyProcedureExecution.h"

#include "BAASExceptions.h"

#include <new>
#include <utility>

BAAS_NAMESPACE_BEGIN

std::string_view legacy_procedure_run_code_name(const LegacyProcedureRunCode code) noexcept
{
    switch (code) {
        case LegacyProcedureRunCode::Success: return "success";
        case LegacyProcedureRunCode::MissingProcedure: return "missing_procedure";
        case LegacyProcedureRunCode::InvalidDefinition: return "invalid_definition";
        case LegacyProcedureRunCode::Cancelled: return "cancelled";
        case LegacyProcedureRunCode::DeadlineExceeded: return "deadline_exceeded";
        case LegacyProcedureRunCode::DeviceDisconnected: return "device_disconnected";
        case LegacyProcedureRunCode::ForegroundPackageMismatch:
            return "foreground_package_mismatch";
        case LegacyProcedureRunCode::ResourceNotFound: return "resource_not_found";
        case LegacyProcedureRunCode::BudgetExceeded: return "budget_exceeded";
        case LegacyProcedureRunCode::ResourceExhausted: return "resource_exhausted";
        case LegacyProcedureRunCode::Unavailable: return "unavailable";
        case LegacyProcedureRunCode::Internal: return "internal";
    }
    return "internal";
}

bool valid_legacy_procedure_id(
    const std::string_view value, const std::size_t max_bytes) noexcept
{
    if (value.empty() || value.size() > max_bytes || value.front() == '/' ||
        value.back() == '/' || value.find("//") != std::string_view::npos ||
        value.find('\\') != std::string_view::npos ||
        value.find(':') != std::string_view::npos ||
        value.find('\0') != std::string_view::npos) return false;
    std::size_t begin{};
    while (begin < value.size()) {
        const auto end = value.find('/', begin);
        const auto segment = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (segment.empty() || segment == "." || segment == ".." ||
            segment.front() == '-' || segment.back() == '-' ||
            segment.front() == '.' || segment.back() == '.') return false;
        for (const auto character : segment) {
            if (!(character >= 'a' && character <= 'z') &&
                !(character >= '0' && character <= '9') && character != '-' &&
                character != '_' && character != '.') return false;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return true;
}

LegacyProcedureRunResult map_legacy_procedure_exception(
    const std::exception_ptr error) noexcept
{
    if (!error) return {LegacyProcedureRunCode::Internal, false, {}};
    try {
        std::rethrow_exception(error);
    } catch (const LegacyProcedureDeadlineExceeded&) {
        return {LegacyProcedureRunCode::DeadlineExceeded, false, {}};
    } catch (const LegacyProcedureCancelled&) {
        return {LegacyProcedureRunCode::Cancelled, false, {}};
    } catch (const LegacyProcedureForegroundPackageMismatch&) {
        return {LegacyProcedureRunCode::ForegroundPackageMismatch, true, {}};
    } catch (const HumanTakeOverError&) {
        return {LegacyProcedureRunCode::Cancelled, false, {}};
    } catch (const ValueError&) {
        return {LegacyProcedureRunCode::InvalidDefinition, false, {}};
    } catch (const TypeError&) {
        return {LegacyProcedureRunCode::InvalidDefinition, false, {}};
    } catch (const KeyError&) {
        return {LegacyProcedureRunCode::InvalidDefinition, false, {}};
    } catch (const RequestHumanTakeOver&) {
        return {LegacyProcedureRunCode::Unavailable, false, {}};
    } catch (const EmulatorNotRunningError&) {
        return {LegacyProcedureRunCode::DeviceDisconnected, true, {}};
    } catch (const ConnectionError&) {
        return {LegacyProcedureRunCode::DeviceDisconnected, true, {}};
    } catch (const PathError&) {
        return {LegacyProcedureRunCode::ResourceNotFound, true, {}};
    } catch (const GameStuckError&) {
        return {LegacyProcedureRunCode::BudgetExceeded, false, {}};
    } catch (const TooManyClicksAtOnePlaceError&) {
        return {LegacyProcedureRunCode::BudgetExceeded, false, {}};
    } catch (const TooManyClicksBetweenTwoClicksError&) {
        return {LegacyProcedureRunCode::BudgetExceeded, false, {}};
    } catch (const std::bad_alloc&) {
        return {LegacyProcedureRunCode::ResourceExhausted, false, {}};
    } catch (...) {
        return {LegacyProcedureRunCode::Internal, false, {}};
    }
}

LegacyProcedureRunResult map_legacy_procedure_exception(
    const std::exception_ptr error,
    const LegacyProcedureExecutionControl& control,
    const bool owner_running) noexcept
{
    bool control_related{};
    try {
        if (error) std::rethrow_exception(error);
    } catch (const LegacyProcedureDeadlineExceeded&) {
        control_related = true;
    } catch (const LegacyProcedureCancelled&) {
        control_related = true;
    } catch (const HumanTakeOverError&) {
        control_related = true;
    } catch (...) {
    }
    if (control_related) {
        switch (control.checkpoint(owner_running)) {
            case LegacyProcedureCheckpoint::DeadlineExceeded:
                return {LegacyProcedureRunCode::DeadlineExceeded, false, {}};
            case LegacyProcedureCheckpoint::Cancelled:
                return {LegacyProcedureRunCode::Cancelled, false, {}};
            case LegacyProcedureCheckpoint::Proceed: break;
        }
    }
    return map_legacy_procedure_exception(error);
}

const char* LegacyProcedureCancelled::what() const noexcept
{
    return "legacy procedure cancelled";
}

const char* LegacyProcedureDeadlineExceeded::what() const noexcept
{
    return "legacy procedure deadline exceeded";
}

const char* LegacyProcedureForegroundPackageMismatch::what() const noexcept
{
    return "legacy procedure foreground package mismatch";
}

LegacyProcedureExecutionControl::LegacyProcedureExecutionControl(
    std::stop_token stop, std::optional<Clock::time_point> deadline) noexcept
    : stop_(std::move(stop)), deadline_(deadline)
{
}

LegacyProcedureCheckpoint LegacyProcedureExecutionControl::checkpoint(
    const bool owner_running) const noexcept
{
    if (deadline_ && Clock::now() >= *deadline_)
        return LegacyProcedureCheckpoint::DeadlineExceeded;
    if (!owner_running || stop_.stop_requested())
        return LegacyProcedureCheckpoint::Cancelled;
    return LegacyProcedureCheckpoint::Proceed;
}

void LegacyProcedureExecutionControl::throw_if_stopped(const bool owner_running) const
{
    switch (checkpoint(owner_running)) {
        case LegacyProcedureCheckpoint::Proceed: return;
        case LegacyProcedureCheckpoint::Cancelled: throw LegacyProcedureCancelled{};
        case LegacyProcedureCheckpoint::DeadlineExceeded:
            throw LegacyProcedureDeadlineExceeded{};
    }
    throw LegacyProcedureCancelled{};
}

LegacyProcedureEffectScope::LegacyProcedureEffectScope(
    LegacyProcedureEffectObserver* observer, const LegacyProcedureEffect effect) noexcept
    : observer_(observer), effect_(effect)
{
    began_ = observer_ == nullptr ||
        observer_->report(effect_, LegacyProcedureEffectStage::Began);
}

LegacyProcedureEffectScope::~LegacyProcedureEffectScope()
{
    if (observer_ != nullptr && began_ && !terminal_)
        static_cast<void>(observer_->report(effect_, LegacyProcedureEffectStage::Unknown));
}

LegacyProcedureEffectScope::LegacyProcedureEffectScope(
    LegacyProcedureEffectScope&& other) noexcept
    : observer_(std::exchange(other.observer_, nullptr)), effect_(other.effect_),
      began_(other.began_), terminal_(other.terminal_)
{
}

bool LegacyProcedureEffectScope::began() const noexcept
{
    return began_;
}

bool LegacyProcedureEffectScope::commit() noexcept
{
    if (!began_ || terminal_) return false;
    if (observer_ != nullptr &&
        !observer_->report(effect_, LegacyProcedureEffectStage::Committed)) return false;
    terminal_ = true;
    return true;
}

BAAS_NAMESPACE_END
