#pragma once
#include "ast.h"
#include <stddef.h>
// No way I had to learn what hash tables just for this
// Linked list of hash tables
#define MAX_SYMBOL_BUCKETS 64

typedef struct Symbol{
    char name[MAX_VAR_LENGTH];      // 'a'
    char type_name[MAX_VAR_LENGTH]; // 'i64'
    int line_declared;              // Line ∲
    struct Symbol *next;            // Pointer for hash collisions (Open chaining)
} Symbol;

typedef struct Scope {
    Symbol *buckets[MAX_SYMBOL_BUCKETS]; // Hash table array for this specific scope
    struct Scope *parent;
    size_t depth;                        // Tracking nesting depth: 0 = global
} Scope;

typedef struct SymbolTable {
    Scope *current_scope;
    Scope *global_scope;
} SymbolTable;

// Scope stack management
SymbolTable *symbol_table_create(void);
void symbol_table_destroy(SymbolTable *st);
void symbol_table_push_scope(SymbolTable *st);
void symbol_table_pop_scope(SymbolTable *st);

// Symbol Ops
bool symbol_table_insert(SymbolTable *st, const char *name, const char *type_name, int line);
Symbol *symbol_table_lookup(const SymbolTable *st, const char *name);
Symbol *symbol_table_lookup_current(SymbolTable *st, const char *name);

// Semantic analysis AST Pass
void semantic_analyze(SymbolTable *st, ASTNode *node);