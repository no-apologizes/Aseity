#include "Headers/lexer.h"
#include "Headers/parser.h"
#include "Headers/ast.h"
#include "Headers/symbol_table.h"
#include "Headers/arena.h"
#include "Headers/ir.h"
#include "Headers/ir_gen.h"
#include "Headers/codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static char* read_file_contents(const char *path) {
    // Open in binary mode ("rb") to get exact byte counts across platforms
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Aseity Driver Error: Could not open source file '%s'\n", path);
        exit(EXIT_FAILURE);
    }

    fseek(file, 0L, SEEK_END);
    const size_t file_size = ftell(file);
    rewind(file);

    // Allocate directly from transient ast_arena
    char *buffer = arena_alloc_transient(&ast_arena, file_size + 1);
    if (!buffer) {
        fprintf(stderr, "Aseity Driver Error: Failed to allocate memory for source file\n");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    const size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}

// Helper function to run the output binary and print exit status
static void run_compiled_binary(const char *cmd, bool show_exit_code) {
    const int status = system(cmd);

    if (status != -1 && WIFEXITED(status)) {
        const int exit_code = WEXITSTATUS(status);
        if (show_exit_code) {
            printf("\nProcess finished with exit code %d\n", exit_code);
        }
    } // No else because it's unreachable
}

int main(const int argc, char **argv) {
    // Guard against no arguments first to prevent segfault
    if (argc < 2) {
        printf("Usage: aseity <file.ase> [options]\n");
        printf("Options:\n");
        printf("  -o <output>    Compile to custom executable name\n");
        printf("  -s             Print returned exit code\n");
        printf("  --emit-ir      Print 3AC Intermediate Representation and exit\n");
        printf("  --run          Compile and immediately execute (default if no -o)\n");
        return 0;
    }

    // Validate file extension: *.ase
    const char *source_path = argv[1];
    const size_t len = strlen(source_path);
    if (len < 4 || strcmp(source_path + len - 4, ".ase") != 0) {
        fprintf(stderr, "Error: Source file '%s' must end with the '.ase' extension\n", source_path);
        return 1;
    }

    const char *output_name = "aseity_out";
    bool print_return_code = false;
    bool run_after_compile = true;
    bool dump_ir_only = false;

    // Parse command line options
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_name = argv[++i];
            run_after_compile = false; // Custom executable specified, don't auto-run
        } else if (strcmp(argv[i], "-s") == 0) {
            print_return_code = true;
        } else if (strcmp(argv[i], "--run") == 0) {
            run_after_compile = true;
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            dump_ir_only = true;
        }
    }

    arena_init(&ast_arena);
    arena_init(&symbol_arena);

    // Read source file into arena
    const char *source_code = read_file_contents(source_path);

    codegen_init("Compiler");
    lexer_init(source_code);

    SymbolTable *st = symbol_table_create();
    symbol_table_insert(st, "print", "intrinsic", 0);

    ASTNode *node = NULL; // Frontend: Lex -> Parse -> Semantics -> 3AC IR

    while ((node = parse_next_statement()) != NULL) {
        semantic_analyze(st, node);

        if (node->type == NODE_FUNC_DECL) {
            const IRFunction *ir_func = ir_gen_function(node);

            // If --emit-ir flag is set, print the 3AC IR for origin
            if (dump_ir_only) {
                ir_print_function(ir_func);
            } else {
                codegen_lower_function(ir_func);
            }
        }
    }

    // Stop compilation here if user requested --emit-ir
    if (dump_ir_only) {
        goto cleanup;
    }

    // Synthesize C @main wrapper that calls Aseity's @origin
    codegen_emit_main_wrapper();
    codegen_optimize_and_print(false);
    codegen_emit_object_and_link(output_name);

    // Execute binary if --run or no -o flag was provided
    if (run_after_compile) {
        char exec_cmd[512];
        snprintf(exec_cmd, sizeof(exec_cmd), "./%s", output_name);
        run_compiled_binary(exec_cmd, print_return_code);

    }

    cleanup:
    codegen_shutdown();
    arena_destroy(&symbol_arena);
    arena_destroy(&ast_arena);

    return 0;
}