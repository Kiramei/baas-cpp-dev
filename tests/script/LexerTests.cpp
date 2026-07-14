#include "script/Lexer.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using baas::script::LexResult;
using baas::script::SourceLocation;
using baas::script::TokenKind;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<TokenKind> kinds(const LexResult& result)
{
    std::vector<TokenKind> output;
    for (const auto& token : result.tokens) {
        output.push_back(token.kind);
    }
    return output;
}

void test_keywords_identifiers_and_locations()
{
    const auto result = baas::script::lex(
        "let 名称 = 42\r\n"
        "async fn fetch_2() { await 名称; return true }"
    );
    check(!result.has_errors(), "valid UTF-8 program should not produce diagnostics");
    check(kinds(result) == std::vector<TokenKind>{
        TokenKind::Let, TokenKind::Identifier, TokenKind::Equal, TokenKind::Integer,
        TokenKind::Async, TokenKind::Fn, TokenKind::Identifier, TokenKind::LeftParen,
        TokenKind::RightParen, TokenKind::LeftBrace, TokenKind::Await, TokenKind::Identifier,
        TokenKind::Semicolon, TokenKind::Return, TokenKind::True, TokenKind::RightBrace,
        TokenKind::EndOfFile,
    }, "keywords, identifiers and punctuation should have stable token kinds");

    check(result.tokens[1].lexeme == "名称", "UTF-8 identifier lexeme should be preserved");
    check(result.tokens[1].span.begin == SourceLocation{4, 1, 5},
          "UTF-8 identifier should begin at byte 4, line 1, column 5");
    check(result.tokens[1].span.end == SourceLocation{10, 1, 7},
          "UTF-8 columns should count code points while offsets count bytes");
    check(result.tokens[4].span.begin == SourceLocation{17, 2, 1},
          "CRLF should count as one line break and two source bytes");
}

void test_all_keywords()
{
    const auto result = baas::script::lex(
        "let fn if else while for in return break continue import as true false null "
        "try catch throw defer async await and or not is"
    );
    check(kinds(result) == std::vector<TokenKind>{
        TokenKind::Let, TokenKind::Fn, TokenKind::If, TokenKind::Else, TokenKind::While,
        TokenKind::For, TokenKind::In, TokenKind::Return, TokenKind::Break,
        TokenKind::Continue, TokenKind::Import, TokenKind::As, TokenKind::True,
        TokenKind::False, TokenKind::Null, TokenKind::Try, TokenKind::Catch,
        TokenKind::Throw, TokenKind::Defer, TokenKind::Async, TokenKind::Await,
        TokenKind::And, TokenKind::Or, TokenKind::Not, TokenKind::Is,
        TokenKind::EndOfFile,
    }, "every language keyword should be recognized");
}

void test_numbers_strings_comments_and_operators()
{
    const auto result = baas::script::lex(
        "# ignored\n"
        "1 2.5 6e2 7.0E-3 /* ignored /* nested */\ncomment */\n"
        "\"a\\n\\t\\\"\\\\\" '单引号\\'' "
        "+ - * ** / // % = == ! != < <= > >= && || & | ^ ~ += -= *= **= /= //= %= ++ -- -> => "
        ". .. , : ; ? ( ) [ ] { }"
    );
    check(!result.has_errors(), "numbers, strings, comments and operators should lex without errors");
    check(result.tokens[0].kind == TokenKind::Integer && result.tokens[0].lexeme == "1",
          "integer should be recognized");
    check(result.tokens[1].kind == TokenKind::Float && result.tokens[1].lexeme == "2.5",
          "decimal float should be recognized");
    check(result.tokens[2].kind == TokenKind::Float && result.tokens[2].lexeme == "6e2",
          "exponent float should be recognized");
    check(result.tokens[4].kind == TokenKind::String && result.tokens[4].value == "a\n\t\"\\",
          "double-quoted string escapes should be decoded");
    check(result.tokens[5].kind == TokenKind::String && result.tokens[5].value == "单引号'",
          "single-quoted UTF-8 string should be decoded");

    const std::vector<TokenKind> operator_kinds{
        TokenKind::Plus, TokenKind::Minus, TokenKind::Star, TokenKind::Power,
        TokenKind::Slash, TokenKind::FloorDivide, TokenKind::Percent, TokenKind::Equal,
        TokenKind::EqualEqual, TokenKind::Bang,
        TokenKind::BangEqual, TokenKind::Less, TokenKind::LessEqual, TokenKind::Greater,
        TokenKind::GreaterEqual, TokenKind::AndAnd, TokenKind::OrOr, TokenKind::Ampersand,
        TokenKind::Pipe, TokenKind::Caret, TokenKind::Tilde, TokenKind::PlusEqual,
        TokenKind::MinusEqual, TokenKind::StarEqual, TokenKind::PowerEqual,
        TokenKind::SlashEqual, TokenKind::FloorDivideEqual, TokenKind::PercentEqual,
        TokenKind::PlusPlus, TokenKind::MinusMinus,
        TokenKind::Arrow, TokenKind::FatArrow, TokenKind::Dot, TokenKind::DotDot,
        TokenKind::Comma, TokenKind::Colon, TokenKind::Semicolon, TokenKind::Question,
        TokenKind::LeftParen, TokenKind::RightParen, TokenKind::LeftBracket,
        TokenKind::RightBracket, TokenKind::LeftBrace, TokenKind::RightBrace,
    };
    for (std::size_t index = 0; index < operator_kinds.size(); ++index) {
        check(result.tokens[6 + index].kind == operator_kinds[index],
              "operator token kind should match its source spelling");
    }
}

void test_error_recovery()
{
    std::string source = "let ok = 1; ";
    source.push_back(static_cast<char>(0xc0)); // illegal overlong UTF-8 lead byte
    source += " @ \"unterminated\n/* open";

    const auto result = baas::script::lex(source);
    check(result.has_errors(), "malformed input should be reported as an error");
    check(result.diagnostics.size() == 4,
          "invalid UTF-8, unexpected character, string and comment should each recover");
    check(result.diagnostics[0].code == "LEX001", "invalid UTF-8 should use LEX001");
    check(result.diagnostics[0].span.begin == SourceLocation{12, 1, 13} &&
              result.diagnostics[0].span.end == SourceLocation{13, 1, 14},
          "invalid UTF-8 diagnostic should have an exact byte and display span");
    check(result.diagnostics[1].code == "LEX004", "unexpected character should use LEX004");
    check(result.diagnostics[2].code == "LEX002", "unterminated string should use LEX002");
    check(result.diagnostics[3].code == "LEX003", "unterminated comment should use LEX003");
    check(result.tokens.back().kind == TokenKind::EndOfFile,
          "lexer should always terminate with EOF after malformed input");
}

void test_invalid_utf8_forms_and_unknown_escape()
{
    std::string source;
    source.append("\xf0\x9f", 2); // truncated four-byte sequence
    source += " \"bad\\q\" ";
    source.append("\xed\xa0\x80", 3); // encoded UTF-16 surrogate

    const auto result = baas::script::lex(source);
    check(result.has_errors(), "invalid UTF-8 forms and unknown escapes should be diagnosed");
    check(result.diagnostics.size() >= 4,
          "recovery should diagnose every invalid byte sequence without crashing");
    check(result.diagnostics[2].code == "LEX005", "unknown escape should use LEX005");
    check(result.tokens.back().kind == TokenKind::EndOfFile, "recovery should reach EOF");
}

}  // namespace

int main()
{
    test_keywords_identifiers_and_locations();
    test_all_keywords();
    test_numbers_strings_comments_and_operators();
    test_error_recovery();
    test_invalid_utf8_forms_and_unknown_escape();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All lexer tests passed\n";
    return EXIT_SUCCESS;
}
