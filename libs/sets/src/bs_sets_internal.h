#ifndef BS_SETS_INTERNAL_H
#define BS_SETS_INTERNAL_H

#include "bs_sets/bs_sets.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline uint64_t bs_splitmix64(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

typedef enum bs_payload_kind {
    BS_PAYLOAD_MINTED_INDEX,
    BS_PAYLOAD_NATURE,
    BS_PAYLOAD_INTEGER
} bs_payload_kind;

typedef struct bs_payload {
    bs_payload_kind kind;
    union {
        uint64_t index;
        const bs_nature *nature;
        bs_int integer;
    } as;
} bs_payload;

_Static_assert(sizeof(bs_payload) == 2 * sizeof(uint64_t),
               "public opaque value storage must match internal payload size");

typedef struct bs_value_impl {
    const bs_nature *nature;
    bs_payload payload;
} bs_value_impl;

static inline bs_value_impl bs_value_unpack(bs_value value)
{
    bs_value_impl result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static inline void bs_value_pack(bs_value_impl value, bs_value *out)
{
    memcpy(out, &value, sizeof(value));
}

_Static_assert(sizeof(bs_value) == sizeof(bs_value_impl),
               "public opaque value storage must match internal value size");
_Static_assert(_Alignof(bs_value) == _Alignof(bs_value_impl),
               "public opaque value storage must match internal value alignment");

typedef struct bs_nature_iter_state {
    uint64_t ordinal;
    bool exhausted;
    void *context;
} bs_nature_iter_state;

typedef struct bs_payload_object bs_payload_object;
typedef void (*bs_payload_object_destroy_fn)(void *data);

/* Allocate immutable payload storage whose lifetime is tied to its Universe. */
bs_status bs_internal_payload_object_create(
    bs_universe *universe,
    size_t size,
    bs_payload_object_destroy_fn destroy,
    const bs_payload_object **out_object,
    void **out_data
);
const void *bs_internal_payload_object_data(
    const bs_payload_object *object
);

typedef struct bs_nature_vtable {
    bool (*equal)(bs_value_impl left, bs_value_impl right);
    bs_status (*compare)(bs_value_impl left, bs_value_impl right, int *out_comparison);
    uint64_t (*hash)(bs_value_impl value);
    bool (*value_valid)(bs_value_impl value, const bs_universe *universe);
    bs_status (*iter_step)(const bs_nature *nature, bs_nature_iter_state *state, bs_value_impl *out_value);
    void (*iter_state_destroy)(bs_nature_iter_state *state);
    bs_status (*step)(const bs_nature *nature, bs_value_impl current, int direction, bs_value_impl *out_value);
    bs_status (*interval_cardinality)(
        const bs_nature *nature,
        bs_value_impl lower,
        bool lower_inclusive,
        bs_value_impl upper,
        bool upper_inclusive,
        bs_int *out_cardinality
    );
} bs_nature_vtable;

bool bs_internal_universe_owns_nature(
    const bs_universe *universe,
    const bs_nature *nature
);

bool bs_internal_value_belongs_to_impl(bs_value_impl value, const bs_universe *universe);
bs_status bs_nature_get_minted_value_impl(const bs_nature *nature, uint64_t index, bs_value_impl *out_value);
bs_status bs_nature_mint_value_impl(bs_universe *universe, bs_nature *nature, bs_value_impl *out_value);
bs_status bs_internal_nature_iter_step_impl(const bs_nature *nature, bs_nature_iter_state *state, bs_value_impl *out_value);
bs_status bs_internal_nature_step_impl(const bs_nature *nature, bs_value_impl current, int direction, bs_value_impl *out_value);
bs_status bs_internal_nature_interval_cardinality_impl(const bs_nature *nature, bs_value_impl lower, bool lower_inclusive, bs_value_impl upper, bool upper_inclusive, bs_int *out_cardinality);
bool bs_internal_value_belongs_to(
    bs_value value,
    const bs_universe *universe
);
const bs_nature_vtable *bs_internal_nature_vtable(const bs_nature *nature);
bs_status bs_internal_nature_iter_step(
    const bs_nature *nature,
    bs_nature_iter_state *state,
    bs_value *out_value
);
void bs_internal_nature_iter_state_destroy(
    const bs_nature *nature,
    bs_nature_iter_state *state
);
bs_status bs_internal_nature_step(
    const bs_nature *nature,
    bs_value current,
    int direction,
    bs_value *out_value
);
bs_status bs_internal_nature_interval_cardinality(
    const bs_nature *nature,
    bs_value lower,
    bool lower_inclusive,
    bs_value upper,
    bool upper_inclusive,
    bs_int *out_cardinality
);

#ifndef NDEBUG
void bs_internal_universe_register_store(const bs_universe *universe);
void bs_internal_universe_unregister_store(const bs_universe *universe);
#else
static inline void bs_internal_universe_register_store(const bs_universe *universe) { (void)universe; }
static inline void bs_internal_universe_unregister_store(const bs_universe *universe) { (void)universe; }
#endif

#endif /* BS_SETS_INTERNAL_H */
