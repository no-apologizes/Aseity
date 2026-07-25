#include "Headers/lexer.h"
#include "Headers/parser.h"
#include "Headers/ast.h"
#include "Headers/symbol_table.h"
#include "Headers/arena.h"
#include <stdio.h>

static void print_ast(const ASTNode *node, int indent) {
    if (!node) return;

    for (int i = 0; i < indent; i++) printf("  ");

    switch (node->type) {
        case NODE_LITERAL:
            printf("LITERAL: %.*s\n", (int)node->token.length, node->token.start);
            break;

        case NODE_IDENTIFIER:
            printf("IDENTIFIER: %s\n", node->var_decl.var_name);
            break;

        case NODE_BINOP:
            printf("BINOP (Op Type: %d)\n", node->binop.op_type);
            print_ast(node->binop.left, indent + 1);
            print_ast(node->binop.right, indent + 1);
            break;

        case NODE_VAR_DECL:
            printf("VAR_DECL [Type: %s, Name: %s]\n", node->var_decl.type_name, node->var_decl.var_name);
            print_ast(node->var_decl.value, indent + 1);
            break;

        case NODE_BLOCK:
            printf("BLOCK (%zu statements):\n", node->block.count);
            for (size_t i = 0; i < node->block.count; i++) {
                print_ast(node->block.statements[i], indent + 1);
            }
            break;

        case NODE_IF:
            printf("IF STATEMENT:\n");
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Condition:\n");
            print_ast(node->if_stmt.condition, indent + 2);
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Then Block:\n");
            print_ast(node->if_stmt.then_block, indent + 2);
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Else Block:\n");
            print_ast(node->if_stmt.else_block, indent + 2);
            break;

        case NODE_AS_LOOP:
            printf("AS LOOP:\n");
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Condition:\n");
            print_ast(node->as_loop.condition, indent + 2);
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Body:\n");
            print_ast(node->as_loop.body_block, indent + 2);
            break;

        case NODE_RETURN:
            printf("RETURN:\n");
            print_ast(node->ret_stmt.expr, indent + 1);
            break;

        case NODE_FUNC_DECL:
            printf("FUNC_DECL [Returns: %s, Name: %s]\n", node->func_decl.return_type, node->func_decl.func_name);
            print_ast(node->func_decl.params, indent + 1);
            print_ast(node->func_decl.body, indent + 1);
            break;

        case NODE_PARAM_LIST:
            printf("PARAMS (%zu):\n", node->param_list.count);
            for (size_t i = 0; i < node->param_list.count; i++) {
                print_ast(node->param_list.params[i], indent + 1);
            }
            break;

        case NODE_CALL: // Fixes the UNKNOWN NODE 'bug'
            printf("CALL: %s()\n", node->call_stmt.func_name);
            if (node->call_stmt.receiver) print_ast(node->call_stmt.receiver, indent + 1);
            print_ast(node->call_stmt.args, indent + 1);
            break;

        default:
            printf("UNKNOWN NODE\n");
            break;
    }
}

int main(void) {
    arena_init(&ast_arena);
    arena_init(&symbol_arena);

    const char *test_script =
        "str msg = \"Data payload: \\\"Aseity\\\"\\n\" |\n"
        "bool flag = 'ℝ' |\n"
        "i64 add(a: i64, b: i64) [\n"
        "   i64 c = a b +|\n"
        "   c return|"
        "]"
        "i64 x = 1|\n"
        "as (x 6 <) [\n"
        "   add(x 1)|\n"
        "   2 2 add()|\n"
        "   2.add(2)|\n"
        "   i64 x = x 1 +|\n"
        "]|";

    printf("=== Test Script ===\n%s\n", test_script);
    printf("=== AST Parser Output ===\n\n");

    lexer_init(test_script);

    SymbolTable *st = symbol_table_create();
    ASTNode *node = NULL;

    while ((node = parse_next_statement()) != NULL) {
        semantic_analyze(st, node);
        print_ast(node, 0);
        // Memory is automatically reclaimed by the arena
    }

    // Teardown arenas on program exit
    arena_destroy(&symbol_arena);
    arena_destroy(&ast_arena);

    return 0;
}