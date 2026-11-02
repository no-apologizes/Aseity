#pragma once
#include "type.h"

typedef struct Symbol {
    uint32_t name_id;
    Type *type;
    bool is_const;
    uint32_t line, col;
} Symbol;

enum { SCOPE_INLINE_CAPACITY = 62 };

typedef struct SymbolPoolEntry {
    Symbol symbol;
    struct SymbolPoolEntry *next;
} SymbolPoolEntry;

typedef struct Scope {
    struct Scope *parent;

    // Non-global scopes only, fixed capacity array
    Symbol *symbols[SCOPE_INLINE_CAPACITY];
    uint32_t symbol_count;

    // Global/file scope only, hash table and NULL/0 on every other scope
    SymbolPoolEntry **hash_buckets;
    size_t hash_bucket_count;
} Scope;

typedef struct SymbolTable {
    Scope *global_scope;
    Scope *current_scope;
} SymbolTable;

typedef enum {
    SYMBOL_DECLARE_OK,
    SYMBOL_DECLARE_ERR_DUPLICATION,    // Same scope redeclaration, regardless of shadowing

    SYMBOL_DECLARE_ERR_NEEDS_SHADOW,   // Would shadow outside scope but no 'shadow'
    SYMBOL_A_LONE_SHADOW,              // For a shadow without an owner
    SYMBOL_DECLARE_ERR_SCOPE_FULL,     // Hit SCOPE_INLINE_CAPACITY in non-global scope
    SYMBOL_DECLARE_ERR_NAME_IS_STRUCT, // name_id already names a struct
} SymbolDeclareResult;

void symbol_table_init(SymbolTable *st);
void scope_push(SymbolTable *st);
void scope_pop(SymbolTable *st);

//
SymbolDeclareResult symbol_declare(SymbolTable *st, uint32_t name_id, Type *type,
                                    bool is_const, bool shadow_explicit,
                                    uint32_t line, uint32_t col, Symbol **out_symbol);

// Walks current scope -> parent -> ... -> global
Symbol *symbol_lookup(SymbolTable *st, uint32_t name_id);

// Used by struct_declare to check for declaration of var or func of an existing name
Symbol *symbol_lookup_global(SymbolTable *st, uint32_t name_id);

// This is sooooo ugly
typedef  void (*SymbolVisitFn)(Symbol *symbol, void *user_data);
void scope_for_each(const Scope *scope, SymbolVisitFn fn, void *user_data);