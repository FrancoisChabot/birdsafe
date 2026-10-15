/**
 * @file chirp_ast_arena.h
 * @brief Monotonic AST arena allocator for the Chirp frontend.
 */

#ifndef CHIRP_FRONTEND_CHIRP_AST_ARENA_H
#define CHIRP_FRONTEND_CHIRP_AST_ARENA_H

#include "bs_core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chirp_ast_arena_chunk chirp_ast_arena_chunk;

/** Monotonic arena allocator for AST nodes and contiguous metadata. */
typedef struct chirp_ast_arena {
    chirp_ast_arena_chunk *head;
    size_t default_chunk_size;
} chirp_ast_arena;

/** Initializes an AST arena with the given default chunk size (0 defaults to 8KB). */
BS_API void chirp_ast_arena_init(chirp_ast_arena *arena, size_t default_chunk_size);

/** Destroys an AST arena and frees all underlying chunk allocations. Safe for NULL. */
BS_API void chirp_ast_arena_destroy(chirp_ast_arena *arena);

/** Allocates a memory block of the specified size and byte alignment. Returns NULL on OOM. */
BS_API void *chirp_ast_arena_alloc(chirp_ast_arena *arena, size_t size, size_t align);

/** Allocates an array of count elements of elem_size bytes with standard alignment. */
BS_API void *chirp_ast_arena_alloc_array(chirp_ast_arena *arena, size_t count, size_t elem_size);

#ifdef __cplusplus
}
#endif

#endif /* CHIRP_FRONTEND_CHIRP_AST_ARENA_H */
