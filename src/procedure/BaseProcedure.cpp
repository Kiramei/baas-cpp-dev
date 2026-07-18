//
// Created by pc on 2024/8/10.
//

#include "procedure/BaseProcedure.h"

#include "BAAS.h"
#include "utils/BAASChronoUtil.h"

#include <algorithm>

BAAS_NAMESPACE_BEGIN


BaseProcedure::BaseProcedure(
    BAAS* baas, const BAASConfig& possible_feature,
    const LegacyProcedureExecutionControl* execution_control,
    LegacyProcedureEffectObserver* effect_observer)
{
    this->baas = baas;
    this->possible_feature = possible_feature;
    this->logger = baas->get_logger();
    this->show_log = baas->script_show_image_compare_log;
    this->execution_control = execution_control;
    this->effect_observer = effect_observer;
}

void BaseProcedure::implement(
        BAASConfig& output,
        bool skip_first_screenshot
)
{
    throw std::runtime_error("BaseProcedure::implement() should not be called");
}

void BaseProcedure::clear_resource() noexcept
{
}

BaseProcedure::~BaseProcedure() noexcept
{
}

void BaseProcedure::checkpoint() const
{
    if (execution_control != nullptr) {
        execution_control->throw_if_stopped(baas->is_running());
        return;
    }
    if (!baas->is_running())
        throw HumanTakeOverError("Flag Run turned to false manually");
}

LegacyProcedureEffectScope BaseProcedure::effect_scope(
    const LegacyProcedureEffect effect) const noexcept
{
    return {effect_observer, effect};
}

void BaseProcedure::bounded_wait(const double seconds) const
{
    if (seconds <= 0.0) return;
    auto effect = effect_scope(LegacyProcedureEffect::Wait);
    if (!effect.began()) throw RuntimeError("Legacy procedure effect observer rejected wait");
    constexpr auto max_slice_ms = 50;
    auto remaining = static_cast<long long>(seconds * 1000.0);
    while (remaining > 0) {
        checkpoint();
        const auto slice = static_cast<int>(std::min<long long>(remaining, max_slice_ms));
        BAASChronoUtil::sleepMS(slice);
        remaining -= slice;
        checkpoint();
    }
    if (!effect.commit()) throw RuntimeError("Legacy procedure effect observer rejected wait commit");
}


BAAS_NAMESPACE_END
