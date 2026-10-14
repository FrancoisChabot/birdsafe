#include "bs_bignum.h"
#include "bs_core/bs_alloc.h"
#include <string.h>

void bs_bignum_free(bs_bignum_t *b) {
    bs_free(b);
}
bs_bignum_t *bs_bignum_alloc(size_t n) {
    if (!n || n > (SIZE_MAX - sizeof(bs_bignum_t)) / sizeof(uint64_t))
        return NULL;
    bs_bignum_t *b = bs_alloc(sizeof(*b) + n * sizeof(uint64_t));
    if (!b)
        return NULL;
    bs_atomic_init(&b->ref_count, 1);
    b->type = BS_TYPE_INTEGER;
    b->sign = BS_SIGN_POSITIVE;
    b->flags = 0;
    b->len = n;
    return b;
}
bs_bignum_t *bs_bignum_clone(const bs_bignum_t *b) {
    if (!b)
        return NULL;
    bs_bignum_t *c = bs_bignum_alloc(b->len);
    if (c) {
        c->type = b->type;
        c->sign = b->sign;
        c->flags = b->flags;
        memcpy(bs_bignum_limbs(c), bs_bignum_limbs_const(b), b->len * sizeof(uint64_t));
    }
    return c;
}
void bs_int_retain_bignum(bs_int a) {
    bs_bignum_retain(bs_int_to_bignum(a));
}
void bs_int_release_bignum(bs_int a) {
    bs_bignum_release(bs_int_to_bignum(a));
}
