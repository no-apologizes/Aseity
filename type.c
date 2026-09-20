#include "Headers/type.h"
#include "Headers/arena.h"
#include <string.h>
#include <stdint.h>

#define f_static_inline __attribute__((__always_inline__)) static inline

typedef struct Type Type;

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
    // 4 bytes of padding
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
// Doubled for hollow, UNIT, NEVER, and VILE still only ever use index [kind][0]
static Type prim_types[NUM_PRIM_TYPES][2];

// Complex types, hash-consed and one shared table for all four kinds because they're all accessed the same way
static TypePoolEntry **compound_buckets;
static size_t compound_bucket_count;

// is hollow before or after the value?
f_static_inline void hollow_wrap_size(const size_t base_size, const size_t base_align,
                                        size_t *out_size, size_t *out_align) {
    const size_t align = base_align > 0 ? base_align : 1;
    const size_t tag_end = (1 + align - 1) & ~(align - 1);
    size_t total = tag_end + base_size;
    total = (total + align - 1) & ~(align - 1); // Final round up for struct alignment
    *out_size = total;
    *out_align = align;
}

// Sets both hollow and non-hollow types
f_static_inline void set_prim(const TypeKind kind, const size_t size_bytes, const size_t align_bytes) {
    prim_types[kind][0] = (Type){.kind = kind, .size_bytes = size_bytes, .align_bytes = align_bytes, .hollow = false};
    size_t hsize, halign;
    hollow_wrap_size(size_bytes, align_bytes, &hsize, &halign);
    prim_types[kind][1] = (Type){.kind = kind, .size_bytes = hsize, .align_bytes = halign, .hollow = true};
}

void type_init(void) {
    // Only one valid kind of these types
    prim_types[TYPE_UNIT][0]  = (Type){.kind = TYPE_UNIT};
    prim_types[TYPE_NEVER][0] = (Type){.kind = TYPE_NEVER};
    prim_types[TYPE_VILE][0]  = (Type){.kind = TYPE_VILE};

    set_prim(TYPE_U8,   1,  1);
    set_prim(TYPE_I8,   1,  1);
    set_prim(TYPE_U16,  2,  2);
    set_prim(TYPE_I16,  2,  2);
    set_prim(TYPE_U32,  4,  4);
    set_prim(TYPE_I32,  4,  4);
    set_prim(TYPE_F32,  4,  4);
    set_prim(TYPE_U64,  8,  8);
    set_prim(TYPE_I64,  8,  8);
    set_prim(TYPE_F64,  8,  8);
    set_prim(TYPE_U128, 16, 16);
    set_prim(TYPE_I128, 16, 16);
    set_prim(TYPE_F128, 16, 16);

    set_prim(TYPE_BOOL, 1,  1);
    set_prim(TYPE_STR,  16, 8); // ptr + length, undecided
    set_prim(TYPE_CHAR, 4,  4); // Room for a full unicode codepoint

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
    // hollow lives on Type's top level now, so fold it in once for ever kind, and is harmless for
    // func/unit/never/vile but give struct and array correct identity without touching their own cases below
    h ^= (uint32_t)t->hollow;
    switch (t->kind) {
    case TYPE_PTR: {
        h ^= hash_ptr(t->pointer.pointee);
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

Type *type_get_prim(const TypeKind kind, const bool hollow) {
    // unit, never, and vile never have a hollow variant
    if (kind == TYPE_UNIT || kind == TYPE_NEVER || kind == TYPE_VILE) {
        return &prim_types[kind][0];
    }
    return &prim_types[kind][hollow ? 1 : 0];
}

Type *type_intern_ptr(Type *pointee, const bool hollow) {
    // Pointer width is fixed regardless of pointee and hollow, as there is a valid bit pattern
    Type query = {.kind = TYPE_PTR, .size_bytes = 8, .align_bytes = 8, .hollow = hollow};
    query.pointer.pointee = pointee;
    return compound_lookup_or_intern(&query);
}

Type *type_intern_array(Type *element, const size_t length, const bool hollow) {
    // Only whole arrays can be hollow, for now...
    Type query = {.kind = TYPE_ARRAY, .hollow = hollow};
    query.array.element = element;
    query.array.length = length;
    const size_t base_size = element->size_bytes * length;
    const size_t base_align = element->align_bytes;
    if (hollow) {
        hollow_wrap_size(base_size, base_align, &query.size_bytes, &query.align_bytes);
    } else {
        query.size_bytes = base_size;
        query.align_bytes = base_align;
    }
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

Type *type_struct_declare(const uint32_t name_id, const bool hollow, bool *out_already_declared) {
    // Build a probe with just enough set,
    // so a real struct of this would land in the same bucket
    Type probe = {.kind = TYPE_STRUCT, .hollow = hollow};
    probe.structure.name_id = name_id;
    const size_t bucket = type_hash(&probe) % compound_bucket_count;

    for (TypePoolEntry *e = compound_buckets[bucket]; e; e = e->next) {
        // hollow is a part of a struct's identity
        if (e->type.kind == TYPE_STRUCT && e->type.structure.name_id == name_id
            && e->type.hollow == hollow) {
            // Already exists, could be a valid forward-reference or declaration
            *out_already_declared = true;
            return &e->type;
        }
    }

    // New, insert placeholder before any fields exist, this is what makes self-referential structs possible
    *out_already_declared = false;
    Type query = {.kind = TYPE_STRUCT, .hollow = hollow};
    query.structure.name_id = name_id;
    query.structure.is_complete = false;

    TypePoolEntry *entry = arena_alloc_bump(&ast_arena, sizeof(TypePoolEntry));
    entry->type = query;
    entry->next = compound_buckets[bucket];
    compound_buckets[bucket] = entry;
    return &entry->type;
}


void type_struct_complete(Type *struct_type, const uint32_t field_count,
                            const uint32_t *field_name_ids, Type **field_types) {
    size_t total_size = 0;
    size_t max_align = 1;
    for (uint32_t i = 0; i < field_count; i++) {
        const size_t falign = field_types[i]->align_bytes;
        total_size = (total_size + falign - 1) & ~(falign - 1); // Round up to falign
        total_size += field_types[i]->size_bytes;
        if (falign > max_align) { max_align = falign; }
    }
    total_size = (total_size + max_align - 1) & ~(max_align - 1); // Final round-up

    // Fields must outlive this call, as the callers array could be short-lived, and
    // StructField is itself private to this file so build the real array here so nothing
    // outside of this needs to know StructField exists
    StructField *fields = arena_alloc_bump(&ast_arena, field_count * sizeof(StructField));
    for (uint32_t i = 0; i < field_count; i++) {
        fields[i].name_id = field_name_ids[i];
        fields[i].type = field_types[i];
    }

    if (struct_type->hollow) {
        hollow_wrap_size(total_size, max_align, &total_size, &max_align);
    }

    // Mutate the same type type_struct_declare already interned
    struct_type->structure.field_count = field_count;
    struct_type->structure.fields = fields;
    struct_type->structure.is_complete = true;
    struct_type->size_bytes = total_size;
    struct_type->align_bytes = max_align;
}

bool type_is_equal(const Type *a, const Type *b) {
    if (a->kind != b->kind) return false;
    if (a->hollow != b->hollow) return false;
    switch (a->kind) {
        case TYPE_PTR: {
            // Valid only because pointee was interned before this type was built.
            // If type_intern_ptr(i64_type, false) was called twice,
            // The first one would go though and not find an entry, and create one, and returns its address.
            // The second would do the same and hash to the same exact bucket, and it walks until one of its entries also passes type_is_equal
            // same bucket && passes type_is_equal == same returned address
            return a->pointer.pointee == b->pointer.pointee;
        }
        case TYPE_ARRAY: {
            return a->array.element == b->array.element && a->array.length == b->array.length;
        }
        case TYPE_STRUCT: {
            return a->structure.name_id == b->structure.name_id;// + hollow, checked already
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
TypeKind type_kind_of(const Type *t) { return t->kind; }
size_t type_size_of(const Type *t) { return t->size_bytes; }
size_t type_align_of(const Type *t) { return t->align_bytes; }
bool type_is_hollow(const Type *t) { return t->hollow; }