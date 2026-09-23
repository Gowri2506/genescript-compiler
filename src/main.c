/*
 * main.c -- GeneScript compiler driver ("gsc"), pure C.
 *
 * Pipeline:
 *   source.gs --[flex+bison OR hand-written recursive-descent]--> AST
 *            --[optimizer.c: domain rewrites + CSE]--> optimized AST
 *            --[codegen.c, LLVM-C API]--> LLVM Module
 *            --[this file]--> .ll (text IR) + .bc (bitcode)
 *            --[llc, LLVM's backend]--> .s (assembly) + .o (object code)
 *            --[clang]--> linked executable (linked against runtime.o)
 *
 * This ties together Lab Experiments 1-3, 5-7, 9, 10 in one runnable
 * tool. See README.md for the full experiment-by-experiment mapping.
 *
 * Usage:
 *   gsc <file.gs> [--frontend=bison|rd] [--emit-llvm] [--no-run] [-o NAME]
 *                 [--no-opt] [--show-opt]
 *                 [--dump-tokens] [--dump-ast] [--dump-tac] [--dump-symtab]
 */
#include "ast.h"
#include "codegen.h"
#include "optimizer.h"
#include "dump.h"

#include <llvm-c/Core.h>
#include <llvm-c/BitWriter.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

extern FILE *yyin;
extern int yyparse(void);
extern ASTNode *gs_ast_root;
extern ASTNode *rd_parse_program(void);

static void strip_ext(const char *path, char *out, size_t outsz) {
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, base, len);
    out[len] = '\0';
}

/* Find runtime.o next to the gsc executable itself, so gsc works no
   matter which directory it is run from. Falls back to ./runtime.o. */
static void find_runtime(const char *argv0, char *out, size_t outsz) {
    char exe[1024];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1); /* Linux / WSL */
    if (n > 0) exe[n] = '\0';
    else { strncpy(exe, argv0, sizeof(exe) - 1); exe[sizeof(exe) - 1] = '\0'; }

    char *slash = strrchr(exe, '/');
    if (slash) {
        *slash = '\0';
        snprintf(out, outsz, "%s/runtime.o", exe);
        if (access(out, R_OK) == 0) return;
    }
    snprintf(out, outsz, "runtime.o");
}

/* system() returns a wait status; turn it into a plain exit code. */
static int exit_code_of(int status) {
    if (status == -1) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "Usage: %s <file.gs> [options]\n"
            "  --frontend=bison|rd  parser to use (default: bison)\n"
            "  --no-opt, -O0        skip the optimizer\n"
            "  --show-opt           print every optimization applied\n"
            "  --dump-tokens        print the lexer's token stream\n"
            "  --dump-ast           print the syntax tree\n"
            "  --dump-tac           print three-address code before/after optimization\n"
            "  --dump-symtab        print the symbol table\n"
            "  --emit-llvm          print the generated LLVM IR\n"
            "  --no-run             compile and link, but don't run\n"
            "  -o NAME              name of the output executable\n", argv[0]);
        return 1;
    }

    const char *inputFile = NULL;
    const char *frontend = "bison";
    int emitLLVM = 0;
    int runAfter = 1;
    int optimize = 1, showOpt = 0;
    int dumpTokens = 0, dumpAst = 0, dumpTac = 0, dumpSymtab = 0;
    char outName[512] = "";

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--frontend=", 11) == 0) frontend = a + 11;
        else if (strcmp(a, "--emit-llvm") == 0) emitLLVM = 1;
        else if (strcmp(a, "--no-run") == 0) runAfter = 0;
        else if (strcmp(a, "--no-opt") == 0 || strcmp(a, "-O0") == 0) optimize = 0;
        else if (strcmp(a, "--show-opt") == 0) showOpt = 1;
        else if (strcmp(a, "--dump-tokens") == 0) dumpTokens = 1;
        else if (strcmp(a, "--dump-ast") == 0) dumpAst = 1;
        else if (strcmp(a, "--dump-tac") == 0) dumpTac = 1;
        else if (strcmp(a, "--dump-symtab") == 0) dumpSymtab = 1;
        else if (a[0] == '-' && strcmp(a, "-o") != 0) {
            fprintf(stderr, "Unknown option '%s' (run gsc with no arguments for help)\n", a);
            return 1;
        }
        else if (strcmp(a, "-o") == 0 && i + 1 < argc) { strncpy(outName, argv[++i], sizeof(outName) - 1); }
        else if (a[0] != '-') inputFile = a;
    }

    if (!inputFile) {
        fprintf(stderr, "Error: no input file given\n");
        return 1;
    }

    yyin = fopen(inputFile, "r");
    if (!yyin) {
        fprintf(stderr, "Error: could not open '%s'\n", inputFile);
        return 1;
    }

    if (dumpTokens) dump_tokens(yyin, stdout); /* lexical analysis, on its own */

    ASTNode *root = NULL;
    if (strcmp(frontend, "bison") == 0) {
        if (yyparse() != 0) {
            fprintf(stderr, "Compilation failed (Bison/LALR front end).\n");
            return 1;
        }
        root = gs_ast_root;
    } else if (strcmp(frontend, "rd") == 0) {
        root = rd_parse_program();
    } else {
        fprintf(stderr, "Unknown --frontend value '%s' (use bison or rd)\n", frontend);
        return 1;
    }
    fclose(yyin);

    if (!root) {
        fprintf(stderr, "Compilation failed: no AST produced.\n");
        return 1;
    }

    if (dumpAst) dump_ast(root, stdout);

    /* ---- optimization (AST -> AST) ---- */
    if (dumpTac) {
        printf(optimize ? "== TAC (before optimization) ==\n" : "== TAC ==\n");
        dump_tac(root, stdout);
        printf("\n");
    }
    if (optimize) {
        optimize_program(root, showOpt ? stdout : NULL);
        if (dumpTac) {
            printf("== TAC (after optimization) ==\n");
            dump_tac(root, stdout);
            printf("\n");
        }
    }
    if (dumpSymtab) dump_symtab(root, stdout);
    fflush(stdout);

    char base[512];
    strip_ext(inputFile, base, sizeof(base));
    if (outName[0] == '\0') strncpy(outName, base, sizeof(outName) - 1);

    LLVMContextRef ctx = LLVMContextCreate();
    char moduleName[560];
    snprintf(moduleName, sizeof(moduleName), "%s_module", base);
    LLVMModuleRef module = codegen_program(root, ctx, moduleName);

    if (emitLLVM) {
        char *ir = LLVMPrintModuleToString(module);
        fputs(ir, stdout);
        LLVMDisposeMessage(ir);
    }

    char llPath[600], bcPath[600], sPath[600], oPath[600], exePath[600];
    snprintf(llPath, sizeof(llPath), "%s.ll", base);
    snprintf(bcPath, sizeof(bcPath), "%s.bc", base);
    snprintf(sPath, sizeof(sPath), "%s.s", base);
    snprintf(oPath, sizeof(oPath), "%s.o", base);
    snprintf(exePath, sizeof(exePath), "%s.exe", outName);

    char *err = NULL;
    if (LLVMPrintModuleToFile(module, llPath, &err)) {
        fprintf(stderr, "Could not write %s: %s\n", llPath, err ? err : "unknown error");
        if (err) LLVMDisposeMessage(err);
        return 1;
    }
    if (LLVMWriteBitcodeToFile(module, bcPath) != 0) {
        fprintf(stderr, "Could not write %s\n", bcPath);
        return 1;
    }

    fprintf(stderr, "[gsc] wrote LLVM IR:      %s\n", llPath);
    fprintf(stderr, "[gsc] wrote LLVM bitcode: %s\n", bcPath);

    /* Hand the bitcode to LLVM's own backend (llc) to emit real
       assembly text and a real object file -- Experiment 10. */
    char runtimePath[1100];
    find_runtime(argv[0], runtimePath, sizeof(runtimePath));

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "llc %s -o %s", bcPath, sPath);
    if (system(cmd) != 0) { fprintf(stderr, "llc (assembly) failed\n"); return 1; }
    snprintf(cmd, sizeof(cmd), "llc -filetype=obj %s -o %s", bcPath, oPath);
    if (system(cmd) != 0) { fprintf(stderr, "llc (object) failed\n"); return 1; }
    fprintf(stderr, "[gsc] wrote assembly:     %s\n", sPath);
    fprintf(stderr, "[gsc] wrote object code:  %s\n", oPath);

    /* Link against the runtime library and (optionally) run it. */
    snprintf(cmd, sizeof(cmd), "clang -no-pie %s %s -o %s", oPath, runtimePath, exePath);
    if (system(cmd) != 0) {
        fprintf(stderr, "Linking failed (could not link against %s -- run `make` first)\n", runtimePath);
        return 1;
    }
    fprintf(stderr, "[gsc] linked executable:  %s\n\n", exePath);

    LLVMDisposeModule(module);
    LLVMContextDispose(ctx);

    if (runAfter) {
        /* "./name.exe" for a bare name; use the path as-is if it already
           has a directory in it (e.g. -o /tmp/out or -o build/out). */
        if (strchr(exePath, '/')) snprintf(cmd, sizeof(cmd), "%s", exePath);
        else snprintf(cmd, sizeof(cmd), "./%s", exePath);
        fflush(stdout);
        return exit_code_of(system(cmd)); /* pass the program's exit code through */
    }

    return 0;
}
