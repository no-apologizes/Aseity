#include "Headers/lexer.h"
#include "Headers/parser.h"
#include "Headers/ast.h"
#include "Headers/symbol_table.h"
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

        default:
            printf("UNKNOWN NODE\n");
            break;
    }
}

int main(void) {
    const char *test_script =
        "str msg = \"Data payload: \\\"Aseity\\\"\\n\" |\n"
        "bool flag = 'ℝ' |\n"
        "population 2 * return |\n"
        "as (x 6 <) [\n"
        "   print(ℕ)|\n"
        "   if (x 5 -) [\n"
        "      0 return|\n"
        "   ]|\n"
        "   i64 x = x 1 +|\n"
        "]|";

    printf("≛≛≛ Test Script ≛≛≛\n%s\n", test_script); // Star equals :)
    printf("≛≛≛ AST Parser Output ≛≛≛\n\n");

    lexer_init(test_script);

    SymbolTable *st = symbol_table_create();
    ASTNode *node = NULL;
    //int statement_count = 1;

    while ((node = parse_next_statement()) != NULL) {
        // Run semantic pass over parsed statement
        semantic_analyze(st, node);

        // Print and free AST
        print_ast(node, 0);
        free_ast(node);
    }

    return 0;
}