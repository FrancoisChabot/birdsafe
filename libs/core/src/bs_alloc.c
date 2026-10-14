#include "bs_core/bs_alloc.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifndef NDEBUG
#include <stdatomic.h>
static atomic_bool allocator_frozen = ATOMIC_VAR_INIT(false);

static inline void freeze_allocator(void) {
    if (!atomic_load_explicit(&allocator_frozen, memory_order_relaxed)) {
        atomic_store_explicit(&allocator_frozen, true, memory_order_relaxed);
    }
}
#else
#define freeze_allocator() ((void)0)
#endif

static void *default_alloc(size_t n, void *ud) {
    (void)ud;
    return malloc(n);
}

static void *default_realloc(void *p, size_t n, void *ud) {
    (void)ud;
    return realloc(p, n);
}

static void default_free(void *p, void *ud) {
    (void)ud;
    free(p);
}

static const bs_allocator_t default_allocator = {
    default_alloc, default_realloc, default_free, NULL
};

static bs_allocator_t active_allocator = {
    default_alloc, default_realloc, default_free, NULL
};

bs_allocator_t bs_allocator_default(void) {
    return default_allocator;
}

bool bs_set_allocator(bs_allocator_t a) {
    if (!a.alloc || !a.realloc || !a.free)
        return false;
#ifndef NDEBUG
    assert(!atomic_load_explicit(&allocator_frozen, memory_order_relaxed) &&
           "bs_set_allocator: cannot change allocator after allocations have begun or allocator already set");
    atomic_store_explicit(&allocator_frozen, true, memory_order_relaxed);
#endif
    active_allocator = a;
    return true;
}

bs_allocator_t bs_get_allocator(void) {
    return active_allocator;
}

void *bs_alloc(size_t n) {
    freeze_allocator();
    if (n == 0)
        n = 1;
    return active_allocator.alloc(n, active_allocator.user_data);
}

void *bs_calloc(size_t count, size_t size) {
    freeze_allocator();
    if (count != 0 && size > SIZE_MAX / count)
        return NULL;
    size_t total = count * size;
    if (total == 0)
        total = 1;
    void *ptr = active_allocator.alloc(total, active_allocator.user_data);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void *bs_realloc(void *p, size_t n) {
    freeze_allocator();
    if (n == 0)
        n = 1;
    return active_allocator.realloc(p, n, active_allocator.user_data);
}

void bs_free(void *p) {
    if (p) {
        active_allocator.free(p, active_allocator.user_data);
    }
}
