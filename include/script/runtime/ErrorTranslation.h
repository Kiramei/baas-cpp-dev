#pragma once

#include "script/runtime/ValueHeap.h"

#include <string_view>

namespace baas::script::runtime {

struct LanguageErrorDescriptor {
    LanguageErrorCode code;

    [[nodiscard]] std::string_view name() const noexcept {
        return language_error_code_name(code);
    }
    [[nodiscard]] bool catchable() const noexcept {
        return language_error_code_catchable(code);
    }
};

// Total, allocation-free mapping for the RT001-RT023 foundation errors.
// Materializing a RuntimeError into ErrorMetadata and unwinding frames belong
// to the future VM/host translation boundary.
[[nodiscard]] LanguageErrorDescriptor translate_runtime_error_code(RuntimeErrorCode code) noexcept;

}  // namespace baas::script::runtime
