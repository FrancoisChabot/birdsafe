# Chirp Grammar

This document describes the implemented surface grammar in the bootstrap
frontend at `interpreter/lib/frontend`, with a few semantic notes where the
interpreter materially narrows what the parser accepts.

> Parser artifact note: the frontend AST is a parse artifact, not the language's
> semantic authority. Some syntactically accepted forms are rejected later, such
> as invalid assignment targets or non-identifier member access on `.`.

## Lexical Notes

See [04_lexical.md](./04_lexical.md) for tokenization details. The most relevant
grammar-facing facts are:

- `mut`, `final`, and `pub` are contextual identifiers, not reserved keywords.
- `∈`, `∉`, `∪`, and `∩` may also be written as `` `in ``, `` `notin ``,
  `` `or ``, and `` `and ``.
- `-` is never part of a numeric token.
- Numbers may lex with a fractional part, but the interpreter still rejects
  floating-point values.
- Format strings are lexed as dedicated f-string token sequences and become a
  single expression node.

## Program And Statements

```ebnf
Program   = { Statement } EOF ;
Statement = LetStmt | BreakStmt | DebugStmt | IfStmt | ExprOrAssignStmt ;

LetStmt          = "let" LetBinding ";" ;
BreakStmt        = "break" [ Expr ] ";" ;
DebugStmt        = "debug" "{" { DebugInnerStmt } "}" ;
ExprOrAssignStmt = Expr [ AssignmentOp Expr ] ";" ;
IfStmt           = "if" "(" Expr ")" IfBranch [ "else" IfBranch ] [ ";" ] ;
IfBranch         = BlockExpr | BraceExpr | Statement ;
DebugInnerStmt   = BreakStmt | DebugStmt | IfStmt | ExprOrAssignStmt ;

AssignmentOp = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
```

Notes:

- `break;` is equivalent to `break `void;`.
- `debug { ... }` is statement-only. The parser rejects `let` directly inside a
  debug block, but other statements are allowed.
- The left-hand side of an assignment is parsed as a plain expression and
  validated later by the interpreter.
- `if` without `else` is statement-only.
- `if (...) a else b;` is an expression statement when both branches are
  expressions.
- `if (...) x = 1; else x = 2;` is a statement-form `if`.

## Bindings

```ebnf
BindingName        = Identifier | Intrinsic ;
BindingModifier    = "mut" | "final" ;
LetBindingModifier = BindingModifier | "pub" ;

BindingNoInit = { BindingModifier } BindingName [ ":" Expr ] ;
NamedBinding  = { BindingModifier } BindingName [ ":" Expr ] [ "=" Expr ] ;
LetBinding    = { LetBindingModifier } BindingName [ ParamList ] [ ":" Expr ] "=" Expr ;
FieldBinding  = NamedBinding
              | { BindingModifier } BindingName ParamList [ ":" Expr ] "=" Expr ;

ParamList = "(" [ BindingNoInit { "," BindingNoInit } ] ")" ;
```

Notes:

- Function sugar is implemented in both `let` bindings and struct fields. A form
  such as `let f(x: int): int = body;` is lowered to a lambda initializer.
- `pub` is only valid in `let` bindings.
- The parser accepts intrinsic names as binding names so boot files can define
  backtick-prefixed bindings. The interpreter only permits such definitions in
  top-level boot scope.
- `final` prevents shadowing from descendant scopes. It does not mean
  assignment-immutable by itself.

## Expressions

Expression parsing is precedence-based and left-associative at each binary level.

```ebnf
Expr       = LogicOr ;
LogicOr    = LogicAnd { "||" LogicAnd } ;
LogicAnd   = Equality { "&&" Equality } ;
Equality   = Comparison { ( "==" | "!=" ) Comparison } ;
Comparison = Range { ComparisonOp Range } ;
Range      = Term { RangeOp Term } ;
Term       = Factor { ( "+" | "-" | "∪" ) Factor } ;
Factor     = Unary { ( "*" | "/" | "%" | "∩" ) Unary } ;
Unary      = "&" Unary
           | "&mut" Unary
           | "->" Unary
           | "->mut" Unary
           | ( "!" | "-" | "*" | "~" ) Unary
           | Postfix ;
Postfix    = Primary { "." Primary | CallArgs | IndexArgs } ;

ComparisonOp = ">" | ">=" | "<" | "<=" | "∈" | "∉" ;
RangeOp      = ".." | "..=" ;
```

Notes:

- `&mut` and `->mut` are fused spellings; whitespace changes the parse.
- The parser accepts `.` followed by any primary expression, but the interpreter
  only accepts an identifier on the right-hand side.
- `..` is inclusive-start/exclusive-end. `..=` is inclusive-start/inclusive-end.
- The frontend has enum values for other range flavors, but they are not
  tokenized or parsed by the current lexer/parser.

## Calls And Indexing

```ebnf
CallArgs  = "(" [ PositionalArgs | NamedArgs ] ")" ;
IndexArgs = "[" Expr { "," Expr } "]" ;

PositionalArgs = Expr { "," Expr } ;
NamedArgs      = Identifier "=" Expr { "," Identifier "=" Expr } ;
```

Notes:

- Calls may use either all positional or all named arguments, never both.
- Index expressions require at least one argument.
- Struct construction is not a distinct syntactic form. `Point(1, 2)` and
  `Point(x=1, y=2)` are ordinary call syntax whose meaning is determined later.

## Primary Expressions

```ebnf
Primary =
    StructExpr
  | EnumExpr
  | ForExpr
  | WhileExpr
  | IfExpr
  | MatchExpr
  | ListExpr
  | Number
  | String
  | FString
  | Character
  | SymbolicConstant
  | Identifier
  | Intrinsic
  | LambdaExpr
  | SignatureExpr
  | GroupExpr
  | BlockExpr
  | BraceExpr ;
```

### Lambdas, Signatures, And Grouping

```ebnf
LambdaExpr    = "(" [ BindingNoInit { "," BindingNoInit } ] ")" [ ":" Expr ] "=>" Expr ;
SignatureExpr = "(" [ BindingNoInit { "," BindingNoInit } ] ")" "->" Expr ;
GroupExpr     = "(" Expr ")" ;
```

Notes:

- The parser decides between grouping and lambda/signature syntax by scanning
  ahead for `=>` or `->` after the matching `)`.
- Signature expressions are runtime values. The current interpreter only uses
  them as arity-based set constraints for lambdas, plus permissive acceptance of
  some built-in callable values.
- Parser quirk: `(params) : X -> Y` is currently accepted, but only `Y` is kept.
  Treat that as an implementation quirk, not supported surface syntax.

### Structs And Enums

```ebnf
StructExpr = "struct" StructFields ;
EnumExpr   = "enum" "{" [ Identifier { "," Identifier } [ "," ] ] "}" ;

StructFields = "{" [ FieldBinding { "," FieldBinding } [ "," ] ] "}" ;
```

Notes:

- Struct fields may have bounds, default initializers, or function-sugar bodies.
- Enum variants are bare identifiers.
- `Color.Red` is parsed as ordinary `.` syntax and interpreted as enum-variant
  selection because the left-hand side is an enum family value.

### Control Expressions

```ebnf
ForExpr   = "for" "(" BindingNoInit "∈" Expr ")" Expr ;
WhileExpr = "while" "(" Expr ")" Expr ;
IfExpr    = "if" "(" Expr ")" Expr "else" Expr ;
MatchExpr = "match" Expr "{" [ MatchArm { "," MatchArm } [ "," ] ] "}" ;
MatchArm  = Expr "=>" Expr ;
```

Notes:

- `for` and `while` are expressions and currently evaluate to `void`.
- The interpreter currently supports `for` only over range values.
- `match` evaluates arms from left to right.
- If a pattern expression has set semantics, a match arm tests membership
  (`subject ∈ pattern`); otherwise it tests ordinary equality.
- A non-exhaustive `match` is a runtime error.

### Lists

```ebnf
ListExpr = "[" [ Expr { "," Expr } [ "," ] ] "]" ;
```

### Format Strings

```ebnf
FString = 'f"' FStringPart { "{" Expr "}" FStringPart } '"' ;
```

This is a user-level description. The lexer actually emits
`fstring_head` / `fstring_middle` / `fstring_tail` / `fstring_literal` tokens.

### Blocks And Brace Expressions

```ebnf
BlockExpr = "do" "{" { Statement } [ Expr ] "}" ;

BraceExpr             = EmptySet
                      | ConstructedSet
                      | AnonymousStructLiteral
                      | EnumeratedSet ;
EmptySet              = "{" "}" ;
ConstructedSet        = "{" BindingNoInit "|" Expr "}" ;
AnonymousStructLiteral = "{" Identifier "=" Expr { "," Identifier "=" Expr } [ "," ] "}" ;
EnumeratedSet         = "{" Expr { "," Expr } [ "," ] "}" ;
```

Brace disambiguation is implemented in this order:

1. `{}` is an empty enumerated set.
2. `{ name : bound | cond }` and `{ name | cond }` are constructed sets.
3. `{ name = expr, ... }` is an anonymous struct literal.
4. Everything else is an enumerated set.

Notes:

- Braces never mean a statement block by themselves; `do { ... }` is required.
- A block's trailing expression is implicitly lowered to `break expr`.
- A block with no trailing expression implicitly yields `void`.
- Anonymous struct literals require a concrete struct context at evaluation
  time, such as a bound, parameter constraint, or constructor field type.
- `Point { x: 1 }` is not part of the grammar.
