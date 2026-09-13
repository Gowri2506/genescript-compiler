/* ast.c -- construction helpers for the GeneScript AST (see ast.h) */
#include "ast.h"
#include <stdlib.h>
#include <string.h>

static ASTNode *alloc_node(NodeKind kind, int line) {
    ASTNode *n = (ASTNode *)calloc(1, sizeof(ASTNode));
    n->kind = kind;
    n->line = line;
    return n;
}

ASTNode *ast_new_program(ASTList *statements) {
    ASTNode *n = alloc_node(NODE_PROGRAM, 0);
    n->statements = statements;
    return n;
}

ASTNode *ast_new_sequence_decl(const char *name, const char *value, int line) {
    ASTNode *n = alloc_node(NODE_SEQUENCE_DECL, line);
    n->name = strdup(name);
    n->strval = strdup(value);
    return n;
}

ASTNode *ast_new_assignment(const char *name, ASTNode *expr, int line) {
    ASTNode *n = alloc_node(NODE_ASSIGNMENT, line);
    n->name = strdup(name);
    n->expr = expr;
    return n;
}

ASTNode *ast_new_print(ASTNode *expr, int print_binary, int line) {
    ASTNode *n = alloc_node(NODE_PRINT, line);
    n->expr = expr;
    n->print_binary = print_binary;
    return n;
}

ASTNode *ast_new_expr_stmt(ASTNode *expr, int line) {
    ASTNode *n = alloc_node(NODE_EXPR_STMT, line);
    n->expr = expr;
    return n;
}

ASTNode *ast_new_compare(const char *left, const char *right, int line) {
    ASTNode *n = alloc_node(NODE_COMPARE, line);
    n->left = strdup(left);
    n->right = strdup(right);
    return n;
}

ASTNode *ast_new_identifier(const char *name, int line) {
    ASTNode *n = alloc_node(NODE_IDENTIFIER, line);
    n->name = strdup(name);
    return n;
}

ASTNode *ast_new_string_literal(const char *value, int line) {
    ASTNode *n = alloc_node(NODE_STRING_LITERAL, line);
    n->strval = strdup(value);
    return n;
}

ASTNode *ast_new_func_call(FuncKind func, ASTList *args, int line) {
    ASTNode *n = alloc_node(NODE_FUNC_CALL, line);
    n->func = func;
    n->args = args;
    return n;
}

ASTList *ast_list_prepend(ASTNode *node, ASTList *rest) {
    ASTList *l = (ASTList *)malloc(sizeof(ASTList));
    l->node = node;
    l->next = rest;
    return l;
}

ASTList *ast_list_append_single(ASTNode *node) {
    return ast_list_prepend(node, NULL);
}

ASTList *ast_list_reverse(ASTList *list) {
    ASTList *prev = NULL;
    while (list) {
        ASTList *next = list->next;
        list->next = prev;
        prev = list;
        list = next;
    }
    return prev;
}

const char *ast_kind_name(NodeKind k) {
    switch (k) {
        case NODE_PROGRAM: return "Program";
        case NODE_SEQUENCE_DECL: return "SequenceDecl";
        case NODE_ASSIGNMENT: return "Assignment";
        case NODE_PRINT: return "Print";
        case NODE_EXPR_STMT: return "ExprStmt";
        case NODE_COMPARE: return "Compare";
        case NODE_IDENTIFIER: return "Identifier";
        case NODE_STRING_LITERAL: return "StringLiteral";
        case NODE_FUNC_CALL: return "FuncCall";
    }
    return "?";
}

const char *ast_func_name(FuncKind f) {
    switch (f) {
        case FUNC_GC_CONTENT: return "GC_CONTENT";
        case FUNC_LENGTH: return "LENGTH";
        case FUNC_FIND_MOTIF: return "FIND_MOTIF";
        case FUNC_REVERSE_COMPLEMENT: return "REVERSE_COMPLEMENT";
        case FUNC_REVERSE: return "REVERSE";
        case FUNC_COMPLEMENT: return "COMPLEMENT";
        case FUNC_TRANSLATE: return "TRANSLATE";
    }
    return "?";
}
