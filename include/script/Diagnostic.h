#pragma once

#include "script/SourceLocation.h"

#include <string>

namespace baas::script {

enum class DiagnosticSeverity {
    Error,
    Warning,
};

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    std::string code;
    std::string message;
    SourceSpan span{};
};

}  // namespace baas::script
