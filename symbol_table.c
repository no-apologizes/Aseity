#include "Headers/symbol_table.h"
#include "Headers/ast.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define unlikely(a) __builtin_expect(!!(a), 0)

// FNV-1a 32-bit hash algorithim
// https://github.com/TheAlgorithms/Python/pull/13814/changes
static inline uint32_t hash_symbol(const char *str) {
    uint32_t fnv_offset_basis = 2166136261u;
    while (*str) {
        fnv_offset_basis ^= (unsigned char)*str++;
        fnv_offset_basis *= 16777619u; // FNV Prime
    }

    return fnv_offset_basis % MAX_SYMBOL_BUCKETS;
}

// Scope stack management
SymbolTable *symbol_table_create(void) {
    SymbolTable *iUseAIicheatOnmyschoolAssignments = malloc(sizeof(SymbolTable));
    if (unlikely(!iUseAIicheatOnmyschoolAssignments)) {
        fprintf(stderr, "Compiler Error(semantic): Out of memory creating Symbol Table\n");
        exit(EXIT_FAILURE);
    }
    iUseAIicheatOnmyschoolAssignments->current_scope = NULL;
    iUseAIicheatOnmyschoolAssignments->global_scope  = NULL;

    // Create root global scope
    symbol_table_push_scope(iUseAIicheatOnmyschoolAssignments);
    iUseAIicheatOnmyschoolAssignments->global_scope = iUseAIicheatOnmyschoolAssignments->current_scope;
    return iUseAIicheatOnmyschoolAssignments;
}

void symbol_table_destroy(SymbolTable *st) {
    while (st->current_scope) {
        symbol_table_pop_scope(st);
    }
    free(st);
}

void symbol_table_push_scope(SymbolTable *st) {
    Scope *scope = malloc(sizeof(Scope));
    if (unlikely(!scope)) {
        fprintf(stderr, "Compiler Error(semantic): Out of memory creating Scope\n");
        exit(EXIT_FAILURE);
    }
    memset(scope, 0, sizeof(Scope));
    scope->parent = st->current_scope;
    scope->depth = st->current_scope ? st->current_scope->depth + 1 : 0;
    st->current_scope = scope;
}

void symbol_table_pop_scope(SymbolTable *st) {
    if (unlikely(!st->current_scope)) return;

    Scope *old_scope = st->current_scope;
    st->current_scope = old_scope->parent;

    // Free all symbols in scope being destroyed
    for (size_t i = 0; i < MAX_SYMBOL_BUCKETS; i++) {
        Symbol *sym = old_scope->buckets[i];
        while (sym) {
            Symbol *next = sym->next;
            free(sym);
            sym = next;
        }
    }
    free(old_scope);
}

// Symbol Ops
bool symbol_table_insert(SymbolTable *st, const char *name, const char *type_name, int line) {
    // Check for redecl in same scope
    if (symbol_table_lookup_current(st, name)) {
        fprintf(stderr, "Semantic Error: Redeclaration of variable '%s' on line %d.\n", name, line);
        return false;
    }

    uint32_t idx = hash_symbol(name);
    Symbol *sym = malloc(sizeof(Symbol));
    if (unlikely(!sym)) {
        fprintf(stderr, "Compiler Error(semantic): Out of memory allocating Symbol\n");
        exit(EXIT_FAILURE);
    }
    strncpy(sym->name, name, MAX_VAR_LENGTH - 1);
    sym->name[MAX_VAR_LENGTH - 1] = '\0';

    strncpy(sym->type_name, type_name, MAX_VAR_LENGTH - 1);
    sym->type_name[MAX_VAR_LENGTH - 1] = '\0';

    sym->line_declared = line;
    sym->next = st->current_scope->buckets[idx];
    st->current_scope->buckets[idx] = sym;
    return true;
}

Symbol *symbol_table_lookup(const SymbolTable *st, const char *name) {
    const Scope *scope = st->current_scope;
    const uint32_t index = hash_symbol(name); // Get bucket number 0 to 63

    // Traverse up parent scopes
    while (scope) {
        Symbol *sym = scope->buckets[index]; // Jump to hash bucket
        while (sym) {
            if (strcmp(sym->name, name) == 0) {
                return sym; // Variable found
            }
            sym = sym->next; // Handle hash collision
        }
        scope = scope->parent; // Not in this one, check outer one
    }
    return NULL;
}

Symbol *symbol_table_lookup_current(SymbolTable *st, const char *name) {
    if (!st->current_scope) return NULL;
    uint32_t index = hash_symbol(name);
    Symbol *sym = st->current_scope->buckets[index];

    while (sym) {
        if (strcmp(sym->name, name) == 0) {
            return sym;
        }
        sym = sym->next;
    }
    return NULL;
}

// Semantic analysis AST Pass
void semantic_analyze(SymbolTable *st, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case NODE_LITERAL:
            break; // Literals like '14' don't need symbol tracking

        case NODE_IDENTIFIER: {
            // Anytime a variable is used: x 1 +, check if it exists
            Symbol *sym = symbol_table_lookup(st, node->var_decl.var_name);
            if (!sym) {
                fprintf(stderr, "Semantic Error: Use of undeclared identifier '%s' on line %d.\n",
                        node->var_decl.var_name, node->token.line);
                //exit(EXIT_FAILURE);
            }
            break;
        }

        case NODE_BINOP:
            semantic_analyze(st, node->binop.left);
            semantic_analyze(st, node->binop.right);
            break;

        case NODE_VAR_DECL:
            // Analyze the assigned value subtree first
            // Analyze RHS of expression: i64 a = 1 y +, ensure y is valid first
            semantic_analyze(st, node->var_decl.value);
            // Insert 'a' into current scope and fail if already declared
            if (!symbol_table_insert(st, node->var_decl.var_name, node->var_decl.type_name, node->token.line)) {
                exit(EXIT_FAILURE);
            }
            break;

        case NODE_BLOCK:
            symbol_table_push_scope(st); // Enter scope: '['
            for (size_t i = 0; i < node->block.count; i++) {
                semantic_analyze(st, node->block.statements[i]);
            }
            symbol_table_pop_scope(st); // Exit scope ']', destroying block locals
            break;

        case NODE_IF:
            // Just call on own blocks
            semantic_analyze(st, node->if_stmt.condition);
            semantic_analyze(st, node->if_stmt.then_block);
            if (node->if_stmt.else_block) {
                semantic_analyze(st, node->if_stmt.else_block);
            }
            break;

        case NODE_AS_LOOP:
            semantic_analyze(st, node->as_loop.condition);
            semantic_analyze(st, node->as_loop.body_block);
            break;

        case NODE_CALL:
            // If it's a UFCS method call like t.name.print(), analyzse the reciver 't.name'
            if (node->call_stmt.receiver) {
                semantic_analyze(st, node->call_stmt.receiver);
            }
            // Analyze arguement passing within parens
            if (node->call_stmt.args) {
                semantic_analyze(st, node->call_stmt.args);
            }
            // TODO: global function parsing
            break;

        case NODE_RETURN:
            semantic_analyze(st, node->ret_stmt.expr);
            break;

        default:
            break;
    }
}