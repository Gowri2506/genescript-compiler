/* dump.c -- readable views of each compiler phase (see dump.h) */
#include "dump.h"
#include "parser.tab.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int yylex(void);
extern int yylineno;
extern YYSTYPE yylval;
extern void yyrestart(FILE *in);

/* ------------------------------------------------------------------ */
/* growable string buffer                                              */
/* ------------------------------------------------------------------ */

typedef struct { char *s; size_t len, cap; } Buf;

static void buf_add(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (b->len + (size_t)n + 1 > b->cap) {
        b->cap = (b->len + (size_t)n + 1) * 2;
        b->s = (char *)realloc(b->s, b->cap);
    }
    va_start(ap, fmt);
    vsnprintf(b->s + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

static void expr_text(Buf *b, const ASTNode *e) {
    if (!e) { buf_add(b, "?"); return; }
    switch (e->kind) {
        case NODE_IDENTIFIER: buf_add(b, "%s", e->name); return;
        case NODE_STRING_LITERAL: buf_add(b, "\"%s\"", e->strval); return;
        case NODE_FUNC_CALL:
            buf_add(b, "%s(", ast_func_name(e->func));
            for (const ASTList *a = e->args; a; a = a->next) {
                expr_text(b, a->node);
                if (a->next) buf_add(b, ", ");
            }
            buf_add(b, ")");
            return;
        default: buf_add(b, "?"); return;
    }
}

char *expr_to_string(const ASTNode *e) {
    Buf b = {0};
    buf_add(&b, "%s", "");
    expr_text(&b, e);
    return b.s;
}

/* ------------------------------------------------------------------ */
/* --dump-tokens                                                       */
/* ------------------------------------------------------------------ */

static const char *token_name(int tok) {
    switch (tok) {
        case IDENTIFIER: return "IDENTIFIER";
        case STRING: return "STRING";
        case SEQUENCE: return "SEQUENCE";
        case PRINT: return "PRINT";
        case COMPARE: return "COMPARE";
        case WITH: return "WITH";
        case GC_CONTENT: return "GC_CONTENT";
        case LENGTH: return "LENGTH";
        case FIND_MOTIF: return "FIND_MOTIF";
        case REVERSE_COMPLEMENT: return "REVERSE_COMPLEMENT";
        case REVERSE: return "REVERSE";
        case COMPLEMENT: return "COMPLEMENT";
        case TRANSLATE: return "TRANSLATE";
        case ASSIGN: return "ASSIGN";
        case LPAREN: return "LPAREN";
        case RPAREN: return "RPAREN";
        case COMMA: return "COMMA";
        case SEMICOLON: return "SEMICOLON";
        case SEMICOLON_BIN: return "SEMICOLON_BIN";
        default: return "UNKNOWN";
    }
}

void dump_tokens(FILE *in, FILE *out) {
    rewind(in);
    yylineno = 1;
    yyrestart(in);
    fprintf(out, "== Tokens ==\n");
    fprintf(out, "  %-5s %-20s %s\n", "LINE", "TOKEN", "LEXEME");
    int tok;
    while ((tok = yylex()) != 0) {
        const char *lexeme = "";
        char quoted[512];
        if (tok == IDENTIFIER) lexeme = yylval.str;
        else if (tok == STRING) { snprintf(quoted, sizeof(quoted), "\"%s\"", yylval.str); lexeme = quoted; }
        else if (tok == ASSIGN) lexeme = "=";
        else if (tok == LPAREN) lexeme = "(";
        else if (tok == RPAREN) lexeme = ")";
        else if (tok == COMMA) lexeme = ",";
        else if (tok == SEMICOLON) lexeme = ";";
        else if (tok == SEMICOLON_BIN) lexeme = ";b";
        else lexeme = token_name(tok); /* keywords: the lexeme is the keyword */
        fprintf(out, "  %-5d %-20s %s\n", yylineno, token_name(tok), lexeme);
    }
    fprintf(out, "\n");
    /* hand the file back to the parser from the top */
    rewind(in);
    yylineno = 1;
    yyrestart(in);
}

/* ------------------------------------------------------------------ */
/* --dump-ast                                                          */
/* ------------------------------------------------------------------ */

static void ast_node(const ASTNode *n, FILE *out, const char *prefix, int last);

static void ast_children(const ASTNode *n, FILE *out, const char *prefix) {
    const ASTNode *kids[64];
    int k = 0;
    switch (n->kind) {
        case NODE_PROGRAM:
            for (const ASTList *s = n->statements; s && k < 64; s = s->next) kids[k++] = s->node;
            break;
        case NODE_ASSIGNMENT: case NODE_PRINT: case NODE_EXPR_STMT:
            kids[k++] = n->expr;
            break;
        case NODE_FUNC_CALL:
            for (const ASTList *a = n->args; a && k < 64; a = a->next) kids[k++] = a->node;
            break;
        default: break;
    }
    for (int i = 0; i < k; i++) ast_node(kids[i], out, prefix, i == k - 1);
}

static void ast_label(const ASTNode *n, FILE *out) {
    switch (n->kind) {
        case NODE_PROGRAM: fprintf(out, "Program"); break;
        case NODE_SEQUENCE_DECL: fprintf(out, "SequenceDecl %s = \"%s\"", n->name, n->strval); break;
        case NODE_ASSIGNMENT: fprintf(out, "Assignment %s", n->name); break;
        case NODE_PRINT: fprintf(out, "Print%s", n->print_binary ? " (binary)" : ""); break;
        case NODE_EXPR_STMT: fprintf(out, "ExprStmt%s", n->print_binary ? " (binary)" : ""); break;
        case NODE_COMPARE: fprintf(out, "Compare %s WITH %s", n->left, n->right); break;
        case NODE_IDENTIFIER: fprintf(out, "Identifier %s", n->name); break;
        case NODE_STRING_LITERAL: fprintf(out, "StringLiteral \"%s\"", n->strval); break;
        case NODE_FUNC_CALL: fprintf(out, "FuncCall %s", ast_func_name(n->func)); break;
    }
    if (n->kind != NODE_PROGRAM && n->line > 0 &&
        n->kind != NODE_IDENTIFIER && n->kind != NODE_STRING_LITERAL && n->kind != NODE_FUNC_CALL)
        fprintf(out, "   (line %d)", n->line);
    fputc('\n', out);
}

static void ast_node(const ASTNode *n, FILE *out, const char *prefix, int last) {
    fprintf(out, "%s%s", prefix, last ? "└── " : "├── ");
    ast_label(n, out);
    char next[512];
    snprintf(next, sizeof(next), "%s%s", prefix, last ? "    " : "│   ");
    ast_children(n, out, next);
}

void dump_ast(const ASTNode *program, FILE *out) {
    fprintf(out, "== AST ==\n");
    ast_label(program, out);
    ast_children(program, out, "");
    fprintf(out, "\n");
}

/* ------------------------------------------------------------------ */
/* --dump-tac : three-address code ("Bio-IR")                          */
/* ------------------------------------------------------------------ */

typedef struct { FILE *out; int temp; } Tac;

/* Emit code for `e`; returns the operand that holds its value
   (a variable name, a literal, or a fresh temporary tN). */
static char *tac_expr(Tac *t, const ASTNode *e, const char *target) {
    if (e->kind == NODE_IDENTIFIER) return strdup(e->name);
    if (e->kind == NODE_STRING_LITERAL) {
        Buf b = {0};
        buf_add(&b, "\"%s\"", e->strval);
        return b.s;
    }
    /* function call: operands first, then one instruction */
    char *ops[4] = {0};
    int n = 0;
    for (const ASTList *a = e->args; a && n < 4; a = a->next) ops[n++] = tac_expr(t, a->node, NULL);

    Buf args = {0};
    buf_add(&args, "%s", "");
    for (int i = 0; i < n; i++) { buf_add(&args, "%s%s", i ? ", " : "", ops[i]); free(ops[i]); }

    if (e->func == FUNC_FIND_MOTIF) { /* no result value: a statement */
        fprintf(t->out, "  FIND_MOTIF %s\n", args.s);
        free(args.s);
        return NULL;
    }
    char *dest;
    if (target) dest = strdup(target);
    else { Buf d = {0}; buf_add(&d, "t%d", ++t->temp); dest = d.s; }
    fprintf(t->out, "  %s = %s %s\n", dest, ast_func_name(e->func), args.s);
    free(args.s);
    return dest;
}

static void tac_print(Tac *t, const ASTNode *e, int binary) {
    char *v = tac_expr(t, e, NULL);
    if (v) { fprintf(t->out, "  %s %s\n", binary ? "PRINT_BIN" : "PRINT", v); free(v); }
}

void dump_tac(const ASTNode *program, FILE *out) {
    Tac t = { out, 0 };
    for (const ASTList *s = program->statements; s; s = s->next) {
        const ASTNode *st = s->node;
        switch (st->kind) {
            case NODE_SEQUENCE_DECL:
                fprintf(out, "  %s = LOAD_SEQ \"%s\"\n", st->name, st->strval);
                break;
            case NODE_ASSIGNMENT: {
                char *v = tac_expr(&t, st->expr, st->name);
                if (v && strcmp(v, st->name) != 0) fprintf(out, "  %s = %s\n", st->name, v);
                free(v);
                break;
            }
            case NODE_PRINT:
            case NODE_EXPR_STMT:
                tac_print(&t, st->expr, st->print_binary);
                break;
            case NODE_COMPARE:
                fprintf(out, "  COMPARE %s, %s\n", st->left, st->right);
                break;
            default: break;
        }
    }
    fprintf(out, "  HALT\n");
}

/* ------------------------------------------------------------------ */
/* --dump-symtab                                                       */
/* ------------------------------------------------------------------ */

typedef struct Sym { const char *name; const char *type; struct Sym *next; } Sym;

static const char *sym_type(Sym *syms, const char *name) {
    for (Sym *s = syms; s; s = s->next) if (strcmp(s->name, name) == 0) return s->type;
    return "?";
}

static const char *expr_type(Sym *syms, const ASTNode *e) {
    if (e->kind == NODE_IDENTIFIER) return sym_type(syms, e->name);
    if (e->kind == NODE_STRING_LITERAL) return "SEQUENCE";
    switch (e->func) {
        case FUNC_GC_CONTENT: return "PERCENT";
        case FUNC_LENGTH: return "INT";
        case FUNC_TRANSLATE: return "PROTEIN";
        case FUNC_FIND_MOTIF: return "REPORT";
        default: return "SEQUENCE";
    }
}

void dump_symtab(const ASTNode *program, FILE *out) {
    Sym *syms = NULL;
    fprintf(out, "== Symbol table ==\n");
    fprintf(out, "  %-10s %-9s %-7s %-8s %-5s %s\n", "NAME", "TYPE", "LENGTH", "GC%", "LINE", "VALUE / SOURCE");
    for (const ASTList *s = program->statements; s; s = s->next) {
        const ASTNode *st = s->node;
        if (st->kind != NODE_SEQUENCE_DECL && st->kind != NODE_ASSIGNMENT) continue;

        Sym *sym = (Sym *)malloc(sizeof(Sym));
        sym->name = st->name;
        sym->next = syms;

        if (st->kind == NODE_SEQUENCE_DECL) {
            size_t len = strlen(st->strval), gc = 0;
            for (size_t i = 0; i < len; i++) {
                char c = (char)toupper((unsigned char)st->strval[i]);
                if (c == 'G' || c == 'C') gc++;
            }
            char gcs[16];
            snprintf(gcs, sizeof(gcs), "%.2f", len ? 100.0 * (double)gc / (double)len : 0.0);
            sym->type = "SEQUENCE";
            fprintf(out, "  %-10s %-9s %-7zu %-8s %-5d \"%s\"\n",
                    st->name, sym->type, len, gcs, st->line, st->strval);
        } else {
            sym->type = expr_type(syms, st->expr);
            char *src = expr_to_string(st->expr);
            fprintf(out, "  %-10s %-9s %-7s %-8s %-5d %s%s\n",
                    st->name, sym->type, "-", "-", st->line, src,
                    st->name[0] == '$' ? "   (compiler temporary)" : "");
            free(src);
        }
        syms = sym;
    }
    fprintf(out, "\n");
}
