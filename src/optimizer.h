#ifndef GENESCRIPT_OPTIMIZER_H
#define GENESCRIPT_OPTIMIZER_H

#include "ast.h"
#include <stdio.h>

/*
 * optimizer.c -- machine-independent optimizations on the GeneScript AST,
 * run after parsing and before LLVM code generation.
 *
 * Two passes, in this order:
 *
 *  1. Domain-specific algebraic simplification (DNA identities):
 *       REVERSE(COMPLEMENT(x))   -> REVERSE_COMPLEMENT(x)
 *       COMPLEMENT(REVERSE(x))   -> REVERSE_COMPLEMENT(x)
 *       LENGTH(f(x))             -> LENGTH(x)       f in {REVERSE, COMPLEMENT, REVERSE_COMPLEMENT}
 *       GC_CONTENT(f(x))         -> GC_CONTENT(x)   (same f: none of them change the G/C count)
 *     and, where the value is only an argument to another call:
 *       REVERSE(REVERSE(x)) -> x,  COMPLEMENT(COMPLEMENT(x)) -> x,
 *       REVERSE_COMPLEMENT(REVERSE_COMPLEMENT(x)) -> x
 *
 *  2. Common-subexpression elimination via local value numbering -- the
 *     DAG construction for a basic block (GeneScript programs are one
 *     straight-line block). Every expression gets a value number (a
 *     canonical key built from its operator and its operands' value
 *     numbers); a key that is computed more than once is computed only
 *     the first time:
 *       - if a user variable already holds it, later uses read that
 *         variable (  y = GC_CONTENT(dna);  ->  y = x;  )
 *       - otherwise a compiler temporary ($cse1, $cse2, ...) is inserted to
 *         hold it, and every occurrence reads the temporary.
 *     Reassigning a variable gives it a new value number, so stale
 *     values are never reused.
 *
 * Every rewrite can be reported (gsc --show-opt).
 */

typedef struct OptStats {
    int domain_rewrites;
    int cse_reuses;
    int temps_created;
} OptStats;

/* Optimize `program` in place. If `report` is non-NULL, a line is written
   to it for every transformation applied. Returns counts of each kind. */
OptStats optimize_program(ASTNode *program, FILE *report);

#endif
