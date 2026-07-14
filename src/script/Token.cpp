#include "script/Token.h"

namespace baas::script {

const char* token_kind_name(const TokenKind kind) noexcept
{
#define BAAS_SCRIPT_TOKEN_NAME(name) case TokenKind::name: return #name
    switch (kind) {
        BAAS_SCRIPT_TOKEN_NAME(EndOfFile);
        BAAS_SCRIPT_TOKEN_NAME(Identifier);
        BAAS_SCRIPT_TOKEN_NAME(Integer);
        BAAS_SCRIPT_TOKEN_NAME(Float);
        BAAS_SCRIPT_TOKEN_NAME(String);
        BAAS_SCRIPT_TOKEN_NAME(Let);
        BAAS_SCRIPT_TOKEN_NAME(Fn);
        BAAS_SCRIPT_TOKEN_NAME(If);
        BAAS_SCRIPT_TOKEN_NAME(Else);
        BAAS_SCRIPT_TOKEN_NAME(While);
        BAAS_SCRIPT_TOKEN_NAME(For);
        BAAS_SCRIPT_TOKEN_NAME(In);
        BAAS_SCRIPT_TOKEN_NAME(Return);
        BAAS_SCRIPT_TOKEN_NAME(Break);
        BAAS_SCRIPT_TOKEN_NAME(Continue);
        BAAS_SCRIPT_TOKEN_NAME(Import);
        BAAS_SCRIPT_TOKEN_NAME(As);
        BAAS_SCRIPT_TOKEN_NAME(True);
        BAAS_SCRIPT_TOKEN_NAME(False);
        BAAS_SCRIPT_TOKEN_NAME(Null);
        BAAS_SCRIPT_TOKEN_NAME(Try);
        BAAS_SCRIPT_TOKEN_NAME(Catch);
        BAAS_SCRIPT_TOKEN_NAME(Throw);
        BAAS_SCRIPT_TOKEN_NAME(Defer);
        BAAS_SCRIPT_TOKEN_NAME(Async);
        BAAS_SCRIPT_TOKEN_NAME(Await);
        BAAS_SCRIPT_TOKEN_NAME(And);
        BAAS_SCRIPT_TOKEN_NAME(Or);
        BAAS_SCRIPT_TOKEN_NAME(Not);
        BAAS_SCRIPT_TOKEN_NAME(Is);
        BAAS_SCRIPT_TOKEN_NAME(Plus);
        BAAS_SCRIPT_TOKEN_NAME(Minus);
        BAAS_SCRIPT_TOKEN_NAME(Star);
        BAAS_SCRIPT_TOKEN_NAME(Power);
        BAAS_SCRIPT_TOKEN_NAME(Slash);
        BAAS_SCRIPT_TOKEN_NAME(FloorDivide);
        BAAS_SCRIPT_TOKEN_NAME(Percent);
        BAAS_SCRIPT_TOKEN_NAME(Equal);
        BAAS_SCRIPT_TOKEN_NAME(EqualEqual);
        BAAS_SCRIPT_TOKEN_NAME(Bang);
        BAAS_SCRIPT_TOKEN_NAME(BangEqual);
        BAAS_SCRIPT_TOKEN_NAME(Less);
        BAAS_SCRIPT_TOKEN_NAME(LessEqual);
        BAAS_SCRIPT_TOKEN_NAME(Greater);
        BAAS_SCRIPT_TOKEN_NAME(GreaterEqual);
        BAAS_SCRIPT_TOKEN_NAME(AndAnd);
        BAAS_SCRIPT_TOKEN_NAME(OrOr);
        BAAS_SCRIPT_TOKEN_NAME(Ampersand);
        BAAS_SCRIPT_TOKEN_NAME(Pipe);
        BAAS_SCRIPT_TOKEN_NAME(Caret);
        BAAS_SCRIPT_TOKEN_NAME(Tilde);
        BAAS_SCRIPT_TOKEN_NAME(PlusEqual);
        BAAS_SCRIPT_TOKEN_NAME(MinusEqual);
        BAAS_SCRIPT_TOKEN_NAME(StarEqual);
        BAAS_SCRIPT_TOKEN_NAME(PowerEqual);
        BAAS_SCRIPT_TOKEN_NAME(SlashEqual);
        BAAS_SCRIPT_TOKEN_NAME(FloorDivideEqual);
        BAAS_SCRIPT_TOKEN_NAME(PercentEqual);
        BAAS_SCRIPT_TOKEN_NAME(PlusPlus);
        BAAS_SCRIPT_TOKEN_NAME(MinusMinus);
        BAAS_SCRIPT_TOKEN_NAME(Arrow);
        BAAS_SCRIPT_TOKEN_NAME(FatArrow);
        BAAS_SCRIPT_TOKEN_NAME(Dot);
        BAAS_SCRIPT_TOKEN_NAME(DotDot);
        BAAS_SCRIPT_TOKEN_NAME(Comma);
        BAAS_SCRIPT_TOKEN_NAME(Colon);
        BAAS_SCRIPT_TOKEN_NAME(Semicolon);
        BAAS_SCRIPT_TOKEN_NAME(Question);
        BAAS_SCRIPT_TOKEN_NAME(LeftParen);
        BAAS_SCRIPT_TOKEN_NAME(RightParen);
        BAAS_SCRIPT_TOKEN_NAME(LeftBracket);
        BAAS_SCRIPT_TOKEN_NAME(RightBracket);
        BAAS_SCRIPT_TOKEN_NAME(LeftBrace);
        BAAS_SCRIPT_TOKEN_NAME(RightBrace);
    }
#undef BAAS_SCRIPT_TOKEN_NAME
    return "Unknown";
}

}  // namespace baas::script
