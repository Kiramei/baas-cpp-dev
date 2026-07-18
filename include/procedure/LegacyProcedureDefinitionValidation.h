#pragma once

#include "core_defines.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string_view>

BAAS_NAMESPACE_BEGIN

// Validates the currently supported direct-definition legacy procedure shape
// before any value is narrowed to the old engine's int/long long fields.
[[nodiscard]] bool valid_legacy_procedure_definition(
    const nlohmann::json& definition) noexcept;
[[nodiscard]] bool legacy_procedure_definition_features_available(
    const nlohmann::json& definition,
    const std::function<bool(std::string_view)>& available);

BAAS_NAMESPACE_END
