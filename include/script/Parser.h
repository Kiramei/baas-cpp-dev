#pragma once

#include "script/Ast.h"
#include "script/Diagnostic.h"
#include "script/Token.h"

#include <string_view>
#include <vector>

namespace baas::script {

struct ParseResult {
    ast::Program program;
    std::vector<Diagnostic> diagnostics;
    [[nodiscard]] bool has_errors() const noexcept;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    [[nodiscard]] ParseResult parse_program();

private:
    std::vector<Token> tokens_;
};

// Convenience entrypoint. The result owns the complete AST, tokens are not
// retained, and lexical diagnostics are preserved before parser diagnostics.
[[nodiscard]] ParseResult parse(std::string_view source);

}  // namespace baas::script
