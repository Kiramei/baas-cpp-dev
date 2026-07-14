# BAAS Script lexical and syntactic grammar (Draft 0.1)

This document is the grammar contract for the draft implementation. The
semantic rules in `LANGUAGE_SPEC_DRAFT.md` remain normative where grammar alone
cannot express a restriction. Lexical scope, function, control-flow, and module
semantics are refined by `CONTROL_FLOW_AND_MODULES.md`; diagnostic,
throw/catch, and cleanup semantics are refined by `ERRORS_AND_CLEANUP.md`.
Draft 0.1 is not the language 1.0 compatibility freeze.

## Notation

Productions use extended BNF. `x?` is optional, `x*` repeats zero or more
times, `x+` repeats one or more times, and `x | y` selects one alternative.
Quoted text is an exact source spelling. A trailing comma is accepted in
parameter, argument, list, and map sequences.

## Source decoding and locations

- A source file is an untagged sequence of valid UTF-8 bytes. Overlong forms,
  surrogate code points, values above U+10FFFF, truncated sequences, and a
  leading UTF-8 BOM are errors. A BOM is diagnosed as `LEX006`; it is never
  silently stripped.
- Byte offsets are zero-based. Lines and display columns are one-based. A
  column advances by one Unicode scalar value, not by UTF-8 byte count or
  terminal cell width.
- `CRLF`, `CR`, and `LF` each advance one logical line. `CRLF` occupies two
  bytes but is one line break.
- Tokens own their UTF-8 lexeme. String tokens additionally own their decoded
  byte sequence. Diagnostics and AST nodes carry half-open source spans.
- Lexing is error-recovering and always appends one end-of-file token.

## Lexical grammar

```ebnf
source          = trivia*, (token, trivia*)*, EOF ;
trivia          = whitespace | line-comment | block-comment ;
whitespace      = " " | "\t" | "\r" | "\n" | "\f" | "\v" ;
line-comment    = "#", { scalar - "\r" - "\n" } ;
block-comment   = "/*", { block-comment | scalar - "/*" - "*/" }, "*/" ;

identifier      = identifier-start, identifier-continue* ;
identifier-start = "_" | ASCII-letter | non-ASCII-scalar ;
identifier-continue = identifier-start | ASCII-digit ;

integer         = ASCII-digit+ ;
float           = ASCII-digit+, ".", ASCII-digit+, exponent?
                | ASCII-digit+, exponent ;
exponent        = ("e" | "E"), ("+" | "-")?, ASCII-digit+ ;

string          = double-string | single-string ;
double-string   = '"', double-character*, '"' ;
single-string   = "'", single-character*, "'" ;
escape          = "\\", ("\\" | '"' | "'" | "n" | "r" | "t"
                         | "b" | "f" | "v" | "0") ;
```

Strings cannot contain an unescaped logical newline. Draft 0.1 has no raw,
multiline, byte, interpolation, hexadecimal, Unicode-code-point, or octal
literal. An unknown escape produces `LEX005` and recovers by inserting the
escaped character. A NUL produced by `\0` is valid inside a language string.

The draft identifier rule deliberately describes the current implementation:
every valid non-ASCII Unicode scalar is accepted, without normalization. The
1.0 freeze must either retain this byte-sensitive rule or adopt a versioned
XID/NFC profile with migration diagnostics; implementations may not silently
normalize or case-fold identifiers.

Keywords are ASCII and case-sensitive:

```text
let fn if else while for in return break continue import as
true false null try catch throw defer async await and or not is
```

The longest operator wins. The parser currently consumes:

```text
+ - * ** / // % = == != < <= > >=
+= -= *= /= //= %= . , : ; ( ) [ ] { }
```

The lexer reserves `!`, `&&`, `||`, `&`, `|`, `^`, `~`, `**=`, `++`, `--`,
`->`, `=>`, `..`, and `?`. They are tokens, not valid Draft 0.1 expressions;
using them yields a parser diagnostic. `//` is always floor division, never a
comment.

## Syntactic grammar

```ebnf
module          = statement*, EOF ;

statement       = block
                | let-statement
                | if-statement
                | while-statement
                | for-statement
                | function-declaration
                | return-statement
                | break-statement
                | continue-statement
                | import-statement
                | throw-statement
                | try-statement
                | defer-statement
                | expression-statement ;

block           = "{", statement*, "}" ;
let-statement   = "let", identifier, "=", expression, ";" ;
if-statement    = "if", "(", expression, ")", statement,
                  ("else", statement)? ;
while-statement = "while", "(", expression, ")", statement ;
for-statement   = "for", "(", identifier, "in", expression, ")", statement ;

function-declaration = "async"?, "fn", identifier, parameters, function-body ;
function-body   = block ;
parameters      = "(", parameter-list?, ")" ;
parameter-list  = parameter, (",", parameter)*, ","? ;
parameter       = identifier, ("=", expression)? ;

return-statement   = "return", expression?, ";" ;
break-statement    = "break", ";" ;
continue-statement = "continue", ";" ;
import-statement   = "import", string, "as", identifier, ";" ;
throw-statement    = "throw", expression, ";" ;
try-statement      = "try", block, "catch", (identifier | "(", identifier, ")"), block ;
defer-statement    = "defer", statement ;
expression-statement = expression, ";" ;
```

Simple statements require semicolons. Blocks and compound statements do not.
`if`, `while`, and `for` accept either one statement or a block as their body.
Function and `try`/`catch` bodies require blocks.

```ebnf
expression      = assignment ;
assignment      = logical-or, (assignment-op, assignment)? ;
assignment-op   = "=" | "+=" | "-=" | "*=" | "/=" | "//=" | "%=" ;

logical-or      = logical-and, ("or", logical-and)* ;
logical-and     = equality, ("and", equality)* ;
equality        = ordering, (("==" | "!=" | "is" | "not", "is"), ordering)* ;
ordering        = additive,
                  (("<" | "<=" | ">" | ">=" | "in" | "not", "in"), additive)* ;
additive        = multiplicative, (("+" | "-"), multiplicative)* ;
multiplicative  = unary, (("*" | "/" | "//" | "%"), unary)* ;
unary           = ("+" | "-" | "not" | "await"), unary | power ;
power           = postfix, ("**", unary)? ;

postfix         = primary, (member | call | subscript)* ;
member          = ".", identifier ;
call            = "(", argument-list?, ")" ;
argument-list   = argument, (",", argument)*, ","? ;
argument        = (identifier, "=")?, expression ;
subscript       = "[", (index | slice), "]" ;
index           = expression ;
slice           = expression?, ":", expression?, (":", expression?)? ;

primary         = "null" | "true" | "false" | integer | float | string
                | identifier | "(", expression, ")"
                | list-literal | map-literal | function-expression ;
list-literal    = "[", (expression, (",", expression)*, ","?)?, "]" ;
map-literal     = "{", (map-entry, (",", map-entry)*, ","?)?, "}" ;
map-entry       = expression, ":", expression ;
function-expression = "async"?, "fn", parameters, function-body ;
```

Assignment and exponentiation are right-associative; other binary levels are
left-associative. Postfix operations bind most tightly. Because `power` parses
its right operand as `unary`, `2 ** -3` is valid. `await` and unary operators
bind below postfix and above multiplication.

## Contextual and static restrictions

The parser or semantic analyzer additionally enforces:

- assignment targets are identifiers, member expressions, or index
  expressions; slices are not assignment targets in Draft 0.1;
- a required parameter cannot follow a defaulted parameter, and parameter names
  are unique;
- positional call arguments cannot follow named arguments, and named arguments
  are unique;
- `return` and `defer` occur inside a function; `break` and `continue` occur
  inside the current function's loop; `await` occurs inside an async function;
- declarations bind in source order, unknown or uninitialized reads are errors,
  and implicit globals are forbidden;
- map syntax accepts expression keys, but the runtime contract requires a
  string key and must raise a stable type error for another value;
- lexical and parser recovery diagnostics do not authorize executing a module;
  package validation rejects any module containing an error diagnostic.

## Conformance boundary

The checked-in lexer, parser, and lexical semantic tests exercise this Draft
0.1 grammar on MSVC Debug and Release. Runtime evaluation, module loading,
structured exception execution, async suspension, formatting, fuzzing, and the
version-1 normative corpus remain separate gates.
