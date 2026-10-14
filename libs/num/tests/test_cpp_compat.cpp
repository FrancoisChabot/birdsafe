/**
 * @file test_cpp_compat.cpp
 * @brief C++ language compatibility test for bs_num public headers.
 */

#include <bs_num.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#define TEST_ASSERT(cond)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(                                                                          \
                stderr, "C++ Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__);         \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

int main() {
    std::printf("Testing C++ header compatibility...\n");

    /* Constants */
    bs_int z = BS_INT_ZERO;
    bs_int one = BS_INT_ONE;
    bs_int null_handle = BS_INT_NULL;

    TEST_ASSERT(bs_int_is_zero(z));
    TEST_ASSERT(!bs_int_is_zero(one));
    TEST_ASSERT(bs_int_is_null(null_handle));

    /* Basic arithmetic in C++ */
    bs_int a = bs_int_from_i64(100);
    bs_int b = bs_int_from_i64(200);
    bs_int sum = bs_int_add(a, b);

    int64_t val = 0;
    TEST_ASSERT(bs_int_to_i64(sum, &val));
    TEST_ASSERT(val == 300);

    bs_int_release(sum);
    bs_int_release(b);
    bs_int_release(a);

    /* C++ call to Phase 4 APIs */
    bs_int base = bs_int_from_i64(5);
    bs_int exp = bs_int_from_i64(3);
    bs_int mod = bs_int_from_i64(13);
    bs_int pm = bs_int_pow_mod(base, exp, mod); /* 125 mod 13 = 8 */
    int64_t pm_val = 0;
    TEST_ASSERT(bs_int_to_i64(pm, &pm_val) && pm_val == 8);
    TEST_ASSERT(bs_int_test_bit(pm, 3));
    TEST_ASSERT(bs_int_hash(pm) != 0);

    bs_int sq = bs_int_sqrt(pm); /* sqrt(8) = 2 */
    int64_t sq_val = 0;
    TEST_ASSERT(bs_int_to_i64(sq, &sq_val) && sq_val == 2);

    bs_int_release(sq);
    bs_int_release(pm);
    bs_int_release(base);
    bs_int_release(exp);
    bs_int_release(mod);

    std::printf("C++ compatibility test passed successfully!\n");
    return 0;
}
