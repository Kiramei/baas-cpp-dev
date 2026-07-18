#include "runtime/procedure/CoDetectPythonCompatExecutor.h"

#include <algorithm>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace baas::runtime::procedure {
namespace {

using ::baas::script::host::ProcedureDeadlineScope;
using ::baas::script::host::ProcedureEffect;
using ::baas::script::host::ProcedureEffectStage;
using ::baas::script::host::ProcedureExecutionRequest;
using ::baas::script::host::ProcedureExecutorError;
using ::baas::script::host::ProcedureExecutorErrorCode;
using ::baas::script::host::ProcedureExecutorOutcome;
using ::baas::script::host::ProcedureUnavailableReason;
using ::baas::script::runtime::HostEffectState;

[[nodiscard]] ProcedureExecutorError executor_error(
    const CoDetectOperationError error,
    const HostEffectState effect = HostEffectState::NotStarted) noexcept
{
    switch (error) {
        case CoDetectOperationError::Cancelled:
            return {ProcedureExecutorErrorCode::Cancelled, false, effect};
        case CoDetectOperationError::ContextDeadlineExceeded:
            return {ProcedureExecutorErrorCode::DeadlineExceeded, false, effect,
                    ProcedureDeadlineScope::Context};
        case CoDetectOperationError::CallDeadlineExceeded:
            return {ProcedureExecutorErrorCode::DeadlineExceeded, false, effect,
                    ProcedureDeadlineScope::Call};
        case CoDetectOperationError::DeviceDisconnected:
            return {ProcedureExecutorErrorCode::DeviceDisconnected, true, effect};
        case CoDetectOperationError::ResourceNotFound:
            return {ProcedureExecutorErrorCode::ResourceNotFound, false, effect};
        case CoDetectOperationError::ResourceExhausted:
            return {ProcedureExecutorErrorCode::ResourceExhausted, false, effect};
        case CoDetectOperationError::Unavailable:
            return {ProcedureExecutorErrorCode::Unavailable, true, effect};
        case CoDetectOperationError::Internal:
            return {ProcedureExecutorErrorCode::Internal, false, effect};
    }
    return {ProcedureExecutorErrorCode::Internal, false, effect};
}

[[nodiscard]] CoDetectOperationError control_error(const CoDetectControlState state) noexcept
{
    switch (state) {
        case CoDetectControlState::ContextDeadlineExceeded:
            return CoDetectOperationError::ContextDeadlineExceeded;
        case CoDetectControlState::CallDeadlineExceeded:
            return CoDetectOperationError::CallDeadlineExceeded;
        case CoDetectControlState::Cancelled:
            return CoDetectOperationError::Cancelled;
        case CoDetectControlState::Proceed: break;
    }
    return CoDetectOperationError::Internal;
}

class RequestControl final : public CoDetectControl {
public:
    RequestControl(const ProcedureExecutionRequest& request,
                   const CoDetectDeviceSession& session,
                   const std::uint64_t start, const std::uint64_t timeout) noexcept
        : request_(request), session_(session), start_(start), timeout_(timeout)
    {
    }

    [[nodiscard]] CoDetectControlState poll() const noexcept override
    {
        if (request_.deadline_exceeded())
            return CoDetectControlState::ContextDeadlineExceeded;
        const auto now = session_.monotonic_ms();
        if (now < start_ || now - start_ >= timeout_)
            return CoDetectControlState::CallDeadlineExceeded;
        if (request_.cancelled()) return CoDetectControlState::Cancelled;
        return CoDetectControlState::Proceed;
    }

private:
    const ProcedureExecutionRequest& request_;
    const CoDetectDeviceSession& session_;
    std::uint64_t start_{};
    std::uint64_t timeout_{};
};

template <class T, class F>
[[nodiscard]] CoDetectResult<T> invoke_effect(
    const ProcedureExecutionRequest& request, const CoDetectControl& control,
    const ProcedureEffect effect, F&& operation)
{
    if (const auto state = control.poll(); state != CoDetectControlState::Proceed)
        return control_error(state);
    if (!request.effects().report(effect, ProcedureEffectStage::Began))
        return CoDetectOperationError::Internal;
    CoDetectResult<T> result = CoDetectOperationError::Internal;
    try {
        result = std::forward<F>(operation)();
    } catch (const std::bad_alloc&) {
        result = CoDetectOperationError::ResourceExhausted;
    } catch (...) {
        result = CoDetectOperationError::Internal;
    }
    const auto succeeded = std::holds_alternative<T>(result);
    if (!request.effects().report(
            effect, succeeded ? ProcedureEffectStage::Committed
                              : ProcedureEffectStage::Unknown))
        return CoDetectOperationError::Internal;
    if (const auto state = control.poll(); state != CoDetectControlState::Proceed)
        return control_error(state);
    return result;
}

[[nodiscard]] bool applicable(
    const CoDetectReaction& reaction, const CoDetectProfile profile) noexcept
{
    return reaction.profiles.empty() ||
        std::find(reaction.profiles.begin(), reaction.profiles.end(), profile) !=
            reaction.profiles.end();
}

class Executor final : public ::baas::script::host::ProcedureExecutor {
public:
    Executor(std::shared_ptr<const CoDetectPythonCompatDefinition> definition,
             std::vector<CoDetectTerminalBinding> terminals,
             std::shared_ptr<CoDetectDeviceSession> session,
             std::shared_ptr<CoDetectFeatureView> features,
             std::shared_ptr<const ::baas::script::host::ProcedureSnapshot> expected_snapshot = {},
             std::string expected_procedure_id = {},
             std::string expected_implementation = {})
        : definition_(std::move(definition)), terminals_(std::move(terminals)),
          session_(std::move(session)), features_(std::move(features)),
          expected_snapshot_(std::move(expected_snapshot)),
          expected_procedure_id_(std::move(expected_procedure_id)),
          expected_implementation_(std::move(expected_implementation))
    {
    }

    [[nodiscard]] ProcedureExecutorOutcome execute(
        const ProcedureExecutionRequest& request) override
    {
        try {
            return execute_checked(request);
        } catch (const std::bad_alloc&) {
            return ProcedureExecutorOutcome::failure(
                {ProcedureExecutorErrorCode::ResourceExhausted, false,
                 HostEffectState::Unknown});
        } catch (...) {
            return ProcedureExecutorOutcome::failure(
                {ProcedureExecutorErrorCode::Internal, false,
                 HostEffectState::Unknown});
        }
    }

private:
    [[nodiscard]] ProcedureExecutorOutcome fail(
        const CoDetectOperationError error,
        const HostEffectState effect = HostEffectState::NotStarted) const
    {
        return ProcedureExecutorOutcome::failure(executor_error(error, effect));
    }

    [[nodiscard]] ProcedureExecutorOutcome execute_checked(
        const ProcedureExecutionRequest& request)
    {
        if (!request.procedure() || request.device_id() != session_->device_id())
            return ProcedureExecutorOutcome::failure(
                {ProcedureExecutorErrorCode::InvalidRequest, false,
                 HostEffectState::NotStarted});
        if (expected_snapshot_ &&
            (request.snapshot().get() != expected_snapshot_.get() ||
             request.procedure()->procedure_id() != expected_procedure_id_ ||
             request.procedure()->implementation_sha256() !=
                 expected_implementation_))
            return ProcedureExecutorOutcome::failure(
                {ProcedureExecutorErrorCode::InvalidRequest, false,
                 HostEffectState::NotStarted});
        const auto start = session_->monotonic_ms();
        RequestControl control(request, *session_, start, definition_->loop().timeout_ms);
        if (const auto state = control.poll(); state != CoDetectControlState::Proceed)
            return fail(control_error(state));

        auto feature_last = start;
        auto foreground_last = start;
        std::uint64_t failed_cycles{};
        std::optional<std::pair<std::string, CoDetectClick>> last_click;
        std::uint64_t last_click_time{};
        bool skip_first = definition_->loop().skip_first_screenshot;

        for (;;) {
            if (const auto state = control.poll(); state != CoDetectControlState::Proceed)
                return fail(control_error(state));

            std::shared_ptr<const CoDetectFrame> frame;
            if (skip_first) {
                skip_first = false;
                frame = session_->latest_frame();
                if (!frame) {
                    ProcedureExecutorError error{
                        ProcedureExecutorErrorCode::Unavailable, true,
                        HostEffectState::NotStarted, ProcedureDeadlineScope::Call,
                        ProcedureUnavailableReason::RecentFrameUnavailable};
                    return ProcedureExecutorOutcome::failure(error);
                }
            } else {
                auto captured = invoke_effect<std::shared_ptr<const CoDetectFrame>>(
                    request, control, ProcedureEffect::Capture,
                    [&] { return session_->capture(control); });
                if (const auto* error = std::get_if<CoDetectOperationError>(&captured))
                    return fail(*error);
                frame = std::get<std::shared_ptr<const CoDetectFrame>>(std::move(captured));
                if (!frame) return fail(CoDetectOperationError::Internal);
                session_->publish_latest_frame(frame);
            }

            auto now = session_->monotonic_ms();
            if (now < feature_last || now < foreground_last)
                return fail(CoDetectOperationError::Internal);
            const auto& foreground = definition_->foreground_check();
            if ((!foreground.android_only || session_->is_android()) &&
                now - feature_last > foreground.idle_feature_ms &&
                now - foreground_last > foreground.interval_ms) {
                foreground_last = now;
                auto matched = invoke_effect<bool>(
                    request, control, ProcedureEffect::ForegroundCheck,
                    [&] { return session_->foreground_matches(control); });
                if (const auto* error = std::get_if<CoDetectOperationError>(&matched))
                    return fail(*error);
                if (!std::get<bool>(matched))
                    return ProcedureExecutorOutcome::failure({
                        ProcedureExecutorErrorCode::ForegroundPackageMismatch, true,
                        HostEffectState::NotStarted});
            }

            for (;;) {
                bool all_loading = !definition_->loading_all_rgb().empty();
                for (const auto& feature : definition_->loading_all_rgb()) {
                    auto result = match_rgb(request, control, *frame, feature);
                    if (const auto* error = std::get_if<CoDetectOperationError>(&result))
                        return fail(*error);
                    if (!std::get<bool>(result)) {
                        all_loading = false;
                        break;
                    }
                }
                if (!all_loading) break;
                auto captured = invoke_effect<std::shared_ptr<const CoDetectFrame>>(
                    request, control, ProcedureEffect::Capture,
                    [&] { return session_->capture(control); });
                if (const auto* error = std::get_if<CoDetectOperationError>(&captured))
                    return fail(*error);
                frame = std::get<std::shared_ptr<const CoDetectFrame>>(std::move(captured));
                if (!frame) return fail(CoDetectOperationError::Internal);
                session_->publish_latest_frame(frame);
                auto waited = invoke_effect<std::monostate>(
                    request, control, ProcedureEffect::Wait,
                    [&] { return session_->wait(session_->screenshot_interval_ms(), control); });
                if (const auto* error = std::get_if<CoDetectOperationError>(&waited))
                    return fail(*error);
            }

            for (const auto& feature : definition_->ends_rgb()) {
                auto result = match_rgb(request, control, *frame, feature);
                if (const auto* error = std::get_if<CoDetectOperationError>(&result))
                    return fail(*error);
                if (std::get<bool>(result)) return terminal(control, feature);
            }
            for (const auto& feature : definition_->ends_image()) {
                auto result = match_image(request, control, *frame, feature.feature, feature.match);
                if (const auto* error = std::get_if<CoDetectOperationError>(&result))
                    return fail(*error);
                if (std::get<bool>(result)) return terminal(control, feature.feature);
            }

            const CoDetectReaction* ordinary{};
            const auto select_ordinary = [&](const auto& reactions, const bool image)
                -> std::optional<ProcedureExecutorOutcome> {
                auto selected = first_reaction(
                    request, control, *frame, reactions, image);
                if (const auto* error =
                        std::get_if<CoDetectOperationError>(&selected))
                    return fail(*error);
                ordinary = std::get<const CoDetectReaction*>(selected);
                return std::nullopt;
            };
            if (auto error = select_ordinary(definition_->reactions_rgb(), false))
                return std::move(*error);
            if (!ordinary) {
                if (auto error = select_ordinary(
                        definition_->reactions_rgb_profiled(), false))
                    return std::move(*error);
            }
            if (!ordinary) {
                if (auto error = select_ordinary(definition_->reactions_image(), true))
                    return std::move(*error);
            }
            if (!ordinary) {
                if (auto error = select_ordinary(
                        definition_->reactions_image_profiled(), true))
                    return std::move(*error);
            }
            bool popup_matched{};
            if (ordinary) {
                now = session_->monotonic_ms();
                if (now < feature_last) return fail(CoDetectOperationError::Internal);
                feature_last = now;
                failed_cycles = 0;
                bool duplicate{};
                if (last_click && last_click->first == ordinary->feature &&
                    last_click->second == ordinary->click) {
                    if (now < last_click_time) return fail(CoDetectOperationError::Internal);
                    duplicate = now - last_click_time <=
                        definition_->loop().duplicate_click_window_ms;
                }
                if (!ordinary->click.match_only() && !duplicate) {
                    if (auto error = issue_click(request, control, ordinary->click))
                        return fail(*error, HostEffectState::Unknown);
                    last_click = std::pair{ordinary->feature, ordinary->click};
                    last_click_time = session_->monotonic_ms();
                }
            } else {
                auto popup_result = first_reaction(
                    request, control, *frame, definition_->popups_rgb(), false);
                if (const auto* error =
                        std::get_if<CoDetectOperationError>(&popup_result))
                    return fail(*error);
                const CoDetectReaction* popup =
                    std::get<const CoDetectReaction*>(popup_result);
                if (!popup) {
                    popup_result = first_reaction(
                        request, control, *frame,
                        definition_->popups_profiled_image(), true);
                    if (const auto* error =
                            std::get_if<CoDetectOperationError>(&popup_result))
                        return fail(*error);
                    popup = std::get<const CoDetectReaction*>(popup_result);
                }
                if (popup) {
                    popup_matched = true;
                    if (!popup->click.match_only()) {
                        if (auto error = issue_click(request, control, popup->click))
                            return fail(*error, HostEffectState::Unknown);
                        last_click = std::pair{popup->feature, popup->click};
                        last_click_time = session_->monotonic_ms();
                    }
                }
            }
            if (!ordinary && !popup_matched) {
                ++failed_cycles;
                const auto& tentative = definition_->loop().tentative;
                if (tentative.enabled && failed_cycles > tentative.after_failed_cycles) {
                    if (auto error = issue_click(request, control, tentative.click))
                        return fail(*error, HostEffectState::Unknown);
                    const auto interval = session_->screenshot_interval_ms();
                    if (tentative.post_wait_screenshot_intervals != 0 &&
                        interval > UINT64_MAX / tentative.post_wait_screenshot_intervals)
                        return fail(CoDetectOperationError::Internal);
                    auto waited = invoke_effect<std::monostate>(
                        request, control, ProcedureEffect::Wait, [&] {
                            return session_->wait(
                                interval * tentative.post_wait_screenshot_intervals,
                                control);
                        });
                    if (const auto* error = std::get_if<CoDetectOperationError>(&waited))
                        return fail(*error);
                }
            }
        }
    }

    [[nodiscard]] CoDetectResult<bool> match_rgb(
        const ProcedureExecutionRequest& request, const CoDetectControl& control,
        const CoDetectFrame& frame, const std::string_view feature)
    {
        return invoke_effect<bool>(request, control, ProcedureEffect::Vision,
            [&] { return features_->match_rgb(frame, feature, control); });
    }

    [[nodiscard]] CoDetectResult<bool> match_image(
        const ProcedureExecutionRequest& request, const CoDetectControl& control,
        const CoDetectFrame& frame, const std::string_view feature,
        const CoDetectImageMatch match)
    {
        return invoke_effect<bool>(request, control, ProcedureEffect::Vision,
            [&] { return features_->match_image(frame, feature, match, control); });
    }

    [[nodiscard]] std::variant<const CoDetectReaction*, CoDetectOperationError>
    first_reaction(
        const ProcedureExecutionRequest& request, const CoDetectControl& control,
        const CoDetectFrame& frame, const std::vector<CoDetectReaction>& reactions,
        const bool image)
    {
        for (const auto& reaction : reactions) {
            if (!applicable(reaction, session_->profile())) continue;
            auto result = image
                ? match_image(request, control, frame, reaction.feature, reaction.image_match)
                : match_rgb(request, control, frame, reaction.feature);
            if (const auto* error = std::get_if<CoDetectOperationError>(&result)) {
                return *error;
            }
            if (std::get<bool>(result)) return &reaction;
        }
        return nullptr;
    }

    [[nodiscard]] std::optional<CoDetectOperationError> issue_click(
        const ProcedureExecutionRequest& request, const CoDetectControl& control,
        const CoDetectClick click)
    {
        auto result = invoke_effect<std::monostate>(
            request, control, ProcedureEffect::Input,
            [&] { return session_->click(click, control); });
        if (const auto* error = std::get_if<CoDetectOperationError>(&result)) return *error;
        return std::nullopt;
    }

    [[nodiscard]] ProcedureExecutorOutcome terminal(
        const CoDetectControl& control, const std::string_view source) const
    {
        if (const auto state = control.poll(); state != CoDetectControlState::Proceed)
            return fail(control_error(state));
        const auto found = std::find_if(terminals_.begin(), terminals_.end(),
            [&](const auto& item) { return item.source == source; });
        if (found == terminals_.end())
            return ProcedureExecutorOutcome::failure(
                {ProcedureExecutorErrorCode::Internal, false,
                 HostEffectState::NotStarted});
        return ProcedureExecutorOutcome::success(found->id);
    }

    std::shared_ptr<const CoDetectPythonCompatDefinition> definition_;
    std::vector<CoDetectTerminalBinding> terminals_;
    std::shared_ptr<CoDetectDeviceSession> session_;
    std::shared_ptr<CoDetectFeatureView> features_;
    std::shared_ptr<const ::baas::script::host::ProcedureSnapshot> expected_snapshot_;
    std::string expected_procedure_id_;
    std::string expected_implementation_;
};

void validate_terminal_bindings(
    const CoDetectPythonCompatDefinition& definition,
    const std::vector<CoDetectTerminalBinding>& terminals)
{
    if (terminals.empty())
        throw std::invalid_argument("co-detect terminal bindings are absent");
    std::set<std::string, std::less<>> sources;
    for (const auto& terminal : terminals) {
        if (terminal.source.empty() || terminal.id.empty() ||
            !sources.emplace(terminal.source).second)
            throw std::invalid_argument("co-detect terminal binding is invalid");
    }
    const auto require = [&](const std::string_view source) {
        if (!sources.contains(source))
            throw std::invalid_argument("co-detect end has no terminal binding");
    };
    for (const auto& source : definition.ends_rgb()) require(source);
    for (const auto& source : definition.ends_image()) require(source.feature);
}

}  // namespace

std::shared_ptr<::baas::script::host::ProcedureExecutor> make_co_detect_python_compat_executor(
    std::shared_ptr<const CoDetectPythonCompatDefinition> definition,
    std::vector<CoDetectTerminalBinding> terminals,
    std::shared_ptr<CoDetectDeviceSession> session,
    std::shared_ptr<CoDetectFeatureView> features)
{
    if (!definition || !session || !features)
        throw std::invalid_argument("co-detect executor dependency is absent");
    validate_terminal_bindings(*definition, terminals);
    return std::make_shared<Executor>(std::move(definition), std::move(terminals),
                                      std::move(session), std::move(features));
}

std::shared_ptr<::baas::script::host::ProcedureExecutor>
make_activated_co_detect_python_compat_executor(
    std::shared_ptr<const RuntimeProcedureActivation> activation,
    const std::string_view procedure_id,
    std::shared_ptr<CoDetectDeviceSession> session,
    std::shared_ptr<CoDetectFeatureView> features)
{
    if (!activation || !session || !features)
        throw std::invalid_argument("co-detect activation dependency is absent");
    const auto activated = activation->resolve_definition(procedure_id);
    if (!activated || activated->engine() != co_detect_python_compat_engine)
        throw std::invalid_argument("co-detect activated definition is unavailable");
    auto loaded = load_co_detect_python_compat_definition(activated->bytes());
    if (!loaded)
        throw std::invalid_argument("co-detect activated definition is invalid");
    std::vector<CoDetectTerminalBinding> terminals;
    terminals.reserve(activated->terminals().size());
    for (const auto& terminal : activated->terminals())
        terminals.push_back({terminal.source, terminal.id});
    validate_terminal_bindings(*loaded.definition, terminals);
    return std::make_shared<Executor>(
        std::move(loaded.definition), std::move(terminals), std::move(session),
        std::move(features), activation->snapshot(), activated->procedure_id(),
        activated->implementation_sha256());
}

}  // namespace baas::runtime::procedure
