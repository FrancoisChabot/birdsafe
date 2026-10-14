#include "bs_core.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    /* 1. Verify default allocator properties */
    bs_allocator_t def = bs_allocator_default();
    assert(def.alloc != NULL);
    assert(def.realloc != NULL);
    assert(def.free != NULL);
    assert(def.user_data == NULL);

    bs_allocator_t active = bs_get_allocator();
    assert(active.alloc == def.alloc);
    assert(active.realloc == def.realloc);
    assert(active.free == def.free);

    /* 2. Allocation with default allocator */
    void *p = bs_alloc(64);
    assert(p != NULL);
    memset(p, 0x42, 64);

    /* 3. Calloc zero initialization and overflow rejection */
    int *nums = (int *)bs_calloc(16, sizeof(int));
    assert(nums != NULL);
    for (int i = 0; i < 16; ++i) {
        assert(nums[i] == 0);
    }
    bs_free(nums);

    void *overflow = bs_calloc(SIZE_MAX / 2, 4);
    assert(overflow == NULL);

    /* 4. Realloc and free */
    p = bs_realloc(p, 128);
    assert(p != NULL);
    bs_free(p);
    bs_free(NULL);

    return 0;
}
