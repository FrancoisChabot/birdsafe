/**
 * @file bs_intrinsics.h
 * @brief [INTERNAL] Compiler intrinsics, bitwise operations, and overflow arithmetic.
 *
 * This header is an internal implementation detail of bs_num and is not part
 * of the public user API.
 */

#ifndef BS_NUM_INTERNAL_BS_INTRINSICS_H
#define BS_NUM_INTERNAL_BS_INTRINSICS_H

#include "bs_core/bs_config.h"
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Bitwise Intrinsics & Hashing                                              */
/* ========================================================================= */

#if defined(__GNUC__) || defined(__clang__)

/** Counts leading zeros in a 64-bit word. Returns 64 if x == 0. */
BS_INLINE size_t bs_clz64(uint64_t x) {
    return x ? (size_t)__builtin_clzll(x) : 64;
}

/** Counts trailing zeros in a 64-bit word. Returns 64 if x == 0. */
BS_INLINE size_t bs_ctz64(uint64_t x) {
    return x ? (size_t)__builtin_ctzll(x) : 64;
}

/** Returns the number of 1-bits in a 64-bit word. */
BS_INLINE size_t bs_popcount64(uint64_t x) {
    return (size_t)__builtin_popcountll(x);
}

#elif defined(_MSC_VER)
#include <intrin.h>

/** Counts leading zeros in a 64-bit word. Returns 64 if x == 0. */
BS_INLINE size_t bs_clz64(uint64_t x) {
    unsigned long index;
    return _BitScanReverse64(&index, x) ? (63 - (size_t)index) : 64;
}

/** Counts trailing zeros in a 64-bit word. Returns 64 if x == 0. */
BS_INLINE size_t bs_ctz64(uint64_t x) {
    unsigned long index;
    return _BitScanForward64(&index, x) ? (size_t)index : 64;
}

/** Returns the number of 1-bits in a 64-bit word. */
BS_INLINE size_t bs_popcount64(uint64_t x) {
    return (size_t)__popcnt64(x);
}

#else

/** Counts leading zeros in a 64-bit word. Returns 64 if x == 0. */
BS_INLINE size_t bs_clz64(uint64_t x) {
    if (!x) return 64;
    size_t n = 0;
    if (!(x & 0xFFFFFFFF00000000ULL)) { n += 32; x <<= 32; }
    if (!(x & 0xFFFF000000000000ULL)) { n += 16; x <<= 16; }
    if (!(x & 0xFF00000000000000ULL)) { n += 8;  x <<= 8;  }
    if (!(x & 0xF000000000000000ULL)) { n += 4;  x <<= 4;  }
    if (!(x & 0xC000000000000000ULL)) { n += 2;  x <<= 2;  }
    if (!(x & 0x8000000000000000ULL)) { n += 1; }
    return n;
}

/** Counts trailing zeros in a 64-bit word. Returns 64 if x == 0. */
BS_INLINE size_t bs_ctz64(uint64_t x) {
    if (!x) return 64;
    size_t n = 0;
    if (!(x & 0x00000000FFFFFFFFULL)) { n += 32; x >>= 32; }
    if (!(x & 0x000000000000FFFFULL)) { n += 16; x >>= 16; }
    if (!(x & 0x00000000000000FFULL)) { n += 8;  x >>= 8;  }
    if (!(x & 0x000000000000000FULL)) { n += 4;  x >>= 4;  }
    if (!(x & 0x0000000000000003ULL)) { n += 2;  x >>= 2;  }
    if (!(x & 0x0000000000000001ULL)) { n += 1; }
    return n;
}

/** Returns the number of 1-bits in a 64-bit word. */
BS_INLINE size_t bs_popcount64(uint64_t x) {
    x = (x & 0x5555555555555555ULL) + ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x & 0x0F0F0F0F0F0F0F0FULL) + ((x >> 4) & 0x0F0F0F0F0F0F0F0FULL);
    return (size_t)((x * 0x0101010101010101ULL) >> 56);
}
#endif

/** 64-bit SplitMix pseudo-random hash generator for 64-bit integers. */
BS_INLINE uint64_t bs_splitmix64(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

/* ========================================================================= */
/* Overflow-Checked Arithmetic                                              */
/* ========================================================================= */

/** Unsigned 64-bit addition with overflow check. Returns true on overflow. */
BS_INLINE bool bs_add_overflow_u64(uint64_t a, uint64_t b, uint64_t *res) {
    *res = a + b;
    return *res < a;
}

/** Unsigned 64-bit subtraction with underflow check. Returns true on underflow. */
BS_INLINE bool bs_sub_overflow_u64(uint64_t a, uint64_t b, uint64_t *res) {
    *res = a - b;
    return a < b;
}

/** Unsigned 64-bit multiplication with overflow check. Returns true on overflow. */
BS_INLINE bool bs_mul_overflow_u64(uint64_t a, uint64_t b, uint64_t *res) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    uint64_t hi;
    *res = _umul128(a, b, &hi);
    return hi != 0;
#elif defined(BS_HAS_INT128)
    bs_uint128_t prod = (bs_uint128_t)a * b;
    *res = (uint64_t)prod;
    return (prod >> 64) != 0;
#else
    if (a != 0 && b > UINT64_MAX / a) {
        return true;
    }
    *res = a * b;
    return false;
#endif
}

/** Signed 64-bit addition with overflow check. Returns true on overflow. */
BS_INLINE bool bs_add_overflow_i64(int64_t a, int64_t b, int64_t *res) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        return true;
    }
    *res = a + b;
    return false;
}

/** Signed 64-bit subtraction with overflow check. Returns true on overflow. */
BS_INLINE bool bs_sub_overflow_i64(int64_t a, int64_t b, int64_t *res) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
        return true;
    }
    *res = a - b;
    return false;
}

/** Signed 64-bit multiplication with overflow check. Returns true on overflow. */
BS_INLINE bool bs_mul_overflow_i64(int64_t a, int64_t b, int64_t *res) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    int64_t hi;
    int64_t lo = _mul128(a, b, &hi);
    *res = lo;
    return hi != (lo >> 63);
#elif defined(BS_HAS_INT128)
    bs_int128_t prod = (bs_int128_t)a * b;
    *res = (int64_t)prod;
    return (prod < INT64_MIN || prod > INT64_MAX);
#else
    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b) return true;
        } else {
            if (b < INT64_MIN / a) return true;
        }
    } else if (a < 0) {
        if (b > 0) {
            if (a < INT64_MIN / b) return true;
        } else {
            if (a == INT64_MIN || b == INT64_MIN) return true;
            if (-a > INT64_MAX / -b) return true;
        }
    }
    *res = a * b;
    return false;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
#define bs_add_overflow(a, b, res) __builtin_add_overflow((a), (b), (res))
#define bs_sub_overflow(a, b, res) __builtin_sub_overflow((a), (b), (res))
#define bs_mul_overflow(a, b, res) __builtin_mul_overflow((a), (b), (res))
#elif defined(__cplusplus)
inline bool bs_add_overflow(int64_t a, int64_t b, int64_t *res) { return bs_add_overflow_i64(a, b, res); }
inline bool bs_add_overflow(uint64_t a, uint64_t b, uint64_t *res) { return bs_add_overflow_u64(a, b, res); }
inline bool bs_sub_overflow(int64_t a, int64_t b, int64_t *res) { return bs_sub_overflow_i64(a, b, res); }
inline bool bs_sub_overflow(uint64_t a, uint64_t b, uint64_t *res) { return bs_sub_overflow_u64(a, b, res); }
inline bool bs_mul_overflow(int64_t a, int64_t b, int64_t *res) { return bs_mul_overflow_i64(a, b, res); }
inline bool bs_mul_overflow(uint64_t a, uint64_t b, uint64_t *res) { return bs_mul_overflow_u64(a, b, res); }
#else
#define bs_add_overflow(a, b, res) _Generic(*(res), \
    int64_t: bs_add_overflow_i64((int64_t)(a), (int64_t)(b), (int64_t *)(res)), \
    uint64_t: bs_add_overflow_u64((uint64_t)(a), (uint64_t)(b), (uint64_t *)(res)) \
)
#define bs_sub_overflow(a, b, res) _Generic(*(res), \
    int64_t: bs_sub_overflow_i64((int64_t)(a), (int64_t)(b), (int64_t *)(res)), \
    uint64_t: bs_sub_overflow_u64((uint64_t)(a), (uint64_t)(b), (uint64_t *)(res)) \
)
#define bs_mul_overflow(a, b, res) _Generic(*(res), \
    int64_t: bs_mul_overflow_i64((int64_t)(a), (int64_t)(b), (int64_t *)(res)), \
    uint64_t: bs_mul_overflow_u64((uint64_t)(a), (uint64_t)(b), (uint64_t *)(res)) \
)
#endif

#ifdef __cplusplus
}
#endif

#endif /* BS_NUM_INTERNAL_BS_INTRINSICS_H */
