#include "Headers/type.h"
#include "Headers/arena.h"
#include <string.h>
#include <stdint.h>

#define f_static_inline __attribute__((__always_inline__)) static inline

enum {
    TYPE_POOL_INITIAL_BUCKETS = 256,
    NUM_PRIM_TYPES = (TYPE_PTR), // Everything before the first complex type
};

typedef struct {
    uint32_t name_id;
    Type *type;
} StructField;

struct Type {
    TypeKind kind;
    // 4 Bytes padding
    size_t size_bytes;
    size_t align_bytes;
    bool hollow;
    union {
        struct {
            Type *pointee;
        } pointer;
        struct {
            Type *element;
            size_t length;
        } array;
        struct {
            uint32_t name_id;
            uint32_t field_count; // While uint16_t would work and never be reached,
            // code generators produce massive structs, so 65535 isn't a safe number
            StructField *fields;
            bool is_complete;
        } structure;
        struct {
            Type *return_type;
            uint16_t param_count; // uint16_t is fine here because you don't need more than 65k params
            // and Java's JVM even limits it to 255
            Type **params;
        } function;
    };
};

// Each hash bucket is a linked list of these, the entry owns a real type by value, so it never moves
typedef struct TypePoolEntry {
    Type type;
    struct TypePoolEntry *next;
} TypePoolEntry;

// Small, fixed set of these, so a lookup table beats a hash table
static Type prim_types[NUM_PRIM_TYPES];

// Complex types, hash-consed and one shared table for all four kinds because they're all accessed the same way
static TypePoolEntry **compound_buckets;
static size_t compound_bucket_count;

void type_init(void) {
    prim_types[TYPE_UNIT]   = (Type){.kind = TYPE_UNIT};
    prim_types[TYPE_NEVER]  = (Type){.kind = TYPE_NEVER};
    prim_types[TYPE_VILE]   = (Type){.kind = TYPE_VILE};

    prim_types[TYPE_U8]   = (Type){.kind = TYPE_U8,   .size_bytes = 1,  .align_bytes = 1};
    prim_types[TYPE_I8]   = (Type){.kind = TYPE_I8,   .size_bytes = 1,  .align_bytes = 1};
    prim_types[TYPE_U16]  = (Type){.kind = TYPE_U16,  .size_bytes = 2,  .align_bytes = 2};
    prim_types[TYPE_I16]  = (Type){.kind = TYPE_I16,  .size_bytes = 2,  .align_bytes = 2};
    prim_types[TYPE_U32]  = (Type){.kind = TYPE_U32,  .size_bytes = 4,  .align_bytes = 4};
    prim_types[TYPE_I32]  = (Type){.kind = TYPE_I32,  .size_bytes = 4,  .align_bytes = 4};
    prim_types[TYPE_F32]  = (Type){.kind = TYPE_F32,  .size_bytes = 4,  .align_bytes = 4};
    prim_types[TYPE_U64]  = (Type){.kind = TYPE_U64,  .size_bytes = 8,  .align_bytes = 8};
    prim_types[TYPE_I64]  = (Type){.kind = TYPE_I64,  .size_bytes = 8,  .align_bytes = 8};
    prim_types[TYPE_F64]  = (Type){.kind = TYPE_F64,  .size_bytes = 8,  .align_bytes = 8};
    prim_types[TYPE_U128] = (Type){.kind = TYPE_U128, .size_bytes = 16, .align_bytes = 16};
    prim_types[TYPE_I128] = (Type){.kind = TYPE_I128, .size_bytes = 16, .align_bytes = 16};
    prim_types[TYPE_F128] = (Type){.kind = TYPE_F128, .size_bytes = 16, .align_bytes = 16};

    prim_types[TYPE_BOOL] = (Type){.kind = TYPE_BOOL, .size_bytes = 1,  .align_bytes = 1};
    prim_types[TYPE_STR]  = (Type){.kind = TYPE_STR,  .size_bytes = 16, .align_bytes = 8}; // ptr + length, undecided
    prim_types[TYPE_CHAR] = (Type){.kind = TYPE_CHAR, .size_bytes = 4,  .align_bytes = 4}; // Room for a full unicode codepoint

    compound_bucket_count = TYPE_POOL_INITIAL_BUCKETS;
    compound_buckets = arena_alloc_bump(&ast_arena, compound_bucket_count * sizeof(TypePoolEntry*));
    memset(compound_buckets, 0, compound_bucket_count * sizeof(TypePoolEntry*));
}

f_static_inline uint32_t fnv_hash(const uint8_t *data, const size_t length) {
    uint32_t fnv_offset_basis = 2166136261U;
    for (size_t i = 0; i < length; i++) {
        fnv_offset_basis ^= (unsigned char)data[i];
        fnv_offset_basis *= 16777619u; // FNV Prime
    }
    return fnv_offset_basis;
}

f_static_inline uint32_t hash_ptr(const void *ptr) {
    return fnv_hash((const uint8_t*)&ptr, sizeof(ptr));
}

f_static_inline uint32_t type_hash(const Type *t) {
    uint32_t h = (uint32_t)t->kind * 2654435761U; // Knuth's prime multiplicative hashing multiplier for 32-bit number
    switch (t->kind) {
    case TYPE_PTR: {
        h ^= hash_ptr(t->pointer.pointee);
        h ^= (uint32_t)t->pointer.nullable;
        break;
    }
    case TYPE_ARRAY: {
        h ^= hash_ptr(t->array.element);
        h ^= (uint32_t)t->array.length;
        break;
    }
    case TYPE_STRUCT: {
        h ^= t->structure.name_id; // ONLY name, a struct's identity is its name: nominal typing
        break;
    }
    case TYPE_FUNCTION: {
        h ^= hash_ptr(t->function.return_type);
        for (uint16_t andie = 0; andie < t->function.param_count; andie++) {
            h ^= hash_ptr(t->function.params[andie]) * (uint32_t)(andie + 1); // *(i+1) so param order matters, XOR is commutative, so 'i64, bool' and 'bool, i64' would hash to the same thing, but multiplying each param by its position changes what they hash to
        }
    }
    break;
    default: break;
    }
    return h;
}

f_static_inline Type *compound_lookup_or_intern(const Type *query) {
    const size_t bucket = type_hash(query) % compound_bucket_count;
    for (TypePoolEntry *e = compound_buckets[bucket]; e; e = e->next) {
        if (type_is_equal(&e->type, query)) { return &e->type; }
    }

    // If it's new, link it in
    TypePoolEntry *entry = arena_alloc_bump(&ast_arena, sizeof(TypePoolEntry));
    entry->type = *query;
    entry->next = compound_buckets[bucket];
    compound_buckets[bucket] = entry;
    return &entry->type;
}

Type *type_get_prim(const TypeKind kind) { return &prim_types[kind]; }

Type *type_intern_ptr(Type *pointee, const bool nullable) {
    Type query = {.kind = TYPE_PTR, .size_bytes = 8, .align_bytes = 8}; // Pointer width, fixed regradless of pointee
    query.pointer.pointee = pointee;
    query.pointer.nullable = nullable;
    return compound_lookup_or_intern(&query);
}

Type *type_intern_array(Type *element, size_t length) {
    Type query = {.kind = TYPE_ARRAY};
    query.array.element = element;
    query.array.length = length;
    query.size_bytes = element->size_bytes * length;
    query.align_bytes = element->align_bytes;
    return compound_lookup_or_intern(&query);
}

Type *type_intern_function(Type *return_type, const uint16_t param_count, Type **params) {
    // Never store caller's array pointer directly,
    // as it might be short-lived, so copy into this arena, so it outlives whatever called this
    Type **owned_params = NULL;
    if (param_count > 0) {
        owned_params = arena_alloc_bump(&ast_arena, param_count * sizeof(Type*));
        memcpy(owned_params, params, param_count * sizeof(Type*));
    }

    Type query = {.kind = TYPE_FUNCTION};
    query.function.return_type = return_type;
    query.function.param_count = param_count;
    query.function.params = owned_params;
    return compound_lookup_or_intern(&query);
}

Type *type_struct_declare(const uint32_t name_id, bool *out_already_declared) {
    // Build a probe with just enough set,
    // so a real struct of this would land in the same bucket
    Type probe = {.kind = TYPE_STRUCT};
    probe.structure.name_id = name_id;
    const size_t bucket = type_hash(&probe) % compound_bucket_count;

    for (TypePoolEntry *e = compound_buckets[bucket]; e; e = e->next) {
        if (e->type.kind == TYPE_STRUCT && e->type.structure.name_id == name_id) {
            // Already exists, could be a valid forward-reference or declaration
            *out_already_declared = true;
            return &e->type;
        }
    }

    // New, insert placeholder before any fields exist, this is what makes self-referential structs possible
    *out_already_declared = false;
    Type query = {.kind = TYPE_STRUCT};
    query.structure.name_id = name_id;
    query.structure.is_complete = false;

    TypePoolEntry *entry = arena_alloc_bump(&ast_arena, sizeof(TypePoolEntry));
    entry->type = query;
    entry->next = compound_buckets[bucket];
    compound_buckets[bucket] = entry;
    return &entry->type;
}

void type_struct_complete(Type *struct_type, const uint32_t field_count, StructField *fields) {
    size_t total_size = 0;
    size_t max_align = 1;
    for (uint32_t i = 0; i < field_count; i++) {
        const size_t falign = fields[i].type->align_bytes;
        total_size = (total_size + falign - 1) & ~(falign - 1); // Round up to falign
        total_size += fields[i].type->size_bytes;
        if (falign > max_align) { max_align = falign; }
    }
    total_size = (total_size + max_align - 1) & ~(max_align - 1); // Final round-up

    // Mutate the same type type_struct_declare already interned
    struct_type->structure.field_count = field_count;
    struct_type->structure.fields = fields;
    struct_type->structure.is_complete = true;
    struct_type->size_bytes = total_size;
    struct_type->align_bytes = max_align;
}

bool type_is_equal(const Type *a, const Type *b) {
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case TYPE_PTR: {
            // Valid only because pointee was interned before this type was built.
            // If type_intern_ptr(i64_type, false) was called twice,
            // The first one would go though and not find an entry, and create one, and returns its address.
            // The second would do the same and hash to the same exact bucket, and it walks until one of its entries also passes type_is_equal
            // same bucket && passes type_is_equal == same returned address
            return a->pointer.pointee == b->pointer.pointee && a->pointer.nullable == b->pointer.nullable;
        }
        case TYPE_ARRAY: {
            return a->array.element == b->array.element && a->array.length == b->array.length;
        }
        case TYPE_STRUCT: {
            return a->structure.name_id == b->structure.name_id;
        }
        case TYPE_FUNCTION: {
            if (a->function.return_type != b->function.return_type) { return false; }
            if (a->function.param_count != b->function.param_count) { return false; }
            for (uint16_t andie = 0; andie < a->function.param_count; andie++) {
                if (a->function.params[andie] != b->function.params[andie]) { return false; }
            }
            return true;
        }
        default: return true;
    }
}