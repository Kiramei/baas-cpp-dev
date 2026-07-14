#pragma once

#include "script/Diagnostic.h"
#include "script/Token.h"

#include <string_view>
#include <vector>

namespace baas::script {

struct LexResult {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

class Lexer {
public:
    explicit Lexer(std::string_view source) noexcept;

    // The returned result owns every lexeme and decoded value and therefore
    // remains valid after the source is destroyed.
    [[nodiscard]] LexResult tokenize();

private:
    std::string_view source_;
};

[[nodiscard]] LexResult lex(std::string_view source);

}  // namespace baas::script
