#pragma once

#include "script/Diagnostic.h"
#include "script/SemanticAnalyzer.h"

#include <string_view>
#include <vector>

namespace baas::script {

struct SyntaxCheckOptions {
    SemanticOptions semantic{};
};

struct SyntaxCheckResult {
    std::vector<Diagnostic> diagnostics;
    std::size_t visited_ast_nodes{};

    [[nodiscard]] bool has_errors() const noexcept;
};

// Runs lexical, syntactic, then lexical-semantic validation. Semantic analysis
// is skipped when lexing/parsing already produced an error, avoiding cascades.
[[nodiscard]] SyntaxCheckResult check_source(
    std::string_view source, SyntaxCheckOptions options = {});

}  // namespace baas::script
