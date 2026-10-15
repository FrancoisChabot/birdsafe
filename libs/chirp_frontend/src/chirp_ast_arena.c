/**
 * @file chirp_ast_arena.c
 * @brief Implementation of the monotonic AST arena allocator.
 */

#include "chirp_frontend/chirp_ast_arena.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

#define CHIRP_AST_ARENA_DEFAULT_CHUNK 8192

struct chirp_ast_arena_chunk {
    chirp_ast_arena_chunk *next;
    size_t capacity;
    size_t used;
};

static inline size_t align_up(size_t n, size_t align) {
    if (align <= 1) return n;
    return (n + align - 1) & ~(align - 1);
}

void chirp_ast_arena_init(chirp_ast_arena *arena, size_t default_chunk_size) {
    assert(arena != NULL);
    arena->head = NULL;
    arena->default_chunk_size = default_chunk_size > 0 ? default_chunk_size : CHIRP_AST_ARENA_DEFAULT_CHUNK;
}

void chirp_ast_arena_destroy(chirp_ast_arena *arena) {
    if (arena == NULL) return;
    chirp_ast_arena_chunk *cur = arena->head;
    while (cur != NULL) {
        chirp_ast_arena_chunk *next = cur->next;
        bs_free(cur);
        cur = next;
    }
    arena->head = NULL;
}

void *chirp_ast_arena_alloc(chirp_ast_arena *arena, size_t size, size_t align) {
    assert(arena != NULL);
    if (size == 0) return NULL;
    if (align == 0) align = sizeof(void *);

    // Check if the current chunk has enough aligned space
    if (arena->head != NULL) {
        chirp_ast_arena_chunk *chunk = arena->head;
        uintptr_t data_base = (uintptr_t)(chunk + 1);
        uintptr_t current_addr = data_base + chunk->used;
        uintptr_t aligned_addr = (uintptr_t)align_up((size_t)current_addr, align);
        size_t offset = (size_t)(aligned_addr - data_base);

        if (offset + size <= chunk->capacity) {
            chunk->used = offset + size;
            return (void *)aligned_addr;
        }
    }

    // Allocate a new chunk
    size_t needed = size + align;
    size_t alloc_cap = needed > arena->default_chunk_size ? needed : arena->default_chunk_size;
    size_t total_bytes = sizeof(chirp_ast_arena_chunk) + alloc_cap;

    chirp_ast_arena_chunk *new_chunk = (chirp_ast_arena_chunk *)bs_alloc(total_bytes);
    if (new_chunk == NULL) {
        return NULL;
    }

    new_chunk->next = arena->head;
    new_chunk->capacity = alloc_cap;

    uintptr_t data_base = (uintptr_t)(new_chunk + 1);
    uintptr_t aligned_addr = (uintptr_t)align_up((size_t)data_base, align);
    size_t offset = (size_t)(aligned_addr - data_base);

    new_chunk->used = offset + size;
    arena->head = new_chunk;

    return (void *)aligned_addr;
}

void *chirp_ast_arena_alloc_array(chirp_ast_arena *arena, size_t count, size_t elem_size) {
    if (count == 0 || elem_size == 0) return NULL;
    return chirp_ast_arena_alloc(arena, count * elem_size, sizeof(void *));
}
