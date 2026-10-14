/**
 * @file test_bs_num.c
 * @brief Comprehensive test suite for bs_num core functionality.
 *
 * Validates constructors, conversions, string I/O, arithmetic, bitwise logic,
 * shifts (including negative carry-out edge cases), predicates, number theory,
 * reference counting, and pluggable allocator leak tracking.
 */

#include "bs_bignum.h"
#include "bs_intrinsics.h"
#include "bs_num.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "TEST FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__);              \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

/* ========================================================================= */
/* Custom Tracking Allocator (Leak Checker)                                  */
/* ========================================================================= */

typedef struct alloc_node {
    void *ptr;
    size_t size;
    struct alloc_node *next;
} alloc_node_t;

static alloc_node_t *g_alloc_list = NULL;
static size_t g_active_allocations = 0;
static size_t g_active_bytes = 0;
static bool g_fail_allocations = false;

static void *tracking_alloc(size_t size, void *user_data) {
    (void)user_data;
    if (g_fail_allocations)
        return NULL;
    void *ptr = malloc(size);
    if (!ptr)
        return NULL;
    alloc_node_t *node = (alloc_node_t *)malloc(sizeof(alloc_node_t));
    if (!node) {
        free(ptr);
        return NULL;
    }
    node->ptr = ptr;
    node->size = size;
    node->next = g_alloc_list;
    g_alloc_list = node;
    g_active_allocations++;
    g_active_bytes += size;
    return ptr;
}

static void *tracking_realloc(void *ptr, size_t new_size, void *user_data) {
    (void)user_data;
    if (g_fail_allocations)
        return NULL;
    if (!ptr)
        return tracking_alloc(new_size, user_data);

    alloc_node_t **curr = &g_alloc_list;
    while (*curr && (*curr)->ptr != ptr) {
        curr = &(*curr)->next;
    }
    TEST_ASSERT(*curr != NULL && "Reallocating untracked pointer");

    void *new_ptr = realloc(ptr, new_size);
    if (!new_ptr)
        return NULL;

    g_active_bytes = g_active_bytes - (*curr)->size + new_size;
    (*curr)->ptr = new_ptr;
    (*curr)->size = new_size;
    return new_ptr;
}

static void tracking_free(void *ptr, void *user_data) {
    (void)user_data;
    if (!ptr)
        return;

    alloc_node_t **curr = &g_alloc_list;
    while (*curr && (*curr)->ptr != ptr) {
        curr = &(*curr)->next;
    }
    TEST_ASSERT(*curr != NULL && "Freeing untracked pointer");

    alloc_node_t *to_delete = *curr;
    *curr = to_delete->next;
    g_active_allocations--;
    g_active_bytes -= to_delete->size;
    free(to_delete);
    free(ptr);
}

static void setup_tracking_allocator(void) {
    bs_allocator_t alloc = {.alloc = tracking_alloc,
                            .realloc = tracking_realloc,
                            .free = tracking_free,
                            .user_data = NULL};
    bool ok = bs_set_allocator(alloc);
    TEST_ASSERT(ok);
}

static void check_zero_leaks(const char *test_name) {
    if (g_active_allocations != 0 || g_active_bytes != 0) {
        fprintf(stderr,
                "MEMORY LEAK DETECTED in %s: %zu live blocks, %zu bytes\n",
                test_name,
                g_active_allocations,
                g_active_bytes);
        exit(1);
    }
}

/* ========================================================================= */
/* Test Cases                                                                */
/* ========================================================================= */

static void test_constructors_and_i64_conversions(void) {
    printf("Testing constructors and 64-bit conversions...\n");

    /* Zero */
    bs_int z = bs_int_from_i64(0);
    TEST_ASSERT(bs_int_is_zero(z));
    TEST_ASSERT(bs_int_is_fixnum(z));
    int64_t out64 = -1;
    TEST_ASSERT(bs_int_to_i64(z, &out64) && out64 == 0);
    uint64_t uout64 = 1;
    TEST_ASSERT(bs_int_to_u64(z, &uout64) && uout64 == 0);
    bs_int_release(z);

    /* Fixnum Boundaries */
    bs_int max_fix = bs_int_from_i64(BS_FIXNUM_MAX);
    TEST_ASSERT(bs_int_is_fixnum(max_fix));
    TEST_ASSERT(bs_int_to_i64(max_fix, &out64) && out64 == BS_FIXNUM_MAX);
    bs_int_release(max_fix);

    bs_int min_fix = bs_int_from_i64(BS_FIXNUM_MIN);
    TEST_ASSERT(bs_int_is_fixnum(min_fix));
    TEST_ASSERT(bs_int_to_i64(min_fix, &out64) && out64 == BS_FIXNUM_MIN);
    bs_int_release(min_fix);

    /* Bignum Promoted from i64 */
    bs_int beyond_max = bs_int_from_i64(BS_FIXNUM_MAX + 1);
    TEST_ASSERT(bs_int_is_bignum(beyond_max));
    TEST_ASSERT(bs_int_to_i64(beyond_max, &out64) && out64 == BS_FIXNUM_MAX + 1);
    bs_int_release(beyond_max);

    bs_int beyond_min = bs_int_from_i64(BS_FIXNUM_MIN - 1);
    TEST_ASSERT(bs_int_is_bignum(beyond_min));
    TEST_ASSERT(bs_int_to_i64(beyond_min, &out64) && out64 == BS_FIXNUM_MIN - 1);
    bs_int_release(beyond_min);

    bs_int int64_max_val = bs_int_from_i64(INT64_MAX);
    TEST_ASSERT(bs_int_is_bignum(int64_max_val));
    TEST_ASSERT(bs_int_to_i64(int64_max_val, &out64) && out64 == INT64_MAX);
    bs_int_release(int64_max_val);

    bs_int int64_min_val = bs_int_from_i64(INT64_MIN);
    TEST_ASSERT(bs_int_is_bignum(int64_min_val));
    TEST_ASSERT(bs_int_to_i64(int64_min_val, &out64) && out64 == INT64_MIN);
    bs_int_release(int64_min_val);

    /* u64 Conversion */
    bs_int u64_val = bs_int_from_u64(UINT64_MAX);
    TEST_ASSERT(bs_int_is_bignum(u64_val));
    TEST_ASSERT(bs_int_to_u64(u64_val, &uout64) && uout64 == UINT64_MAX);
    /* Should fail to fit in signed i64 */
    TEST_ASSERT(!bs_int_to_i64(u64_val, &out64));
    bs_int_release(u64_val);

    check_zero_leaks("test_constructors_and_i64_conversions");
}

static void test_words_conversions(void) {
    printf("Testing word export/import...\n");

    uint64_t raw_words[3] = {
        UINT64_C(0x1111222233334444), UINT64_C(0x5555666677778888), UINT64_C(0x9999AAAABBBBCCCC)};
    bs_int a = bs_int_from_words(raw_words, 3, 1);
    TEST_ASSERT(bs_int_is_bignum(a));
    TEST_ASSERT(bs_int_word_count(a) == 3);

    /* Query capacity */
    size_t req = bs_int_to_words(a, NULL, 0);
    TEST_ASSERT(req == 3);

    uint64_t out_words[3] = {0};
    TEST_ASSERT(bs_int_to_words(a, out_words, 3) == 3);
    TEST_ASSERT(out_words[0] == raw_words[0]);
    TEST_ASSERT(out_words[1] == raw_words[1]);
    TEST_ASSERT(out_words[2] == raw_words[2]);

    bs_int_release(a);

    /* Normalization to canonical zero */
    uint64_t zero_words[2] = {0, 0};
    bs_int z = bs_int_from_words(zero_words, 2, 1);
    TEST_ASSERT(bs_int_is_zero(z));
    TEST_ASSERT(bs_int_word_count(z) == 0);
    bs_int_release(z);

    check_zero_leaks("test_words_conversions");
}

static void test_string_formatting_and_parsing(void) {
    printf("Testing string parsing and formatting...\n");

    /* Decimal round-trip */
    const char *dec_str = "1234567890123456789012345678901234567890";
    bs_int a = BS_INT_NULL;
    TEST_ASSERT(bs_int_from_str(dec_str, 10, &a));
    TEST_ASSERT(bs_int_is_bignum(a));

    char *s = bs_int_to_str(a, 10);
    TEST_ASSERT(s != NULL);
    TEST_ASSERT(strcmp(s, dec_str) == 0);
    bs_free(s);
    bs_int_release(a);

    /* Negative Hex round-trip with 0x prefix auto-detection */
    const char *hex_str = "-0xdeadbeefcafebabe0123456789abcdef";
    TEST_ASSERT(bs_int_from_str(hex_str, 0, &a));
    TEST_ASSERT(bs_int_is_negative(a));

    s = bs_int_to_str(a, 16);
    TEST_ASSERT(s != NULL);
    /* Format output is "-deadbeefcafebabe0123456789abcdef" (without 0x) */
    TEST_ASSERT(strcmp(s, "-deadbeefcafebabe0123456789abcdef") == 0);
    bs_free(s);
    bs_int_release(a);

    /* Binary auto-detection (0b) */
    TEST_ASSERT(bs_int_from_str("0b101101", 0, &a));
    int64_t v = 0;
    TEST_ASSERT(bs_int_to_i64(a, &v) && v == 45);
    bs_int_release(a);

    /* Octal auto-detection (0o) */
    TEST_ASSERT(bs_int_from_str("0o77", 0, &a));
    TEST_ASSERT(bs_int_to_i64(a, &v) && v == 63);
    bs_int_release(a);

    /* Base 36 round-trip */
    const char *b36_str = "z1y2x3w4v5u6t7s8r9";
    TEST_ASSERT(bs_int_from_str(b36_str, 36, &a));
    s = bs_int_to_str(a, 36);
    TEST_ASSERT(s != NULL && strcmp(s, b36_str) == 0);
    bs_free(s);
    bs_int_release(a);

    /* Invalid input error handling */
    TEST_ASSERT(!bs_int_from_str("", 10, &a));
    TEST_ASSERT(!bs_int_from_str("+", 10, &a));
    TEST_ASSERT(!bs_int_from_str("-", 10, &a));
    TEST_ASSERT(!bs_int_from_str("0x", 0, &a));
    TEST_ASSERT(!bs_int_from_str("123a", 10, &a));
    TEST_ASSERT(!bs_int_from_str("123", 1, &a));
    TEST_ASSERT(!bs_int_from_str("123", 37, &a));

    check_zero_leaks("test_string_formatting_and_parsing");
}

static void test_arithmetic_add_sub(void) {
    printf("Testing addition and subtraction...\n");

    /* Fixnum + Fixnum = Fixnum */
    bs_int a = bs_int_from_i64(100);
    bs_int b = bs_int_from_i64(250);
    bs_int c = bs_int_add(a, b);
    int64_t v = 0;
    TEST_ASSERT(bs_int_is_fixnum(c));
    TEST_ASSERT(bs_int_to_i64(c, &v) && v == 350);
    bs_int_release(c);

    /* Fixnum overflow promoting to Bignum */
    bs_int max_f = bs_int_from_i64(BS_FIXNUM_MAX);
    bs_int one = BS_INT_ONE;
    bs_int big = bs_int_add(max_f, one);
    TEST_ASSERT(bs_int_is_bignum(big));
    TEST_ASSERT(bs_int_to_i64(big, &v) && v == BS_FIXNUM_MAX + 1);

    /* Subtraction demoting Bignum back to Fixnum */
    bs_int back = bs_int_sub(big, one);
    TEST_ASSERT(bs_int_is_fixnum(back));
    TEST_ASSERT(bs_int_to_i64(back, &v) && v == BS_FIXNUM_MAX);

    /* Subtraction to zero */
    bs_int zero = bs_int_sub(big, big);
    TEST_ASSERT(bs_int_is_zero(zero));

    bs_int_release(a);
    bs_int_release(b);
    bs_int_release(max_f);
    bs_int_release(big);
    bs_int_release(back);
    bs_int_release(zero);

    check_zero_leaks("test_arithmetic_add_sub");
}

static void test_arithmetic_mul(void) {
    printf("Testing multiplication...\n");

    /* Small multiplication */
    bs_int a = bs_int_from_i64(-12345);
    bs_int b = bs_int_from_i64(6789);
    bs_int c = bs_int_mul(a, b);
    int64_t v;
    TEST_ASSERT(bs_int_to_i64(c, &v) && v == (int64_t)-12345 * 6789);
    bs_int_release(a);
    bs_int_release(b);
    bs_int_release(c);

    /* Large multi-limb multiplication */
    bs_int big1 = BS_INT_NULL, big2 = BS_INT_NULL;
    TEST_ASSERT(bs_int_from_str("10000000000000000000000000000000000000000", 10, &big1));
    TEST_ASSERT(bs_int_from_str("20000000000000000000000000000000000000000", 10, &big2));
    bs_int prod = bs_int_mul(big1, big2);

    char *s = bs_int_to_str(prod, 10);
    TEST_ASSERT(s != NULL);
    TEST_ASSERT(
        strcmp(
            s,
            "200000000000000000000000000000000000000000000000000000000000000000000000000000000") ==
        0);
    bs_free(s);

    bs_int_release(big1);
    bs_int_release(big2);
    bs_int_release(prod);

    check_zero_leaks("test_arithmetic_mul");
}

static void test_arithmetic_div_rem(void) {
    printf("Testing division and remainder (with invariant a == q*b + r)...\n");

    /* Signs: (+,+), (+,-), (-,+), (-,-) */
    int64_t num_cases[][2] = {{100, 7}, {100, -7}, {-100, 7}, {-100, -7}, {7, 100}, {-7, 100}};

    for (size_t i = 0; i < sizeof(num_cases) / sizeof(num_cases[0]); i++) {
        int64_t na = num_cases[i][0];
        int64_t nb = num_cases[i][1];
        bs_int a = bs_int_from_i64(na);
        bs_int b = bs_int_from_i64(nb);

        bs_int q = BS_INT_NULL, r = BS_INT_NULL;
        TEST_ASSERT(bs_int_div_rem(a, b, &q, &r));

        int64_t q_val, r_val;
        TEST_ASSERT(bs_int_to_i64(q, &q_val) && q_val == (na / nb));
        TEST_ASSERT(bs_int_to_i64(r, &r_val) && r_val == (na % nb));

        /* Invariant check: q * b + r == a */
        bs_int q_mul_b = bs_int_mul(q, b);
        bs_int restored = bs_int_add(q_mul_b, r);
        TEST_ASSERT(bs_int_eq(restored, a));

        bs_int_release(q_mul_b);
        bs_int_release(restored);
        bs_int_release(q);
        bs_int_release(r);
        bs_int_release(a);
        bs_int_release(b);
    }

    /* Division by zero */
    bs_int a = bs_int_from_i64(42);
    bs_int q = BS_INT_NULL, r = BS_INT_NULL;
    TEST_ASSERT(!bs_int_div_rem(a, BS_INT_ZERO, &q, &r));
    bs_int div_zero = bs_int_div(a, BS_INT_ZERO);
    TEST_ASSERT(bs_int_is_null(div_zero));
    bs_int_release(a);

    /* Test numerator with fewer limbs than denominator (a < b across limbs) */
    bs_int small_pos = bs_int_from_i64(7);
    bs_int large_pos = bs_int_shl(BS_INT_ONE, 100);
    bs_int q_small = BS_INT_NULL, r_small = BS_INT_NULL;
    TEST_ASSERT(bs_int_div_rem(small_pos, large_pos, &q_small, &r_small));
    TEST_ASSERT(bs_int_is_zero(q_small));
    TEST_ASSERT(bs_int_eq(r_small, small_pos));
    bs_int_release(q_small);
    bs_int_release(r_small);

    /* Negative small numerator */
    bs_int small_neg = bs_int_from_i64(-7);
    TEST_ASSERT(bs_int_div_rem(small_neg, large_pos, &q_small, &r_small));
    TEST_ASSERT(bs_int_is_zero(q_small));
    TEST_ASSERT(bs_int_eq(r_small, small_neg));
    bs_int_release(q_small);
    bs_int_release(r_small);

    /* String-based large division: 10^15 / 10^30 */
    bs_int n_10_15 = BS_INT_NULL, n_10_30 = BS_INT_NULL;
    TEST_ASSERT(bs_int_from_str("1000000000000000", 10, &n_10_15));
    TEST_ASSERT(bs_int_from_str("1000000000000000000000000000000", 10, &n_10_30));
    TEST_ASSERT(bs_int_div_rem(n_10_15, n_10_30, &q_small, &r_small));
    TEST_ASSERT(bs_int_is_zero(q_small));
    TEST_ASSERT(bs_int_eq(r_small, n_10_15));
    bs_int_release(q_small);
    bs_int_release(r_small);

    /* Equal large values: 10^30 / 10^30 == 1 (rem 0) */
    TEST_ASSERT(bs_int_div_rem(n_10_30, n_10_30, &q_small, &r_small));
    TEST_ASSERT(bs_int_eq(q_small, BS_INT_ONE));
    TEST_ASSERT(bs_int_is_zero(r_small));
    bs_int_release(q_small);
    bs_int_release(r_small);

    /* BS_FIXNUM_MIN / -1 promotes to bignum +2^62 */
    bs_int fix_min = bs_int_from_i64(BS_FIXNUM_MIN);
    bs_int neg_one = bs_int_from_i64(-1);
    TEST_ASSERT(bs_int_div_rem(fix_min, neg_one, &q_small, &r_small));
    TEST_ASSERT(bs_int_is_bignum(q_small));
    TEST_ASSERT(bs_int_is_positive(q_small));
    TEST_ASSERT(bs_int_is_zero(r_small));
    bs_int_release(q_small);
    bs_int_release(r_small);
    bs_int_release(fix_min);
    bs_int_release(neg_one);

    bs_int_release(n_10_15);
    bs_int_release(n_10_30);
    bs_int_release(small_pos);
    bs_int_release(small_neg);
    bs_int_release(large_pos);

    check_zero_leaks("test_arithmetic_div_rem");
}

static void test_power_and_number_theory(void) {
    printf("Testing pow, gcd, and lcm...\n");

    /* Pow */
    bs_int two = bs_int_from_i64(2);
    bs_int pow_val = bs_int_pow(two, 64);
    char *s = bs_int_to_str(pow_val, 16);
    TEST_ASSERT(s != NULL && strcmp(s, "10000000000000000") == 0);
    bs_free(s);

    /* 0^0 == 1 */
    bs_int zero_pow_zero = bs_int_pow(BS_INT_ZERO, 0);
    TEST_ASSERT(bs_int_eq(zero_pow_zero, BS_INT_ONE));
    bs_int_release(zero_pow_zero);

    /* GCD and LCM */
    bs_int n48 = bs_int_from_i64(48);
    bs_int n18 = bs_int_from_i64(-18);
    bs_int g = bs_int_gcd(n48, n18);
    int64_t g_val;
    TEST_ASSERT(bs_int_to_i64(g, &g_val) && g_val == 6);

    bs_int l = bs_int_lcm(n48, n18);
    int64_t l_val;
    TEST_ASSERT(bs_int_to_i64(l, &l_val) && l_val == 144);

    bs_int_release(two);
    bs_int_release(pow_val);
    bs_int_release(n48);
    bs_int_release(n18);
    bs_int_release(g);
    bs_int_release(l);

    check_zero_leaks("test_power_and_number_theory");
}

static void test_bitwise_operations(void) {
    printf("Testing bitwise operations (infinite two's-complement)...\n");

    bs_int a = bs_int_from_i64(12); /* 0...01100 */
    bs_int b = bs_int_from_i64(10); /* 0...01010 */

    bs_int and_res = bs_int_and(a, b); /* 8 */
    bs_int or_res = bs_int_or(a, b);   /* 14 */
    bs_int xor_res = bs_int_xor(a, b); /* 6 */
    bs_int not_res = bs_int_not(a);    /* -13 */

    int64_t v;
    TEST_ASSERT(bs_int_to_i64(and_res, &v) && v == 8);
    TEST_ASSERT(bs_int_to_i64(or_res, &v) && v == 14);
    TEST_ASSERT(bs_int_to_i64(xor_res, &v) && v == 6);
    TEST_ASSERT(bs_int_to_i64(not_res, &v) && v == -13);

    bs_int_release(and_res);
    bs_int_release(or_res);
    bs_int_release(xor_res);
    bs_int_release(not_res);

    /* Negative XOR Negative (should be positive) */
    bs_int neg1 = bs_int_from_i64(-1);
    bs_int neg2 = bs_int_from_i64(-2);
    bs_int neg_xor = bs_int_xor(neg1, neg2);
    TEST_ASSERT(bs_int_to_i64(neg_xor, &v) && v == 1);
    bs_int_release(neg_xor);

    bs_int_release(a);
    bs_int_release(b);
    bs_int_release(neg1);
    bs_int_release(neg2);

    check_zero_leaks("test_bitwise_operations");
}

static void test_shifts_and_p0_bug_regression(void) {
    printf("Testing bit shifts and P0 right-shift overflow bug fix...\n");

    /* Basic shift left */
    bs_int one = BS_INT_ONE;
    bs_int shl64 = bs_int_shl(one, 64);
    TEST_ASSERT(bs_int_is_bignum(shl64));
    char *s = bs_int_to_str(shl64, 16);
    TEST_ASSERT(s != NULL && strcmp(s, "10000000000000000") == 0);
    bs_free(s);

    /* Basic shift right */
    bs_int shr64 = bs_int_shr(shl64, 64);
    TEST_ASSERT(bs_int_eq(shr64, BS_INT_ONE));
    bs_int_release(shr64);
    bs_int_release(shl64);

    /* Negative integer right shift (arithmetic floor towards -inf) */
    bs_int neg7 = bs_int_from_i64(-7);
    bs_int neg_shr1 = bs_int_shr(neg7, 1); /* floor(-7 / 2) == -4 */
    int64_t v;
    TEST_ASSERT(bs_int_to_i64(neg_shr1, &v) && v == -4);
    bs_int_release(neg7);
    bs_int_release(neg_shr1);

    /* REGRESSION TEST FOR P0 BUG:
     * x = - ( (2^64 - 1) * 2^64 + 1 )
     * Limb representation: magnitude has limb[0]=1, limb[1]=UINT64_MAX, sign = -1.
     * x in hex: -0xffffffffffffffff0000000000000001
     * x >> 64 must be floor(x / 2^64) = - (2^64 - 1) - 1 = - 2^64.
     * Previously, carry-out from limb 0 overflowed limb 1 and wrapped to 0!
     */
    uint64_t words[2] = {1, UINT64_MAX};
    bs_int p0_x = bs_int_from_words(words, 2, -1);
    TEST_ASSERT(!bs_int_is_null(p0_x));

    bs_int p0_shifted = bs_int_shr(p0_x, 64);
    TEST_ASSERT(!bs_int_is_null(p0_shifted));
    TEST_ASSERT(!bs_int_is_zero(p0_shifted));
    TEST_ASSERT(bs_int_is_negative(p0_shifted));

    char *p0_str = bs_int_to_str(p0_shifted, 16);
    TEST_ASSERT(p0_str != NULL);
    TEST_ASSERT(strcmp(p0_str, "-10000000000000000") == 0);
    bs_free(p0_str);

    bs_int_release(p0_x);
    bs_int_release(p0_shifted);

    check_zero_leaks("test_shifts_and_p0_bug_regression");
}

static void test_bit_metrics(void) {
    printf("Testing bit length and popcount...\n");

    bs_int zero = BS_INT_ZERO;
    TEST_ASSERT(bs_int_bit_len(zero) == 0);
    TEST_ASSERT(bs_int_popcount(zero) == 0);

    bs_int one = BS_INT_ONE;
    TEST_ASSERT(bs_int_bit_len(one) == 1);
    TEST_ASSERT(bs_int_popcount(one) == 1);

    bs_int n7 = bs_int_from_i64(7); /* 0b111 */
    TEST_ASSERT(bs_int_bit_len(n7) == 3);
    TEST_ASSERT(bs_int_popcount(n7) == 3);
    bs_int_release(n7);

    /* 65-bit number */
    bs_int big = bs_int_shl(one, 64);
    TEST_ASSERT(bs_int_bit_len(big) == 65);
    TEST_ASSERT(bs_int_popcount(big) == 1);
    bs_int_release(big);

    check_zero_leaks("test_bit_metrics");
}

static void test_ref_counting(void) {
    printf("Testing reference counting and uniqueness...\n");

    /* Allocate bignum */
    bs_int a = BS_INT_NULL;
    TEST_ASSERT(bs_int_from_str("123456789012345678901234567890", 10, &a));
    TEST_ASSERT(bs_int_is_bignum(a));

    bs_bignum_t *b = bs_int_to_bignum(a);
    TEST_ASSERT(bs_atomic_load_acquire(&b->ref_count) == 1);

    bs_int retained = bs_int_retain(a);
    TEST_ASSERT(retained.raw == a.raw);
    TEST_ASSERT(bs_atomic_load_acquire(&b->ref_count) == 2);

    /* Clone deep copy */
    bs_bignum_t *clone = bs_bignum_clone(b);
    TEST_ASSERT(clone != NULL);
    TEST_ASSERT(clone != b);
    TEST_ASSERT(bs_atomic_load_acquire(&clone->ref_count) == 1);
    bs_int cloned_int = bs_int_from_bignum(clone);
    TEST_ASSERT(bs_int_eq(cloned_int, a));

    bs_int_release(cloned_int);
    bs_int_release(retained);
    TEST_ASSERT(bs_atomic_load_acquire(&b->ref_count) == 1);

    bs_int_release(a);

    check_zero_leaks("test_ref_counting");
}

static void test_allocator_lifecycle(void) {
    printf("Testing allocator interface and lifecycle...\n");

    /* 1. Verify default allocator */
    bs_allocator_t def = bs_allocator_default();
    TEST_ASSERT(def.alloc != NULL);
    TEST_ASSERT(def.realloc != NULL);
    TEST_ASSERT(def.free != NULL);
    TEST_ASSERT(def.user_data == NULL);

    /* 2. Verify all-or-nothing validation (reject incomplete allocators) */
    bs_allocator_t current = bs_get_allocator();
    TEST_ASSERT(current.alloc == tracking_alloc);
    TEST_ASSERT(current.realloc == tracking_realloc);
    TEST_ASSERT(current.free == tracking_free);

    bs_allocator_t invalid = def;
    invalid.alloc = NULL;
    TEST_ASSERT(!bs_set_allocator(invalid));
    TEST_ASSERT(bs_get_allocator().alloc == current.alloc);

    invalid = def;
    invalid.realloc = NULL;
    TEST_ASSERT(!bs_set_allocator(invalid));
    TEST_ASSERT(bs_get_allocator().realloc == current.realloc);

    invalid = def;
    invalid.free = NULL;
    TEST_ASSERT(!bs_set_allocator(invalid));
    TEST_ASSERT(bs_get_allocator().free == current.free);

    /* 3. Test direct allocation and re-allocation with tracking allocator */
    void *p = bs_alloc(64);
    TEST_ASSERT(p != NULL);
    TEST_ASSERT(g_active_allocations == 1);
    p = bs_realloc(p, 128);
    TEST_ASSERT(p != NULL);
    TEST_ASSERT(g_active_allocations == 1);
    bs_free(p);
    TEST_ASSERT(g_active_allocations == 0);
    bs_free(NULL); /* Must be safe to pass NULL */

    check_zero_leaks("test_allocator_lifecycle");
}

static void test_null_handling(void) {
    printf("Testing BS_INT_NULL safety and validation...\n");

    TEST_ASSERT(bs_int_is_null(BS_INT_NULL));
    TEST_ASSERT(bs_int_to_str(BS_INT_NULL, 10) == NULL);

    int64_t v64 = 0;
    uint64_t uv64 = 0;
    TEST_ASSERT(!bs_int_to_i64(BS_INT_NULL, &v64));
    TEST_ASSERT(!bs_int_to_u64(BS_INT_NULL, &uv64));

    bs_int one = BS_INT_ONE;
    TEST_ASSERT(bs_int_is_null(bs_int_add(BS_INT_NULL, one)));
    TEST_ASSERT(bs_int_is_null(bs_int_add(one, BS_INT_NULL)));
    TEST_ASSERT(bs_int_is_null(bs_int_sub(BS_INT_NULL, one)));
    TEST_ASSERT(bs_int_is_null(bs_int_sub(one, BS_INT_NULL)));
    TEST_ASSERT(bs_int_is_null(bs_int_mul(BS_INT_NULL, one)));
    TEST_ASSERT(bs_int_is_null(bs_int_mul(one, BS_INT_NULL)));
    TEST_ASSERT(bs_int_is_null(bs_int_div(BS_INT_NULL, one)));
    TEST_ASSERT(bs_int_is_null(bs_int_div(one, BS_INT_NULL)));
    TEST_ASSERT(bs_int_is_null(bs_int_rem(BS_INT_NULL, one)));
    TEST_ASSERT(bs_int_is_null(bs_int_rem(one, BS_INT_NULL)));
    TEST_ASSERT(bs_int_is_null(bs_int_neg(BS_INT_NULL)));
    TEST_ASSERT(bs_int_is_null(bs_int_abs(BS_INT_NULL)));
    TEST_ASSERT(bs_int_is_null(bs_int_pow(BS_INT_NULL, 0)));
    TEST_ASSERT(bs_int_is_null(bs_int_shl(BS_INT_NULL, 1)));
    TEST_ASSERT(bs_int_is_null(bs_int_shr(BS_INT_NULL, 1)));
    TEST_ASSERT(bs_int_is_null(bs_int_and(BS_INT_NULL, one)));
    TEST_ASSERT(bs_int_is_null(bs_int_or(BS_INT_NULL, one)));
    TEST_ASSERT(bs_int_is_null(bs_int_xor(BS_INT_NULL, one)));
    TEST_ASSERT(bs_int_is_null(bs_int_not(BS_INT_NULL)));

    bs_int q = BS_INT_NULL, r = BS_INT_NULL;
    TEST_ASSERT(!bs_int_div_rem(BS_INT_NULL, one, &q, &r));
    TEST_ASSERT(!bs_int_div_rem(one, BS_INT_NULL, &q, &r));

    /* Equality rejects null handles. Ordered comparisons require valid inputs;
     * test their ordinary boundary behavior here. */
    TEST_ASSERT(!bs_int_eq(BS_INT_NULL, BS_INT_ZERO));
    TEST_ASSERT(bs_int_lt(BS_INT_NEG_ONE, BS_INT_ZERO));
    TEST_ASSERT(bs_int_lte(BS_INT_ZERO, BS_INT_ZERO));
    TEST_ASSERT(bs_int_gt(BS_INT_ONE, BS_INT_ZERO));
    TEST_ASSERT(bs_int_gte(BS_INT_ZERO, BS_INT_ZERO));

    check_zero_leaks("test_null_handling");
}

static void test_allocation_failures(void) {
    printf("Testing allocation failure handling...\n");

    /* Force the active tracking allocator to simulate allocation failure */
    g_fail_allocations = true;

    bs_int shifted = bs_int_shl(BS_INT_ONE, 64);
    TEST_ASSERT(bs_int_is_null(shifted));

    bs_int parsed = BS_INT_NEG_ONE;
    TEST_ASSERT(!bs_int_from_str("12345678901234567890", 10, &parsed));
    TEST_ASSERT(parsed.raw == BS_INT_NEG_ONE.raw);

    /* The initial dividend/divisor are fixnums, so the forced failure occurs
     * in result allocation and must leave both caller outputs unchanged. */
    bs_int q = BS_INT_NEG_ONE;
    bs_int r = BS_INT_ONE;
    bs_int dividend = bs_int_from_i64(BS_FIXNUM_MIN);
    bs_int divisor = BS_INT_NEG_ONE;
    TEST_ASSERT(!bs_int_div_rem(dividend, divisor, &q, &r));
    TEST_ASSERT(q.raw == BS_INT_NEG_ONE.raw);
    TEST_ASSERT(r.raw == BS_INT_ONE.raw);

    g_fail_allocations = false;
    check_zero_leaks("test_allocation_failures");
}

static void test_fixnum_fast_paths_and_aliasing(void) {
    printf("Testing fixnum fast paths, boundary promotion, and aliasing...\n");

    /* Negating BS_FIXNUM_MIN */
    bs_int min_f = bs_int_from_i64(BS_FIXNUM_MIN);
    bs_int neg_min = bs_int_neg(min_f);
    TEST_ASSERT(bs_int_is_bignum(neg_min));
    TEST_ASSERT(bs_int_is_positive(neg_min));
    char *s = bs_int_to_str(neg_min, 16);
    TEST_ASSERT(s != NULL && strcmp(s, "4000000000000000") == 0);
    bs_free(s);

    /* Abs on BS_FIXNUM_MIN */
    bs_int abs_min = bs_int_abs(min_f);
    TEST_ASSERT(bs_int_is_bignum(abs_min));
    TEST_ASSERT(bs_int_eq(abs_min, neg_min));
    bs_int_release(abs_min);
    bs_int_release(neg_min);

    /* Fixnum bitwise fast paths */
    bs_int f1 = bs_int_from_i64(-42);
    bs_int f2 = bs_int_from_i64(137);
    bs_int and_res = bs_int_and(f1, f2);
    bs_int or_res = bs_int_or(f1, f2);
    bs_int xor_res = bs_int_xor(f1, f2);
    bs_int not_res = bs_int_not(f1);

    int64_t v;
    TEST_ASSERT(bs_int_to_i64(and_res, &v) && v == (-42 & 137));
    TEST_ASSERT(bs_int_to_i64(or_res, &v) && v == (-42 | 137));
    TEST_ASSERT(bs_int_to_i64(xor_res, &v) && v == (-42 ^ 137));
    TEST_ASSERT(bs_int_to_i64(not_res, &v) && v == ~(-42));

    bs_int_release(and_res);
    bs_int_release(or_res);
    bs_int_release(xor_res);
    bs_int_release(not_res);

    /* Aliasing: operations where a and b are identical handles */
    bs_int sum_self = bs_int_add(f2, f2);
    TEST_ASSERT(bs_int_to_i64(sum_self, &v) && v == 274);
    bs_int_release(sum_self);

    bs_int sub_self = bs_int_sub(f2, f2);
    TEST_ASSERT(bs_int_is_zero(sub_self));
    bs_int_release(sub_self);

    bs_int mul_self = bs_int_mul(f2, f2);
    TEST_ASSERT(bs_int_to_i64(mul_self, &v) && v == 137 * 137);
    bs_int_release(mul_self);

    bs_int div_self = bs_int_div(f2, f2);
    TEST_ASSERT(bs_int_eq(div_self, BS_INT_ONE));
    bs_int_release(div_self);

    bs_int_release(f1);
    bs_int_release(f2);
    bs_int_release(min_f);

    check_zero_leaks("test_fixnum_fast_paths_and_aliasing");
}

static void test_phase4_modular_sqrt_bits_hash(void) {
    printf("Testing Phase 4: pow_mod, sqrt, test_bit, ctz, and hash...\n");

    /* 1. Modular exponentiation */
    bs_int b2 = bs_int_from_i64(2);
    bs_int e10 = bs_int_from_i64(10);
    bs_int m1000 = bs_int_from_i64(1000);
    bs_int pm = bs_int_pow_mod(b2, e10, m1000);
    int64_t v = 0;
    TEST_ASSERT(bs_int_to_i64(pm, &v) && v == 24);
    bs_int_release(pm);

    /* Negative base: (-3)^3 mod 10 == -27 mod 10 == 3 */
    bs_int b_neg3 = bs_int_from_i64(-3);
    bs_int e3 = bs_int_from_i64(3);
    bs_int m10 = bs_int_from_i64(10);
    pm = bs_int_pow_mod(b_neg3, e3, m10);
    TEST_ASSERT(bs_int_to_i64(pm, &v) && v == 3);
    bs_int_release(pm);
    bs_int_release(b_neg3);
    bs_int_release(e3);
    bs_int_release(m10);

    /* Zero exponent */
    bs_int e0 = bs_int_from_i64(0);
    pm = bs_int_pow_mod(b2, e0, m1000);
    TEST_ASSERT(bs_int_eq(pm, BS_INT_ONE));
    bs_int_release(pm);
    bs_int_release(e0);
    bs_int_release(b2);
    bs_int_release(e10);
    bs_int_release(m1000);

    /* Large bignum pow_mod */
    bs_int big_b = BS_INT_NULL, big_e = BS_INT_NULL, big_m = BS_INT_NULL;
    TEST_ASSERT(bs_int_from_str("12345678901234567890", 10, &big_b));
    TEST_ASSERT(bs_int_from_str("65537", 10, &big_e));
    TEST_ASSERT(bs_int_from_str("999999999999999999999999999999", 10, &big_m));
    bs_int big_pm = bs_int_pow_mod(big_b, big_e, big_m);
    TEST_ASSERT(!bs_int_is_null(big_pm));
    TEST_ASSERT(bs_int_lt(big_pm, big_m));
    TEST_ASSERT(!bs_int_is_negative(big_pm));
    bs_int_release(big_pm);
    bs_int_release(big_b);
    bs_int_release(big_e);
    bs_int_release(big_m);

    /* 2. Integer square root & remainder */
    bs_int n0 = bs_int_from_i64(0);
    bs_int s0 = BS_INT_NULL, r0 = BS_INT_NULL;
    TEST_ASSERT(bs_int_sqrt_rem(n0, &s0, &r0));
    TEST_ASSERT(bs_int_is_zero(s0) && bs_int_is_zero(r0));
    bs_int_release(n0);
    bs_int_release(s0);
    bs_int_release(r0);

    bs_int n10 = bs_int_from_i64(10);
    bs_int s10 = BS_INT_NULL, r10 = BS_INT_NULL;
    TEST_ASSERT(bs_int_sqrt_rem(n10, &s10, &r10));
    TEST_ASSERT(bs_int_to_i64(s10, &v) && v == 3);
    TEST_ASSERT(bs_int_to_i64(r10, &v) && v == 1);
    bs_int s10_direct = bs_int_sqrt(n10);
    TEST_ASSERT(bs_int_eq(s10_direct, s10));
    bs_int_release(s10_direct);
    bs_int_release(n10);
    bs_int_release(s10);
    bs_int_release(r10);

    /* Large bignum sqrt: (10^18)^2 + 42 */
    bs_int base_10_18 = BS_INT_NULL;
    TEST_ASSERT(bs_int_from_str("1000000000000000000", 10, &base_10_18));
    bs_int sq_10_18 = bs_int_mul(base_10_18, base_10_18);
    bs_int c42 = bs_int_from_i64(42);
    bs_int big_target = bs_int_add(sq_10_18, c42);
    bs_int big_s = BS_INT_NULL, big_r = BS_INT_NULL;
    TEST_ASSERT(bs_int_sqrt_rem(big_target, &big_s, &big_r));
    TEST_ASSERT(bs_int_eq(big_s, base_10_18));
    TEST_ASSERT(bs_int_to_i64(big_r, &v) && v == 42);
    bs_int_release(base_10_18);
    bs_int_release(sq_10_18);
    bs_int_release(c42);
    bs_int_release(big_target);
    bs_int_release(big_s);
    bs_int_release(big_r);

    /* Negative input rejected */
    bs_int neg5 = bs_int_from_i64(-5);
    TEST_ASSERT(bs_int_is_null(bs_int_sqrt(neg5)));
    bs_int_release(neg5);

    /* 3. Bit testing & ctz */
    bs_int n12 = bs_int_from_i64(12); /* 0b1100 */
    TEST_ASSERT(!bs_int_test_bit(n12, 0));
    TEST_ASSERT(!bs_int_test_bit(n12, 1));
    TEST_ASSERT(bs_int_test_bit(n12, 2));
    TEST_ASSERT(bs_int_test_bit(n12, 3));
    TEST_ASSERT(!bs_int_test_bit(n12, 4));
    TEST_ASSERT(bs_int_ctz(n12) == 2);
    bs_int_release(n12);

    /* Negative bit testing: -1 has all bits set */
    bs_int neg1 = bs_int_from_i64(-1);
    TEST_ASSERT(bs_int_test_bit(neg1, 0));
    TEST_ASSERT(bs_int_test_bit(neg1, 62));
    TEST_ASSERT(bs_int_test_bit(neg1, 100));
    TEST_ASSERT(bs_int_ctz(neg1) == 0);
    bs_int_release(neg1);

    /* -4 has bits 0 and 1 as 0, bit 2 as 1 */
    bs_int neg4 = bs_int_from_i64(-4);
    TEST_ASSERT(!bs_int_test_bit(neg4, 0));
    TEST_ASSERT(!bs_int_test_bit(neg4, 1));
    TEST_ASSERT(bs_int_test_bit(neg4, 2));
    TEST_ASSERT(bs_int_test_bit(neg4, 3));
    TEST_ASSERT(bs_int_test_bit(neg4, 100));
    TEST_ASSERT(bs_int_ctz(neg4) == 2);
    bs_int_release(neg4);

    /* Multi-limb positive and negative bignum bit testing */
    /* big_pos = 2^100 + 2^5 */
    bs_int bit100 = bs_int_shl(BS_INT_ONE, 100);
    bs_int bit5 = bs_int_shl(BS_INT_ONE, 5);
    bs_int big_pos = bs_int_or(bit100, bit5);
    TEST_ASSERT(bs_int_test_bit(big_pos, 5));
    TEST_ASSERT(bs_int_test_bit(big_pos, 100));
    TEST_ASSERT(!bs_int_test_bit(big_pos, 0));
    TEST_ASSERT(!bs_int_test_bit(big_pos, 4));
    TEST_ASSERT(!bs_int_test_bit(big_pos, 6));
    TEST_ASSERT(!bs_int_test_bit(big_pos, 99));
    TEST_ASSERT(!bs_int_test_bit(big_pos, 101));
    TEST_ASSERT(!bs_int_test_bit(big_pos, 200));

    /* big_neg = - (2^100 + 2^5) */
    bs_int big_neg = bs_int_neg(big_pos);
    /* In two's complement, -X has:
     * - bits 0..4 as 0
     * - bit 5 as 1 (least significant set bit remains 1)
     * - bits 6..99 inverted from X (which were 0, so now 1)
     * - bit 100 inverted from X (which was 1, so now 0)
     * - bits >= 101 as 1 (sign extension) */
    TEST_ASSERT(!bs_int_test_bit(big_neg, 0));
    TEST_ASSERT(!bs_int_test_bit(big_neg, 4));
    TEST_ASSERT(bs_int_test_bit(big_neg, 5));
    TEST_ASSERT(bs_int_test_bit(big_neg, 6));
    TEST_ASSERT(bs_int_test_bit(big_neg, 63));
    TEST_ASSERT(bs_int_test_bit(big_neg, 64));
    TEST_ASSERT(bs_int_test_bit(big_neg, 99));
    TEST_ASSERT(!bs_int_test_bit(big_neg, 100));
    TEST_ASSERT(bs_int_test_bit(big_neg, 101));
    TEST_ASSERT(bs_int_test_bit(big_neg, 200));

    /* Negative bignum with zero limb in middle: -2^128 */
    uint64_t pow128_words[3] = {0, 0, 1};
    bs_int neg_pow128 = bs_int_from_words(pow128_words, 3, -1);
    TEST_ASSERT(!bs_int_test_bit(neg_pow128, 0));
    TEST_ASSERT(!bs_int_test_bit(neg_pow128, 63));
    TEST_ASSERT(!bs_int_test_bit(neg_pow128, 64));
    TEST_ASSERT(!bs_int_test_bit(neg_pow128, 127));
    TEST_ASSERT(bs_int_test_bit(neg_pow128, 128));
    TEST_ASSERT(bs_int_test_bit(neg_pow128, 129));
    TEST_ASSERT(bs_int_test_bit(neg_pow128, 300));
    bs_int_release(neg_pow128);

    /* Predicate tests on bignums */
    TEST_ASSERT(bs_int_is_even(big_pos));
    TEST_ASSERT(!bs_int_is_odd(big_pos));
    TEST_ASSERT(bs_int_is_even(big_neg));
    TEST_ASSERT(!bs_int_is_odd(big_neg));
    TEST_ASSERT(bs_int_is_positive(big_pos));
    TEST_ASSERT(!bs_int_is_negative(big_pos));
    TEST_ASSERT(!bs_int_is_positive(big_neg));
    TEST_ASSERT(bs_int_is_negative(big_neg));
    TEST_ASSERT(bs_int_sign(big_pos) == 1);
    TEST_ASSERT(bs_int_sign(big_neg) == -1);

    bs_int_release(bit100);
    bs_int_release(bit5);
    bs_int_release(big_pos);
    bs_int_release(big_neg);

    /* Zero ctz */
    TEST_ASSERT(bs_int_ctz(BS_INT_ZERO) == 0);

    /* 4. Hashing */
    bs_int h1 = bs_int_from_i64(123456);
    bs_int h2 = bs_int_from_i64(123456);
    bs_int h3 = bs_int_from_i64(-123456);
    TEST_ASSERT(bs_int_hash(h1) == bs_int_hash(h2));
    TEST_ASSERT(bs_int_hash(h1) != bs_int_hash(h3));

    bs_int bhash1 = BS_INT_NULL, bhash2 = BS_INT_NULL;
    TEST_ASSERT(bs_int_from_str("123456789012345678901234567890", 10, &bhash1));
    TEST_ASSERT(bs_int_from_str("123456789012345678901234567890", 10, &bhash2));
    TEST_ASSERT(bs_int_hash(bhash1) == bs_int_hash(bhash2));

    bs_int_release(h1);
    bs_int_release(h2);
    bs_int_release(h3);
    bs_int_release(bhash1);
    bs_int_release(bhash2);

    check_zero_leaks("test_phase4_modular_sqrt_bits_hash");
}

static void test_internal_intrinsics(void) {
    printf("Testing internal bitwise intrinsics and overflow arithmetic...\n");

    TEST_ASSERT(bs_clz64(1ULL) == 63);
    TEST_ASSERT(bs_clz64(0x8000000000000000ULL) == 0);
    TEST_ASSERT(bs_clz64(0ULL) == 64);
    TEST_ASSERT(bs_ctz64(1ULL) == 0);
    TEST_ASSERT(bs_ctz64(8ULL) == 3);
    TEST_ASSERT(bs_ctz64(0ULL) == 64);
    TEST_ASSERT(bs_popcount64(0ULL) == 0);
    TEST_ASSERT(bs_popcount64(7ULL) == 3);
    TEST_ASSERT(bs_splitmix64(0ULL) != 0);
    TEST_ASSERT(bs_splitmix64(1ULL) != bs_splitmix64(2ULL));

    /* Unsigned overflow tests */
    uint64_t ures = 0;
    TEST_ASSERT(!bs_add_overflow(10ULL, 20ULL, &ures) && ures == 30);
    TEST_ASSERT(bs_add_overflow(UINT64_MAX, 1ULL, &ures));
    TEST_ASSERT(!bs_sub_overflow(20ULL, 10ULL, &ures) && ures == 10);
    TEST_ASSERT(bs_sub_overflow(10ULL, 20ULL, &ures));
    TEST_ASSERT(!bs_mul_overflow(10ULL, 20ULL, &ures) && ures == 200);
    TEST_ASSERT(bs_mul_overflow(UINT64_MAX / 2 + 1, 2ULL, &ures));
    TEST_ASSERT(bs_mul_overflow(UINT64_MAX, UINT64_MAX, &ures));

    /* Direct unsigned fallback tests */
    TEST_ASSERT(!bs_add_overflow_u64(10ULL, 20ULL, &ures) && ures == 30);
    TEST_ASSERT(bs_add_overflow_u64(UINT64_MAX, 1ULL, &ures));
    TEST_ASSERT(!bs_sub_overflow_u64(20ULL, 10ULL, &ures) && ures == 10);
    TEST_ASSERT(bs_sub_overflow_u64(10ULL, 20ULL, &ures));
    TEST_ASSERT(!bs_mul_overflow_u64(10ULL, 20ULL, &ures) && ures == 200);
    TEST_ASSERT(bs_mul_overflow_u64(UINT64_MAX / 2 + 1, 2ULL, &ures));

    /* Signed overflow tests */
    int64_t sres = 0;
    TEST_ASSERT(!bs_add_overflow(INT64_C(10), INT64_C(20), &sres) && sres == 30);
    TEST_ASSERT(bs_add_overflow(INT64_MAX, INT64_C(1), &sres));
    TEST_ASSERT(bs_add_overflow(INT64_MIN, INT64_C(-1), &sres));
    TEST_ASSERT(!bs_sub_overflow(INT64_C(10), INT64_C(20), &sres) && sres == -10);
    TEST_ASSERT(bs_sub_overflow(INT64_MIN, INT64_C(1), &sres));
    TEST_ASSERT(bs_sub_overflow(INT64_MAX, INT64_C(-1), &sres));
    TEST_ASSERT(!bs_mul_overflow(INT64_C(-7), INT64_C(8), &sres) && sres == -56);
    TEST_ASSERT(bs_mul_overflow(INT64_MAX, INT64_C(2), &sres));
    TEST_ASSERT(bs_mul_overflow(INT64_MIN, INT64_C(-1), &sres));
    TEST_ASSERT(bs_mul_overflow(INT64_MIN, INT64_C(2), &sres));
    TEST_ASSERT(bs_mul_overflow(INT64_MIN, INT64_MIN, &sres));

    /* Direct signed fallback tests */
    TEST_ASSERT(!bs_add_overflow_i64(INT64_C(10), INT64_C(20), &sres) && sres == 30);
    TEST_ASSERT(bs_add_overflow_i64(INT64_MAX, INT64_C(1), &sres));
    TEST_ASSERT(bs_add_overflow_i64(INT64_MIN, INT64_C(-1), &sres));
    TEST_ASSERT(!bs_sub_overflow_i64(INT64_C(10), INT64_C(20), &sres) && sres == -10);
    TEST_ASSERT(bs_sub_overflow_i64(INT64_MIN, INT64_C(1), &sres));
    TEST_ASSERT(bs_sub_overflow_i64(INT64_MAX, INT64_C(-1), &sres));
    TEST_ASSERT(!bs_mul_overflow_i64(INT64_C(-7), INT64_C(8), &sres) && sres == -56);
    TEST_ASSERT(bs_mul_overflow_i64(INT64_MAX, INT64_C(2), &sres));
    TEST_ASSERT(bs_mul_overflow_i64(INT64_MIN, INT64_C(-1), &sres));
    TEST_ASSERT(bs_mul_overflow_i64(INT64_MIN, INT64_C(2), &sres));
    TEST_ASSERT(bs_mul_overflow_i64(INT64_MIN, INT64_MIN, &sres));
}

/* ========================================================================= */
/* Main Test Runner                                                          */
/* ========================================================================= */

int main(void) {
    printf("=========================================\n");
    printf("Running comprehensive bs_num test suite\n");
    printf("=========================================\n");

    setup_tracking_allocator();

    test_allocator_lifecycle();
    test_internal_intrinsics();
    test_null_handling();
    test_allocation_failures();
    test_constructors_and_i64_conversions();
    test_words_conversions();
    test_string_formatting_and_parsing();
    test_arithmetic_add_sub();
    test_arithmetic_mul();
    test_arithmetic_div_rem();
    test_power_and_number_theory();
    test_bitwise_operations();
    test_shifts_and_p0_bug_regression();
    test_fixnum_fast_paths_and_aliasing();
    test_bit_metrics();
    test_phase4_modular_sqrt_bits_hash();
    test_ref_counting();

    printf("\nAll bs_num tests passed with 0 memory leaks!\n");
    return 0;
}
