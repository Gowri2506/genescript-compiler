#ifndef GENESCRIPT_CODEGEN_H
#define GENESCRIPT_CODEGEN_H

#include "ast.h"
#include <llvm-c/Types.h>

/*
 * Walks the AST (built by either the Bison parser or the hand-written
 * recursive-descent parser -- both produce the same ASTNode tree) and
 * emits an LLVM module containing a `main` function that calls into
 * the runtime library (runtime.c) to perform each operation.
 *
 * Uses LLVM's C API (llvm-c/Core.h) exclusively -- this whole compiler
 * is now pure C, no C++.
 *
 * Exits the process with a semantic-error message if it finds an
 * invalid nucleotide or an undeclared sequence reference.
 */
LLVMModuleRef codegen_program(ASTNode *root, LLVMContextRef ctx, const char *moduleName);

#endif
