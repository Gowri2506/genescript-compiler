/*
 * rdparser.c -- hand-written recursive-descent parser for GeneScript.
 *
 * Satisfies Lab Experiment 2 ("Implementation of handwritten parser
 * using LLVM") and Experiment 5 ("Write a recursive descent parser for
 * the CFG language and implement it using LLVM").
 *
 * This is a *second, independent* front end: it reuses the same
 * Flex-generated lexer (yylex(), from lexer.l) that the Bison grammar
 * (parser.y) uses, but does its own top-down parsing by hand instead
 * of going through yyparse(). Both front ends build the identical
 * ASTNode tree (ast.h) and feed the same LLVM codegen (codegen.c) --
 * main.c lets you pick either one with --frontend=bison|rd.
 *
 * Pure C (no C++), matching the rest of this compiler.
 */
#include "ast.h"
#include "parser.tab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int yylex(void);
extern int yylineno;
extern YYSTYPE yylval;

static int cur_tok;
static char *cur_str = NULL;

static void advance(void) {
    cur_tok = yylex();
    cur_str = yylval.str;
}

static void fail(const char *expected) {
    fprintf(stderr, "Parse Error (Recursive-Descent, line %d): expected %s\n",
            yylineno, expected);
    exit(1);
}

static void expect(int tok, const char *what) {
    if (cur_tok != tok) fail(what);
}

static ASTNode *parse_expr(void);

static FuncKind token_to_func(int tok) {
    switch (tok) {
        case GC_CONTENT: return FUNC_GC_CONTENT;
        case LENGTH: return FUNC_LENGTH;
        case FIND_MOTIF: return FUNC_FIND_MOTIF;
        case REVERSE_COMPLEMENT: return FUNC_REVERSE_COMPLEMENT;
        case REVERSE: return FUNC_REVERSE;
        case COMPLEMENT: return FUNC_COMPLEMENT;
        case TRANSLATE: return FUNC_TRANSLATE;
        default:
            fail("a function name");
            return FUNC_LENGTH; /* unreachable */
    }
}

static int is_func_token(int tok) {
    switch (tok) {
        case GC_CONTENT: case LENGTH: case FIND_MOTIF:
        case REVERSE_COMPLEMENT: case REVERSE: case COMPLEMENT: case TRANSLATE:
            return 1;
        default:
            return 0;
    }
}

/* expr -> IDENTIFIER | STRING | FUNC_NAME '(' expr (',' expr)? ')' */
static ASTNode *parse_expr(void) {
    int line = yylineno;

    if (cur_tok == IDENTIFIER) {
        ASTNode *n = ast_new_identifier(cur_str, line);
        advance();
        return n;
    }
    if (cur_tok == STRING) {
        ASTNode *n = ast_new_string_literal(cur_str, line);
        advance();
        return n;
    }
    if (is_func_token(cur_tok)) {
        FuncKind fk = token_to_func(cur_tok);
        advance();
        expect(LPAREN, "'('");
        advance();
        ASTNode *first = parse_expr();
        ASTList *args;
        if (fk == FUNC_FIND_MOTIF) {
            expect(COMMA, "',' (FIND_MOTIF takes 2 arguments)");
            advance();
            ASTNode *second = parse_expr();
            args = ast_list_prepend(first, ast_list_append_single(second));
        } else {
            args = ast_list_append_single(first);
        }
        expect(RPAREN, "')'");
        advance();
        return ast_new_func_call(fk, args, line);
    }
    fail("an identifier, string, or function call");
    return NULL; /* unreachable */
}

/*
 * statement -> SEQUENCE IDENTIFIER '=' STRING (';' | ';b')
 *            | IDENTIFIER '=' expr (';' | ';b')
 *            | PRINT expr (';' | ';b')
 *            | COMPARE IDENTIFIER WITH IDENTIFIER ';'
 *            | expr (';' | ';b')
 */
static ASTNode *parse_statement(void) {
    int line = yylineno;

    if (cur_tok == SEQUENCE) {
        advance();
        expect(IDENTIFIER, "an identifier after SEQUENCE");
        char *name = strdup(cur_str);
        advance();
        expect(ASSIGN, "'='");
        advance();
        expect(STRING, "a quoted DNA sequence");
        char *value = strdup(cur_str);
        advance();
        if (cur_tok != SEMICOLON && cur_tok != SEMICOLON_BIN) fail("';'");
        advance();
        ASTNode *n = ast_new_sequence_decl(name, value, line);
        free(name);
        free(value);
        return n;
    }

    if (cur_tok == PRINT) {
        advance();
        ASTNode *e = parse_expr();
        int is_bin = (cur_tok == SEMICOLON_BIN);
        if (cur_tok != SEMICOLON && cur_tok != SEMICOLON_BIN) fail("';' or ';b'");
        advance();
        return ast_new_print(e, is_bin, line);
    }

    if (cur_tok == COMPARE) {
        advance();
        expect(IDENTIFIER, "an identifier after COMPARE");
        char *left = strdup(cur_str);
        advance();
        expect(WITH, "'WITH'");
        advance();
        expect(IDENTIFIER, "an identifier after WITH");
        char *right = strdup(cur_str);
        advance();
        expect(SEMICOLON, "';'");
        advance();
        ASTNode *n = ast_new_compare(left, right, line);
        free(left);
        free(right);
        return n;
    }

    if (cur_tok == IDENTIFIER) {
        /* Could be `name = expr;` (assignment) or a bare `name;`
           expression statement -- one token of lookahead decides. */
        char *name = strdup(cur_str);
        advance();
        if (cur_tok == ASSIGN) {
            advance();
            ASTNode *rhs = parse_expr();
            if (cur_tok != SEMICOLON && cur_tok != SEMICOLON_BIN) fail("';'");
            advance();
            ASTNode *n = ast_new_assignment(name, rhs, line);
            free(name);
            return n;
        }
        ASTNode *idNode = ast_new_identifier(name, line);
        free(name);
        int is_bin = (cur_tok == SEMICOLON_BIN);
        if (cur_tok != SEMICOLON && cur_tok != SEMICOLON_BIN) fail("';' or ';b'");
        advance();
        ASTNode *n = ast_new_expr_stmt(idNode, line);
        n->print_binary = is_bin;
        return n;
    }

    /* bare expression statement (function call), auto-printed */
    ASTNode *e = parse_expr();
    int is_bin = (cur_tok == SEMICOLON_BIN);
    if (cur_tok != SEMICOLON && cur_tok != SEMICOLON_BIN) fail("';' or ';b'");
    advance();
    ASTNode *n = ast_new_expr_stmt(e, line);
    n->print_binary = is_bin;
    return n;
}

ASTNode *rd_parse_program(void) {
    advance(); /* prime the first token */
    ASTList *head = NULL, *tail = NULL;
    while (cur_tok != 0 /* EOF */) {
        ASTNode *stmt = parse_statement();
        ASTList *node = ast_list_append_single(stmt);
        if (!head) { head = tail = node; }
        else { tail->next = node; tail = node; }
    }
    return ast_new_program(head);
}
