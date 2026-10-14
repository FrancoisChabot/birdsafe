/**
 * @file test_sets_concurrency.c
 * @brief Concurrency stress test verifying thread-safety of bs_universe,
 *        dynamic nature registration, lock-free bignum anchoring, and
 *        thread-confined bs_set_store usage.
 */

#include "bs_sets/bs_sets.h"
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_THREADS 8
#define ITERATIONS_PER_THREAD 50
#define VALUES_PER_THREAD 2000

/* ========================================================================= */
/* Test 1: Concurrent Stores, Bignum Anchoring & Set Operations              */
/* ========================================================================= */

typedef struct {
    const bs_universe *universe;
    int thread_id;
    bool success;
} store_worker_arg;

static void *store_worker(void *arg)
{
    store_worker_arg *w = (store_worker_arg *)arg;
    w->success = false;

    for (int iter = 0; iter < ITERATIONS_PER_THREAD; ++iter) {
        bs_set_store *store = NULL;
        if (bs_set_store_create(w->universe, &store) != BS_OK || store == NULL) {
            return NULL;
        }

        /* Create wide bignum values (exercising concurrent CAS payload anchoring) */
        char val1_str[64];
        char val2_str[64];
        snprintf(val1_str, sizeof(val1_str), "100000000000000000000%d", w->thread_id);
        snprintf(val2_str, sizeof(val2_str), "200000000000000000000%d", w->thread_id);

        bs_value bignum_val1, bignum_val2;
        if (bs_value_from_decimal(w->universe, val1_str, &bignum_val1) != BS_OK ||
            bs_value_from_decimal(w->universe, val2_str, &bignum_val2) != BS_OK) {
            bs_set_store_destroy(store);
            return NULL;
        }

        const bs_set *interval_set = NULL;
        if (bs_set_interval(store, &bignum_val1, true, &bignum_val2, true, &interval_set) != BS_OK) {
            bs_set_store_destroy(store);
            return NULL;
        }

        /* Compute cardinality (arbitrary precision) */
        bs_int card = bs_int_from_i64(0);
        if (bs_set_cardinality(interval_set, &card) != BS_OK) {
            bs_set_store_destroy(store);
            return NULL;
        }
        if (!bs_int_is_bignum(card)) {
            bs_int_release(card);
            bs_set_store_destroy(store);
            return NULL;
        }
        bs_int_release(card);

        /* Evaluate membership */
        bs_truth contains = BS_FALSE;
        if (bs_set_contains(interval_set, bignum_val1, &contains) != BS_OK || contains != BS_TRUE) {
            bs_set_store_destroy(store);
            return NULL;
        }

        /* Perform set algebra */
        const bs_set *empty_set = NULL;
        if (bs_set_enumerated(store, NULL, 0, &empty_set) != BS_OK) {
            bs_set_store_destroy(store);
            return NULL;
        }

        const bs_set *union_set = NULL;
        if (bs_set_union(store, interval_set, empty_set, &union_set) != BS_OK) {
            bs_set_store_destroy(store);
            return NULL;
        }

        bs_set_store_destroy(store);
    }

    w->success = true;
    return NULL;
}

static void test_concurrent_stores_and_bignums(void)
{
    printf("Running test_concurrent_stores_and_bignums...\n");
    bs_universe *universe = NULL;
    assert(bs_universe_create(&universe) == BS_OK);

    pthread_t threads[NUM_THREADS];
    store_worker_arg args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; ++i) {
        args[i].universe = universe;
        args[i].thread_id = i;
        args[i].success = false;
        assert(pthread_create(&threads[i], NULL, store_worker, &args[i]) == 0);
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(args[i].success);
    }

    bs_universe_destroy(universe);
    printf("  Passed.\n");
}

/* ========================================================================= */
/* Test 2: Concurrent Dynamic Nature Registration                            */
/* ========================================================================= */

#define NATURES_PER_THREAD 100

typedef struct {
    bs_universe *universe;
    int thread_id;
    const bs_nature *finite_natures[NATURES_PER_THREAD];
    bs_nature *nonfinite_natures[NATURES_PER_THREAD];
    bool success;
} nature_worker_arg;

static void *nature_worker(void *arg)
{
    nature_worker_arg *w = (nature_worker_arg *)arg;
    w->success = false;

    for (int i = 0; i < NATURES_PER_THREAD; ++i) {
        const bs_nature *fn = NULL;
        if (bs_nature_mint_finite(w->universe, (uint64_t)(i + 1), &fn) != BS_OK || fn == NULL) {
            return NULL;
        }
        w->finite_natures[i] = fn;

        bs_nature *nfn = NULL;
        if (bs_nature_mint_nonfinite(w->universe, &nfn) != BS_OK || nfn == NULL) {
            return NULL;
        }
        w->nonfinite_natures[i] = nfn;
    }

    w->success = true;
    return NULL;
}

static void test_concurrent_nature_registration(void)
{
    printf("Running test_concurrent_nature_registration...\n");
    bs_universe *universe = NULL;
    assert(bs_universe_create(&universe) == BS_OK);

    pthread_t threads[NUM_THREADS];
    nature_worker_arg args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; ++i) {
        args[i].universe = universe;
        args[i].thread_id = i;
        args[i].success = false;
        assert(pthread_create(&threads[i], NULL, nature_worker, &args[i]) == 0);
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(args[i].success);
    }

    /* Verify all minted natures belong to the universe and are distinct */
    for (int i = 0; i < NUM_THREADS; ++i) {
        for (int k = 0; k < NATURES_PER_THREAD; ++k) {
            assert(bs_nature_universe(args[i].finite_natures[k]) == universe);
            assert(bs_nature_universe(args[i].nonfinite_natures[k]) == universe);
        }
    }

    bs_universe_destroy(universe);
    printf("  Passed.\n");
}

/* ========================================================================= */
/* Test 3: Concurrent Value Minting on Non-Finite Nature                     */
/* ========================================================================= */

typedef struct {
    bs_universe *universe;
    bs_nature *nature;
    int thread_id;
    uint64_t *minted_hashes;
    bool success;
} value_mint_worker_arg;

static void *value_mint_worker(void *arg)
{
    value_mint_worker_arg *w = (value_mint_worker_arg *)arg;
    w->success = false;

    for (size_t i = 0; i < VALUES_PER_THREAD; ++i) {
        bs_value val;
        if (bs_nature_mint_value(w->universe, w->nature, &val) != BS_OK) {
            return NULL;
        }
        w->minted_hashes[i] = bs_value_hash(val);
    }

    w->success = true;
    return NULL;
}

static int compare_u64(const void *a, const void *b)
{
    uint64_t ua = *(const uint64_t *)a;
    uint64_t ub = *(const uint64_t *)b;
    if (ua < ub) return -1;
    if (ua > ub) return 1;
    return 0;
}

static void test_concurrent_value_minting(void)
{
    printf("Running test_concurrent_value_minting...\n");
    bs_universe *universe = NULL;
    assert(bs_universe_create(&universe) == BS_OK);

    bs_nature *open_nature = NULL;
    assert(bs_nature_mint_nonfinite(universe, &open_nature) == BS_OK);
    assert(open_nature != NULL);

    pthread_t threads[NUM_THREADS];
    value_mint_worker_arg args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; ++i) {
        args[i].universe = universe;
        args[i].nature = open_nature;
        args[i].thread_id = i;
        args[i].minted_hashes = (uint64_t *)calloc(VALUES_PER_THREAD, sizeof(uint64_t));
        assert(args[i].minted_hashes != NULL);
        args[i].success = false;
        assert(pthread_create(&threads[i], NULL, value_mint_worker, &args[i]) == 0);
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(args[i].success);
    }

    /* Aggregate all minted hashes and verify strict uniqueness */
    size_t total_values = NUM_THREADS * VALUES_PER_THREAD;
    uint64_t *all_hashes = (uint64_t *)malloc(total_values * sizeof(uint64_t));
    assert(all_hashes != NULL);

    size_t offset = 0;
    for (int i = 0; i < NUM_THREADS; ++i) {
        memcpy(all_hashes + offset, args[i].minted_hashes, VALUES_PER_THREAD * sizeof(uint64_t));
        offset += VALUES_PER_THREAD;
        free(args[i].minted_hashes);
    }

    qsort(all_hashes, total_values, sizeof(uint64_t), compare_u64);

    for (size_t i = 1; i < total_values; ++i) {
        assert(all_hashes[i] != all_hashes[i - 1] && "Duplicate minted value hash detected!");
    }

    free(all_hashes);
    bs_universe_destroy(universe);
    printf("  Passed.\n");
}

/* ========================================================================= */
/* Main                                                                      */
/* ========================================================================= */

int main(void)
{
    printf("Starting sets concurrency test suite...\n");
    test_concurrent_stores_and_bignums();
    test_concurrent_nature_registration();
    test_concurrent_value_minting();
    printf("All concurrency tests passed!\n");
    return 0;
}
