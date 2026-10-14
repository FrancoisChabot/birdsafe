#include "bs_limb.h"
#include "bs_intrinsics.h"
#include "bs_core/bs_alloc.h"
#include <string.h>

static int compare_magnitudes(const uint64_t *a, size_t a_len, const uint64_t *b, size_t b_len) {
    a_len = bs_limbs_normalized_len(a, a_len);
    b_len = bs_limbs_normalized_len(b, b_len);

    if (a_len != b_len)
        return a_len > b_len ? 1 : -1;

    while (a_len > 0) {
        --a_len;
        if (a[a_len] != b[a_len])
            return a[a_len] > b[a_len] ? 1 : -1;
    }
    return 0;
}

bs_limb_t bs_limbs_add_1(uint64_t *result, const uint64_t *a, size_t len, uint64_t b) {
    uint64_t carry = b;
    size_t i = 0;

    for (; i < len && carry; ++i) {
        uint64_t value = a[i];
        result[i] = value + carry;
        carry = result[i] < value;
    }
    for (; i < len; ++i)
        result[i] = a[i];
    return carry;
}

bs_limb_t
bs_limbs_add(uint64_t *result, const uint64_t *a, size_t a_len, const uint64_t *b, size_t b_len) {
    uint64_t carry = 0;

    for (size_t i = 0; i < a_len; ++i) {
        uint64_t a_word = a[i];
        uint64_t b_word = i < b_len ? b[i] : 0;
        uint64_t partial = a_word + b_word;
        uint64_t carry_from_sum = partial < a_word;
        uint64_t sum = partial + carry;
        uint64_t carry_from_carry = sum < partial;

        result[i] = sum;
        carry = carry_from_sum | carry_from_carry;
    }
    return carry;
}

bs_limb_t bs_limbs_sub_1(uint64_t *result, const uint64_t *a, size_t len, uint64_t b) {
    if (len == 0)
        return b != 0;

    uint64_t first = a[0];
    result[0] = first - b;
    uint64_t borrow = first < b;

    for (size_t i = 1; i < len; ++i) {
        uint64_t word = a[i];
        result[i] = word - borrow;
        borrow = word < borrow;
    }
    return borrow;
}

bs_limb_t
bs_limbs_sub(uint64_t *result, const uint64_t *a, size_t a_len, const uint64_t *b, size_t b_len) {
    uint64_t borrow = 0;

    for (size_t i = 0; i < a_len; ++i) {
        uint64_t a_word = a[i];
        uint64_t b_word = i < b_len ? b[i] : 0;
        uint64_t partial = a_word - b_word;
        uint64_t borrow_from_sub = a_word < b_word;
        uint64_t difference = partial - borrow;
        uint64_t borrow_from_borrow = partial < borrow;

        result[i] = difference;
        borrow = borrow_from_sub | borrow_from_borrow;
    }
    return borrow;
}

void bs_limb_mul_words(uint64_t a, uint64_t b, uint64_t *low, uint64_t *high) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    *low = _umul128(a, b, high);
#elif defined(BS_HAS_INT128)
    bs_uint128_t product = (bs_uint128_t)a * b;
    *low = (uint64_t)product;
    *high = (uint64_t)(product >> 64);
#else
    uint64_t a_low = (uint32_t)a;
    uint64_t a_high = a >> 32;
    uint64_t b_low = (uint32_t)b;
    uint64_t b_high = b >> 32;
    uint64_t p0 = a_low * b_low;
    uint64_t p1 = a_low * b_high;
    uint64_t p2 = a_high * b_low;
    uint64_t p3 = a_high * b_high;
    uint64_t middle = (p0 >> 32) + (uint32_t)p1 + (uint32_t)p2;

    *low = (middle << 32) | (uint32_t)p0;
    *high = p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32);
#endif
}

uint64_t bs_limbs_mul_1(uint64_t *result, const uint64_t *a, size_t len, uint64_t b) {
    uint64_t carry = 0;

    for (size_t i = 0; i < len; ++i) {
        uint64_t low;
        uint64_t high;
        bs_limb_mul_words(a[i], b, &low, &high);

        uint64_t product = low + carry;
        high += product < low;
        result[i] = product;
        carry = high;
    }
    return carry;
}

void bs_limbs_mul(
    uint64_t *result, const uint64_t *a, size_t a_len, const uint64_t *b, size_t b_len) {
    memset(result, 0, (a_len + b_len) * sizeof(*result));

    for (size_t i = 0; i < a_len; ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b_len; ++j) {
            uint64_t low;
            uint64_t high;
            bs_limb_mul_words(a[i], b[j], &low, &high);

            uint64_t current = result[i + j];
            uint64_t partial = current + low;
            uint64_t carry_from_sum = partial < current;
            uint64_t sum = partial + carry;
            uint64_t carry_from_carry = sum < partial;

            result[i + j] = sum;
            carry = high + carry_from_sum + carry_from_carry;
        }

        size_t k = i + b_len;
        while (carry && k < a_len + b_len) {
            uint64_t current = result[k];
            result[k] = current + carry;
            carry = result[k] < current;
            ++k;
        }
    }
}

int bs_limbs_cmp(const uint64_t *a, size_t a_len, const uint64_t *b, size_t b_len) {
    return compare_magnitudes(a, a_len, b, b_len);
}

static int bit_at(const uint64_t *words, size_t bit) {
    return (int)((words[bit / 64] >> (bit % 64)) & 1);
}

static void div_1(uint64_t *quotient,
                  size_t quotient_len,
                  uint64_t *remainder,
                  const uint64_t *numerator,
                  size_t numerator_len,
                  uint64_t d) {
    if (quotient && quotient_len > 0)
        memset(quotient, 0, quotient_len * sizeof(*quotient));
    uint64_t rem = 0;
    for (size_t i = numerator_len; i-- > 0;) {
#if defined(BS_HAS_INT128)
        __uint128_t cur = ((__uint128_t)rem << 64) | numerator[i];
        uint64_t q = (uint64_t)(cur / d);
        rem = (uint64_t)(cur % d);
#else
        uint64_t q = 0, r = rem;
        for (int bit = 63; bit >= 0; --bit) {
            uint64_t hi = r >> 63;
            r = (r << 1) | ((numerator[i] >> bit) & 1);
            if (hi || r >= d) {
                r -= d;
                q |= UINT64_C(1) << bit;
            }
        }
        rem = r;
#endif
        if (quotient && i < quotient_len)
            quotient[i] = q;
    }
    remainder[0] = rem;
}

static void divide_bits(uint64_t *quotient,
                        size_t quotient_len,
                        uint64_t *remainder,
                        const uint64_t *numerator,
                        size_t numerator_len,
                        const uint64_t *denominator,
                        size_t denominator_len) {
    if (quotient && quotient_len > 0)
        memset(quotient, 0, quotient_len * sizeof(*quotient));
    memset(remainder, 0, denominator_len * sizeof(*remainder));

    size_t bit_count = numerator_len * 64;
    while (bit_count > 0) {
        --bit_count;
        uint64_t carry = (uint64_t)bit_at(numerator, bit_count);

        for (size_t i = 0; i < denominator_len; ++i) {
            uint64_t next_carry = remainder[i] >> 63;
            remainder[i] = (remainder[i] << 1) | carry;
            carry = next_carry;
        }

        if (carry ||
            compare_magnitudes(remainder, denominator_len, denominator, denominator_len) >= 0) {
            uint64_t borrow = 0;
            for (size_t i = 0; i < denominator_len; ++i) {
                uint64_t r_word = remainder[i];
                uint64_t d_word = denominator[i];
                uint64_t partial = r_word - d_word;
                uint64_t borrow_from_sub = r_word < d_word;
                uint64_t difference = partial - borrow;
                uint64_t borrow_from_borrow = partial < borrow;

                remainder[i] = difference;
                borrow = borrow_from_sub | borrow_from_borrow;
            }

            if (quotient && bit_count / 64 < quotient_len)
                quotient[bit_count / 64] |= UINT64_C(1) << (bit_count % 64);
        }
    }
}

#if defined(BS_HAS_INT128)
static void div_knuth(uint64_t *quotient,
                      size_t quotient_len,
                      uint64_t *remainder,
                      const uint64_t *numerator,
                      size_t numerator_len,
                      const uint64_t *denominator,
                      size_t denominator_len) {
    if (quotient && quotient_len > 0)
        memset(quotient, 0, quotient_len * sizeof(*quotient));
    memset(remainder, 0, denominator_len * sizeof(*remainder));

    size_t n = denominator_len;
    size_t m = numerator_len - n;

    uint64_t stack_u[128];
    uint64_t stack_v[64];
    uint64_t *u = (numerator_len + 1 <= 128)
                      ? stack_u
                      : (uint64_t *)bs_alloc((numerator_len + 1) * sizeof(uint64_t));
    uint64_t *v = (n <= 64) ? stack_v : (uint64_t *)bs_alloc(n * sizeof(uint64_t));
    if (!u || !v) {
        if (u && u != stack_u)
            bs_free(u);
        if (v && v != stack_v)
            bs_free(v);
        divide_bits(quotient,
                    quotient_len,
                    remainder,
                    numerator,
                    numerator_len,
                    denominator,
                    denominator_len);
        return;
    }

    size_t s = bs_clz64(denominator[n - 1]);
    if (s == 0) {
        memcpy(v, denominator, n * sizeof(uint64_t));
        memcpy(u, numerator, numerator_len * sizeof(uint64_t));
        u[numerator_len] = 0;
    } else {
        for (size_t i = n - 1; i > 0; --i)
            v[i] = (denominator[i] << s) | (denominator[i - 1] >> (64 - s));
        v[0] = denominator[0] << s;

        u[numerator_len] = numerator[numerator_len - 1] >> (64 - s);
        for (size_t i = numerator_len - 1; i > 0; --i)
            u[i] = (numerator[i] << s) | (numerator[i - 1] >> (64 - s));
        u[0] = numerator[0] << s;
    }

    for (size_t j = m + 1; j-- > 0;) {
        uint64_t u_top = u[j + n];
        uint64_t u_next = u[j + n - 1];
        uint64_t q_hat;
        uint64_t r_hat;

        if (u_top == v[n - 1]) {
            q_hat = UINT64_MAX;
            r_hat = u_next + v[n - 1];
            if (r_hat < u_next)
                goto step_d4;
        } else {
            __uint128_t u_dbl = ((__uint128_t)u_top << 64) | u_next;
            q_hat = (uint64_t)(u_dbl / v[n - 1]);
            r_hat = (uint64_t)(u_dbl % v[n - 1]);
        }

        while ((__uint128_t)q_hat * v[n - 2] > (((__uint128_t)r_hat << 64) | u[j + n - 2])) {
            q_hat--;
            r_hat += v[n - 1];
            if (r_hat < v[n - 1])
                break;
        }

    step_d4:;
        uint64_t carry = 0;
        uint64_t borrow = 0;
        for (size_t i = 0; i < n; ++i) {
            uint64_t p_lo, p_hi;
            bs_limb_mul_words(q_hat, v[i], &p_lo, &p_hi);
            uint64_t p_sum = p_lo + carry;
            carry = p_hi + (p_sum < p_lo);

            uint64_t u_word = u[j + i];
            uint64_t sub1 = u_word - p_sum;
            uint64_t b1 = u_word < p_sum;
            uint64_t diff = sub1 - borrow;
            uint64_t b2 = sub1 < borrow;
            u[j + i] = diff;
            borrow = b1 | b2;
        }
        uint64_t u_word = u[j + n];
        uint64_t sub1 = u_word - carry;
        uint64_t b1 = u_word < carry;
        uint64_t diff = sub1 - borrow;
        uint64_t b2 = sub1 < borrow;
        u[j + n] = diff;
        borrow = b1 | b2;

        if (borrow) {
            q_hat--;
            uint64_t add_carry = 0;
            for (size_t i = 0; i < n; ++i) {
                uint64_t sum = u[j + i] + v[i] + add_carry;
                add_carry = (sum < u[j + i]) || (add_carry && sum == u[j + i]);
                u[j + i] = sum;
            }
            u[j + n] += add_carry;
        }

        if (quotient && j < quotient_len)
            quotient[j] = q_hat;
    }

    if (s == 0) {
        memcpy(remainder, u, n * sizeof(uint64_t));
    } else {
        for (size_t i = 0; i < n - 1; ++i)
            remainder[i] = (u[i] >> s) | (u[i + 1] << (64 - s));
        remainder[n - 1] = u[n - 1] >> s;
    }

    if (u != stack_u)
        bs_free(u);
    if (v != stack_v)
        bs_free(v);
}
#endif

void bs_limbs_div(uint64_t *quotient,
                  uint64_t *remainder,
                  const uint64_t *numerator,
                  size_t numerator_len,
                  const uint64_t *denominator,
                  size_t denominator_len) {
    size_t quotient_len =
        numerator_len >= denominator_len ? numerator_len - denominator_len + 1 : 0;
    if (numerator_len < denominator_len ||
        compare_magnitudes(numerator, numerator_len, denominator, denominator_len) < 0) {
        if (quotient && quotient_len > 0)
            memset(quotient, 0, quotient_len * sizeof(*quotient));
        memset(remainder, 0, denominator_len * sizeof(*remainder));
        if (numerator_len > 0 && numerator)
            memcpy(remainder, numerator, numerator_len * sizeof(*remainder));
        return;
    }
    if (denominator_len == 1) {
        div_1(quotient, quotient_len, remainder, numerator, numerator_len, denominator[0]);
        return;
    }
#if defined(BS_HAS_INT128)
    div_knuth(
        quotient, quotient_len, remainder, numerator, numerator_len, denominator, denominator_len);
#else
    divide_bits(
        quotient, quotient_len, remainder, numerator, numerator_len, denominator, denominator_len);
#endif
}
