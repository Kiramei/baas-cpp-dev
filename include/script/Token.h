#pragma once

#include "script/SourceLocation.h"

#include <string>

namespace baas::script {

enum class TokenKind {
    EndOfFile,
    Identifier,
    Integer,
    Float,
    String,

    Let,
    Fn,
    If,
    Else,
    While,
    For,
    In,
    Return,
    Break,
    Continue,
    Import,
    As,
    True,
    False,
    Null,
    Try,
    Catch,
    Throw,
    Defer,
    Async,
    Await,
    And,
    Or,
    Not,
    Is,

    Plus,
    Minus,
    Star,
    Power,
    Slash,
    FloorDivide,
    Percent,
    Equal,
    EqualEqual,
    Bang,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AndAnd,
    OrOr,
    Ampersand,
    Pipe,
    Caret,
    Tilde,
    PlusEqual,
    MinusEqual,
    StarEqual,
    PowerEqual,
    SlashEqual,
    FloorDivideEqual,
    PercentEqual,
    PlusPlus,
    MinusMinus,
    Arrow,
    FatArrow,
    Dot,
    DotDot,
    Comma,
    Colon,
    Semicolon,
    Question,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
};

struct Token {
    TokenKind kind{TokenKind::EndOfFile};
    std::string lexeme;
    // Decoded contents for string tokens. Empty for all other token kinds.
    std::string value;
    SourceSpan span{};
};

[[nodiscard]] const char* token_kind_name(TokenKind kind) noexcept;

}  // namespace baas::script
