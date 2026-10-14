/**
 * @file bs_bignum.h
 * @brief [INTERNAL] Heap-allocated multi-precision integer header layout and
 * operations.
 *
 * This header is an internal implementation detail of bs_num and is not part
 * of the public user API.
 *
 * Implements a pure 16-byte header struct. Limbs are stored immediately
 * following the header at `(bs_limb_t *)(b + 1)` in a single contiguous memory
 * block.
 *
 * Part of bs_num - Pure C Numerics Runtime
 */

#ifndef BS_NUM_INTERNAL_BS_BIGNUM_H
#define BS_NUM_INTERNAL_BS_BIGNUM_H

#include "bs_limb.h"
#include "bs_core/bs_config.h"
#include "bs_num/bs_int.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Numeric object type discriminant.
 */
typedef enum bs_num_type {
    BS_TYPE_INTEGER = 0  /**< Arbitrary-precision integer */
} bs_num_type_t;

/**
 * @brief Sign indicators for bignums.
 */
#define BS_SIGN_POSITIVE ((int8_t)1)
#define BS_SIGN_NEGATIVE ((int8_t) - 1)

/**
 * @brief Header for a heap-allocated multi-precision numeric object.
 *
 * Exactly 16 bytes on 64-bit platforms, naturally maintaining 8-byte and
 * 16-byte alignment for the trailing limbs payload located at `(bs_limb_t *)(b
 * + 1)`.
 */
typedef struct bs_bignum {
    bs_atomic_uint32_t ref_count; /**< Thread-safe reference count (starts at 1) */
    uint16_t type;                /**< bs_num_type_t discriminant */
    int8_t sign;                  /**< Sign: BS_SIGN_POSITIVE (+1) or BS_SIGN_NEGATIVE (-1) */
    uint8_t flags;                /**< Reserved / status flags (must be 0) */
    size_t len;                   /**< Number of active limbs allocated (len >= 1) */
} bs_bignum_t;

_Static_assert(sizeof(bs_bignum_t) == 16, "bs_bignum_t header must be exactly 16 bytes");
_Static_assert(_Alignof(bs_bignum_t) >= _Alignof(bs_limb_t),
               "bs_bignum_t alignment must satisfy limb alignment");

/**
 * @brief Gets a mutable pointer to the limb payload immediately following the
 * header.
 */
BS_INLINE bs_limb_t *bs_bignum_limbs(bs_bignum_t *b) {
    assert(b != NULL);
    return (bs_limb_t *)(b + 1);
}

/**
 * @brief Gets an immutable pointer to the limb payload immediately following
 * the header.
 */
BS_INLINE const bs_limb_t *bs_bignum_limbs_const(const bs_bignum_t *b) {
    assert(b != NULL);
    return (const bs_limb_t *)(b + 1);
}

/**
 * @brief Casts a bignum handle to its underlying heap pointer.
 */
BS_INLINE bs_bignum_t *bs_int_to_bignum(bs_int a) {
    assert(bs_int_is_bignum(a));
    return (bs_bignum_t *)a.raw;
}

/**
 * @brief Casts a bignum handle to its underlying const heap pointer.
 */
BS_INLINE const bs_bignum_t *bs_int_to_bignum_const(bs_int a) {
    assert(bs_int_is_bignum(a));
    return (const bs_bignum_t *)a.raw;
}

/**
 * @brief Wraps a heap bignum pointer into a bs_int handle.
 */
BS_INLINE bs_int bs_int_from_bignum(bs_bignum_t *b) {
    assert(((uintptr_t)b & BS_INT_TAG_MASK) == 0);
    return (bs_int){.raw = (uintptr_t)b};
}


/**
 * @brief Frees a bignum and its trailing limb storage.
 */
BS_API void bs_bignum_free(bs_bignum_t *b);

/**
 * @brief Allocates a new bignum with space for @p limb_count limbs in a single
 * contiguous block.
 */
BS_API bs_bignum_t *bs_bignum_alloc(size_t limb_count);

/**
 * @brief Creates an independent deep copy of a bignum for Copy-on-Write
 * unsharing.
 */
BS_API bs_bignum_t *bs_bignum_clone(const bs_bignum_t *b);

/**
 * @brief Increments the reference count of a bignum.
 */
BS_INLINE bs_bignum_t *bs_bignum_retain(bs_bignum_t *b) {
    if (b != NULL) {
        bs_atomic_fetch_add_relaxed(&b->ref_count, 1);
    }
    return b;
}

/**
 * @brief Decrements the reference count of a bignum and frees it if ref_count
 * reaches 0.
 */
BS_INLINE void bs_bignum_release(bs_bignum_t *b) {
    if (b != NULL) {
        if (bs_atomic_fetch_sub_acq_rel(&b->ref_count, 1) == 1) {
            bs_bignum_free(b);
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* BS_NUM_INTERNAL_BS_BIGNUM_H */
