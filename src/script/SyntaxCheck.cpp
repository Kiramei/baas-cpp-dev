#include "script/SyntaxCheck.h"

#include "script/Parser.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace baas::script {

bool SyntaxCheckResult::has_errors() const noexcept
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

SyntaxCheckResult check_source(const std::string_view source, const SyntaxCheckOptions options)
{
    auto parsed = parse(source);
    SyntaxCheckResult result;
    result.diagnostics = std::move(parsed.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    auto semantic = analyze_semantics(parsed.program, options.semantic);
    result.visited_ast_nodes = semantic.visited_ast_nodes;
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(semantic.diagnostics.begin()),
        std::make_move_iterator(semantic.diagnostics.end()));
    return result;
}

}  // namespace baas::script
