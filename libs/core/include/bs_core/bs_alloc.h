/**
 * @file bs_alloc.h
 * @brief Memory allocation and pluggable allocator interface for BirdSafe.
 */

#ifndef BS_CORE_BS_ALLOC_H
#define BS_CORE_BS_ALLOC_H

#include "bs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Consumer Allocation API                                                   */
/* ========================================================================= */

/** Allocates uninitialized memory. Returns NULL on OOM. */
BS_API void *bs_alloc(size_t size);

/** Allocates zeroed memory with overflow protection on count * size. Returns NULL on OOM or overflow. */
BS_API void *bs_calloc(size_t count, size_t size);

/** Resizes an allocated block. Returns NULL on failure (original block untouched). */
BS_API void *bs_realloc(void *ptr, size_t new_size);

/** Frees memory allocated via bs_alloc/bs_calloc/bs_realloc. Safe to pass NULL. */
BS_API void bs_free(void *ptr);

/* ========================================================================= */
/* Pluggable Allocator Interface (Embedders / Custom Runtimes)               */
/* ========================================================================= */

typedef void *(*bs_malloc_fn)(size_t size, void *user_data);
typedef void *(*bs_realloc_fn)(void *ptr, size_t new_size, void *user_data);
typedef void (*bs_free_fn)(void *ptr, void *user_data);

typedef struct bs_allocator {
    bs_malloc_fn  alloc;     /**< Callback returning aligned memory */
    bs_realloc_fn realloc;   /**< Reallocation callback */
    bs_free_fn    free;      /**< Deallocation callback */
    void         *user_data; /**< Opaque user pointer passed to callbacks */
} bs_allocator_t;

/** Returns the default allocator backed by standard libc malloc/realloc/free. */
BS_API bs_allocator_t bs_allocator_default(void);

/**
 * Configures the global allocator. Must be called at startup before any
 * allocation occurs. Returns false if any callback is NULL.
 * In debug builds (when NDEBUG is not defined), calling this after allocations
 * have begun or calling it multiple times will trigger an assertion.
 */
BS_API bool bs_set_allocator(bs_allocator_t allocator);

/** Returns the currently active allocator. */
BS_API bs_allocator_t bs_get_allocator(void);

#ifdef __cplusplus
}
#endif

#endif /* BS_CORE_BS_ALLOC_H */
