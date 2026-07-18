#include "procedure/LegacyProcedureDefinitionValidation.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string_view>

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

nlohmann::json valid_definition()
{
    return {{"procedure_type", 0}, {"ends", "joined"},
            {"max_stuck_time", 20.0}, {"max_execute_time", 6000.0},
            {"max_click_times", 20},
            {"possibles", nlohmann::json::array({
                nlohmann::json::array({"menu", 640, 360, 0.0, 0.0, 0.0, 1, 0.0, 1, 5})})},
            {"tentative_click", nlohmann::json::array({true, 640, 360, 10.0})}};
}

void test_bounds_and_shape()
{
    check(baas::valid_legacy_procedure_definition(valid_definition()),
          "bounded complete direct definition must be accepted");
    const auto reject = [](const auto& mutation, const std::string_view message) {
        auto value = valid_definition();
        mutation(value);
        check(!baas::valid_legacy_procedure_definition(value), message);
    };
    reject([](auto& value) { value["max_click_times"] = -1; },
           "negative click budget must be rejected before queue access");
    reject([](auto& value) { value["procedure_type"] = -1; },
           "direct activated definitions must be executable type zero");
    reject([](auto& value) { value["ignored_field"] = true; },
           "signed direct definitions must reject silently ignored fields");
    reject([](auto& value) { value["max_stuck_time"] = 1e308; },
           "huge stuck time must be rejected before long long narrowing");
    reject([](auto& value) { value["max_execute_time"] =
               std::numeric_limits<double>::infinity(); },
           "non-finite execution time must be rejected");
    reject([](auto& value) { value["possibles"][0][3] = 1e308; },
           "huge click interval guard must be rejected");
    reject([](auto& value) { value["possibles"][0][4] = -0.1; },
           "negative pre-wait must be rejected");
    reject([](auto& value) { value["possibles"][0][6] = 0; },
           "zero click count must be rejected");
    reject([](auto& value) { value["possibles"][0][8] = 3; },
           "unknown offset type must be rejected");
    reject([](auto& value) { value["possibles"][0][9] = -1; },
           "negative offset must be rejected");
    reject([](auto& value) { value["tentative_click"][3] = 1e308; },
           "huge tentative wait must be rejected");
    reject([](auto& value) { value["ends"] = nlohmann::json::array(); },
           "direct definition must declare at least one terminal source");
    reject([](auto& value) { value["ends"] = nlohmann::json::array({"joined", "joined"}); },
           "duplicate source terminals must be rejected");
    reject([](auto& value) { value["ends"] = std::string("bad\0name", 8); },
           "terminal and feature keys must reject embedded NUL");
    reject([](auto& value) { value["possibles"][0][0] = "bad\nname"; },
           "terminal and feature keys must reject control characters");
}

void test_feature_preflight()
{
    const auto available = [](const std::string_view name) {
        return name == "joined" || name == "menu";
    };
    auto value = valid_definition();
    check(baas::legacy_procedure_definition_features_available(value, available),
          "every referenced legacy feature must pass preflight");
    value["ends"] = "missing";
    check(!baas::legacy_procedure_definition_features_available(value, available),
          "missing end feature must fail before execution effects");
    value = valid_definition();
    value["possibles"][0][0] = "missing";
    check(!baas::legacy_procedure_definition_features_available(value, available),
          "missing possible feature must fail before execution effects");
    try {
        static_cast<void>(baas::legacy_procedure_definition_features_available(
            valid_definition(), [](const std::string_view) -> bool {
                throw std::bad_alloc{};
            }));
        check(false, "feature predicate allocation failure must propagate to typed owner mapping");
    } catch (const std::bad_alloc&) {
    }
}

}  // namespace

int main()
{
    test_bounds_and_shape();
    test_feature_preflight();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "legacy procedure definition validation tests passed\n";
    return EXIT_SUCCESS;
}
