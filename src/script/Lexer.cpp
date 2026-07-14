#include "script/Lexer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <utility>

namespace baas::script {
namespace {

struct DecodedCodePoint {
    char32_t value{};
    std::size_t length{1};
    bool valid{false};
};

[[nodiscard]] DecodedCodePoint decode_utf8(const std::string_view source, const std::size_t offset) noexcept
{
    if (offset >= source.size()) {
        return {0, 0, false};
    }

    const auto first = static_cast<std::uint8_t>(source[offset]);
    if (first <= 0x7f) {
        return {first, 1, true};
    }

    std::size_t length = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
        length = 2;
        value = first & 0x1f;
        minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        length = 3;
        value = first & 0x0f;
        minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        length = 4;
        value = first & 0x07;
        minimum = 0x10000;
    } else {
        return {0, 1, false};
    }

    if (offset + length > source.size()) {
        return {0, 1, false};
    }
    for (std::size_t index = 1; index < length; ++index) {
        const auto byte = static_cast<std::uint8_t>(source[offset + index]);
        if ((byte & 0xc0) != 0x80) {
            return {0, 1, false};
        }
        value = (value << 6) | (byte & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        return {0, 1, false};
    }
    return {value, length, true};
}

[[nodiscard]] bool is_ascii_digit(const char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard]] bool is_identifier_start(const char32_t value) noexcept
{
    return value == U'_' || (value >= U'a' && value <= U'z') ||
           (value >= U'A' && value <= U'Z') || value >= 0x80;
}

[[nodiscard]] bool is_identifier_continue(const char32_t value) noexcept
{
    return is_identifier_start(value) || (value >= U'0' && value <= U'9');
}

[[nodiscard]] TokenKind keyword_kind(const std::string_view text) noexcept
{
    if (text == "let") return TokenKind::Let;
    if (text == "fn") return TokenKind::Fn;
    if (text == "if") return TokenKind::If;
    if (text == "else") return TokenKind::Else;
    if (text == "while") return TokenKind::While;
    if (text == "for") return TokenKind::For;
    if (text == "in") return TokenKind::In;
    if (text == "return") return TokenKind::Return;
    if (text == "break") return TokenKind::Break;
    if (text == "continue") return TokenKind::Continue;
    if (text == "import") return TokenKind::Import;
    if (text == "as") return TokenKind::As;
    if (text == "true") return TokenKind::True;
    if (text == "false") return TokenKind::False;
    if (text == "null") return TokenKind::Null;
    if (text == "try") return TokenKind::Try;
    if (text == "catch") return TokenKind::Catch;
    if (text == "throw") return TokenKind::Throw;
    if (text == "defer") return TokenKind::Defer;
    if (text == "async") return TokenKind::Async;
    if (text == "await") return TokenKind::Await;
    if (text == "and") return TokenKind::And;
    if (text == "or") return TokenKind::Or;
    if (text == "not") return TokenKind::Not;
    if (text == "is") return TokenKind::Is;
    return TokenKind::Identifier;
}

class Scanner {
public:
    explicit Scanner(const std::string_view source) noexcept : source_(source) {}

    [[nodiscard]] LexResult run()
    {
        while (!at_end()) {
            skip_trivia();
            if (at_end()) {
                break;
            }

            const auto decoded = decode_utf8(source_, offset_);
            if (!decoded.valid) {
                diagnose_invalid_utf8();
                continue;
            }
            if (is_identifier_start(decoded.value)) {
                scan_identifier();
            } else if (decoded.length == 1 && is_ascii_digit(static_cast<char>(decoded.value))) {
                scan_number();
            } else if (decoded.value == U'\"' || decoded.value == U'\'') {
                scan_string(static_cast<char>(decoded.value));
            } else {
                scan_operator_or_error();
            }
        }

        const auto eof = location();
        result_.tokens.push_back({TokenKind::EndOfFile, {}, {}, {eof, eof}});
        return std::move(result_);
    }

private:
    [[nodiscard]] bool at_end() const noexcept { return offset_ >= source_.size(); }

    [[nodiscard]] bool starts_with(const std::string_view text) const noexcept
    {
        return source_.substr(offset_, text.size()) == text;
    }

    [[nodiscard]] SourceLocation location() const noexcept
    {
        return {offset_, line_, column_};
    }

    void advance()
    {
        if (at_end()) {
            return;
        }
        if (source_[offset_] == '\r') {
            ++offset_;
            if (!at_end() && source_[offset_] == '\n') {
                ++offset_;
            }
            ++line_;
            column_ = 1;
            return;
        }
        if (source_[offset_] == '\n') {
            ++offset_;
            ++line_;
            column_ = 1;
            return;
        }
        const auto decoded = decode_utf8(source_, offset_);
        offset_ += decoded.valid ? decoded.length : 1;
        ++column_;
    }

    void advance_ascii(const std::size_t count)
    {
        for (std::size_t index = 0; index < count; ++index) {
            advance();
        }
    }

    void add_token(const TokenKind kind, const SourceLocation start, std::string value = {})
    {
        const auto end = location();
        result_.tokens.push_back({
            kind,
            std::string(source_.substr(start.byte_offset, end.byte_offset - start.byte_offset)),
            std::move(value),
            {start, end},
        });
    }

    void add_error(std::string code, std::string message, const SourceLocation start)
    {
        result_.diagnostics.push_back({
            DiagnosticSeverity::Error,
            std::move(code),
            std::move(message),
            {start, location()},
        });
    }

    void diagnose_invalid_utf8()
    {
        const auto start = location();
        advance();
        add_error("LEX001", "invalid UTF-8 byte sequence", start);
    }

    void skip_trivia()
    {
        for (;;) {
            while (!at_end()) {
                const char current = source_[offset_];
                if (current != ' ' && current != '\t' && current != '\n' && current != '\r' &&
                    current != '\f' && current != '\v') {
                    break;
                }
                advance();
            }

            if (starts_with("#")) {
                advance();
                while (!at_end() && source_[offset_] != '\n' && source_[offset_] != '\r') {
                    const auto decoded = decode_utf8(source_, offset_);
                    if (!decoded.valid) {
                        diagnose_invalid_utf8();
                    } else {
                        advance();
                    }
                }
                continue;
            }

            if (starts_with("/*")) {
                const auto start = location();
                advance_ascii(2);
                std::size_t depth = 1;
                while (!at_end() && depth != 0) {
                    if (starts_with("/*")) {
                        ++depth;
                        advance_ascii(2);
                        continue;
                    }
                    if (starts_with("*/")) {
                        --depth;
                        advance_ascii(2);
                        continue;
                    }
                    const auto decoded = decode_utf8(source_, offset_);
                    if (!decoded.valid) {
                        diagnose_invalid_utf8();
                    } else {
                        advance();
                    }
                }
                if (depth != 0) {
                    add_error("LEX003", "unterminated block comment", start);
                    return;
                }
                continue;
            }
            return;
        }
    }

    void scan_identifier()
    {
        const auto start = location();
        while (!at_end()) {
            const auto decoded = decode_utf8(source_, offset_);
            if (!decoded.valid || !is_identifier_continue(decoded.value)) {
                break;
            }
            advance();
        }
        const auto text = source_.substr(start.byte_offset, offset_ - start.byte_offset);
        add_token(keyword_kind(text), start);
    }

    void scan_number()
    {
        const auto start = location();
        while (!at_end() && is_ascii_digit(source_[offset_])) {
            advance();
        }

        bool is_float = false;
        if (!at_end() && source_[offset_] == '.' && offset_ + 1 < source_.size() &&
            is_ascii_digit(source_[offset_ + 1])) {
            is_float = true;
            advance();
            while (!at_end() && is_ascii_digit(source_[offset_])) {
                advance();
            }
        }

        if (!at_end() && (source_[offset_] == 'e' || source_[offset_] == 'E')) {
            std::size_t lookahead = offset_ + 1;
            if (lookahead < source_.size() && (source_[lookahead] == '+' || source_[lookahead] == '-')) {
                ++lookahead;
            }
            if (lookahead < source_.size() && is_ascii_digit(source_[lookahead])) {
                is_float = true;
                advance();
                if (!at_end() && (source_[offset_] == '+' || source_[offset_] == '-')) {
                    advance();
                }
                while (!at_end() && is_ascii_digit(source_[offset_])) {
                    advance();
                }
            }
        }
        add_token(is_float ? TokenKind::Float : TokenKind::Integer, start);
    }

    void scan_string(const char quote)
    {
        const auto start = location();
        advance();
        std::string value;

        while (!at_end()) {
            if (source_[offset_] == quote) {
                advance();
                add_token(TokenKind::String, start, std::move(value));
                return;
            }
            if (source_[offset_] == '\n' || source_[offset_] == '\r') {
                add_error("LEX002", "unterminated string literal", start);
                add_token(TokenKind::String, start, std::move(value));
                return;
            }
            if (source_[offset_] == '\\') {
                const auto escape_start = location();
                advance();
                if (at_end()) {
                    add_error("LEX002", "unterminated string literal", start);
                    add_token(TokenKind::String, start, std::move(value));
                    return;
                }
                const char escaped = source_[offset_];
                advance();
                switch (escaped) {
                    case '\\': value.push_back('\\'); break;
                    case '\"': value.push_back('\"'); break;
                    case '\'': value.push_back('\''); break;
                    case 'n': value.push_back('\n'); break;
                    case 'r': value.push_back('\r'); break;
                    case 't': value.push_back('\t'); break;
                    case 'b': value.push_back('\b'); break;
                    case 'f': value.push_back('\f'); break;
                    case 'v': value.push_back('\v'); break;
                    case '0': value.push_back('\0'); break;
                    default:
                        value.push_back(escaped);
                        add_error("LEX005", "unknown escape sequence", escape_start);
                        break;
                }
                continue;
            }

            const auto decoded = decode_utf8(source_, offset_);
            if (!decoded.valid) {
                diagnose_invalid_utf8();
                value.append("\xef\xbf\xbd");
                continue;
            }
            value.append(source_.substr(offset_, decoded.length));
            advance();
        }

        add_error("LEX002", "unterminated string literal", start);
        add_token(TokenKind::String, start, std::move(value));
    }

    void emit_operator(const TokenKind kind, const std::size_t length = 1)
    {
        const auto start = location();
        advance_ascii(length);
        add_token(kind, start);
    }

    void scan_operator_or_error()
    {
        if (starts_with("//=")) return emit_operator(TokenKind::FloorDivideEqual, 3);
        if (starts_with("**=")) return emit_operator(TokenKind::PowerEqual, 3);
        if (starts_with("==")) return emit_operator(TokenKind::EqualEqual, 2);
        if (starts_with("!=")) return emit_operator(TokenKind::BangEqual, 2);
        if (starts_with("<=")) return emit_operator(TokenKind::LessEqual, 2);
        if (starts_with(">=")) return emit_operator(TokenKind::GreaterEqual, 2);
        if (starts_with("&&")) return emit_operator(TokenKind::AndAnd, 2);
        if (starts_with("||")) return emit_operator(TokenKind::OrOr, 2);
        if (starts_with("+=")) return emit_operator(TokenKind::PlusEqual, 2);
        if (starts_with("-=")) return emit_operator(TokenKind::MinusEqual, 2);
        if (starts_with("*=")) return emit_operator(TokenKind::StarEqual, 2);
        if (starts_with("/=")) return emit_operator(TokenKind::SlashEqual, 2);
        if (starts_with("%=")) return emit_operator(TokenKind::PercentEqual, 2);
        if (starts_with("++")) return emit_operator(TokenKind::PlusPlus, 2);
        if (starts_with("--")) return emit_operator(TokenKind::MinusMinus, 2);
        if (starts_with("->")) return emit_operator(TokenKind::Arrow, 2);
        if (starts_with("=>")) return emit_operator(TokenKind::FatArrow, 2);
        if (starts_with("..")) return emit_operator(TokenKind::DotDot, 2);
        if (starts_with("//")) return emit_operator(TokenKind::FloorDivide, 2);
        if (starts_with("**")) return emit_operator(TokenKind::Power, 2);

        const char current = source_[offset_];
        switch (current) {
            case '+': return emit_operator(TokenKind::Plus);
            case '-': return emit_operator(TokenKind::Minus);
            case '*': return emit_operator(TokenKind::Star);
            case '/': return emit_operator(TokenKind::Slash);
            case '%': return emit_operator(TokenKind::Percent);
            case '=': return emit_operator(TokenKind::Equal);
            case '!': return emit_operator(TokenKind::Bang);
            case '<': return emit_operator(TokenKind::Less);
            case '>': return emit_operator(TokenKind::Greater);
            case '&': return emit_operator(TokenKind::Ampersand);
            case '|': return emit_operator(TokenKind::Pipe);
            case '^': return emit_operator(TokenKind::Caret);
            case '~': return emit_operator(TokenKind::Tilde);
            case '.': return emit_operator(TokenKind::Dot);
            case ',': return emit_operator(TokenKind::Comma);
            case ':': return emit_operator(TokenKind::Colon);
            case ';': return emit_operator(TokenKind::Semicolon);
            case '?': return emit_operator(TokenKind::Question);
            case '(': return emit_operator(TokenKind::LeftParen);
            case ')': return emit_operator(TokenKind::RightParen);
            case '[': return emit_operator(TokenKind::LeftBracket);
            case ']': return emit_operator(TokenKind::RightBracket);
            case '{': return emit_operator(TokenKind::LeftBrace);
            case '}': return emit_operator(TokenKind::RightBrace);
            default: break;
        }

        const auto start = location();
        advance();
        add_error("LEX004", "unexpected character", start);
    }

    std::string_view source_;
    std::size_t offset_{0};
    std::size_t line_{1};
    std::size_t column_{1};
    LexResult result_;
};

}  // namespace

bool LexResult::has_errors() const noexcept
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

Lexer::Lexer(const std::string_view source) noexcept : source_(source) {}

LexResult Lexer::tokenize()
{
    return Scanner(source_).run();
}

LexResult lex(const std::string_view source)
{
    return Lexer(source).tokenize();
}

}  // namespace baas::script
