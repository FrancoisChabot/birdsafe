# Chirp Specification: Lexical Structure

This document describes the lexical behavior of the current bootstrap frontend in
`interpreter/lib/frontend`. It is intentionally limited to implemented behavior.

## Source Text

Chirp source is read as UTF-8 text. Line and column tracking is based on the
frontend's UTF-8-aware scanner.

## Whitespace And Comments

The lexer discards:

- space
- tab
- carriage return
- newline

Line comments start with `//` and continue to the end of the line. There are no
block comments.

## Identifiers, Intrinsics, And Symbols

### Identifiers

Identifiers begin with an ASCII letter or `_`, followed by zero or more ASCII
letters, digits, or `_`.

```ebnf
Identifier = ( "A".."Z" | "a".."z" | "_" )
             { "A".."Z" | "a".."z" | "0".."9" | "_" } ;
```

### Intrinsics

An intrinsic is a backtick followed immediately by an identifier.

```ebnf
Intrinsic = "`" Identifier ;
```

Most intrinsics remain ordinary intrinsic tokens, but four spellings are
tokenized as operators instead:

- `` `in `` => `∈`
- `` `notin `` => `∉`
- `` `or `` => `∪`
- `` `and `` => `∩`

### Symbolic Constants

Symbolic constants start with `#` and then use identifier spelling.

```ebnf
SymbolicConstant = "#" Identifier ;
```

`#1` is not valid; the first character after `#` must be alphabetic or `_`.

## Keywords And Contextual Modifiers

Reserved keywords:

```chirp
let struct if else while for break match do debug enum
```

Contextual binding modifiers:

```chirp
mut final pub
```

`mut`, `final`, and `pub` are lexed as identifiers. They become modifiers only
in binding position.

Boot-defined names such as `int`, `bool`, `string`, `void`, `true`, `false`,
and `undecided` are not keywords. They are ordinary identifiers that happen to
be populated by bootstrapping.

## Literals

### Numbers

The lexer recognizes digit sequences, optionally followed by a fractional part.

```ebnf
Number = Digit { Digit } [ "." Digit { Digit } ] ;
Digit  = "0".."9" ;
```

Important implementation details:

- `-` is always a separate token. Negative values are parsed as unary negation.
- `1.2` lexes as one `Number` token.
- `1.foo` lexes as `Number("1")`, `.`, `Identifier("foo")`.
- `1.` lexes as `Number("1")`, `.`.
- The interpreter currently rejects fractional numeric literals semantically.

### Strings

Ordinary strings are delimited by `"..."`.

```ebnf
String     = '"' { StringChar } '"' ;
StringChar = ? any source character; a backslash escapes the next character for
               delimiter-scanning purposes ? ;
```

The lexer only finds the closing `"`. Escape validation is deferred until
evaluation. The interpreter currently accepts:

- `\\`
- `\'`
- `\"`
- `\n`
- `\r`
- `\t`
- `\0`
- `\uXXXX`

### Format Strings

Format strings start with `f"` and allow interpolation with `{ ... }`.

```ebnf
FString     = 'f"' FStringPart { "{" Expr "}" FStringPart } '"' ;
FStringPart = ? literal text between interpolations ? ;
```

Implementation notes:

- Interpolations are lexed by temporarily returning to ordinary tokenization
  until the matching `}`.
- Nested braces inside an interpolation are tracked correctly.
- Literal f-string segments use the same escape decoding as ordinary strings,
  and additionally accept `\{` and `\}`.
- Unterminated f-strings are lexical errors.

### Characters

Character literals are enclosed in single quotes and must contain exactly one
Unicode scalar value, either directly or through an escape.

```ebnf
Character     = "'" ( Utf8Scalar | Escape | UnicodeEscape ) "'" ;
Escape        = "\" ( "\" | "'" | '"' | "n" | "r" | "t" | "0" ) ;
UnicodeEscape = "\u" HexDigit HexDigit HexDigit HexDigit ;
```

The lexer validates character literals immediately. Empty literals, multi-code
point literals, malformed UTF-8, malformed escapes, and surrogate `\uXXXX`
values are lexical errors.

## Operators And Punctuation

The current token set is:

| Spelling | Token role |
| --- | --- |
| `(` `)` | grouping, parameter lists, conditions |
| `{` `}` | sets, anonymous struct literals, `struct`/`enum` bodies, `do`/`debug` blocks |
| `[` `]` | list literals and indexing |
| `,` | separator |
| `.` | member access |
| `..` | range |
| `..=` | inclusive-end range |
| `;` | statement terminator |
| `:` | bounds and field/type annotations |
| `|` | constructed-set separator |
| `=` | initializer / assignment / named-argument separator |
| `==` | equality |
| `!=` | inequality |
| `+` `-` `*` `/` `%` | arithmetic operators |
| `+=` `-=` `*=` `/=` `%=` | compound assignment |
| `!` | logical negation |
| `~` | complement unary operator |
| `&&` `\|\|` | boolean operators |
| `>` `>=` `<` `<=` | comparisons |
| `&` | address-of |
| `&mut` | mutable address-of |
| `->` | pointer/signature arrow |
| `->mut` | mutable pointer-type operator |
| `=>` | lambda and match-arm arrow |
| `∈` or `` `in `` | membership |
| `∉` or `` `notin `` | negated membership |
| `∪` or `` `or `` | set union |
| `∩` or `` `and `` | set intersection |

## Tokenization Notes

- Longest match is used for `..=` before `..`, and for two-character operators
  before one-character operators.
- `&mut` and `->mut` are fused spellings. Whitespace breaks them apart, so
  `& mut` and `-> mut` are tokenized differently.
- The lexer produces explicit `error` tokens for malformed characters,
  malformed symbolic constants, bad intrinsics such as a lone backtick, and
  other unrecognized input.
