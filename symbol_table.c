#include "Headers/symbol_table.h"
#include "Headers/arena.h"
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
    SymbolTable *iUseAIicheatOnmyschoolAssignments = arena_alloc_recyclable(&symbol_arena, sizeof(SymbolTable));
    iUseAIicheatOnmyschoolAssignments->current_scope = NULL;
    iUseAIicheatOnmyschoolAssignments->global_scope  = NULL;
    iUseAIicheatOnmyschoolAssignments->struct_registry = NULL;

    symbol_table_push_scope(iUseAIicheatOnmyschoolAssignments);
    iUseAIicheatOnmyschoolAssignments->global_scope = iUseAIicheatOnmyschoolAssignments->current_scope;
    return iUseAIicheatOnmyschoolAssignments;
}

void symbol_table_destroy(SymbolTable *st) {
    while (st->current_scope) {
        symbol_table_pop_scope(st);
    }
    arena_free_recyclable(&symbol_arena, st, sizeof(SymbolTable));
}

void symbol_table_push_scope(SymbolTable *st) {
    Scope *scope = arena_alloc_recyclable(&symbol_arena, sizeof(Scope));
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
            arena_free_recyclable(&symbol_arena, sym, sizeof(Symbol));
            sym = next;
        }
    }
    arena_free_recyclable(&symbol_arena, old_scope, sizeof(Scope));
}

// Symbol Ops
bool symbol_table_insert(SymbolTable *st, const char *name, const char *type_name, int line) {
    if (!st || !st->current_scope) return false;

    // Check ONLY the current active scope so child blocks can shadow outer variables
    Symbol *existing = symbol_table_lookup_current(st, name);
    if (existing) {
        // Update type and line if re-declared/assigned in the same scope
        strncpy(existing->type_name, type_name, MAX_VAR_LENGTH - 1);
        existing->type_name[MAX_VAR_LENGTH - 1] = '\0';
        return true;
    }

    uint32_t idx = hash_symbol(name);
    Symbol *sym = arena_alloc_recyclable(&symbol_arena, sizeof(Symbol));

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
// So, because semantic analysis is a hierachal tree walk the control has to return to the parent node after validating its children, the analysis relies on std C func calls (call and ret instructions)
// The tiny cost of dispatching the node inside a recursive function doesn't remove the recursive calls
// And direct threading doesn't work on trees as lexing and parsing are just linear streams
// *disgrading the fact that gcc with -03 automatically lowers a switch (node->type) into a hardware jump table anyways.
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

        case NODE_FUNC_DECL:
            // Register function in global scope
            if (!symbol_table_insert(st, node->func_decl.func_name, node->func_decl.return_type, node->token.line)) exit(EXIT_FAILURE);

            symbol_table_push_scope(st); // Enter func scope
            semantic_analyze(st, node->func_decl.params); // Loads params into scope

            ASTNode *body = node->func_decl.body;
            for (size_t i = 0; i < body->block.count; i++) {
                semantic_analyze(st, body->block.statements[i]);
            }
            symbol_table_pop_scope(st); // Exit function scope
            break;

        case NODE_PARAM_LIST:
            // Insert parameter vars into active scope
            for (size_t i = 0; i < node->param_list.count; i++) {
                ASTNode *param = node->param_list.params[i];
                if (param->type == NODE_VAR_DECL) {
                    if (!symbol_table_insert(st, param->var_decl.var_name, param->var_decl.type_name, param->token.line)) exit(EXIT_FAILURE);
                }
            }
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

        case NODE_STRUCT_DECL: {
            StructDef *sdef = arena_alloc_recyclable(&symbol_arena, sizeof(StructDef));
            strncpy(sdef->name, node->struct_decl.struct_name, MAX_VAR_LENGTH - 1);
            sdef->total_size = 0;
            sdef->fields = NULL;
            sdef->next = st->struct_registry;
            st->struct_registry = sdef;

            FieldDef **current_field = &sdef->fields;
            ASTNode *fields_block = node->struct_decl.fields;

            for (size_t i = 0; i < fields_block->block.count; i++) {
                ASTNode *field_decl = fields_block->block.statements[i];
                if (field_decl->type == NODE_VAR_DECL) {
                    FieldDef *f = arena_alloc_recyclable(&symbol_arena, sizeof(FieldDef));
                    strncpy(f->name, field_decl->var_decl.var_name, MAX_VAR_LENGTH - 1);
                    strncpy(f->type_name, field_decl->var_decl.type_name, MAX_VAR_LENGTH - 1);

                    // Align to 8-byte boundaries for System V x86_64
                    size_t align = 8;
                    sdef->total_size = (sdef->total_size + align - 1) & ~(align - 1);
                    f->offset = sdef->total_size;

                    // Assume 8 bytes for all fields during bootstrap (i64 / ptrs)
                    sdef->total_size += 8;

                    *current_field = f;
                    current_field = &f->next;
                }
            }
            // Final alignment padding
            sdef->total_size = (sdef->total_size + 7) & ~(size_t)7;
            break;
        }

        case NODE_MEMBER_ACCESS: {
            // Verify the base instance variable actually exists in the local scope
            Symbol *sym = symbol_table_lookup(st, node->member_access.instance_name);
            if (!sym) {
                fprintf(stderr, "Semantic Error: Undeclared instance '%s' on line %d.\n",
                        node->member_access.instance_name, node->token.line);
                exit(EXIT_FAILURE);
            }
            break;
        }

        case NODE_RETURN:
            semantic_analyze(st, node->ret_stmt.expr);
            break;

        default:
            break;
    }
}