#include "bs_num.h"
#include "bs_bignum.h"
#include "bs_intrinsics.h"
#include <ctype.h>
#include <limits.h>
#include <string.h>

/* A non-owning view keeps fixnums and bignums on the same arithmetic paths. */
typedef struct {
    const uint64_t *p;
    size_t n;
    uint64_t one;
    int sign;
} view_t;
static view_t view(bs_int a) {
    view_t v = {0};
    if (bs_int_is_fixnum(a)) {
        int64_t x = bs_int_to_fixnum_unchecked(a);
        v.sign = (x > 0) - (x < 0);
        if (x < 0)
            v.one = (uint64_t)(-(x + 1)) + 1;
        else
            v.one = (uint64_t)x;
        v.p = NULL;
        v.n = v.one ? 1 : 0;
    } else if (bs_int_is_bignum(a)) {
        const bs_bignum_t *b = bs_int_to_bignum_const(a);
        v.p = bs_bignum_limbs_const(b);
        v.n = b->len;
        v.sign = b->sign;
    }
    return v;
}
static const uint64_t *view_data(const view_t *v) {
    return v->p ? v->p : &v->one;
}
static bs_int make_int(int sign, const uint64_t *p, size_t n) {
    n = bs_limbs_normalized_len(p, n);
    if (!n || !sign)
        return BS_INT_ZERO;
    if (n == 1 && ((sign > 0 && p[0] <= (uint64_t)BS_FIXNUM_MAX) ||
                   (sign < 0 && p[0] <= (UINT64_C(1) << 62)))) {
        int64_t x = sign > 0 ? (int64_t)p[0]
                             : (p[0] == (UINT64_C(1) << 62) ? BS_FIXNUM_MIN : -(int64_t)p[0]);
        return bs_int_from_fixnum_unchecked(x);
    }
    bs_bignum_t *b = bs_bignum_alloc(n);
    if (!b)
        return BS_INT_NULL;
    b->len = n;
    b->sign = sign > 0 ? BS_SIGN_POSITIVE : BS_SIGN_NEGATIVE;
    memcpy(bs_bignum_limbs(b), p, n * sizeof(*p));
    return bs_int_from_bignum(b);
}
bs_int bs_int_from_i64(int64_t x) {
    if (bs_int_fits_fixnum(x)) {
        return bs_int_from_fixnum_unchecked(x);
    }
    uint64_t m = x < 0 ? (uint64_t)(-(x + 1)) + 1 : (uint64_t)x;
    return make_int((x > 0) - (x < 0), &m, 1);
}
bs_int bs_int_from_u64(uint64_t x) {
    if (x <= (uint64_t)BS_FIXNUM_MAX) {
        return bs_int_from_fixnum_unchecked((int64_t)x);
    }
    return make_int(1, &x, 1);
}
bs_int bs_int_from_words(const uint64_t *w, size_t n, int s) {
    if (s < -1 || s > 1 || (n && !w))
        return BS_INT_NULL;
    n = bs_limbs_normalized_len(w, n);
    if (!n)
        return BS_INT_ZERO;
    if (!s)
        return BS_INT_NULL;
    return make_int(s, w, n);
}
static bool mul_add_chunk(
    uint64_t **limbs_ptr, size_t *count_ptr, size_t *cap_ptr, uint64_t mult, uint64_t add) {
    uint64_t *limbs = *limbs_ptr;
    size_t count = *count_ptr;
    size_t cap = *cap_ptr;
    uint64_t carry = add;
    for (size_t i = 0; i < count; i++) {
        uint64_t lo, hi;
        bs_limb_mul_words(limbs[i], mult, &lo, &hi);
        uint64_t z = lo + carry;
        hi += z < lo;
        limbs[i] = z;
        carry = hi;
    }
    if (carry) {
        if (count == cap) {
            if (cap > SIZE_MAX / 2 / sizeof(*limbs))
                return false;
            cap *= 2;
            uint64_t *resized = bs_realloc(limbs, cap * sizeof(*limbs));
            if (!resized)
                return false;
            *limbs_ptr = limbs = resized;
            *cap_ptr = cap;
        }
        limbs[count++] = carry;
        *count_ptr = count;
    }
    return true;
}

bool bs_int_from_str(const char *str, int base, bs_int *out) {
    if (!str || !out || base < 0 || base == 1 || base > 36)
        return false;
    while (isspace((unsigned char)*str))
        ++str;
    int sign = 1;
    if (*str == '+' || *str == '-') {
        if (*str++ == '-')
            sign = -1;
    }
    if (base == 0) {
        base = 10;
        if (str[0] == '0' && str[1]) {
            if (str[1] == 'x' || str[1] == 'X')
                base = 16, str += 2;
            else if (str[1] == 'b' || str[1] == 'B')
                base = 2, str += 2;
            else if (str[1] == 'o' || str[1] == 'O')
                base = 8, str += 2;
        }
    } else if (str[0] == '0' && ((base == 16 && (str[1] == 'x' || str[1] == 'X')) ||
                                 (base == 2 && (str[1] == 'b' || str[1] == 'B')) ||
                                 (base == 8 && (str[1] == 'o' || str[1] == 'O'))))
        str += 2;
    size_t capacity = 2;
    size_t limb_count = 1;
    uint64_t *limbs = bs_alloc(capacity * sizeof(*limbs));
    if (!limbs)
        return false;
    limbs[0] = 0;
    bool has_digit = false;

    uint64_t max_mult = (unsigned)base;
    int max_digits = 1;
    while (max_mult <= UINT64_MAX / (unsigned)base) {
        max_mult *= (unsigned)base;
        max_digits++;
    }

    uint64_t chunk = 0;
    uint64_t chunk_mult = 1;
    int chunk_len = 0;

    for (; *str && !isspace((unsigned char)*str); str++) {
        int c = (unsigned char)*str;
        int digit = isdigit(c)
                        ? c - '0'
                        : (tolower(c) >= 'a' && tolower(c) <= 'z' ? tolower(c) - 'a' + 10 : -1);
        if (digit < 0 || digit >= base) {
            bs_free(limbs);
            return false;
        }
        has_digit = true;
        chunk = chunk * (unsigned)base + (unsigned)digit;
        chunk_mult *= (unsigned)base;
        chunk_len++;

        if (chunk_len == max_digits) {
            if (!mul_add_chunk(&limbs, &limb_count, &capacity, chunk_mult, chunk)) {
                bs_free(limbs);
                return false;
            }
            chunk = 0;
            chunk_mult = 1;
            chunk_len = 0;
        }
    }
    if (chunk_len > 0) {
        if (!mul_add_chunk(&limbs, &limb_count, &capacity, chunk_mult, chunk)) {
            bs_free(limbs);
            return false;
        }
    }
    while (isspace((unsigned char)*str))
        ++str;
    if (!has_digit || *str) {
        bs_free(limbs);
        return false;
    }
    bs_int result = make_int(sign, limbs, limb_count);
    bs_free(limbs);
    if (bs_int_is_null(result))
        return false;
    *out = result;
    return true;
}
static size_t get_mag(bs_int a, view_t *v, uint64_t *small) {
    *v = view(a);
    if (!v->p) {
        *small = v->one;
        v->p = small;
    }
    return v->n;
}
bool bs_int_to_i64(bs_int a, int64_t *out) {
    if (bs_int_is_null(a) || !out)
        return false;
    if (bs_int_is_fixnum(a)) {
        *out = bs_int_to_fixnum_unchecked(a);
        return true;
    }
    view_t v;
    uint64_t s;
    size_t n = get_mag(a, &v, &s);
    uint64_t m = n ? v.p[0] : 0;
    if (n > 1 || (v.sign >= 0 && m > INT64_MAX) || (v.sign < 0 && m > (UINT64_C(1) << 63)))
        return false;
    *out = v.sign < 0 ? (m == (UINT64_C(1) << 63) ? INT64_MIN : -(int64_t)m) : (int64_t)m;
    return true;
}
bool bs_int_to_u64(bs_int a, uint64_t *out) {
    if (bs_int_is_null(a) || !out)
        return false;
    if (bs_int_is_fixnum(a)) {
        int64_t val = bs_int_to_fixnum_unchecked(a);
        if (val < 0)
            return false;
        *out = (uint64_t)val;
        return true;
    }
    view_t v;
    uint64_t s;
    size_t n = get_mag(a, &v, &s);
    if (v.sign < 0 || n > 1)
        return false;
    *out = n ? v.p[0] : 0;
    return true;
}
size_t bs_int_word_count(bs_int a) {
    assert(!bs_int_is_null(a));
    return view(a).n;
}
size_t bs_int_to_words(bs_int a, uint64_t *out, size_t cap) {
    assert(!bs_int_is_null(a));
    view_t v = view(a);
    if (out && cap >= v.n && v.n)
        memcpy(out, view_data(&v), v.n * sizeof(*out));
    return v.n;
}
int bs_int_sign(bs_int a) {
    assert(!bs_int_is_null(a));
    if (bs_int_is_fixnum(a)) {
        int64_t v = bs_int_to_fixnum_unchecked(a);
        return (v > 0) - (v < 0);
    }
    return bs_int_to_bignum_const(a)->sign;
}
bool bs_int_is_positive(bs_int a) {
    assert(!bs_int_is_null(a));
    if (bs_int_is_fixnum(a)) {
        return bs_int_to_fixnum_unchecked(a) > 0;
    }
    return bs_int_to_bignum_const(a)->sign > 0;
}
bool bs_int_is_negative(bs_int a) {
    assert(!bs_int_is_null(a));
    if (bs_int_is_fixnum(a)) {
        return bs_int_to_fixnum_unchecked(a) < 0;
    }
    return bs_int_to_bignum_const(a)->sign < 0;
}
bool bs_int_is_even(bs_int a) {
    assert(!bs_int_is_null(a));
    if (bs_int_is_fixnum(a)) {
        return (bs_int_to_fixnum_unchecked(a) & 1) == 0;
    }
    const bs_bignum_t *b = bs_int_to_bignum_const(a);
    return !(bs_bignum_limbs_const(b)[0] & 1);
}
bool bs_int_is_odd(bs_int a) {
    assert(!bs_int_is_null(a));
    return !bs_int_is_even(a);
}
int bs_int_cmp(bs_int a, bs_int b) {
    /* A null handle is not numeric zero; catch invalid comparisons instead of
     * letting view() interpret it as a zero-valued handle. */
    assert(!bs_int_is_null(a) && !bs_int_is_null(b));
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        return (va > vb) - (va < vb);
    }
    view_t x = view(a), y = view(b);
    if (x.sign != y.sign)
        return x.sign > y.sign ? 1 : -1;
    if (!x.sign)
        return 0;
    int c = bs_limbs_cmp(view_data(&x), x.n, view_data(&y), y.n);
    return x.sign < 0 ? -c : c;
}
static bs_int add_views(view_t x, view_t y, int yfactor) {
    const uint64_t *xp = view_data(&x), *yp = view_data(&y);
    if (x.sign == 0) {
        y.sign *= yfactor;
        return make_int(y.sign, yp, y.n);
    }
    if (y.sign == 0)
        return make_int(x.sign, xp, x.n);
    int ys = y.sign * yfactor;
    size_t maxn = x.n > y.n ? x.n : y.n;
    if (maxn >= SIZE_MAX / sizeof(uint64_t))
        return BS_INT_NULL;
    size_t n = maxn + 1;
    uint64_t stack_r[64];
    uint64_t *r = (n <= 64) ? stack_r : bs_alloc(n * sizeof(*r));
    if (!r)
        return BS_INT_NULL;
    int s;
    if (x.sign == ys) {
        uint64_t c;
        if (x.n >= y.n)
            c = bs_limbs_add(r, xp, x.n, yp, y.n);
        else
            c = bs_limbs_add(r, yp, y.n, xp, x.n);
        r[n - 1] = c;
        s = x.sign;
    } else {
        int c = bs_limbs_cmp(xp, x.n, yp, y.n);
        if (!c) {
            if (r != stack_r)
                bs_free(r);
            return BS_INT_ZERO;
        }
        const uint64_t *ap = c > 0 ? xp : yp;
        const uint64_t *bp = c > 0 ? yp : xp;
        size_t an = c > 0 ? x.n : y.n;
        size_t bn = c > 0 ? y.n : x.n;
        s = c > 0 ? x.sign : ys;
        uint64_t borrow = bs_limbs_sub(r, ap, an, bp, bn);
        (void)borrow;
        r[an] = 0;
    }
    bs_int z = make_int(s, r, n);
    if (r != stack_r)
        bs_free(r);
    return z;
}
bs_int bs_int_add(bs_int a, bs_int b) {
    if (bs_int_is_null(a) || bs_int_is_null(b))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        int64_t res;
        if (!bs_add_overflow(va, vb, &res) && bs_int_fits_fixnum(res)) {
            return bs_int_from_fixnum_unchecked(res);
        }
    }
    return add_views(view(a), view(b), 1);
}
bs_int bs_int_sub(bs_int a, bs_int b) {
    if (bs_int_is_null(a) || bs_int_is_null(b))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        int64_t res;
        if (!bs_sub_overflow(va, vb, &res) && bs_int_fits_fixnum(res)) {
            return bs_int_from_fixnum_unchecked(res);
        }
    }
    return add_views(view(a), view(b), -1);
}
bs_int bs_int_neg(bs_int a) {
    if (bs_int_is_null(a))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        if (va > BS_FIXNUM_MIN) {
            return bs_int_from_fixnum_unchecked(-va);
        }
    }
    view_t v = view(a);
    return make_int(-v.sign, view_data(&v), v.n);
}
bs_int bs_int_abs(bs_int a) {
    if (bs_int_is_null(a))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        if (va >= 0) {
            return a;
        }
        if (va > BS_FIXNUM_MIN) {
            return bs_int_from_fixnum_unchecked(-va);
        }
    }
    view_t v = view(a);
    return make_int(v.sign ? 1 : 0, view_data(&v), v.n);
}
bs_int bs_int_mul(bs_int a, bs_int b) {
    if (bs_int_is_null(a) || bs_int_is_null(b))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        int64_t res;
        if (!bs_mul_overflow(va, vb, &res) && bs_int_fits_fixnum(res)) {
            return bs_int_from_fixnum_unchecked(res);
        }
    }
    view_t x = view(a), y = view(b);
    if (!x.n || !y.n)
        return BS_INT_ZERO;
    if (x.n > SIZE_MAX - y.n)
        return BS_INT_NULL;
    size_t n = x.n + y.n;
    if (n > SIZE_MAX / sizeof(uint64_t))
        return BS_INT_NULL;
    uint64_t stack_r[64];
    uint64_t *r = (n <= 64) ? stack_r : bs_alloc(n * sizeof(*r));
    if (!r)
        return BS_INT_NULL;
    bs_limbs_mul(r, view_data(&x), x.n, view_data(&y), y.n);
    bs_int z = make_int(x.sign * y.sign, r, n);
    if (r != stack_r)
        bs_free(r);
    return z;
}
static bool divide_views(view_t x, view_t y, bs_int *qo, bs_int *ro) {
    if (!y.n)
        return false;
    int cmp = bs_limbs_cmp(view_data(&x), x.n, view_data(&y), y.n);
    if (cmp < 0) {
        *qo = BS_INT_ZERO;
        *ro = make_int(x.sign, view_data(&x), x.n);
        return !bs_int_is_null(*ro);
    }
    if (cmp == 0) {
        uint64_t one = 1;
        *qo = make_int(x.sign * y.sign, &one, 1);
        *ro = BS_INT_ZERO;
        return !bs_int_is_null(*qo);
    }
    size_t qn = x.n >= y.n ? x.n - y.n + 1 : 1, rn = y.n;
    uint64_t stack_q[64], stack_r[64];
    uint64_t *q = (qn <= 64) ? stack_q : bs_alloc(qn * sizeof(*q));
    uint64_t *r = (rn <= 64) ? stack_r : bs_alloc(rn * sizeof(*r));
    if (!q || !r) {
        if (q && q != stack_q)
            bs_free(q);
        if (r && r != stack_r)
            bs_free(r);
        return false;
    }
    bs_limbs_div(q, r, view_data(&x), x.n, view_data(&y), y.n);
    bs_int qv = make_int(x.sign * y.sign, q, qn);
    if (bs_int_is_null(qv)) {
        if (q != stack_q)
            bs_free(q);
        if (r != stack_r)
            bs_free(r);
        return false;
    }
    bs_int rv = make_int(x.sign, r, rn);
    if (bs_int_is_null(rv)) {
        bs_int_release(qv);
        if (q != stack_q)
            bs_free(q);
        if (r != stack_r)
            bs_free(r);
        return false;
    }
    *qo = qv;
    *ro = rv;
    if (q != stack_q)
        bs_free(q);
    if (r != stack_r)
        bs_free(r);
    return true;
}
bool bs_int_div_rem(bs_int a, bs_int b, bs_int *q, bs_int *r) {
    if (!q || !r || q == r || bs_int_is_null(a) || bs_int_is_null(b))
        return false;
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        if (vb == 0)
            return false;
        if (!(va == BS_FIXNUM_MIN && vb == -1)) {
            *q = bs_int_from_fixnum_unchecked(va / vb);
            *r = bs_int_from_fixnum_unchecked(va % vb);
            return true;
        }
    }
    bs_int x, y;
    if (!divide_views(view(a), view(b), &x, &y))
        return false;
    *q = x;
    *r = y;
    return true;
}
bs_int bs_int_div(bs_int a, bs_int b) {
    if (bs_int_is_null(a) || bs_int_is_null(b))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        if (vb == 0)
            return BS_INT_NULL;
        if (!(va == BS_FIXNUM_MIN && vb == -1)) {
            return bs_int_from_fixnum_unchecked(va / vb);
        }
    }
    bs_int q, r;
    if (!divide_views(view(a), view(b), &q, &r))
        return BS_INT_NULL;
    bs_int_release(r);
    return q;
}
bs_int bs_int_rem(bs_int a, bs_int b) {
    if (bs_int_is_null(a) || bs_int_is_null(b))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        if (vb == 0)
            return BS_INT_NULL;
        return bs_int_from_fixnum_unchecked(va % vb);
    }
    bs_int q, r;
    if (!divide_views(view(a), view(b), &q, &r))
        return BS_INT_NULL;
    bs_int_release(q);
    return r;
}
bs_int bs_int_pow(bs_int a, uint32_t e) {
    if (bs_int_is_null(a))
        return BS_INT_NULL;
    bs_int result = BS_INT_ONE, base = bs_int_retain(a);
    while (e) {
        if (e & 1) {
            bs_int n = bs_int_mul(result, base);
            bs_int_release(result);
            if (bs_int_is_null(n)) {
                bs_int_release(base);
                return BS_INT_NULL;
            }
            result = n;
        }
        e >>= 1;
        if (e) {
            bs_int n = bs_int_mul(base, base);
            bs_int_release(base);
            if (bs_int_is_null(n)) {
                bs_int_release(result);
                return BS_INT_NULL;
            }
            base = n;
        }
    }
    bs_int_release(base);
    return result;
}
bs_int bs_int_gcd(bs_int a, bs_int b) {
    if (bs_int_is_null(a) || bs_int_is_null(b))
        return BS_INT_NULL;
    if (bs_int_is_zero(a))
        return bs_int_abs(b);
    if (bs_int_is_zero(b))
        return bs_int_abs(a);
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        uint64_t ua = va < 0 ? (uint64_t)(-(va + 1)) + 1 : (uint64_t)va;
        uint64_t ub = vb < 0 ? (uint64_t)(-(vb + 1)) + 1 : (uint64_t)vb;
        while (ub != 0) {
            uint64_t t = ua % ub;
            ua = ub;
            ub = t;
        }
        return bs_int_from_u64(ua);
    }
    bs_int x = bs_int_abs(a), y = bs_int_abs(b);
    if (bs_int_is_null(x) || bs_int_is_null(y)) {
        bs_int_release(x);
        bs_int_release(y);
        return BS_INT_NULL;
    }
    while (!bs_int_is_zero(y)) {
        bs_int r = bs_int_rem(x, y);
        bs_int_release(x);
        if (bs_int_is_null(r)) {
            bs_int_release(y);
            return BS_INT_NULL;
        }
        x = y;
        y = r;
    }
    bs_int_release(y);
    return x;
}
bs_int bs_int_lcm(bs_int a, bs_int b) {
    if (bs_int_is_null(a) || bs_int_is_null(b))
        return BS_INT_NULL;
    if (bs_int_is_zero(a) || bs_int_is_zero(b))
        return BS_INT_ZERO;
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        uint64_t ua = va < 0 ? (uint64_t)(-(va + 1)) + 1 : (uint64_t)va;
        uint64_t ub = vb < 0 ? (uint64_t)(-(vb + 1)) + 1 : (uint64_t)vb;
        uint64_t x = ua, y = ub;
        while (y != 0) {
            uint64_t t = x % y;
            x = y;
            y = t;
        }
        uint64_t q = ua / x;
        uint64_t lo, hi;
        bs_limb_mul_words(q, ub, &lo, &hi);
        if (hi == 0 && lo <= (uint64_t)BS_FIXNUM_MAX) {
            return bs_int_from_fixnum_unchecked((int64_t)lo);
        }
        return make_int(1, (const uint64_t[]){lo, hi}, hi ? 2 : 1);
    }
    bs_int g = bs_int_gcd(a, b);
    if (bs_int_is_null(g))
        return BS_INT_NULL;
    bs_int q = bs_int_div(a, g);
    bs_int_release(g);
    if (bs_int_is_null(q))
        return BS_INT_NULL;
    bs_int m = bs_int_mul(q, b);
    bs_int_release(q);
    if (bs_int_is_null(m))
        return BS_INT_NULL;
    bs_int z = bs_int_abs(m);
    bs_int_release(m);
    return z;
}

static bs_int bitop(bs_int a, bs_int b, int op) {
    view_t x = view(a), y = view(b);
    size_t maxn = x.n > y.n ? x.n : y.n;
    if (maxn >= SIZE_MAX / sizeof(uint64_t))
        return BS_INT_NULL;
    size_t n = maxn + 1;
    uint64_t stack_u[64], stack_v[64], stack_r[64];
    uint64_t *u = (n <= 64) ? stack_u : bs_alloc(n * sizeof(*u));
    uint64_t *v = (n <= 64) ? stack_v : bs_alloc(n * sizeof(*v));
    uint64_t *r = (n <= 64) ? stack_r : bs_alloc(n * sizeof(*r));
    if (!u || !v || !r) {
        if (u && u != stack_u)
            bs_free(u);
        if (v && v != stack_v)
            bs_free(v);
        if (r && r != stack_r)
            bs_free(r);
        return BS_INT_NULL;
    }
    memset(u, 0, n * sizeof(*u));
    memset(v, 0, n * sizeof(*v));
    memcpy(u, view_data(&x), x.n * sizeof(*u));
    memcpy(v, view_data(&y), y.n * sizeof(*v));
    if (x.sign < 0) {
        for (size_t i = 0; i < n; i++)
            u[i] = ~u[i];
        bs_limbs_add_1(u, u, n, 1);
    }
    if (y.sign < 0) {
        for (size_t i = 0; i < n; i++)
            v[i] = ~v[i];
        bs_limbs_add_1(v, v, n, 1);
    }
    for (size_t i = 0; i < n; i++)
        r[i] = op == 0 ? (u[i] & v[i]) : op == 1 ? (u[i] | v[i]) : (u[i] ^ v[i]);
    int s = (r[n - 1] >> 63) ? -1 : 1;
    if (s < 0) {
        for (size_t i = 0; i < n; i++)
            r[i] = ~r[i];
        bs_limbs_add_1(r, r, n, 1);
    }
    bs_int z = make_int(s, r, n);
    if (u != stack_u)
        bs_free(u);
    if (v != stack_v)
        bs_free(v);
    if (r != stack_r)
        bs_free(r);
    return z;
}
bs_int bs_int_and(bs_int a, bs_int b) {
    if (bs_int_is_null(a) || bs_int_is_null(b))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        return bs_int_from_fixnum_unchecked(va & vb);
    }
    return bitop(a, b, 0);
}
bs_int bs_int_or(bs_int a, bs_int b) {
    if (bs_int_is_null(a) || bs_int_is_null(b))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        return bs_int_from_fixnum_unchecked(va | vb);
    }
    return bitop(a, b, 1);
}
bs_int bs_int_xor(bs_int a, bs_int b) {
    if (bs_int_is_null(a) || bs_int_is_null(b))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a) && bs_int_is_fixnum(b)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        int64_t vb = bs_int_to_fixnum_unchecked(b);
        return bs_int_from_fixnum_unchecked(va ^ vb);
    }
    return bitop(a, b, 2);
}
bs_int bs_int_not(bs_int a) {
    if (bs_int_is_null(a))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        return bs_int_from_fixnum_unchecked(~va);
    }
    bs_int neg = bs_int_neg(a), one = BS_INT_ONE;
    if (bs_int_is_null(neg))
        return BS_INT_NULL;
    bs_int z = bs_int_sub(neg, one);
    bs_int_release(neg);
    return z;
}
bs_int bs_int_shl(bs_int a, size_t shift) {
    if (bs_int_is_null(a))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        if (va == 0 || shift == 0)
            return a;
        if (shift < 63) {
            if (va > 0 && (uint64_t)va <= ((uint64_t)BS_FIXNUM_MAX >> shift)) {
                return bs_int_from_fixnum_unchecked((int64_t)((uint64_t)va << shift));
            }
            if (va < 0) {
                uint64_t mag = (uint64_t)(-(va + 1)) + 1;
                if (mag <= (UINT64_C(1) << (62 - shift))) {
                    return bs_int_from_fixnum_unchecked((int64_t)((uint64_t)va << shift));
                }
            }
        }
    }
    view_t x = view(a);
    if (!x.n)
        return BS_INT_ZERO;
    if (shift > SIZE_MAX - 63 || x.n > SIZE_MAX - (shift / 64) - 1)
        return BS_INT_NULL;
    size_t words = shift / 64, bits = shift % 64, n = x.n + words + 1;
    if (n > SIZE_MAX / sizeof(uint64_t))
        return BS_INT_NULL;
    uint64_t stack_r[64];
    uint64_t *r = (n <= 64) ? stack_r : bs_alloc(n * sizeof(*r));
    if (!r)
        return BS_INT_NULL;
    memset(r, 0, n * sizeof(*r));
    const uint64_t *xp = view_data(&x);
    for (size_t i = 0; i < x.n; i++) {
        r[i + words] |= xp[i] << bits;
        if (bits)
            r[i + words + 1] |= xp[i] >> (64 - bits);
    }
    bs_int z = make_int(x.sign, r, n);
    if (r != stack_r)
        bs_free(r);
    return z;
}
bs_int bs_int_shr(bs_int a, size_t shift) {
    if (bs_int_is_null(a))
        return BS_INT_NULL;
    if (bs_int_is_fixnum(a)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        if (shift >= 63) {
            return va < 0 ? bs_int_from_fixnum_unchecked(-1) : BS_INT_ZERO;
        }
        return bs_int_from_fixnum_unchecked(va >> shift);
    }
    view_t x = view(a);
    if (!x.n)
        return BS_INT_ZERO;
    size_t words = shift / 64, bits = shift % 64;
    if (words >= x.n)
        return x.sign < 0 ? bs_int_from_fixnum_unchecked(-1) : BS_INT_ZERO;
    size_t n = x.n - words;
    size_t alloc_n = x.sign < 0 ? n + 1 : n;
    if (alloc_n > SIZE_MAX / sizeof(uint64_t))
        return BS_INT_NULL;
    uint64_t stack_r[64];
    uint64_t *r = (alloc_n <= 64) ? stack_r : bs_alloc(alloc_n * sizeof(*r));
    if (!r)
        return BS_INT_NULL;
    const uint64_t *xp = view_data(&x);
    for (size_t i = 0; i < n; i++) {
        r[i] = xp[i + words] >> bits;
        if (bits && i + words + 1 < x.n)
            r[i] |= xp[i + words + 1] << (64 - bits);
    }
    if (x.sign < 0) {
        r[n] = 0;
        int discarded = 0;
        for (size_t i = 0; i < words; i++)
            discarded |= xp[i] != 0;
        if (bits)
            discarded |= (xp[words] & ((UINT64_C(1) << bits) - 1)) != 0;
        if (discarded) {
            uint64_t carry = bs_limbs_add_1(r, r, n, 1);
            if (carry) {
                r[n] = carry;
                n++;
            }
        }
    }
    bs_int z = make_int(x.sign, r, n);
    if (r != stack_r)
        bs_free(r);
    return z;
}
size_t bs_int_bit_len(bs_int a) {
    assert(!bs_int_is_null(a));
    view_t x = view(a);
    if (!x.n)
        return 0;
    return (x.n - 1) * 64 + 64 - bs_clz64(view_data(&x)[x.n - 1]);
}
size_t bs_int_popcount(bs_int a) {
    assert(!bs_int_is_null(a));
    view_t x = view(a);
    size_t n = 0;
    const uint64_t *xp = view_data(&x);
    for (size_t i = 0; i < x.n; i++)
        n += bs_popcount64(xp[i]);
    return n;
}
char *bs_int_to_str(bs_int a, int base) {
    if (bs_int_is_null(a) || base < 2 || base > 36)
        return NULL;
    view_t x = view(a);
    if (!x.n) {
        char *z = bs_alloc(2);
        if (z)
            strcpy(z, "0");
        return z;
    }
    if (x.n > (SIZE_MAX - 3) / 64)
        return NULL;
    size_t max = x.n * 64 + 3;
    char stack_rev[128];
    char *rev = (max <= sizeof(stack_rev)) ? stack_rev : bs_alloc(max);
    uint64_t stack_d[64];
    uint64_t *d = (x.n <= 64) ? stack_d : bs_alloc(x.n * sizeof(uint64_t));
    if (!rev || !d) {
        if (rev && rev != stack_rev)
            bs_free(rev);
        if (d && d != stack_d)
            bs_free(d);
        return NULL;
    }
    memcpy(d, view_data(&x), x.n * sizeof(uint64_t));
    size_t n = x.n, k = 0;
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

    uint64_t max_mult = (unsigned)base;
    int max_digits = 1;
    while (max_mult <= UINT64_MAX / (unsigned)base) {
        max_mult *= (unsigned)base;
        max_digits++;
    }

    while (n) {
        uint64_t rem = 0;
        for (size_t i = n; i-- > 0;) {
#if defined(BS_HAS_INT128)
            __uint128_t cur = ((__uint128_t)rem << 64) | d[i];
            d[i] = (uint64_t)(cur / max_mult);
            rem = (uint64_t)(cur % max_mult);
#else
            /* Bitwise long division keeps formatting portable without a double-width
             * type. */
            uint64_t q = 0, r = rem;
            for (int bit = 63; bit >= 0; --bit) {
                uint64_t hi = r >> 63;
                r = (r << 1) | ((d[i] >> bit) & 1);
                if (hi || r >= max_mult) {
                    r -= max_mult;
                    q |= UINT64_C(1) << bit;
                }
            }
            d[i] = q;
            rem = r;
#endif
        }
        n = bs_limbs_normalized_len(d, n);
        if (n > 0) {
            for (int j = 0; j < max_digits; j++) {
                rev[k++] = digits[rem % (unsigned)base];
                rem /= (unsigned)base;
            }
        } else {
            do {
                rev[k++] = digits[rem % (unsigned)base];
                rem /= (unsigned)base;
            } while (rem > 0);
        }
    }
    if (x.sign < 0)
        rev[k++] = '-';
    char *out = bs_alloc(k + 1);
    if (out) {
        for (size_t i = 0; i < k; i++)
            out[i] = rev[k - i - 1];
        out[k] = 0;
    }
    if (rev != stack_rev)
        bs_free(rev);
    if (d != stack_d)
        bs_free(d);
    return out;
}

bs_int bs_int_pow_mod(bs_int base, bs_int exp, bs_int mod) {
    if (bs_int_is_null(base) || bs_int_is_null(exp) || bs_int_is_null(mod))
        return BS_INT_NULL;
    if (bs_int_is_negative(exp))
        return BS_INT_NULL;
    bs_int m = bs_int_abs(mod);
    if (bs_int_is_null(m))
        return BS_INT_NULL;
    if (bs_int_cmp(m, BS_INT_ONE) <= 0) {
        bs_int_release(m);
        return BS_INT_NULL;
    }
    if (bs_int_is_zero(exp)) {
        bs_int_release(m);
        return BS_INT_ONE;
    }
    bs_int b = bs_int_rem(base, m);
    if (bs_int_is_null(b)) {
        bs_int_release(m);
        return BS_INT_NULL;
    }
    if (bs_int_is_negative(b)) {
        bs_int b_adj = bs_int_add(b, m);
        bs_int_release(b);
        if (bs_int_is_null(b_adj)) {
            bs_int_release(m);
            return BS_INT_NULL;
        }
        b = b_adj;
    }
    bs_int res = BS_INT_ONE;
    bs_int e = bs_int_retain(exp);
    while (!bs_int_is_zero(e)) {
        if (bs_int_is_odd(e)) {
            bs_int prod = bs_int_mul(res, b);
            bs_int_release(res);
            if (bs_int_is_null(prod)) {
                bs_int_release(b);
                bs_int_release(e);
                bs_int_release(m);
                return BS_INT_NULL;
            }
            res = bs_int_rem(prod, m);
            bs_int_release(prod);
            if (bs_int_is_null(res)) {
                bs_int_release(b);
                bs_int_release(e);
                bs_int_release(m);
                return BS_INT_NULL;
            }
        }
        bs_int next_e = bs_int_shr(e, 1);
        bs_int_release(e);
        if (bs_int_is_null(next_e)) {
            bs_int_release(res);
            bs_int_release(b);
            bs_int_release(m);
            return BS_INT_NULL;
        }
        e = next_e;
        if (!bs_int_is_zero(e)) {
            bs_int b_sq = bs_int_mul(b, b);
            bs_int_release(b);
            if (bs_int_is_null(b_sq)) {
                bs_int_release(res);
                bs_int_release(e);
                bs_int_release(m);
                return BS_INT_NULL;
            }
            b = bs_int_rem(b_sq, m);
            bs_int_release(b_sq);
            if (bs_int_is_null(b)) {
                bs_int_release(res);
                bs_int_release(e);
                bs_int_release(m);
                return BS_INT_NULL;
            }
        }
    }
    bs_int_release(b);
    bs_int_release(e);
    bs_int_release(m);
    return res;
}

bool bs_int_sqrt_rem(bs_int a, bs_int *s, bs_int *r) {
    if (bs_int_is_null(a) || !s || !r || s == r || bs_int_is_negative(a))
        return false;
    if (bs_int_is_zero(a)) {
        *s = BS_INT_ZERO;
        *r = BS_INT_ZERO;
        return true;
    }
    if (bs_int_is_fixnum(a)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        uint64_t x = (uint64_t)va;
        uint64_t root = 0;
        uint64_t bit = UINT64_C(1) << 62;
        while (bit > x)
            bit >>= 2;
        while (bit != 0) {
            if (x >= root + bit) {
                x -= root + bit;
                root = (root >> 1) + bit;
            } else {
                root >>= 1;
            }
            bit >>= 2;
        }
        *s = bs_int_from_fixnum_unchecked((int64_t)root);
        *r = bs_int_from_fixnum_unchecked((int64_t)x);
        return true;
    }
    size_t bits = bs_int_bit_len(a);
    bs_int xk = bs_int_shl(BS_INT_ONE, (bits + 1) / 2);
    if (bs_int_is_null(xk))
        return false;
    for (;;) {
        bs_int q = bs_int_div(a, xk);
        if (bs_int_is_null(q)) {
            bs_int_release(xk);
            return false;
        }
        bs_int sum = bs_int_add(xk, q);
        bs_int_release(q);
        if (bs_int_is_null(sum)) {
            bs_int_release(xk);
            return false;
        }
        bs_int next_x = bs_int_shr(sum, 1);
        bs_int_release(sum);
        if (bs_int_is_null(next_x)) {
            bs_int_release(xk);
            return false;
        }
        if (bs_int_cmp(next_x, xk) >= 0) {
            bs_int_release(next_x);
            break;
        }
        bs_int_release(xk);
        xk = next_x;
    }
    bs_int s_sq = bs_int_mul(xk, xk);
    if (bs_int_is_null(s_sq)) {
        bs_int_release(xk);
        return false;
    }
    bs_int rem = bs_int_sub(a, s_sq);
    bs_int_release(s_sq);
    if (bs_int_is_null(rem)) {
        bs_int_release(xk);
        return false;
    }
    *s = xk;
    *r = rem;
    return true;
}

bs_int bs_int_sqrt(bs_int a) {
    bs_int s = BS_INT_NULL, r = BS_INT_NULL;
    if (!bs_int_sqrt_rem(a, &s, &r))
        return BS_INT_NULL;
    bs_int_release(r);
    return s;
}

bool bs_int_test_bit(bs_int a, size_t bit) {
    assert(!bs_int_is_null(a));
    if (bs_int_is_zero(a))
        return false;
    if (bs_int_is_fixnum(a)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        if (bit < 63)
            return (((uint64_t)va >> bit) & 1) != 0;
        return va < 0;
    }
    const bs_bignum_t *b = bs_int_to_bignum_const(a);
    const bs_limb_t *p = bs_bignum_limbs_const(b);
    size_t word = bit / 64;
    size_t bit_idx = bit % 64;
    if (b->sign > 0) {
        if (word >= b->len)
            return false;
        return ((p[word] >> bit_idx) & 1) != 0;
    }
    /* Two's complement for negative numbers: -M = ~(M - 1).
     * Bit k of -M is set iff bit k of (M - 1) is 0.
     * For k >= 64 * b->len, (M - 1) has 0 at bit k, so bit k of -M is 1. */
    if (word >= b->len)
        return true;
    bool borrow = true;
    for (size_t i = 0; i < word; i++) {
        if (p[i] != 0) {
            borrow = false;
            break;
        }
    }
    uint64_t w_val = borrow ? (p[word] - 1) : p[word];
    return !((w_val >> bit_idx) & 1);
}

size_t bs_int_ctz(bs_int a) {
    assert(!bs_int_is_null(a));
    if (bs_int_is_zero(a))
        return 0;
    if (bs_int_is_fixnum(a)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        uint64_t mag = va < 0 ? (uint64_t)(-(va + 1)) + 1 : (uint64_t)va;
        return bs_ctz64(mag);
    }
    view_t v = view(a);
    const uint64_t *p = view_data(&v);
    size_t i = 0;
    while (i < v.n && p[i] == 0)
        i++;
    if (i == v.n)
        return 0;
    return i * 64 + bs_ctz64(p[i]);
}


uint64_t bs_int_hash(bs_int a) {
    assert(!bs_int_is_null(a));
    if (bs_int_is_fixnum(a)) {
        int64_t va = bs_int_to_fixnum_unchecked(a);
        return bs_splitmix64((uint64_t)va);
    }
    view_t v = view(a);
    const uint64_t *p = view_data(&v);
    uint64_t h = bs_splitmix64((uint64_t)(int64_t)v.sign);
    h ^= bs_splitmix64((uint64_t)v.n);
    for (size_t i = 0; i < v.n; i++) {
        h = (h * UINT64_C(0x100000001b3)) ^ bs_splitmix64(p[i]);
    }
    return bs_splitmix64(h);
}
