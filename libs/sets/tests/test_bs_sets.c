#include "bs_sets.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

static bs_status sample_predicate(void *context, bs_value candidate, bs_truth *out_result)
{
    (void)context;
    int64_t val = 0;
    bs_status s = bs_value_as_integer(candidate, &val);
    if (s != BS_OK) {
        *out_result = BS_FALSE;
        return BS_OK;
    }
    *out_result = (val % 2 == 0) ? BS_TRUE : BS_FALSE;
    return BS_OK;
}

static void test_universe_and_natures(void)
{
    bs_universe *u = NULL;
    assert(bs_universe_create(&u) == BS_OK && u != NULL);

    const bs_nature *non = bs_universe_nature_of_natures(u);
    assert(non != NULL);
    assert(bs_nature_universe(non) == u);

    const bs_nature *int_nat = bs_universe_integer_nature(u);
    assert(int_nat != NULL);
    assert(bs_nature_universe(int_nat) == u);

    bool is_fin = true, is_enum = false, is_disc = false;
    assert(bs_nature_is_finite(int_nat, &is_fin) == BS_OK && !is_fin);
    assert(bs_nature_is_enumerable(int_nat, &is_enum) == BS_OK && is_enum);
    assert(bs_nature_is_discrete(int_nat, &is_disc) == BS_OK && is_disc);

    bs_int card = BS_INT_NULL;
    assert(bs_nature_cardinality(int_nat, &card) == BS_SETS_TYPE_ERROR);

    bs_universe_destroy(u);
}

static void test_integers_and_bs_int(void)
{
    bs_universe *u = NULL;
    assert(bs_universe_create(&u) == BS_OK);

    /* 1. Small integers (Fixnums) */
    bs_value v0, v42, v_neg;
    assert(bs_value_from_integer(u, 0, &v0) == BS_OK);
    assert(bs_value_from_integer(u, 42, &v42) == BS_OK);
    assert(bs_value_from_integer(u, -100, &v_neg) == BS_OK);

    int64_t extracted = 0;
    assert(bs_value_as_integer(v42, &extracted) == BS_OK && extracted == 42);
    assert(bs_value_as_integer(v_neg, &extracted) == BS_OK && extracted == -100);

    int cmp = 0;
    assert(bs_value_compare(v0, v42, &cmp) == BS_OK && cmp < 0);
    assert(bs_value_compare(v42, v_neg, &cmp) == BS_OK && cmp > 0);
    assert(bs_value_equal(v42, v42));
    assert(!bs_value_equal(v0, v42));

    /* 2. Direct bs_int interop */
    bs_int bi = bs_int_from_i64(42);
    bs_value v_bi;
    assert(bs_value_from_bs_int(u, bi, &v_bi) == BS_OK);
    assert(bs_value_equal(v42, v_bi));
    bs_int_release(bi);

    bs_int extracted_bi = BS_INT_NULL;
    assert(bs_value_as_bs_int(v42, &extracted_bi) == BS_OK);
    assert(bs_int_to_i64(extracted_bi, &extracted) && extracted == 42);
    bs_int_release(extracted_bi);

    /* 3. Bignum values (> 63 bits) */
    bs_value big_val;
    const char *big_str = "999999999999999999999999999999";
    assert(bs_value_from_decimal(u, big_str, &big_val) == BS_OK);

    /* Does not fit in int64_t */
    assert(bs_value_as_integer(big_val, &extracted) == BS_NUM_OVERFLOW);

    /* Extracts via bs_int */
    assert(bs_value_as_bs_int(big_val, &extracted_bi) == BS_OK);
    char *formatted = bs_int_to_str(extracted_bi, 10);
    assert(formatted != NULL);
    assert(strcmp(formatted, big_str) == 0);
    bs_free(formatted);
    bs_int_release(extracted_bi);

    /* Format via bs_value_format_integer */
    size_t req = 0;
    assert(bs_value_format_integer(big_val, NULL, 0, &req) == BS_OK);
    assert(req == strlen(big_str) + 1);
    char buf[64];
    assert(bs_value_format_integer(big_val, buf, sizeof(buf), &req) == BS_OK);
    assert(strcmp(buf, big_str) == 0);

    bs_universe_destroy(u);
}

static void test_sets_and_intervals(void)
{
    bs_universe *u = NULL;
    assert(bs_universe_create(&u) == BS_OK);
    bs_set_store *store = NULL;
    assert(bs_set_store_create(u, &store) == BS_OK);

    /* Interval [-2, 2] */
    bs_value low, high;
    assert(bs_value_from_integer(u, -2, &low) == BS_OK);
    assert(bs_value_from_integer(u, 2, &high) == BS_OK);

    const bs_set *interval = NULL;
    assert(bs_set_interval(store, &low, true, &high, true, &interval) == BS_OK);

    bs_int card = BS_INT_NULL;
    assert(bs_set_cardinality(interval, &card) == BS_OK);
    uint64_t card_val = 0;
    assert(bs_int_to_u64(card, &card_val) && card_val == 5);
    bs_int_release(card);

    bs_truth truth = BS_UNDECIDED;
    assert(bs_set_is_finite(interval, &truth) == BS_OK && truth == BS_TRUE);

    /* Membership check */
    bs_value zero, hundred;
    assert(bs_value_from_integer(u, 0, &zero) == BS_OK);
    assert(bs_value_from_integer(u, 100, &hundred) == BS_OK);

    assert(bs_set_contains(interval, zero, &truth) == BS_OK && truth == BS_TRUE);
    assert(bs_set_contains(interval, hundred, &truth) == BS_OK && truth == BS_FALSE);

    /* Iteration */
    bs_iterator *it = NULL;
    assert(bs_iterator_create(interval, &it) == BS_OK);
    bs_value next_val;
    int count = 0;
    while (bs_iterator_next(it, &next_val) == BS_OK) {
        count++;
    }
    assert(count == 5);
    bs_iterator_destroy(it);

    /* Predicate set (even numbers) */
    const bs_set *even_set = NULL;
    assert(bs_set_predicate(store, interval, sample_predicate, NULL, NULL, &even_set) == BS_OK);
    assert(bs_set_contains(even_set, zero, &truth) == BS_OK && truth == BS_TRUE);
    bs_value one;
    assert(bs_value_from_integer(u, 1, &one) == BS_OK);
    assert(bs_set_contains(even_set, one, &truth) == BS_OK && truth == BS_FALSE);

    /* Predicate set on infinite bound (integer nature) must report BS_UNDECIDED for finiteness */
    const bs_nature *int_nat = bs_universe_integer_nature(u);
    const bs_set *int_set = NULL;
    assert(bs_set_nature(store, int_nat, &int_set) == BS_OK);
    const bs_set *pred_inf = NULL;
    assert(bs_set_predicate(store, int_set, sample_predicate, NULL, NULL, &pred_inf) == BS_OK);
    bs_truth pred_fin = BS_TRUE;
    assert(bs_set_is_finite(pred_inf, &pred_fin) == BS_OK);
    assert(pred_fin == BS_UNDECIDED);

    bs_set_store_destroy(store);
    bs_universe_destroy(u);
}

static void test_value_hash(void)
{
    bs_universe *u = NULL;
    assert(bs_universe_create(&u) == BS_OK);

    /* 1. Integers */
    bs_value v1, v2, v3;
    assert(bs_value_from_integer(u, 12345, &v1) == BS_OK);
    assert(bs_value_from_integer(u, 12345, &v2) == BS_OK);
    assert(bs_value_from_integer(u, 54321, &v3) == BS_OK);
    assert(bs_value_equal(v1, v2));
    assert(!bs_value_equal(v1, v3));
    assert(bs_value_hash(v1) == bs_value_hash(v2));
    assert(bs_value_hash(v1) != bs_value_hash(v3));

    /* Bignums */
    bs_value b1, b2, b3;
    const char *s1 = "123456789012345678901234567890";
    const char *s2 = "987654321098765432109876543210";
    assert(bs_value_from_decimal(u, s1, &b1) == BS_OK);
    assert(bs_value_from_decimal(u, s1, &b2) == BS_OK);
    assert(bs_value_from_decimal(u, s2, &b3) == BS_OK);
    assert(bs_value_equal(b1, b2));
    assert(!bs_value_equal(b1, b3));
    assert(bs_value_hash(b1) == bs_value_hash(b2));
    assert(bs_value_hash(b1) != bs_value_hash(b3));

    /* 2. Natures */
    const bs_nature *int_nat = bs_universe_integer_nature(u);
    const bs_nature *non = bs_universe_nature_of_natures(u);
    bs_value nv1, nv2, nv3;
    assert(bs_nature_as_value(int_nat, &nv1) == BS_OK);
    assert(bs_nature_as_value(int_nat, &nv2) == BS_OK);
    assert(bs_nature_as_value(non, &nv3) == BS_OK);
    assert(bs_value_equal(nv1, nv2));
    assert(!bs_value_equal(nv1, nv3));
    assert(bs_value_hash(nv1) == bs_value_hash(nv2));
    assert(bs_value_hash(nv1) != bs_value_hash(nv3));

    /* 3. Minted natures */
    const bs_nature *finite_nat = NULL;
    assert(bs_nature_mint_finite(u, 10, &finite_nat) == BS_OK);
    bs_value m0_a, m0_b, m1;
    assert(bs_nature_get_minted_value(finite_nat, 0, &m0_a) == BS_OK);
    assert(bs_nature_get_minted_value(finite_nat, 0, &m0_b) == BS_OK);
    assert(bs_nature_get_minted_value(finite_nat, 1, &m1) == BS_OK);
    assert(bs_value_equal(m0_a, m0_b));
    assert(!bs_value_equal(m0_a, m1));
    assert(bs_value_hash(m0_a) == bs_value_hash(m0_b));
    assert(bs_value_hash(m0_a) != bs_value_hash(m1));

    bs_universe_destroy(u);
}

static void test_universe_and_store_lifecycles(void)
{
    bs_universe *u = NULL;
    assert(bs_universe_create(&u) == BS_OK);

    /* Multiple stores can be created against one universe */
    bs_set_store *s1 = NULL;
    bs_set_store *s2 = NULL;
    bs_set_store *s3 = NULL;
    assert(bs_set_store_create(u, &s1) == BS_OK);
    assert(bs_set_store_create(u, &s2) == BS_OK);
    assert(bs_set_store_create(u, &s3) == BS_OK);

    assert(bs_set_store_universe(s1) == u);
    assert(bs_set_store_universe(s2) == u);
    assert(bs_set_store_universe(s3) == u);

    /* Stores can create sets */
    const bs_nature *int_nat = bs_universe_integer_nature(u);
    const bs_set *set1 = NULL;
    const bs_set *set2 = NULL;
    assert(bs_set_nature(s1, int_nat, &set1) == BS_OK);
    assert(bs_set_nature(s2, int_nat, &set2) == BS_OK);
    assert(bs_set_universe(set1) == u);
    assert(bs_set_universe(set2) == u);

    /* Stores can be destroyed out of creation order (e.g. s2 first) */
    bs_set_store_destroy(s2);

    /* Another store can be created while existing ones live */
    bs_set_store *s4 = NULL;
    assert(bs_set_store_create(u, &s4) == BS_OK);
    bs_set_store_destroy(s4);

    /* Remaining stores destroyed */
    bs_set_store_destroy(s1);
    bs_set_store_destroy(s3);

    /* Clean destruction of universe after all stores are freed */
    bs_universe_destroy(u);
}

int main(void)
{
    test_universe_and_natures();
    test_integers_and_bs_int();
    test_sets_and_intervals();
    test_value_hash();
    test_universe_and_store_lifecycles();
    return 0;
}
