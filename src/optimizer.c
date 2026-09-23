/*
 * optimizer.c -- AST-level optimizer for GeneScript (see optimizer.h).
 *
 * Syllabus mapping: DAG representation of a basic block / common
 * subexpression elimination (pass 2), and algebraic simplification /
 * strength reduction with domain-specific identities (pass 1).
 */
#include "optimizer.h"
#include "dump.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static char *xstrdup_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = (char *)malloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

/* Functions whose result is a value that can be stored and reused.
   FIND_MOTIF prints a report and can't be stored, so it is never CSE'd. */
static int is_pure_value_func(FuncKind f) {
    return f != FUNC_FIND_MOTIF;
}

/* The sequence -> sequence transforms that keep length and G/C count. */
static int is_strand_transform(FuncKind f) {
    return f == FUNC_REVERSE || f == FUNC_COMPLEMENT || f == FUNC_REVERSE_COMPLEMENT;
}

static ASTNode *first_arg(ASTNode *call) {
    return call->args ? call->args->node : NULL;
}

/* ------------------------------------------------------------------ */
/* Pass 1: domain-specific algebraic simplification                    */
/* ------------------------------------------------------------------ */

typedef struct {
    FILE *report;
    OptStats *stats;
} Ctx;

static void report(Ctx *c, int line, const char *kind, const char *fmt, ...) {
    if (!c->report) return;
    fprintf(c->report, "  line %-3d [%s] ", line, kind);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(c->report, fmt, ap);
    va_end(ap);
    fputc('\n', c->report);
}

/* Simplify `e` bottom-up. `as_argument` is 1 when e's value only feeds
   another call; the "x -> x" identities are applied only there, so a
   printed value keeps the label it would have had (e.g. "Reverse:"). */
static ASTNode *simplify(Ctx *c, ASTNode *e, int as_argument) {
    if (!ast_isa(e, NODE_FUNC_CALL)) return e;

    for (ASTList *a = e->args; a; a = a->next) a->node = simplify(c, a->node, 1);

    ASTNode *inner = first_arg(e);
    if (!ast_isa(inner, NODE_FUNC_CALL)) return e;

    char *before = expr_to_string(e);
    ASTNode *result = e;

    /* REVERSE(COMPLEMENT(x)) and COMPLEMENT(REVERSE(x)) -> REVERSE_COMPLEMENT(x):
       two passes over the sequence (and an intermediate allocation)
       become one specialised runtime call. */
    if ((e->func == FUNC_REVERSE && inner->func == FUNC_COMPLEMENT) ||
        (e->func == FUNC_COMPLEMENT && inner->func == FUNC_REVERSE)) {
        e->func = FUNC_REVERSE_COMPLEMENT;
        e->args = inner->args;
    }
    /* LENGTH / GC_CONTENT don't depend on strand orientation. */
    else if ((e->func == FUNC_LENGTH || e->func == FUNC_GC_CONTENT) &&
             is_strand_transform(inner->func)) {
        e->args = inner->args;
    }
    /* Involutions: applying the same transform twice gives x back. */
    else if (as_argument && inner->func == e->func && is_strand_transform(e->func)) {
        result = first_arg(inner);
    }

    char *after = expr_to_string(result);
    int changed = strcmp(before, after) != 0;
    if (changed) {
        report(c, e->line, "domain", "%s  ->  %s", before, after);
        c->stats->domain_rewrites++;
    }
    free(after);
    free(before);
    /* one rewrite can expose another, e.g. LENGTH(REVERSE(REVERSE(x))) */
    return changed ? simplify(c, result, as_argument) : result;
}

static void pass_simplify(Ctx *c, ASTNode *program) {
    for (ASTList *s = program->statements; s; s = s->next) {
        ASTNode *st = s->node;
        if (st->kind == NODE_ASSIGNMENT || st->kind == NODE_PRINT || st->kind == NODE_EXPR_STMT) {
            st->expr = simplify(c, st->expr, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Pass 2: CSE by local value numbering                                */
/* ------------------------------------------------------------------ */

/* name -> value-number key (the current binding of each variable) */
typedef struct VN { char *name; char *key; struct VN *next; } VN;
/* key -> how many times it is computed (pass 2a) / who holds it (pass 2b) */
typedef struct KeyInfo { char *key; int count; char *holder; struct KeyInfo *next; } KeyInfo;

typedef struct {
    Ctx *ctx;
    VN *vn;
    KeyInfo *keys;
    int seq_counter;  /* to give each SEQUENCE declaration a unique value number */
    int temp_counter;
    int rewriting;    /* 0 = counting pass, 1 = rewriting pass */
    ASTList *insert_before; /* statement list cell to insert temps before */
    ASTList **insert_link;  /* pointer to the link that points at insert_before */
} LVN;

static const char *vn_get(LVN *l, const char *name) {
    for (VN *v = l->vn; v; v = v->next) if (strcmp(v->name, name) == 0) return v->key;
    return NULL;
}

static void vn_set(LVN *l, const char *name, char *key) {
    for (VN *v = l->vn; v; v = v->next) {
        if (strcmp(v->name, name) == 0) { v->key = key; return; }
    }
    VN *v = (VN *)malloc(sizeof(VN));
    v->name = strdup(name);
    v->key = key;
    v->next = l->vn;
    l->vn = v;
}

static KeyInfo *key_info(LVN *l, const char *key) {
    for (KeyInfo *k = l->keys; k; k = k->next) if (strcmp(k->key, key) == 0) return k;
    KeyInfo *k = (KeyInfo *)calloc(1, sizeof(KeyInfo));
    k->key = strdup(key);
    k->next = l->keys;
    l->keys = k;
    return k;
}

/* A holder is only valid while its variable still holds that value. */
static const char *valid_holder(LVN *l, KeyInfo *k) {
    if (!k->holder) return NULL;
    const char *cur = vn_get(l, k->holder);
    return (cur && strcmp(cur, k->key) == 0) ? k->holder : NULL;
}

/* Insert `name = expr;` just before the statement currently being processed. */
static void insert_temp(LVN *l, const char *name, ASTNode *expr, int line) {
    ASTNode *assign = ast_new_assignment(name, expr, line);
    ASTList *cell = ast_list_prepend(assign, l->insert_before);
    *l->insert_link = cell;
    l->insert_link = &cell->next;
}

/* Value-number `*slot` (bottom-up). Returns its key (caller owns it).
   `is_assign_rhs`: the expression is the whole right-hand side of an
   assignment -- the assigned variable will hold it, so no temp is needed. */
static char *number_expr(LVN *l, ASTNode **slot, int is_assign_rhs) {
    ASTNode *e = *slot;

    if (e->kind == NODE_IDENTIFIER) {
        const char *k = vn_get(l, e->name);
        return strdup(k ? k : e->name); /* undeclared: codegen reports it */
    }
    if (e->kind == NODE_STRING_LITERAL) {
        return xstrdup_printf("\"%s\"", e->strval);
    }
    if (e->kind != NODE_FUNC_CALL) return strdup("?");

    /* operands first */
    size_t cap = 64, len = 0;
    char *argkeys = (char *)malloc(cap);
    argkeys[0] = '\0';
    for (ASTList *a = e->args; a; a = a->next) {
        char *k = number_expr(l, &a->node, 0);
        size_t need = len + strlen(k) + 2;
        if (need > cap) { while (need > cap) cap *= 2; argkeys = (char *)realloc(argkeys, cap); }
        if (len) { argkeys[len++] = ','; argkeys[len] = '\0'; }
        strcat(argkeys, k);
        len = strlen(argkeys);
        free(k);
    }
    char *key = xstrdup_printf("%s(%s)", ast_func_name(e->func), argkeys);
    free(argkeys);

    if (!is_pure_value_func(e->func)) return key;

    KeyInfo *ki = key_info(l, key);
    if (!l->rewriting) { ki->count++; return key; }

    const char *holder = valid_holder(l, ki);
    if (holder) {
        /* already computed: reuse it */
        char *text = expr_to_string(e);
        report(l->ctx, e->line, "cse", "%s  reuses value already in '%s'", text, holder);
        free(text);
        l->ctx->stats->cse_reuses++;
        *slot = ast_new_identifier(holder, e->line);
    } else if (ki->count >= 2 && !is_assign_rhs) {
        /* first of several computations and no variable holds it: add a temp */
        char *tname = xstrdup_printf("$cse%d", ++l->temp_counter);
        char *text = expr_to_string(e);
        report(l->ctx, e->line, "cse", "%s  computed %d times -> computed once into %s",
               text, ki->count, tname);
        free(text);
        l->ctx->stats->temps_created++;
        insert_temp(l, tname, e, e->line);
        vn_set(l, tname, strdup(key));
        ki->holder = tname;
        *slot = ast_new_identifier(tname, e->line);
    }
    return key;
}

static void lvn_walk(LVN *l, ASTNode *program) {
    ASTList **link = &program->statements;
    while (*link) {
        ASTList *cell = *link;
        ASTNode *st = cell->node;
        l->insert_before = cell;
        l->insert_link = link;

        switch (st->kind) {
            case NODE_SEQUENCE_DECL: {
                char *k = xstrdup_printf("%s#%d", st->name, ++l->seq_counter);
                vn_set(l, st->name, k);
                break;
            }
            case NODE_ASSIGNMENT: {
                char *k = number_expr(l, &st->expr, 1);
                if (l->rewriting && st->expr->kind == NODE_FUNC_CALL &&
                    is_pure_value_func(st->expr->func)) {
                    key_info(l, k)->holder = st->name; /* this variable now holds k */
                }
                vn_set(l, st->name, k);
                break;
            }
            case NODE_PRINT:
            case NODE_EXPR_STMT: {
                char *k = number_expr(l, &st->expr, 0);
                free(k);
                break;
            }
            default:
                break;
        }
        /* insert_temp() may have added cells before `cell`; move past it */
        link = &cell->next;
    }
}

static void pass_cse(Ctx *c, ASTNode *program) {
    /* 2a: count how many times each value is computed */
    LVN count = {0};
    count.ctx = c;
    lvn_walk(&count, program);

    /* 2b: rewrite, using the counts to decide where temps pay off */
    LVN rw = {0};
    rw.ctx = c;
    rw.keys = count.keys;
    rw.rewriting = 1;
    lvn_walk(&rw, program);
    /* tables are process-lifetime, like the rest of the compiler's AST */
}

/* ------------------------------------------------------------------ */

OptStats optimize_program(ASTNode *program, FILE *reportFile) {
    OptStats stats = {0, 0, 0};
    Ctx c = { reportFile, &stats };

    if (reportFile) fprintf(reportFile, "== Optimization report ==\n");
    pass_simplify(&c, program);
    pass_cse(&c, program);
    if (reportFile) {
        if (stats.domain_rewrites + stats.cse_reuses + stats.temps_created == 0)
            fprintf(reportFile, "  (nothing to optimize)\n");
        fprintf(reportFile,
                "  summary: %d domain rewrite(s), %d reused value(s), %d temporary(ies)\n\n",
                stats.domain_rewrites, stats.cse_reuses, stats.temps_created);
    }
    return stats;
}
