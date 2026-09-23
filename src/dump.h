#ifndef GENESCRIPT_DUMP_H
#define GENESCRIPT_DUMP_H

#include "ast.h"
#include <stdio.h>

/*
 * dump.c -- human-readable views of each compiler phase, for learning,
 * debugging, and the UI:
 *
 *   --dump-tokens   lexer output: one token per line       (lexical analysis)
 *   --dump-ast      the syntax tree as an indented tree    (parsing)
 *   --dump-symtab   the symbol table after analysis        (semantic analysis)
 *   --dump-tac      three-address code ("Bio-IR"), before  (intermediate code)
 *                   and after optimization
 */

/* Re-scan `in` from the start and print every token. Leaves the lexer
   ready to be restarted by the caller. */
void dump_tokens(FILE *in, FILE *out);

void dump_ast(const ASTNode *program, FILE *out);
void dump_tac(const ASTNode *program, FILE *out);
void dump_symtab(const ASTNode *program, FILE *out);

/* Source-like text for an expression, e.g. "LENGTH(REVERSE(dna))".
   Caller frees the result. */
char *expr_to_string(const ASTNode *e);

#endif
