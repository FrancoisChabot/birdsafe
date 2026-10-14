#include "bs_core.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int g_alloc_count = 0;
static int g_free_count = 0;

static void *counting_alloc(size_t n, void *ud) {
    (void)ud;
    g_alloc_count++;
    return malloc(n);
}

static void *counting_realloc(void *p, size_t n, void *ud) {
    (void)ud;
    return realloc(p, n);
}

static void counting_free(void *p, void *ud) {
    (void)ud;
    if (p) g_free_count++;
    free(p);
}

static void test_allocator(void) {
    /* 1. Reject incomplete allocators (all-or-nothing validation) */
    bs_allocator_t invalid = {NULL, NULL, NULL, NULL};
    assert(!bs_set_allocator(invalid));
    invalid = (bs_allocator_t){counting_alloc, NULL, counting_free, NULL};
    assert(!bs_set_allocator(invalid));
    invalid = (bs_allocator_t){counting_alloc, counting_realloc, NULL, NULL};
    assert(!bs_set_allocator(invalid));

    /* 2. Configure custom allocator at startup before any allocation */
    bs_allocator_t custom = {
        .alloc = counting_alloc,
        .realloc = counting_realloc,
        .free = counting_free,
        .user_data = NULL
    };
    assert(bs_set_allocator(custom));

    /* 3. Verify active allocator matches custom allocator */
    bs_allocator_t active = bs_get_allocator();
    assert(active.alloc == counting_alloc);
    assert(active.realloc == counting_realloc);
    assert(active.free == counting_free);

    g_alloc_count = 0;
    g_free_count = 0;

    /* 4. Test allocation and memory usage */
    void *custom_p = bs_alloc(128);
    assert(custom_p != NULL);
    assert(g_alloc_count == 1);
    memset(custom_p, 0xAB, 128);

    /* Test bs_calloc zero initialization */
    int *nums = (int *)bs_calloc(16, sizeof(int));
    assert(nums != NULL);
    assert(g_alloc_count == 2);
    for (int i = 0; i < 16; ++i) {
        assert(nums[i] == 0);
    }

    /* Test bs_calloc overflow rejection */
    void *overflow = bs_calloc(SIZE_MAX / 2, 4);
    assert(overflow == NULL);

    /* Test bs_realloc */
    custom_p = bs_realloc(custom_p, 256);
    assert(custom_p != NULL);

    bs_free(custom_p);
    assert(g_free_count == 1);

    bs_free(nums);
    assert(g_free_count == 2);

    bs_free(NULL); /* Safe to pass NULL */
    assert(g_free_count == 2);
}

static void test_status_and_truth(void) {
    assert(bs_status_is_ok(BS_OK));
    assert(!bs_status_is_ok(BS_INVALID_ARGUMENT));
    assert(bs_status_domain(BS_INVALID_ARGUMENT) == BS_DOMAIN_CORE);
    assert(bs_status_code(BS_INVALID_ARGUMENT) == 1);
    assert(bs_status_domain(BS_OUT_OF_MEMORY) == BS_DOMAIN_CORE);
    assert(bs_status_code(BS_OUT_OF_MEMORY) == 2);
    assert(strcmp(bs_status_domain_name(BS_INVALID_ARGUMENT), "core") == 0);

    assert(strcmp(bs_status_string(BS_OK), "OK") == 0);
    assert(strcmp(bs_status_string(BS_INVALID_ARGUMENT), "Invalid argument") == 0);
    assert(strcmp(bs_status_string(BS_OUT_OF_MEMORY), "Out of memory") == 0);
    assert(strcmp(bs_status_string((bs_status)999), "Unknown core status") == 0);
    assert(strcmp(bs_status_string((bs_status)0xFF000001), "Unknown status") == 0);

    assert(strcmp(bs_truth_string(BS_FALSE), "False") == 0);
    assert(strcmp(bs_truth_string(BS_TRUE), "True") == 0);
    assert(strcmp(bs_truth_string(BS_UNDECIDED), "Undecided") == 0);
    assert(strcmp(bs_truth_string((bs_truth)999), "Unknown truth") == 0);
}

int main(void) {
    test_allocator();
    test_status_and_truth();
    return 0;
}
