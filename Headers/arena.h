#pragma once

// Okay so even though L1 caches are 64KB, half are for instructions and the other half are for data, and making this 64KB would push the second half into the L2 cache
#include <stddef.h>
#include <stdint.h>
#define ARENA_PAGE_SIZE 32768 // 32KB rn, 1024 * 32
#define NUM_SEGLIST_BUCKETS 4

typedef struct ArenaPage {
    uint8_t *memory;
    size_t capacity;
    size_t alloc_position;
    struct ArenaPage *next;
} ArenaPage;

typedef struct FreeNode {
    size_t size;
    struct FreeNode *next;
} FreeNode;

typedef struct {
    ArenaPage *page_head;
    ArenaPage *current_page;
    FreeNode *seg_list[NUM_SEGLIST_BUCKETS];
} HybridArena;

// Compiler Arenas
extern HybridArena ast_arena;    // Transient lifetime (linear bump for AST nodes)
extern HybridArena symbol_arena; // Recyclable lifetime (intrusive free-list for symbol table)

void arena_init(HybridArena *arena);
void arena_destroy(HybridArena *arena);
void *arena_alloc_transient(HybridArena *arena, size_t size);
void *arena_alloc_recyclable(HybridArena *arena, size_t size);
void arena_reset_transient(HybridArena *arena);
void arena_free_recyclable(HybridArena *arena, void *ptr, size_t size);