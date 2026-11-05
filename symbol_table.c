#include "symbol_table.h"
#include "arena.h"
#include "common.h"
#include <assert.h>
#include <string.h>

enum { GLOBAL_BUCKET_COUNT = 256 }; // Fixed global bucket count

// All scopes besides the global ones are recycled when their scope is popped so nothing can keep a Symbol* or Scope* after a pop
// If a later pass needs local symbols outliving their scopes changes would be switching to bump allocation (these 4 and scope_pop)
f_static_inline Scope *local_scope_alloc(void) {
    return arena_alloc_recyclable(&symbol_arena, sizeof(Scope));
}
f_static_inline void local_scope_free(Scope *scope) {
    arena_free_recyclable(&symbol_arena, scope, sizeof(Scope));
}
f_static_inline Symbol *local_symbol_alloc(void) {
    return arena_alloc_recyclable(&symbol_arena, sizeof(Symbol));
}
f_static_inline void local_symbol_free(Symbol *symbol) {
    arena_free_recyclable(&symbol_arena, symbol, sizeof(Symbol));
}

f_static_inline size_t global_bucket(const Scope *global, const uint32_t name_id) {
    return fnv_hash(&name_id, sizeof(name_id)) % global->hash_bucket_count;
}

// Looks in exactly one scope
static Symbol *scope_find(const Scope *scope, const uint32_t name_id) {
    if (scope->hash_buckets) { // Global scope
        for (SymbolPoolEntry *e = scope->hash_buckets[global_bucket(scope, name_id)]; e; e = e->next) {
            if (e->symbol.name_id == name_id) { return &e->symbol; }
        }
        return NULL;
    }
    for (uint32_t i = scope->symbol_count; i > 0; i--) { // Newest first
        if (scope->symbols[i - 1]->name_id == name_id) { return scope->symbols[i - 1]; }
    }
    return NULL;
}

// Looks in scope all the way to global
static Symbol *find_from(const Scope *scope, const uint32_t name_id) {
    for (; scope; scope = scope->parent) {
        Symbol *found = scope_find(scope, name_id);
        if (found) { return found; }
    }
    return NULL;
}

void symbol_table_init(SymbolTable *st) {
    // Global scope lives throughout the whole compilation
    Scope *global = arena_alloc_bump(&symbol_arena, sizeof(Scope));
    global->parent = NULL;
    global->symbol_count = 0; // symbols[] is unused on the global scope because it stores them in hash_buckets
    global->is_param_scope = false;
    global->hash_bucket_count = GLOBAL_BUCKET_COUNT;
    global->hash_buckets = arena_alloc_bump(&symbol_arena, GLOBAL_BUCKET_COUNT * sizeof(SymbolPoolEntry *));
    memset(global->hash_buckets, 0, GLOBAL_BUCKET_COUNT * sizeof(SymbolPoolEntry *));
    st->global_scope = global;
    st->current_scope = global;
}

static void push_scope(SymbolTable *st, const bool is_param_scope) {
    Scope *scope = local_scope_alloc();
    scope->parent = st->current_scope;
    scope->symbol_count = 0;
    scope->is_param_scope = is_param_scope; // Must be set every time, as recycled memory still holds the old scope
    scope->hash_buckets = NULL;
    scope->hash_bucket_count = 0;
    st->current_scope = scope;
}

void scope_push(SymbolTable *st) { push_scope(st, false); }
void scope_push_params(SymbolTable *st) { push_scope(st, true); }

void scope_pop(SymbolTable *st) {
    Scope *scope = st->current_scope;
    assert(scope != st->global_scope && "scope_pop on the global scope");
    if (unlikely(scope == st->global_scope)) { return; }

    for (uint32_t i = 0; i < scope->symbol_count; i++) {
        local_symbol_free(scope->symbols[i]);
    }
    st->current_scope = scope->parent;
    local_scope_free(scope);
}

SymbolDeclareResult symbol_declare(const SymbolTable *st, const uint32_t name_id, Type *type,
                                    const bool is_const, const bool shadow_explicit,
                                    const uint32_t line, const uint32_t col, Symbol **out_symbol) {
    if (out_symbol) { *out_symbol = NULL; }
    Scope *cur = st->current_scope;

    // Structs are special
    if (type_struct_lookup(name_id) != NULL) { return SYMBOL_DECLARE_ERR_NAME_IS_STRUCT; }

    // Same symbol twice in a scope is duplication
    Symbol *existing = scope_find(cur, name_id);
    if (existing) {
        if (out_symbol) { *out_symbol = existing; }
        return SYMBOL_DECLARE_ERR_DUPLICATION;
    }

    // Any scope shadowing needs an explicit shadow besides the global one
    Symbol *outer = find_from(cur->parent, name_id);
    if (outer && !shadow_explicit) {
        if (out_symbol) { *out_symbol = outer; }
        return SYMBOL_DECLARE_ERR_NEEDS_SHADOW;
    }
    if (!outer && shadow_explicit) { return SYMBOL_A_LONE_SHADOW; }

    if (cur != st->global_scope && cur->symbol_count >= (uint32_t)SCOPE_INLINE_CAPACITY) {
        return SYMBOL_DECLARE_ERR_SCOPE_FULL;
    }

    Symbol *symbol;
    if (cur->hash_buckets) { // Global scope
        SymbolPoolEntry *entry = arena_alloc_bump(&symbol_arena, sizeof(SymbolPoolEntry));
        const size_t bucket = global_bucket(cur, name_id);
        entry->next = cur->hash_buckets[bucket];
        cur->hash_buckets[bucket] = entry;
        symbol = &entry->symbol;
    } else {
        symbol = local_symbol_alloc();
        cur->symbols[cur->symbol_count++] = symbol;
    }
    *symbol = (Symbol){
        .name_id = name_id, .type = type, .is_const = is_const, .line = line, .col = col,
    };
    if (out_symbol) { *out_symbol = symbol; }
    return SYMBOL_DECLARE_OK;
}

Symbol *symbol_lookup(SymbolTable *st, const uint32_t name_id) {
    return find_from(st->current_scope, name_id);
}

Symbol *symbol_lookup_global(const SymbolTable *st, const uint32_t name_id) {
    return scope_find(st->global_scope, name_id);
}

// Linear scan over every param scope in the chain
bool symbol_is_param(const SymbolTable *st, const Symbol *symbol) {
    for (const Scope *scope = st->current_scope; scope; scope = scope->parent) {
        if (!scope->is_param_scope) { continue; }
        for (uint32_t i = 0; i < scope->symbol_count; i++) {
            if (scope->symbols[i] == symbol) { return true; }
        }
    }
    return false;
}

void scope_for_each(const Scope *scope, const SymbolVisitFn fn, void *user_data) {
    if (scope->hash_buckets) { // Bucket order
        for (size_t b = 0; b < scope->hash_bucket_count; b++) {
            for (SymbolPoolEntry *e = scope->hash_buckets[b]; e; e = e->next) {
                fn(&e->symbol, user_data);
            }
        }
        return;
    }
    for (uint32_t i = 0; i < scope->symbol_count; i++) { // Declaration order
        fn(scope->symbols[i], user_data);
    }
}