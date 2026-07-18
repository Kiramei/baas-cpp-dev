#pragma once

#include "core_defines.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <functional>
#include <string_view>

BAAS_NAMESPACE_BEGIN

// Validates the currently supported direct-definition legacy procedure shape
// before any value is narrowed to the old engine's int/long long fields.
[[nodiscard]] bool valid_legacy_procedure_definition(
    const nlohmann::json& definition);
[[nodiscard]] bool legacy_procedure_definition_features_available(
    const nlohmann::json& definition,
    const std::function<bool(std::string_view)>& available);

#ifdef BAAS_LEGACY_PROCEDURE_DEFINITION_TEST_HOOKS
namespace testing {
void fail_legacy_procedure_definition_validation_at_allocation(
    std::size_t checkpoint) noexcept;
void clear_legacy_procedure_definition_validation_failure() noexcept;
}  // namespace testing
#endif

BAAS_NAMESPACE_END
