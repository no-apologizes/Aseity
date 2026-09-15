#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *str; // Points into arena-owned memory
    size_t length;
} InternedString;

typedef struct StringPool{
    struct StringPoolEntry **buckets;
    size_t bucket_count;

    InternedString *handles; // handle -> string, indexed by handle val
    uint32_t handle_count;
    uint32_t handle_capacity;
} StringPool;

void string_pool_init(StringPool *pool);
void string_pool_destroy(StringPool *pool);

uint32_t string_intern(StringPool *pool, const char *str, size_t length);
const char *string_get_pool(const StringPool *pool, uint32_t handle);
size_t string_get_length(const StringPool *pool, uint32_t handle);