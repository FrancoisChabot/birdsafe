/**
 * @file bs_int.h
 * @brief Primary arbitrary-precision integer API for bs_num.
 *
 * Provides a pointer-sized integer type (`bs_int`) with 63-bit immediate
 * small integers (fixnums) and ref-counted heap-allocated multi-precision
 * integers (bignums).
 *
 * The current representation requires a 64-bit pointer-sized word. Other
 * architectures may be supported if needed by adapting the fixnum width.
 *
 * Tagging Scheme:
 * - Bit 0 == 1: Fixnum (immediate 63-bit signed integer, extracted via arithmetic shift).
 * - Bit 0 == 0: Heap bignum pointer (or 0 for NULL / uninitialized).
 *
 * Ownership and Failure Contract:
 * - Newly created heap results have one owned reference (ref_count == 1). Arguments are borrowed.
 * - Release owned handles with bs_int_release(); retain additional owners with bs_int_retain().
 * - Functions returning bs_int return BS_INT_NULL on allocation failure.
 * - Out-parameters are written only on success unless documented otherwise.
 *
 * Invariant:
 * - Values fitting within [-2^62, 2^62 - 1] are always canonically stored as fixnums.
 */

#ifndef BS_NUM_BS_INT_H
#define BS_NUM_BS_INT_H

#include "bs_core/bs_alloc.h"
#include "bs_core/bs_config.h"
#include "bs_core/bs_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Status and error codes specific to numeric domain operations. */
typedef enum bs_num_status {
    BS_NUM_OVERFLOW    = BS_STATUS_MAKE(BS_DOMAIN_NUM, 1),
    BS_NUM_PARSE_ERROR = BS_STATUS_MAKE(BS_DOMAIN_NUM, 2),
} bs_num_status;

/* ========================================================================= */
/* Primary Integer Type & Representation                                     */
/* ========================================================================= */

/** Primary integer handle: exactly one word (pointer-sized). */
typedef struct bs_int {
    uintptr_t raw;
} bs_int;

#ifdef __cplusplus
static_assert(sizeof(bs_int) == sizeof(void *), "bs_int must be pointer-sized");
#define BS_INT_LITERAL(raw_value) bs_int { (raw_value) }
#else
_Static_assert(sizeof(bs_int) == sizeof(void *), "bs_int must be pointer-sized");
#define BS_INT_LITERAL(raw_value) ((bs_int){.raw = (raw_value)})
#endif

#define BS_INT_TAG_MASK     ((uintptr_t)0x1)
#define BS_INT_TAG_BIGNUM   ((uintptr_t)0x0)
#define BS_INT_TAG_FIXNUM   ((uintptr_t)0x1)
#define BS_INT_FIXNUM_SHIFT 1
#define BS_INT_FIXNUM_BITS  63

#define BS_FIXNUM_MAX ((INT64_C(1) << 62) - 1)
#define BS_FIXNUM_MIN (-BS_FIXNUM_MAX - 1)

/** Well-known constant integer handles. */
#define BS_INT_NULL    BS_INT_LITERAL(0)
#define BS_INT_ZERO    BS_INT_LITERAL(((uintptr_t)0 << BS_INT_FIXNUM_SHIFT) | BS_INT_TAG_FIXNUM)
#define BS_INT_ONE     BS_INT_LITERAL(((uintptr_t)1 << BS_INT_FIXNUM_SHIFT) | BS_INT_TAG_FIXNUM)
#define BS_INT_NEG_ONE BS_INT_LITERAL(((uintptr_t)(intptr_t)(-1) << BS_INT_FIXNUM_SHIFT) | BS_INT_TAG_FIXNUM)

/* ========================================================================= */
/* Inspection & Tagging Primitives                                           */
/* ========================================================================= */

/** Tests if an integer is an immediate 63-bit small integer (fixnum). */
BS_INLINE bool bs_int_is_fixnum(bs_int a) {
    return (a.raw & BS_INT_TAG_MASK) == BS_INT_TAG_FIXNUM;
}

/** Tests if an integer is a pointer to a heap-allocated bignum. */
BS_INLINE bool bs_int_is_bignum(bs_int a) {
    return ((a.raw & BS_INT_TAG_MASK) == BS_INT_TAG_BIGNUM) && (a.raw != 0);
}

/** Tests if an integer handle is uninitialized or null. */
BS_INLINE bool bs_int_is_null(bs_int a) {
    return a.raw == 0;
}

/** Tests if a 64-bit signed integer fits within the 63-bit fixnum range. */
BS_INLINE bool bs_int_fits_fixnum(int64_t v) {
    return (v >= BS_FIXNUM_MIN) && (v <= BS_FIXNUM_MAX);
}

/** Boxes a 63-bit signed value into a fixnum handle without bounds checks. */
BS_INLINE bs_int bs_int_from_fixnum_unchecked(int64_t v) {
    return BS_INT_LITERAL(((uintptr_t)v << BS_INT_FIXNUM_SHIFT) | BS_INT_TAG_FIXNUM);
}

/** Unboxes a fixnum handle into a 64-bit signed integer via arithmetic right shift. */
BS_INLINE int64_t bs_int_to_fixnum_unchecked(bs_int a) {
    assert(bs_int_is_fixnum(a));
    return (int64_t)((intptr_t)a.raw >> BS_INT_FIXNUM_SHIFT);
}

/* ========================================================================= */
/* Memory Lifecycle: Retain & Release                                        */
/* ========================================================================= */

BS_API void bs_int_retain_bignum(bs_int a);
BS_API void bs_int_release_bignum(bs_int a);

/** Increments the reference count if a is a heap bignum (no-op for fixnums). Returns a. */
BS_INLINE bs_int bs_int_retain(bs_int a) {
    if (bs_int_is_bignum(a)) {
        bs_int_retain_bignum(a);
    }
    return a;
}

/** Decrements the reference count if a is a heap bignum, freeing when it drops to 0. */
BS_INLINE void bs_int_release(bs_int a) {
    if (bs_int_is_bignum(a)) {
        bs_int_release_bignum(a);
    }
}

/* ========================================================================= */
/* Constructors & Conversions                                                */
/* ========================================================================= */

/** Creates an integer from an int64_t (fixnum or owned bignum, BS_INT_NULL on OOM). */
BS_API bs_int bs_int_from_i64(int64_t v);

/** Creates an integer from a uint64_t (fixnum or owned bignum, BS_INT_NULL on OOM). */
BS_API bs_int bs_int_from_u64(uint64_t v);

/**
 * Parses an integer string in base [2..36] (or 0 for auto-detect prefixes).
 * Returns true on success (*out owned); on failure returns false and leaves *out unchanged.
 */
BS_API bool bs_int_from_str(const char *str, int base, bs_int *out);

/**
 * Constructs an integer from little-endian 64-bit words and sign (-1, 0, +1).
 * Canonicalizes to fixnum if it fits. Returns owned handle or BS_INT_NULL on OOM/error.
 */
BS_API bs_int bs_int_from_words(const uint64_t *words, size_t count, int sign);

/** Converts an integer to int64_t. Returns true on success, false on overflow. */
BS_API bool bs_int_to_i64(bs_int a, int64_t *out);

/** Converts an integer to uint64_t. Returns true on success, false if negative or overflow. */
BS_API bool bs_int_to_u64(bs_int a, uint64_t *out);

/**
 * Formats an integer to a heap string in base [2..36].
 * The returned string must be released by caller using bs_free(). Returns NULL on OOM.
 */
BS_API char *bs_int_to_str(bs_int a, int base);

/** Returns the number of 64-bit words required to represent magnitude |a|. */
BS_API size_t bs_int_word_count(bs_int a);

/**
 * Exports magnitude |a| into little-endian 64-bit words buffer. Returns required word count.
 * Pass NULL and max_words == 0 to query required capacity.
 */
BS_API size_t bs_int_to_words(bs_int a, uint64_t *out_words, size_t max_words);

/** Returns the sign of a (-1 for negative, 0 for zero, +1 for positive). */
BS_API int bs_int_sign(bs_int a);

/* ========================================================================= */
/* Predicates & Comparisons                                                  */
/* ========================================================================= */

/** Tests if an integer is equal to zero. */
BS_INLINE bool bs_int_is_zero(bs_int a) {
    return a.raw == BS_INT_ZERO.raw;
}

/** Tests if an integer is strictly positive (> 0). */
BS_API bool bs_int_is_positive(bs_int a);

/** Tests if an integer is strictly negative (< 0). */
BS_API bool bs_int_is_negative(bs_int a);

/** Tests if an integer is even. */
BS_API bool bs_int_is_even(bs_int a);

/** Tests if an integer is odd. */
BS_API bool bs_int_is_odd(bs_int a);

/** Three-way comparison between a and b. Returns -1 for a < b, 0 for a == b, +1 for a > b. */
BS_API int bs_int_cmp(bs_int a, bs_int b);

/** Tests equality: a == b. */
BS_INLINE bool bs_int_eq(bs_int a, bs_int b) {
    if (bs_int_is_null(a) || bs_int_is_null(b))
        return false;
    if (a.raw == b.raw)
        return true;
    if (bs_int_is_fixnum(a) || bs_int_is_fixnum(b))
        return false;
    return bs_int_cmp(a, b) == 0;
}

/** Tests inequality: a != b. */
BS_INLINE bool bs_int_ne(bs_int a, bs_int b) {
    return !bs_int_eq(a, b);
}

/** Tests less than: a < b. */
BS_INLINE bool bs_int_lt(bs_int a, bs_int b) {
    return bs_int_cmp(a, b) < 0;
}

/** Tests less than or equal: a <= b. */
BS_INLINE bool bs_int_lte(bs_int a, bs_int b) {
    return bs_int_cmp(a, b) <= 0;
}

/** Tests greater than: a > b. */
BS_INLINE bool bs_int_gt(bs_int a, bs_int b) {
    return bs_int_cmp(a, b) > 0;
}

/** Tests greater than or equal: a >= b. */
BS_INLINE bool bs_int_gte(bs_int a, bs_int b) {
    return bs_int_cmp(a, b) >= 0;
}

/* ========================================================================= */
/* Arithmetic Operations                                                     */
/* ========================================================================= */

/** Addition: returns a + b (auto-promotes/demotes between fixnum and bignum). */
BS_API bs_int bs_int_add(bs_int a, bs_int b);

/** Subtraction: returns a - b. */
BS_API bs_int bs_int_sub(bs_int a, bs_int b);

/** Multiplication: returns a * b. */
BS_API bs_int bs_int_mul(bs_int a, bs_int b);

/** Truncating integer division: returns a / b (rounds toward zero). b must be nonzero. */
BS_API bs_int bs_int_div(bs_int a, bs_int b);

/** Remainder: returns a % b (truncated division). b must be nonzero. */
BS_API bs_int bs_int_rem(bs_int a, bs_int b);

/**
 * Simultaneous quotient and remainder: computes *q = a / b and *r = a % b.
 * Returns true on success (outputs owned), or false if b == 0 or OOM (outputs untouched).
 */
BS_API bool bs_int_div_rem(bs_int a, bs_int b, bs_int *q, bs_int *r);

/** Negation: returns -a. */
BS_API bs_int bs_int_neg(bs_int a);

/** Absolute value: returns |a|. */
BS_API bs_int bs_int_abs(bs_int a);

/** Exponentiation: returns base^exp. */
BS_API bs_int bs_int_pow(bs_int base, uint32_t exp);

/** Greatest Common Divisor: returns gcd(a, b). */
BS_API bs_int bs_int_gcd(bs_int a, bs_int b);

/** Least Common Multiple: returns lcm(a, b). */
BS_API bs_int bs_int_lcm(bs_int a, bs_int b);

/** Modular exponentiation: returns (base^exp) mod mod in [0, |mod| - 1]. |mod| must be > 1. */
BS_API bs_int bs_int_pow_mod(bs_int base, bs_int exp, bs_int mod);

/** Integer square root: returns floor(sqrt(a)). Returns BS_INT_NULL if a < 0 or OOM. */
BS_API bs_int bs_int_sqrt(bs_int a);

/**
 * Integer square root with remainder: computes *s = floor(sqrt(a)) and *r = a - s^2.
 * Returns true on success, false if a < 0 or OOM (outputs untouched).
 */
BS_API bool bs_int_sqrt_rem(bs_int a, bs_int *s, bs_int *r);

/* ========================================================================= */
/* Bitwise Operations                                                        */
/* ========================================================================= */

/** Bitwise AND using infinite two's-complement semantics. */
BS_API bs_int bs_int_and(bs_int a, bs_int b);

/** Bitwise OR using infinite two's-complement semantics. */
BS_API bs_int bs_int_or(bs_int a, bs_int b);

/** Bitwise XOR using infinite two's-complement semantics. */
BS_API bs_int bs_int_xor(bs_int a, bs_int b);

/** Bitwise NOT using infinite two's-complement semantics. */
BS_API bs_int bs_int_not(bs_int a);

/** Left shift: returns a * 2^shift. */
BS_API bs_int bs_int_shl(bs_int a, size_t shift);

/** Arithmetic right shift: returns floor(a / 2^shift). */
BS_API bs_int bs_int_shr(bs_int a, size_t shift);

/** Returns the number of bits needed to represent magnitude |a| (0 for zero). */
BS_API size_t bs_int_bit_len(bs_int a);

/** Returns the number of set bits (popcount) in magnitude |a|. */
BS_API size_t bs_int_popcount(bs_int a);

/** Tests bit at zero-indexed position in two's-complement representation. */
BS_API bool bs_int_test_bit(bs_int a, size_t bit);

/** Returns the number of trailing zero bits in magnitude |a| (0 for zero). */
BS_API size_t bs_int_ctz(bs_int a);

/* ========================================================================= */
/* Hashing                                                                   */
/* ========================================================================= */

/** Computes a 64-bit hash of integer a, compatible with bs_int_eq. */
BS_API uint64_t bs_int_hash(bs_int a);

#ifdef __cplusplus
}
#endif

#endif /* BS_NUM_BS_INT_H */
