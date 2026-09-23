# GeneScript — Language Specification (Lab Experiment 4)

"Defining a real programming language" — this is GeneScript's formal
context-free grammar, as implemented in `src/parser.y` (Bison) and
`src/rdparser.c` (hand-written recursive descent). Both front ends
parse exactly this grammar and build the same AST (`src/ast.h`).

## Lexical grammar (tokens, see `src/lexer.l`)

```
IDENTIFIER          [A-Za-z_][A-Za-z0-9_]*        (excluding keywords below)
STRING              "\"" [^"]* "\""
KEYWORDS            SEQUENCE PRINT COMPARE WITH
                    GC_CONTENT LENGTH FIND_MOTIF
                    REVERSE_COMPLEMENT REVERSE COMPLEMENT TRANSLATE
SYMBOLS             = ( ) , ;
SEMICOLON_BIN       ";b"      -- see "Binary output" exercise below
COMMENT             "//" .* (to end of line, skipped)
WHITESPACE          [ \t\r\n]+ (skipped)
```

## Context-free grammar

```
program          -> statement*

statement        -> SEQUENCE IDENTIFIER '=' STRING terminator
                   | IDENTIFIER '=' expr terminator
                   | PRINT expr terminator
                   | COMPARE IDENTIFIER WITH IDENTIFIER ';'
                   | expr terminator

terminator       -> ';' | ';b'

expr             -> IDENTIFIER
                   | STRING
                   | GC_CONTENT           '(' expr ')'
                   | LENGTH               '(' expr ')'
                   | REVERSE              '(' expr ')'
                   | COMPLEMENT           '(' expr ')'
                   | REVERSE_COMPLEMENT   '(' expr ')'
                   | TRANSLATE            '(' expr ')'
                   | FIND_MOTIF           '(' expr ',' expr ')'
```

## Static (semantic) rules

These are checked in `src/codegen.c`, after parsing; every violation is
a compile-time error with its line number, and `gsc` exits non-zero.

- **DNA alphabet.** A `SEQUENCE` declaration's string, and any string
  literal used as a sequence or motif argument (e.g. `LENGTH("ATGC")`,
  `FIND_MOTIF(dna, "ATG")`), may contain only `A`, `T`, `G`, `C`
  (case-insensitive). Any other character is reported with its exact
  1-indexed position.
- **Declare before use.** Every `IDENTIFIER` must be bound by an earlier
  `SEQUENCE` declaration or assignment (no forward references, no
  scoping -- a flat, single-pass language).
- **No redeclaration.** A name may be declared with `SEQUENCE` only
  once. (Plain assignment `x = ...;` may rebind a name; the newest
  binding wins.)
- **Types.** Every value is either a *sequence* (a declared sequence,
  or the result of `REVERSE`, `COMPLEMENT`, `REVERSE_COMPLEMENT`,
  `TRANSLATE`) or a *number* (`LENGTH` -> integer, `GC_CONTENT` ->
  percentage). Function arguments and both `COMPARE` operands must be
  sequences, so `LENGTH(GC_CONTENT(dna))` is rejected.
- **Nesting.** Because `expr` is recursive, calls can be nested:
  `LENGTH(REVERSE(COMPLEMENT(dna)))` is valid.
- **Statement-only forms.** `FIND_MOTIF(...)` produces a report, not a
  value, so it can only be used as its own statement -- not assigned
  (`m = FIND_MOTIF(...)`) or nested inside another call. `COMPARE` is a
  statement by the grammar.
- **Lexical errors are fatal.** An unexpected character stops
  compilation immediately.

## "Binary output" exercise (Lab Experiment 7)

> Modify the scanner and parser so that terminating a statement with
> "; b" instead of ";" results in the output being printed in binary.

Implemented exactly as specified:

- `src/lexer.l` adds a `";b"` rule (`SEMICOLON_BIN` token). Flex's
  maximal-munch rule means `;b` wins over plain `;` automatically
  whenever the `b` immediately follows the semicolon with no space.
- `src/parser.y` (via the `terminator` rule) and `src/rdparser.c` both
  accept either terminator on every statement except `COMPARE`, setting
  a `print_binary` flag on `PRINT` and bare-expression statements.
  (On `SEQUENCE` declarations and assignments `;b` is accepted but has
  no effect, since those print nothing.)
- `src/codegen.c` passes that flag through to `gs_print_length_value`
  in the runtime (`src/runtime.c`), which prints the integer in binary
  instead of decimal when the flag is set.

Example (`examples/binary_exercise.gs`):

```
SEQUENCE dna = "ATGCGATCGATCG";
PRINT LENGTH(dna);b
```

Output: `Length: 1101 (binary)` (13 in binary).

The flag is only wired up for `LENGTH` in this implementation (the
only integer-valued result in the language); it's accepted but has no
effect on sequence-valued or report-valued prints, since "binary" only
has an obvious meaning for a number. This is documented in `README.md`.
