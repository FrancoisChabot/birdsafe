/**
 * @file bs_limb.h
 * @brief [INTERNAL] Low-level multi-precision integer limbs and primitive
 * arithmetic.
 *
 * This header is an internal implementation detail of bs_num and is not part
 * of the public user API.
 *
 * Limbs are stored in little-endian order (least significant limb first).
 *
 * Part of bs_num - Pure C Numerics Runtime
 */

#ifndef BS_NUM_INTERNAL_BS_LIMB_H
#define BS_NUM_INTERNAL_BS_LIMB_H

#include "bs_core/bs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Primary limb type: 64-bit unsigned integer.
 */
typedef uint64_t bs_limb_t;

/** Multiplies two words into low and high halves of the double-width result. */
void bs_limb_mul_words(uint64_t a, uint64_t b, uint64_t *low, uint64_t *high);

/**
 * @brief Calculates the effective (normalized) length of a limb array,
 * ignoring trailing most-significant zero limbs.
 *
 * @param limbs Pointer to limb array.
 * @param max_len Maximum number of limbs to inspect.
 * @return Number of significant limbs (0 if all limbs are zero).
 */
BS_INLINE size_t bs_limbs_normalized_len(const bs_limb_t *limbs, size_t max_len) {
    if (limbs == NULL || max_len == 0)
        return 0;
    while (max_len > 0 && limbs[max_len - 1] == 0) {
        max_len--;
    }
    return max_len;
}

/**
 * @brief Adds a single limb @p b to limb array @p ap of length @p n.
 *
 * Stores the result in @p rp (length @p n).
 *
 * @param rp Result array of at least @p n limbs.
 * @param ap Source array of @p n limbs.
 * @param n Number of limbs.
 * @param b Single limb to add.
 * @return Carry out (0 or 1).
 */
bs_limb_t bs_limbs_add_1(bs_limb_t *rp, const bs_limb_t *ap, size_t n, bs_limb_t b);

/**
 * @brief Adds two limb arrays: @p ap (len @p an) + @p bp (len @p bn), where @p
 * an >= @p bn.
 *
 * Stores the result in @p rp (length @p an).
 *
 * @param rp Result array of at least @p an limbs.
 * @param ap First source array of @p an limbs.
 * @param an Length of @p ap.
 * @param bp Second source array of @p bn limbs.
 * @param bn Length of @p bp (must be <= @p an).
 * @return Carry out (0 or 1).
 */
bs_limb_t
bs_limbs_add(bs_limb_t *rp, const bs_limb_t *ap, size_t an, const bs_limb_t *bp, size_t bn);

/**
 * @brief Subtracts a single limb @p b from limb array @p ap of length @p n.
 *
 * Stores the result in @p rp (length @p n).
 *
 * @param rp Result array of at least @p n limbs.
 * @param ap Source array of @p n limbs.
 * @param n Number of limbs.
 * @param b Single limb to subtract.
 * @return Borrow out (0 or 1).
 */
bs_limb_t bs_limbs_sub_1(bs_limb_t *rp, const bs_limb_t *ap, size_t n, bs_limb_t b);

/**
 * @brief Subtracts @p bp (len @p bn) from @p ap (len @p an), where @p an >= @p
 * bn.
 *
 * Stores the result in @p rp (length @p an).
 *
 * @param rp Result array of at least @p an limbs.
 * @param ap Minuend array of @p an limbs.
 * @param an Length of @p ap.
 * @param bp Subtrahend array of @p bn limbs.
 * @param bn Length of @p bp (must be <= @p an).
 * @return Borrow out (0 or 1).
 */
bs_limb_t
bs_limbs_sub(bs_limb_t *rp, const bs_limb_t *ap, size_t an, const bs_limb_t *bp, size_t bn);

/**
 * @brief Multiplies limb array @p ap of length @p n by a single limb @p b.
 *
 * Stores the result in @p rp (length @p n).
 *
 * @param rp Result array of at least @p n limbs.
 * @param ap Source array of @p n limbs.
 * @param n Number of limbs.
 * @param b Multiplier limb.
 * @return High limb / carry out.
 */
bs_limb_t bs_limbs_mul_1(bs_limb_t *rp, const bs_limb_t *ap, size_t n, bs_limb_t b);

/**
 * @brief Multiplies two limb arrays: @p ap (len @p an) * @p bp (len @p bn).
 *
 * Stores the result in @p rp (length @p an + @p bn).
 *
 * @param rp Result array of at least (@p an + @p bn) limbs.
 * @param ap First factor of @p an limbs.
 * @param an Length of @p ap.
 * @param bp Second factor of @p bn limbs.
 * @param bn Length of @p bp.
 */
void bs_limbs_mul(bs_limb_t *rp, const bs_limb_t *ap, size_t an, const bs_limb_t *bp, size_t bn);

/**
 * @brief Divides limb array @p np (len @p nn) by @p dp (len @p dn).
 *
 * Quotient is stored in @p qp (len @p nn - @p dn + 1), and remainder in @p rp
 * (len @p dn).
 *
 * @param qp Quotient array of at least (@p nn - @p dn + 1) limbs (or NULL if
 * not needed).
 * @param rp Remainder array of at least @p dn limbs.
 * @param np Numerator array of @p nn limbs.
 * @param nn Length of @p np.
 * @param dp Denominator array of @p dn limbs (most significant limb must not be
 * 0).
 * @param dn Length of @p dp (must be >= 1 and <= @p nn).
 */
void bs_limbs_div(
    bs_limb_t *qp, bs_limb_t *rp, const bs_limb_t *np, size_t nn, const bs_limb_t *dp, size_t dn);

/**
 * @brief Compares two unsigned limb arrays: @p ap (len @p an) vs @p bp (len @p
 * bn).
 *
 * Assumes arrays are normalized (no unneeded leading zeros), or compares
 * numerically.
 *
 * @param ap First limb array.
 * @param an Length of @p ap.
 * @param bp Second limb array.
 * @param bn Length of @p bp.
 * @return > 0 if a > b, < 0 if a < b, 0 if a == b.
 */
int bs_limbs_cmp(const bs_limb_t *ap, size_t an, const bs_limb_t *bp, size_t bn);

#ifdef __cplusplus
}
#endif

#endif /* BS_NUM_INTERNAL_BS_LIMB_H */
