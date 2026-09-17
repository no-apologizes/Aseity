#include "Headers/string_pool.h"
#include "Headers/arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define unlikely(a) __builtin_expect(!!(a), 0)
#define f_static_inline __attribute__((__always_inline__)) static inline

enum {
    STRING_POOL_INITIAL_BUCKETS = 256,
    STRING_POOL_INITIAL_HANDLES = 256,
};

typedef struct StringPoolEntry {
    const char *str;
    size_t length;
    uint32_t handle;
    struct StringPoolEntry *next;
} StringPoolEntry;

f_static_inline uint32_t fnv_hash(const char *str, const size_t length) {
    uint32_t fnv_offset_basis = 2166136261U;
    for (size_t i = 0; i < length; i++) {
        fnv_offset_basis ^= (unsigned char)str[i];
        fnv_offset_basis *= 16777619u; // FNV Prime
    }
    return fnv_offset_basis;
}

void string_pool_init(StringPool *pool) {
    pool->bucket_count = STRING_POOL_INITIAL_BUCKETS;
    pool->buckets = calloc(pool->bucket_count, sizeof(StringPoolEntry *));

    pool->handle_capacity = STRING_POOL_INITIAL_HANDLES;
    pool->handle_count = 0;
    pool->handles = malloc(pool->handle_capacity * sizeof(InternedString));
}

void string_pool_destroy(StringPool *pool) {
    free(pool->buckets);
    free(pool->handles);
    pool->buckets = NULL;
    pool->handles = NULL;
}

uint32_t string_intern(StringPool *pool, const char *str, const size_t length) {
    const uint32_t hash = fnv_hash(str, length);
    const size_t bucket_index = hash % pool->bucket_count;

    // Check if this string has already been interned
    for (StringPoolEntry *entry = pool->buckets[bucket_index]; entry; entry = entry->next) {
        if (entry->length == length && memcmp(entry->str, str, length) == 0) {
            return entry->handle;
        }}

    // New string: copy into arena so it outlives whatever buffer 'str' came from
    char *owned_copy = arena_alloc_bump(&ast_arena, length + 1);
    memcpy(owned_copy, str, length);
    owned_copy[length] = '\0';

    // Grow handle-lookup array if needed
    if (pool->handle_count == pool->handle_capacity) {
        pool->handle_capacity *= 2;
        InternedString *temp = realloc(pool->handles, pool->handle_capacity * sizeof(InternedString));
        if (unlikely(temp == NULL)) {
            (void)fprintf(stderr, "String Interning: Growing handle-lookup array failed\n");
            exit(1);
        }
        pool->handles = temp;
    }
    const uint32_t new_handle = pool->handle_count;
    pool->handles[new_handle] = (InternedString) {.str = owned_copy, .length = length};
    pool->handle_count++;

    // Link new entry into its bucket
    StringPoolEntry *new_entry = arena_alloc_bump(&ast_arena, sizeof(StringPoolEntry));
    new_entry->str = owned_copy;
    new_entry->length = length;
    new_entry->handle = new_handle;
    new_entry->next = pool->buckets[bucket_index];
    pool->buckets[bucket_index] = new_entry;

    return new_handle;
}

const char *string_get_pool(const StringPool *pool, const uint32_t handle) {
    return pool->handles[handle].str;
}

size_t string_get_length(const StringPool *pool, const uint32_t handle) {
    return pool->handles[handle].length;
}