#pragma once

#include "script/runtime/ValueHeap.h"

#include <string_view>

namespace baas::script::runtime {

struct LanguageErrorDescriptor {
    std::string_view code;
    bool catchable;
};

// Total, allocation-free mapping for the RT001-RT023 foundation errors.
// Building structured Error values and unwinding frames belong to the future VM.
[[nodiscard]] LanguageErrorDescriptor translate_runtime_error_code(RuntimeErrorCode code) noexcept;

}  // namespace baas::script::runtime
