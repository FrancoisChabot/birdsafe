#include "bs_core.h"
#include "bs_num.h"
#include "bs_sets.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct mem_header {
    size_t size;
    uint64_t magic;
} mem_header;

#define MAGIC_NUM 0xCAFEBABE12345678ULL

static size_t g_active_bytes = 0;
static size_t g_active_count = 0;
static size_t g_total_count = 0;

static void *tracking_alloc(size_t size, void *user_data)
{
    (void)user_data;
    size_t total = sizeof(mem_header) + size;
    mem_header *hdr = (mem_header *)malloc(total);
    if (!hdr) return NULL;
    hdr->size = size;
    hdr->magic = MAGIC_NUM;
    g_active_bytes += size;
    g_active_count++;
    g_total_count++;
    return (void *)(hdr + 1);
}

static void *tracking_realloc(void *ptr, size_t new_size, void *user_data)
{
    (void)user_data;
    if (!ptr) return tracking_alloc(new_size, user_data);
    mem_header *hdr = ((mem_header *)ptr) - 1;
    assert(hdr->magic == MAGIC_NUM);
    g_active_bytes -= hdr->size;
    size_t total = sizeof(mem_header) + new_size;
    mem_header *new_hdr = (mem_header *)realloc(hdr, total);
    if (!new_hdr) return NULL;
    new_hdr->size = new_size;
    g_active_bytes += new_size;
    return (void *)(new_hdr + 1);
}

static void tracking_free(void *ptr, void *user_data)
{
    (void)user_data;
    if (!ptr) return;
    mem_header *hdr = ((mem_header *)ptr) - 1;
    assert(hdr->magic == MAGIC_NUM);
    g_active_bytes -= hdr->size;
    g_active_count--;
    hdr->magic = 0;
    free(hdr);
}

int main(void)
{
    /* 1. Install tracking allocator */
    bs_allocator_t tracker = {
        .alloc = tracking_alloc,
        .realloc = tracking_realloc,
        .free = tracking_free,
        .user_data = NULL
    };
    assert(bs_set_allocator(tracker));

    g_active_bytes = 0;
    g_active_count = 0;
    g_total_count = 0;

    /* 2. Run multi-module workflow */
    {
        /* Core test */
        char *str = (char *)bs_calloc(32, 1);
        assert(str != NULL);
        bs_free(str);

        /* Num test */
        bs_int a = bs_int_from_i64(1000);
        bs_int b = bs_int_from_i64(2000);
        bs_int sum = bs_int_add(a, b);
        int64_t sum_val = 0;
        assert(bs_int_to_i64(sum, &sum_val) && sum_val == 3000);
        bs_int_release(sum);
        bs_int_release(b);
        bs_int_release(a);

        /* Sets test */
        bs_universe *u = NULL;
        assert(bs_universe_create(&u) == BS_OK);
        bs_set_store *store = NULL;
        assert(bs_set_store_create(u, &store) == BS_OK);

        bs_value v1, v2;
        assert(bs_value_from_integer(u, -10, &v1) == BS_OK);
        assert(bs_value_from_integer(u, 10, &v2) == BS_OK);

        const bs_set *interval = NULL;
        assert(bs_set_interval(store, &v1, true, &v2, true, &interval) == BS_OK);

        bs_int card = BS_INT_NULL;
        assert(bs_set_cardinality(interval, &card) == BS_OK);
        uint64_t card_val = 0;
        assert(bs_int_to_u64(card, &card_val) && card_val == 21);
        bs_int_release(card);

        /* Direct bs_int interop with bignum */
        bs_int big_bi;
        assert(bs_int_from_str("10000000000000000000000000", 10, &big_bi));
        bs_value big_val;
        assert(bs_value_from_bs_int(u, big_bi, &big_val) == BS_OK);
        bs_int_release(big_bi);

        bs_int extracted_bi = BS_INT_NULL;
        assert(bs_value_as_bs_int(big_val, &extracted_bi) == BS_OK);
        char *formatted = bs_int_to_str(extracted_bi, 10);
        assert(strcmp(formatted, "10000000000000000000000000") == 0);
        bs_free(formatted);
        bs_int_release(extracted_bi);

        /* Value hashing */
        assert(bs_value_hash(big_val) != 0);
        assert(bs_value_hash(v1) != bs_value_hash(v2));

        /* Cleanup sets */
        bs_set_store_destroy(store);
        bs_universe_destroy(u);
    }

    /* 3. Verify that all allocations were captured and cleanly released */
    printf("BirdSafe Integration Test Results:\n");
    printf("  Total Allocations Tracked: %zu\n", g_total_count);
    printf("  Active Allocations Remaining: %zu\n", g_active_count);
    printf("  Active Bytes Remaining: %zu\n", g_active_bytes);

    assert(g_total_count > 0);
    assert(g_active_count == 0);
    assert(g_active_bytes == 0);


    printf("SUCCESS: All memory tracked and 0 leaks across core, num, and sets!\n");
    return 0;
}
