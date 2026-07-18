#include "procedure/LegacyProcedureDefinitionValidation.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <new>
#include <string_view>

BAAS_NAMESPACE_BEGIN
namespace {

#ifdef BAAS_LEGACY_PROCEDURE_DEFINITION_TEST_HOOKS
std::atomic<std::size_t> validation_failure_checkpoint{};
std::atomic<std::size_t> validation_checkpoint_count{};

void validation_allocation_checkpoint()
{
    const auto checkpoint = validation_checkpoint_count.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (checkpoint == validation_failure_checkpoint.load(std::memory_order_relaxed))
        throw std::bad_alloc{};
}
#else
void validation_allocation_checkpoint() noexcept
{
}
#endif

[[nodiscard]] bool integer_between(
    const nlohmann::json& value, const std::int64_t minimum,
    const std::int64_t maximum) noexcept
{
    try {
        if (value.is_number_unsigned()) {
            const auto number = value.get<std::uint64_t>();
            return minimum <= 0 && number <= static_cast<std::uint64_t>(maximum);
        }
        if (!value.is_number_integer()) return false;
        const auto number = value.get<std::int64_t>();
        return number >= minimum && number <= maximum;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool finite_between(
    const nlohmann::json& value, const double minimum,
    const double maximum) noexcept
{
    try {
        if (!value.is_number()) return false;
        const auto number = value.get<double>();
        return std::isfinite(number) && number >= minimum && number <= maximum;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool bounded_name(const nlohmann::json& value) noexcept
{
    if (!value.is_string()) return false;
    const auto& name = value.get_ref<const std::string&>();
    if (name.empty() || name.size() > 4'096) return false;
    for (const auto character : name) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7f) return false;
    }
    return true;
}

[[nodiscard]] bool valid_ends(const nlohmann::json& value)
{
    validation_allocation_checkpoint();
    if (bounded_name(value)) return true;
    if (!value.is_array() || value.empty() || value.size() > 256) return false;
    for (std::size_t index{}; index < value.size(); ++index) {
        const auto& item = value[index];
        if (!bounded_name(item)) return false;
        const auto& name = item.get_ref<const std::string&>();
        for (std::size_t previous{}; previous < index; ++previous)
            if (value[previous].get_ref<const std::string&>() == name) return false;
    }
    return true;
}

[[nodiscard]] bool valid_possible(const nlohmann::json& value) noexcept
{
    if (!value.is_array() || value.size() < 3 || value.size() > 10 ||
        !bounded_name(value[0]) ||
        !integer_between(value[1], -1'000'000, 1'000'000) ||
        !integer_between(value[2], -1'000'000, 1'000'000)) return false;
    if (value.size() > 3 && !finite_between(value[3], 0.0, 86'400.0)) return false;
    if (value.size() > 4 && !finite_between(value[4], 0.0, 3'600.0)) return false;
    if (value.size() > 5 && !finite_between(value[5], 0.0, 3'600.0)) return false;
    if (value.size() > 6 && !integer_between(value[6], 1, 1'000)) return false;
    if (value.size() > 7 && !finite_between(value[7], 0.0, 3'600.0)) return false;
    if (value.size() > 8 && !integer_between(value[8], 0, 2)) return false;
    if (value.size() > 9 && !integer_between(value[9], 0, 10'000)) return false;
    return true;
}

}  // namespace

bool valid_legacy_procedure_definition(const nlohmann::json& definition)
{
    try {
        validation_allocation_checkpoint();
        if (!definition.is_object() || !definition.contains("procedure_type") ||
            !integer_between(definition.at("procedure_type"), 0, 0) ||
            !definition.contains("ends") || !valid_ends(definition.at("ends")))
            return false;
        static constexpr std::array<std::string_view, 8> allowed{
            "procedure_type", "ends", "max_stuck_time", "max_execute_time",
            "max_click_times", "show_log", "possibles", "tentative_click"};
        for (const auto& [key, value] : definition.items()) {
            static_cast<void>(value);
            bool found{};
            for (const auto candidate : allowed)
                found = found || candidate == key;
            if (!found) return false;
        }
        if (definition.contains("max_stuck_time") &&
            !finite_between(definition.at("max_stuck_time"), 0.001, 604'800.0))
            return false;
        if (definition.contains("max_execute_time") &&
            !finite_between(definition.at("max_execute_time"), 0.001, 604'800.0))
            return false;
        if (definition.contains("max_click_times") &&
            !integer_between(definition.at("max_click_times"), 1, 100'000))
            return false;
        if (definition.contains("show_log") && !definition.at("show_log").is_boolean())
            return false;
        if (definition.contains("possibles")) {
            const auto& possibles = definition.at("possibles");
            if (!possibles.is_array() || possibles.size() > 4'096) return false;
            for (const auto& possible : possibles)
                if (!valid_possible(possible)) return false;
        }
        if (definition.contains("tentative_click")) {
            const auto& tentative = definition.at("tentative_click");
            if (!tentative.is_array() || tentative.size() != 4 ||
                !tentative[0].is_boolean() ||
                !integer_between(tentative[1], -1'000'000, 1'000'000) ||
                !integer_between(tentative[2], -1'000'000, 1'000'000) ||
                !finite_between(tentative[3], 0.001, 86'400.0)) return false;
        }
        return true;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (...) {
        return false;
    }
}

bool legacy_procedure_definition_features_available(
    const nlohmann::json& definition,
    const std::function<bool(std::string_view)>& available)
{
    try {
        if (!valid_legacy_procedure_definition(definition) || !available) return false;
        const auto& ends = definition.at("ends");
        if (ends.is_string()) {
            if (!available(ends.get_ref<const std::string&>())) return false;
        } else {
            for (const auto& end : ends)
                if (!available(end.get_ref<const std::string&>())) return false;
        }
        if (definition.contains("possibles")) {
            for (const auto& possible : definition.at("possibles"))
                if (!available(possible[0].get_ref<const std::string&>())) return false;
        }
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

#ifdef BAAS_LEGACY_PROCEDURE_DEFINITION_TEST_HOOKS
namespace testing {

void fail_legacy_procedure_definition_validation_at_allocation(
    const std::size_t checkpoint) noexcept
{
    validation_checkpoint_count.store(0, std::memory_order_relaxed);
    validation_failure_checkpoint.store(checkpoint, std::memory_order_relaxed);
}

void clear_legacy_procedure_definition_validation_failure() noexcept
{
    validation_failure_checkpoint.store(0, std::memory_order_relaxed);
    validation_checkpoint_count.store(0, std::memory_order_relaxed);
}

}  // namespace testing
#endif

BAAS_NAMESPACE_END
