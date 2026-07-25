#include "Headers/arena.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define likely(a) __builtin_expect(!!(a), 1)
#define unlikely(a) __builtin_expect(!!(a), 0)

// Global Compiler Arena Instantiations
HybridArena ast_arena;
HybridArena symbol_arena;

/*
 * So WHY are we using Segregated Free-Lists?
 * It's because symbol_arena is highly predictable, as it only ever allocates three things:
 * SymbolTable struct: ~24 bytes
 * Symbol struct: ~144 bytes
 * Scope struct: ~528 bytes
 */

// Helper
static inline int get_seglist_bucket(size_t size) {
    // Notice how they're all powers of 2
    if (size <= 64) return 0;   // SymbolTable
    if (size <= 256) return 1;  // Symbol
    if (size <= 1024) return 2; // Scope
    return 3;                   // Fallback for oversized structs
}

static ArenaPage *allocate_arena_page(size_t size) {
    size_t target_size = size > ARENA_PAGE_SIZE ? size : ARENA_PAGE_SIZE; // If true, set to size and if false set to ARENA_PAGE_SIZE
    ArenaPage *page = malloc(sizeof(ArenaPage));
    if (unlikely(!page)) {
        fprintf(stderr, "Compiler Error(arena): Out of memory allocating ArenaPage header\n");
        exit(EXIT_FAILURE);
    }
    page->memory = malloc(target_size);
    if (unlikely(!page->memory)) {
        fprintf(stderr, "Compiler Error(arena): Out of memory allocating ArenaPage buffer (%zu bytes)", target_size);
        exit(EXIT_FAILURE);
    }
    page->capacity = target_size;
    page->alloc_position = 0;
    page->next = NULL;
    return page;
}

void arena_init(HybridArena *arena) {
    arena->page_head    = allocate_arena_page(ARENA_PAGE_SIZE);
    arena->current_page = arena->page_head;
    for (int i = 0; i < NUM_SEGLIST_BUCKETS; i++) {
        arena->seg_list[i] = NULL;
    }
}

void arena_destroy(HybridArena *arena) {
    ArenaPage *current = arena->page_head;
    while (current) {
        ArenaPage *next = current->next;
        free(current->memory);
        free(current);
        current = next;
    }
    arena->page_head    = NULL;
    arena->current_page = NULL;
    for (int i = 0; i < NUM_SEGLIST_BUCKETS; i++) {
        arena->seg_list[i] = NULL;
    }
}
void *arena_alloc_transient(HybridArena *arena, size_t size) {
    // 8-byte alignment for x86_64 archs
    size = (size + 7) & ~(size_t)7;
    ArenaPage *page = arena->current_page;

    if (unlikely(!page || page->alloc_position + size > page->capacity)) {
        ArenaPage *next_page = allocate_arena_page(size);
        if (page) {
            page->next = next_page;
        } else {
            arena->page_head = next_page;
        }
        arena->current_page = next_page;
        page = next_page;
    }

    void *ptr = &page->memory[page->alloc_position];
    page->alloc_position += size;
    return ptr;
}

void *arena_alloc_recyclable(HybridArena *arena, size_t size) {
    // 8-byte alignment for x86_64 archs
    size = (size + 7) & ~(size_t)7;
    if (size < sizeof(FreeNode)) {
        size = sizeof(FreeNode);
    }

    int bucket = get_seglist_bucket(size);
    FreeNode *hole = arena->seg_list[bucket];

    // O(1): if a hole exists of this exact size, take it
    if (hole) {
        arena->seg_list[bucket] = hole->next;
        return (void*)hole;
    }

    // Fallback to linear bump allocation if no slot is available
    return arena_alloc_transient(arena, size);
}

void arena_reset_transient(HybridArena *arena) {
    ArenaPage *current = arena->page_head;
    while (current) {
        current->alloc_position = 0;
        current = current->next;
    }
    arena->current_page = arena->page_head;
}

void arena_free_recyclable(HybridArena *arena, void *ptr, size_t size) {
    if (unlikely(!ptr)) return;
    // 8-byte alignment for x86_64 archs
    size = (size + 7) & ~(size_t)7;
    if (size < sizeof(FreeNode)) {
        size = sizeof(FreeNode);
    }

    int bucket = get_seglist_bucket(size); // If onlyyyy I could do i64 instead of int :(

    // Link dead memory directly to the head of its size bucket
    FreeNode *new_hole = (FreeNode*)ptr;
    new_hole->size = size;
    new_hole->next = arena->seg_list[bucket];
    arena->seg_list[bucket] = new_hole;
}