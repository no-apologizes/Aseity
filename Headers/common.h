#pragma once
#include <stdint.h>
#include <stddef.h>

#define likely(a) __builtin_expect(!!(a), 1)
#define unlikely(a) __builtin_expect(!!(a), 0)
#define f_static_inline __attribute__((__always_inline__)) static inline

f_static_inline uint32_t fnv_hash(const void *data, const size_t length) {
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t fnv_offset_basis = 2166136261U;
    for (size_t i = 0; i < length; i++) {
        fnv_offset_basis ^= bytes[i];
        fnv_offset_basis *= 16777619u; // FNV Prime
    }
    return fnv_offset_basis;
}