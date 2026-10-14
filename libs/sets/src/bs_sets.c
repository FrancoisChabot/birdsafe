#include "bs_sets_internal.h"

#include <assert.h>
#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

struct bs_nature {
    struct bs_nature *next;
    struct bs_universe *owner;
    const bs_nature_vtable *vtable;
    uint64_t cardinality;
    bs_atomic_uint64_t next_payload;
    bs_payload_kind payload_kind;
    bool finite;
    bool enumerable;
    bool discrete;
};

struct bs_payload_object {
    struct bs_payload_object *next;
    void *data;
    bs_payload_object_destroy_fn destroy;
};

struct bs_universe {
    bs_mutex_t natures_mutex;
    bs_nature *natures;
    bs_atomic_ptr_t payload_objects;
    const bs_nature *nature_of_natures;
    const bs_nature *integer_nature;
#ifndef NDEBUG
    bs_atomic_size_t active_stores;
#endif
};

bs_status bs_internal_payload_object_create(
    bs_universe *universe,
    size_t size,
    bs_payload_object_destroy_fn destroy,
    const bs_payload_object **out_object,
    void **out_data
)
{
    if (universe == NULL || size == 0 || out_object == NULL || out_data == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_payload_object *object = (bs_payload_object *)bs_calloc(1, sizeof(*object));
    if (object == NULL) {
        return BS_OUT_OF_MEMORY;
    }
    object->data = bs_calloc(1, size);
    if (object->data == NULL) {
        bs_free(object);
        return BS_OUT_OF_MEMORY;
    }
    object->destroy = destroy;

    void *head = bs_atomic_load_relaxed(&universe->payload_objects);
    do {
        object->next = (bs_payload_object *)head;
    } while (!bs_atomic_compare_exchange_weak_release_relaxed(
        &universe->payload_objects, &head, object));

    *out_object = object;
    *out_data = object->data;
    return BS_OK;
}

const void *bs_internal_payload_object_data(
    const bs_payload_object *object
)
{
    return object == NULL ? NULL : object->data;
}

static bool owns_nature(const bs_universe *universe, const bs_nature *nature)
{
    if (universe == NULL || nature == NULL) {
        return false;
    }
    return nature->owner == universe;
}

/* ========================================================================= */
/* Nature-of-Natures Implementation                                          */
/* ========================================================================= */

static bool nature_of_natures_equal(bs_value_impl left, bs_value_impl right)
{
    if (left.payload.kind != BS_PAYLOAD_NATURE ||
        right.payload.kind != BS_PAYLOAD_NATURE) {
        return false;
    }
    return left.payload.as.nature == right.payload.as.nature;
}

static bs_status nature_of_natures_compare(
    bs_value_impl left,
    bs_value_impl right,
    int *out_comparison
)
{
    if (left.payload.kind != BS_PAYLOAD_NATURE ||
        right.payload.kind != BS_PAYLOAD_NATURE) {
        return BS_SETS_TYPE_ERROR;
    }
    if (left.payload.as.nature == right.payload.as.nature) {
        *out_comparison = 0;
        return BS_OK;
    }
    return BS_SETS_TYPE_ERROR;
}

static bool nature_of_natures_value_valid(
    bs_value_impl value,
    const bs_universe *universe
)
{
    return value.payload.kind == BS_PAYLOAD_NATURE &&
           value.nature == universe->nature_of_natures &&
           owns_nature(universe, value.payload.as.nature);
}

static bs_status nature_of_natures_iter_step(
    const bs_nature *nature,
    bs_nature_iter_state *state,
    bs_value_impl *out_value
)
{
    (void)nature;
    (void)state;
    (void)out_value;
    return BS_SETS_EXHAUSTED;
}

static uint64_t nature_of_natures_hash(bs_value_impl value)
{
    uint64_t h = (uintptr_t)value.nature;
    h ^= bs_splitmix64((uintptr_t)value.payload.as.nature);
    return bs_splitmix64(h);
}

static const bs_nature_vtable nature_of_natures_vtable = {
    .equal = nature_of_natures_equal,
    .compare = nature_of_natures_compare,
    .hash = nature_of_natures_hash,
    .value_valid = nature_of_natures_value_valid,
    .iter_step = nature_of_natures_iter_step,
    .iter_state_destroy = NULL,
    .step = NULL,
    .interval_cardinality = NULL
};

/* ========================================================================= */
/* Minted Finite Nature Implementation                                       */
/* ========================================================================= */

static bool minted_finite_equal(bs_value_impl left, bs_value_impl right)
{
    if (left.payload.kind != BS_PAYLOAD_MINTED_INDEX ||
        right.payload.kind != BS_PAYLOAD_MINTED_INDEX) {
        return false;
    }
    return left.payload.as.index == right.payload.as.index;
}

static bs_status minted_finite_compare(
    bs_value_impl left,
    bs_value_impl right,
    int *out_comparison
)
{
    if (left.payload.kind != BS_PAYLOAD_MINTED_INDEX ||
        right.payload.kind != BS_PAYLOAD_MINTED_INDEX) {
        return BS_SETS_TYPE_ERROR;
    }
    if (left.payload.as.index < right.payload.as.index) {
        *out_comparison = -1;
    } else if (left.payload.as.index > right.payload.as.index) {
        *out_comparison = 1;
    } else {
        *out_comparison = 0;
    }
    return BS_OK;
}

static bool minted_finite_value_valid(
    bs_value_impl value,
    const bs_universe *universe
)
{
    return value.payload.kind == BS_PAYLOAD_MINTED_INDEX &&
           owns_nature(universe, value.nature) &&
           value.nature->finite &&
           value.nature->payload_kind == BS_PAYLOAD_MINTED_INDEX &&
           value.payload.as.index < value.nature->cardinality;
}

static bs_status minted_finite_iter_step(
    const bs_nature *nature,
    bs_nature_iter_state *state,
    bs_value_impl *out_value
)
{
    if (state->ordinal >= nature->cardinality) {
        return BS_SETS_EXHAUSTED;
    }
    return bs_nature_get_minted_value_impl(nature, state->ordinal++, out_value);
}

static bs_status minted_finite_step(
    const bs_nature *nature,
    bs_value_impl current,
    int direction,
    bs_value_impl *out_value
)
{
    if (current.payload.kind != BS_PAYLOAD_MINTED_INDEX) {
        return BS_SETS_TYPE_ERROR;
    }
    uint64_t idx = current.payload.as.index;
    if (direction > 0) {
        if (idx + 1 >= nature->cardinality) {
            return BS_SETS_EXHAUSTED;
        }
        return bs_nature_get_minted_value_impl(nature, idx + 1, out_value);
    }
    if (direction < 0) {
        if (idx == 0) {
            return BS_SETS_EXHAUSTED;
        }
        return bs_nature_get_minted_value_impl(nature, idx - 1, out_value);
    }
    *out_value = current;
    return BS_OK;
}

static bs_status minted_interval_cardinality(
    const bs_nature *nature,
    bs_value_impl lower,
    bool lower_inclusive,
    bs_value_impl upper,
    bool upper_inclusive,
    bs_int *out_cardinality
)
{
    (void)nature;
    if (lower.payload.kind != BS_PAYLOAD_MINTED_INDEX ||
        upper.payload.kind != BS_PAYLOAD_MINTED_INDEX) {
        return BS_SETS_TYPE_ERROR;
    }
    uint64_t low = lower.payload.as.index;
    uint64_t high = upper.payload.as.index;
    if (!lower_inclusive) {
        if (low == UINT64_MAX) { *out_cardinality = BS_INT_ZERO; return BS_OK; }
        low++;
    }
    if (!upper_inclusive) {
        if (high == 0) { *out_cardinality = BS_INT_ZERO; return BS_OK; }
        high--;
    }
    if (low > high) {
        *out_cardinality = BS_INT_ZERO;
        return BS_OK;
    }
    uint64_t span = high - low;
    bs_int diff = bs_int_from_u64(span);
    if (bs_int_is_null(diff)) return BS_OUT_OF_MEMORY;
    bs_int count = bs_int_add(diff, BS_INT_ONE);
    bs_int_release(diff);
    if (bs_int_is_null(count)) return BS_OUT_OF_MEMORY;

    *out_cardinality = count;
    return BS_OK;
}

static uint64_t minted_hash(bs_value_impl value)
{
    uint64_t h = (uintptr_t)value.nature;
    h ^= bs_splitmix64(value.payload.as.index);
    return bs_splitmix64(h);
}

static const bs_nature_vtable minted_finite_vtable = {
    .equal = minted_finite_equal,
    .compare = minted_finite_compare,
    .hash = minted_hash,
    .value_valid = minted_finite_value_valid,
    .iter_step = minted_finite_iter_step,
    .iter_state_destroy = NULL,
    .step = minted_finite_step,
    .interval_cardinality = minted_interval_cardinality
};

/* ========================================================================= */
/* Minted Non-Finite Nature Implementation                                   */
/* ========================================================================= */

static bool minted_nonfinite_equal(bs_value_impl left, bs_value_impl right)
{
    if (left.payload.kind != BS_PAYLOAD_MINTED_INDEX ||
        right.payload.kind != BS_PAYLOAD_MINTED_INDEX) {
        return false;
    }
    return left.payload.as.index == right.payload.as.index;
}

static bs_status minted_nonfinite_compare(
    bs_value_impl left,
    bs_value_impl right,
    int *out_comparison
)
{
    if (left.payload.kind != BS_PAYLOAD_MINTED_INDEX ||
        right.payload.kind != BS_PAYLOAD_MINTED_INDEX) {
        return BS_SETS_TYPE_ERROR;
    }
    if (left.payload.as.index < right.payload.as.index) {
        *out_comparison = -1;
    } else if (left.payload.as.index > right.payload.as.index) {
        *out_comparison = 1;
    } else {
        *out_comparison = 0;
    }
    return BS_OK;
}

static bool minted_nonfinite_value_valid(
    bs_value_impl value,
    const bs_universe *universe
)
{
    (void) universe;
    return value.payload.kind == BS_PAYLOAD_MINTED_INDEX;
}

static bs_status minted_nonfinite_iter_step(
    const bs_nature *nature,
    bs_nature_iter_state *state,
    bs_value_impl *out_value
)
{
    if (state->exhausted) {
        return BS_SETS_EXHAUSTED;
    }
    out_value->nature = nature;
    out_value->payload.kind = BS_PAYLOAD_MINTED_INDEX;
    out_value->payload.as.index = state->ordinal;
    if (state->ordinal == UINT64_MAX) {
        state->exhausted = true;
    } else {
        ++state->ordinal;
    }
    return BS_OK;
}

static bs_status minted_nonfinite_step(
    const bs_nature *nature,
    bs_value_impl current,
    int direction,
    bs_value_impl *out_value
)
{
    if (current.payload.kind != BS_PAYLOAD_MINTED_INDEX) {
        return BS_SETS_TYPE_ERROR;
    }
    if (direction > 0) {
        if (current.payload.as.index == UINT64_MAX) {
            return BS_SETS_EXHAUSTED;
        }
        out_value->nature = nature;
        out_value->payload.kind = BS_PAYLOAD_MINTED_INDEX;
        out_value->payload.as.index = current.payload.as.index + 1;
        return BS_OK;
    } else if (direction < 0) {
        if (current.payload.as.index == 0) {
            return BS_SETS_EXHAUSTED;
        }
        out_value->nature = nature;
        out_value->payload.kind = BS_PAYLOAD_MINTED_INDEX;
        out_value->payload.as.index = current.payload.as.index - 1;
        return BS_OK;
    }
    *out_value = current;
    return BS_OK;
}

static const bs_nature_vtable minted_nonfinite_vtable = {
    .equal = minted_nonfinite_equal,
    .compare = minted_nonfinite_compare,
    .hash = minted_hash,
    .value_valid = minted_nonfinite_value_valid,
    .iter_step = minted_nonfinite_iter_step,
    .iter_state_destroy = NULL,
    .step = minted_nonfinite_step,
    .interval_cardinality = minted_interval_cardinality
};

/* ========================================================================= */
/* Built-in Integer Nature Implementation (Powered by bs_num)                */
/* ========================================================================= */

static void bignum_anchor_destroy(void *data)
{
    bs_int *val = (bs_int *)data;
    bs_int_release(*val);
}

static bs_status universe_anchor_bignum(bs_universe *universe, bs_int val)
{
    if (!bs_int_is_bignum(val)) return BS_OK;
    const bs_payload_object *obj = NULL;
    void *data = NULL;
    bs_status s = bs_internal_payload_object_create(
        universe, sizeof(bs_int), bignum_anchor_destroy, &obj, &data
    );
    if (s != BS_OK) return s;
    *(bs_int *)data = bs_int_retain(val);
    return BS_OK;
}

static bs_status integer_make_value(
    const bs_universe *universe,
    bs_int val,
    bs_value_impl *out_value
)
{
    if (universe == NULL || out_value == NULL || bs_int_is_null(val)) {
        return BS_INVALID_ARGUMENT;
    }
    if (bs_int_is_bignum(val)) {
        bs_status s = universe_anchor_bignum((bs_universe *)universe, val);
        if (s != BS_OK) return s;
    }
    out_value->nature = universe->integer_nature;
    out_value->payload.kind = BS_PAYLOAD_INTEGER;
    out_value->payload.as.integer = val;
    return BS_OK;
}

static bool integer_value_valid(bs_value_impl value, const bs_universe *universe)
{
    return value.nature == universe->integer_nature &&
           value.payload.kind == BS_PAYLOAD_INTEGER &&
           !bs_int_is_null(value.payload.as.integer);
}

static bool integer_equal(bs_value_impl left, bs_value_impl right)
{
    if (left.payload.kind != BS_PAYLOAD_INTEGER ||
        right.payload.kind != BS_PAYLOAD_INTEGER) {
        return false;
    }
    return bs_int_eq(left.payload.as.integer, right.payload.as.integer);
}

static bs_status integer_compare(bs_value_impl left, bs_value_impl right, int *out_comparison)
{
    if (left.payload.kind != BS_PAYLOAD_INTEGER ||
        right.payload.kind != BS_PAYLOAD_INTEGER) {
        return BS_SETS_TYPE_ERROR;
    }
    *out_comparison = bs_int_cmp(left.payload.as.integer, right.payload.as.integer);
    return BS_OK;
}

typedef struct integer_iteration_state {
    bs_int current_magnitude;
    bool emit_negative;
} integer_iteration_state;

static bs_status integer_iter_step(
    const bs_nature *nature,
    bs_nature_iter_state *state,
    bs_value_impl *out_value
)
{
    integer_iteration_state *cursor = (integer_iteration_state *)state->context;
    if (state->ordinal == 0) {
        cursor = (integer_iteration_state *)bs_calloc(1, sizeof(*cursor));
        if (cursor == NULL) return BS_OUT_OF_MEMORY;
        cursor->current_magnitude = BS_INT_ONE;
        cursor->emit_negative = false;
        state->context = cursor;
        out_value->nature = nature;
        out_value->payload.kind = BS_PAYLOAD_INTEGER;
        out_value->payload.as.integer = BS_INT_ZERO;
        state->ordinal = 1;
        return BS_OK;
    }
    if (cursor == NULL) return BS_INVALID_ARGUMENT;

    bs_int val_to_emit;
    if (cursor->emit_negative) {
        val_to_emit = bs_int_neg(cursor->current_magnitude);
        cursor->emit_negative = false;
        bs_int next_mag = bs_int_add(cursor->current_magnitude, BS_INT_ONE);
        bs_int_release(cursor->current_magnitude);
        cursor->current_magnitude = next_mag;
    } else {
        val_to_emit = bs_int_retain(cursor->current_magnitude);
        cursor->emit_negative = true;
    }

    bs_status status = integer_make_value(nature->owner, val_to_emit, out_value);
    bs_int_release(val_to_emit);
    return status;
}

static void integer_iter_state_destroy(bs_nature_iter_state *state)
{
    integer_iteration_state *cursor = (integer_iteration_state *)state->context;
    if (cursor != NULL) {
        bs_int_release(cursor->current_magnitude);
        bs_free(cursor);
        state->context = NULL;
    }
}

static bs_status integer_step(
    const bs_nature *nature,
    bs_value_impl current,
    int direction,
    bs_value_impl *out_value
)
{
    if (out_value == NULL) return BS_INVALID_ARGUMENT;
    if (current.payload.kind != BS_PAYLOAD_INTEGER) return BS_SETS_TYPE_ERROR;
    if (direction == 0) {
        *out_value = current;
        return BS_OK;
    }
    bs_int cur = current.payload.as.integer;
    bs_int next = direction > 0 ? bs_int_add(cur, BS_INT_ONE) : bs_int_sub(cur, BS_INT_ONE);
    if (bs_int_is_null(next)) return BS_OUT_OF_MEMORY;

    bs_status status = integer_make_value(nature->owner, next, out_value);
    bs_int_release(next);
    return status;
}

static bs_status integer_interval_cardinality(
    const bs_nature *nature,
    bs_value_impl lower,
    bool lower_inclusive,
    bs_value_impl upper,
    bool upper_inclusive,
    bs_int *out_cardinality
)
{
    bs_value_impl low = lower, high = upper;
    bs_status status;
    if (!lower_inclusive) {
        status = integer_step(nature, lower, 1, &low);
        if (status == BS_SETS_EXHAUSTED) { *out_cardinality = BS_INT_ZERO; return BS_OK; }
        if (status != BS_OK) return status;
    }
    if (!upper_inclusive) {
        status = integer_step(nature, upper, -1, &high);
        if (status == BS_SETS_EXHAUSTED) { *out_cardinality = BS_INT_ZERO; return BS_OK; }
        if (status != BS_OK) return status;
    }
    int cmp = 0;
    status = integer_compare(low, high, &cmp);
    if (status != BS_OK) return status;
    if (cmp > 0) { *out_cardinality = BS_INT_ZERO; return BS_OK; }

    bs_int a = low.payload.as.integer;
    bs_int b = high.payload.as.integer;
    bs_int diff = bs_int_sub(b, a);
    if (bs_int_is_null(diff)) return BS_OUT_OF_MEMORY;
    bs_int count = bs_int_add(diff, BS_INT_ONE);
    bs_int_release(diff);
    if (bs_int_is_null(count)) return BS_OUT_OF_MEMORY;

    *out_cardinality = count;
    return BS_OK;
}

static uint64_t integer_hash(bs_value_impl value)
{
    uint64_t h = (uintptr_t)value.nature;
    h ^= bs_int_hash(value.payload.as.integer);
    return bs_splitmix64(h);
}

static const bs_nature_vtable integer_vtable = {
    .equal = integer_equal,
    .compare = integer_compare,
    .hash = integer_hash,
    .value_valid = integer_value_valid,
    .iter_step = integer_iter_step,
    .iter_state_destroy = integer_iter_state_destroy,
    .step = integer_step,
    .interval_cardinality = integer_interval_cardinality
};

/* ========================================================================= */
/* Universe Registry & Built-in Descriptors                                  */
/* ========================================================================= */

typedef struct builtin_nature_descriptor {
    const bs_nature_vtable *vtable;
    bs_payload_kind payload_kind;
    bool finite;
    bool discrete;
    uint64_t cardinality;
    bool is_nature_of_natures;
    bool is_integer;
} builtin_nature_descriptor;

static const builtin_nature_descriptor builtin_natures[] = {
    {
        .vtable = &nature_of_natures_vtable,
        .payload_kind = BS_PAYLOAD_NATURE,
        .finite = false,
        .discrete = false,
        .cardinality = 0,
        .is_nature_of_natures = true,
        .is_integer = false
    },
    {
        .vtable = &integer_vtable,
        .payload_kind = BS_PAYLOAD_INTEGER,
        .finite = false,
        .discrete = true,
        .cardinality = 0,
        .is_nature_of_natures = false,
        .is_integer = true
    }
};

static bs_nature *allocate_nature(
    bs_universe *universe,
    const bs_nature_vtable *vtable,
    bs_payload_kind payload_kind,
    bool finite,
    bool discrete,
    uint64_t cardinality
)
{
    bs_nature *nature = (bs_nature *)bs_calloc(1, sizeof(*nature));
    if (nature == NULL) {
        return NULL;
    }
    nature->owner = universe;
    nature->vtable = vtable;
    nature->payload_kind = payload_kind;
    nature->finite = finite;
    nature->discrete = discrete;
    nature->cardinality = cardinality;
    nature->enumerable = true;
    bs_atomic_init(&nature->next_payload, 0);

    bs_mutex_lock(&universe->natures_mutex);
    nature->next = universe->natures;
    universe->natures = nature;
    bs_mutex_unlock(&universe->natures_mutex);

    return nature;
}

bool bs_internal_universe_owns_nature(
    const bs_universe *universe,
    const bs_nature *nature
)
{
    return owns_nature(universe, nature);
}

bool bs_internal_value_belongs_to_impl(
    bs_value_impl value,
    const bs_universe *universe
)
{
    if (universe == NULL || value.nature == NULL || !owns_nature(universe, value.nature)) {
        return false;
    }
    if (value.nature->vtable != NULL && value.nature->vtable->value_valid != NULL) {
        return value.nature->vtable->value_valid(value, universe);
    }
    return false;
}

const bs_nature_vtable *bs_internal_nature_vtable(const bs_nature *nature)
{
    return nature == NULL ? NULL : nature->vtable;
}

bs_status bs_nature_get_minted_value_impl(
    const bs_nature *nature,
    uint64_t index,
    bs_value_impl *out_value
)
{
    if (nature == NULL || out_value == NULL || nature->owner == NULL ||
        !owns_nature(nature->owner, nature)) {
        return BS_INVALID_ARGUMENT;
    }
    if (!nature->finite || nature->payload_kind != BS_PAYLOAD_MINTED_INDEX) {
        return BS_SETS_TYPE_ERROR;
    }
    if (index >= nature->cardinality) {
        return BS_SETS_OUT_OF_BOUNDS;
    }
    out_value->nature = nature;
    out_value->payload.kind = BS_PAYLOAD_MINTED_INDEX;
    out_value->payload.as.index = index;
    return BS_OK;
}

bs_status bs_nature_mint_value_impl(
    bs_universe *universe,
    bs_nature *nature,
    bs_value_impl *out_value
)
{
    if (universe == NULL || nature == NULL || out_value == NULL ||
        !owns_nature(universe, nature)) {
        return BS_INVALID_ARGUMENT;
    }
    if (nature->payload_kind != BS_PAYLOAD_MINTED_INDEX || nature->finite) {
        return BS_SETS_TYPE_ERROR;
    }
    uint64_t current = bs_atomic_load_relaxed(&nature->next_payload);
    do {
        if (current == UINT64_MAX) {
            return BS_SETS_EXHAUSTED;
        }
    } while (!bs_atomic_compare_exchange_weak_u64_relaxed(
        &nature->next_payload, &current, current + 1));

    out_value->nature = nature;
    out_value->payload.kind = BS_PAYLOAD_MINTED_INDEX;
    out_value->payload.as.index = current;
    return BS_OK;
}

bs_status bs_internal_nature_iter_step_impl(
    const bs_nature *nature,
    bs_nature_iter_state *state,
    bs_value_impl *out_value
)
{
    if (nature == NULL || state == NULL || out_value == NULL || nature->vtable == NULL ||
        nature->vtable->iter_step == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    return nature->vtable->iter_step(nature, state, out_value);
}

void bs_internal_nature_iter_state_destroy(
    const bs_nature *nature,
    bs_nature_iter_state *state
)
{
    if (nature != NULL && state != NULL && nature->vtable != NULL &&
        nature->vtable->iter_state_destroy != NULL) {
        nature->vtable->iter_state_destroy(state);
    }
}

bs_status bs_internal_nature_step_impl(
    const bs_nature *nature,
    bs_value_impl current,
    int direction,
    bs_value_impl *out_value
)
{
    if (nature == NULL || out_value == NULL || nature->vtable == NULL || nature->vtable->step == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    return nature->vtable->step(nature, current, direction, out_value);
}

bs_status bs_internal_nature_interval_cardinality_impl(
    const bs_nature *nature,
    bs_value_impl lower,
    bool lower_inclusive,
    bs_value_impl upper,
    bool upper_inclusive,
    bs_int *out_cardinality
)
{
    if (nature == NULL || nature->vtable == NULL || nature->vtable->interval_cardinality == NULL || out_cardinality == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    return nature->vtable->interval_cardinality(nature, lower, lower_inclusive, upper, upper_inclusive, out_cardinality);
}

/* ========================================================================= */
/* Universe Management                                                       */
/* ========================================================================= */

bs_status bs_universe_create(bs_universe **out_universe)
{
    if (out_universe == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_universe *universe = (bs_universe *)bs_calloc(1, sizeof(*universe));
    if (universe == NULL) {
        return BS_OUT_OF_MEMORY;
    }

    bs_mutex_init(&universe->natures_mutex);
    bs_atomic_init(&universe->payload_objects, NULL);
#ifndef NDEBUG
    bs_atomic_init(&universe->active_stores, 0);
#endif

    for (size_t i = 0; i < sizeof(builtin_natures) / sizeof(builtin_natures[0]); ++i) {
        const builtin_nature_descriptor *descriptor = &builtin_natures[i];
        bs_nature *nature = allocate_nature(
            universe,
            descriptor->vtable,
            descriptor->payload_kind,
            descriptor->finite,
            descriptor->discrete,
            descriptor->cardinality
        );
        if (nature == NULL) {
            bs_universe_destroy(universe);
            return BS_OUT_OF_MEMORY;
        }
        if (descriptor->is_nature_of_natures) {
            universe->nature_of_natures = nature;
        }
        if (descriptor->is_integer) {
            universe->integer_nature = nature;
        }
    }

    *out_universe = universe;
    return BS_OK;
}

void bs_universe_destroy(bs_universe *universe)
{
    if (universe == NULL) {
        return;
    }
#ifndef NDEBUG
    assert(bs_atomic_load_relaxed(&universe->active_stores) == 0 &&
           "Cannot destroy universe: active set stores referencing it must be destroyed first");
#endif
    bs_nature *nature = universe->natures;
    while (nature != NULL) {
        bs_nature *next = nature->next;
        bs_free(nature);
        nature = next;
    }
    bs_payload_object *object = (bs_payload_object *)bs_atomic_load_acquire(&universe->payload_objects);
    while (object != NULL) {
        bs_payload_object *next = object->next;
        if (object->destroy != NULL) {
            object->destroy(object->data);
        }
        bs_free(object->data);
        bs_free(object);
        object = next;
    }
    bs_mutex_destroy(&universe->natures_mutex);
    bs_free(universe);
}

#ifndef NDEBUG
void bs_internal_universe_register_store(const bs_universe *universe)
{
    if (universe != NULL) {
        bs_atomic_fetch_add_size_relaxed(&((bs_universe *)universe)->active_stores, 1);
    }
}

void bs_internal_universe_unregister_store(const bs_universe *universe)
{
    if (universe != NULL) {
        size_t prev = bs_atomic_fetch_sub_size_relaxed(&((bs_universe *)universe)->active_stores, 1);
        assert(prev > 0);
    }
}
#endif

const bs_nature *bs_universe_nature_of_natures(const bs_universe *universe)
{
    return universe == NULL ? NULL : universe->nature_of_natures;
}

bs_status bs_nature_as_value_impl(const bs_nature *nature, bs_value_impl *out_value)
{
    if (nature == NULL || out_value == NULL || nature->owner == NULL ||
        !owns_nature(nature->owner, nature)) {
        return BS_INVALID_ARGUMENT;
    }
    out_value->nature = nature->owner->nature_of_natures;
    out_value->payload.kind = BS_PAYLOAD_NATURE;
    out_value->payload.as.nature = nature;
    return BS_OK;
}

bs_status bs_value_impl_as_nature(bs_value_impl value, const bs_nature **out_nature)
{
    if (out_nature == NULL || value.nature == NULL || value.nature->owner == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    if (value.payload.kind != BS_PAYLOAD_NATURE ||
        value.nature != value.nature->owner->nature_of_natures) {
        return BS_SETS_TYPE_ERROR;
    }
    if (!bs_internal_value_belongs_to_impl(value, value.nature->owner) ||
        !owns_nature(value.nature->owner, value.payload.as.nature)) {
        return BS_INVALID_ARGUMENT;
    }
    *out_nature = value.payload.as.nature;
    return BS_OK;
}

bs_status bs_value_impl_as_minted_index(bs_value_impl value, uint64_t *out_index)
{
    if (out_index == NULL || value.nature == NULL || value.nature->owner == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    if (value.payload.kind != BS_PAYLOAD_MINTED_INDEX ||
        value.nature->payload_kind != BS_PAYLOAD_MINTED_INDEX) {
        return BS_SETS_TYPE_ERROR;
    }
    if (!bs_internal_value_belongs_to_impl(value, value.nature->owner)) {
        return BS_INVALID_ARGUMENT;
    }
    *out_index = value.payload.as.index;
    return BS_OK;
}

bs_status bs_value_impl_as_integer(bs_value_impl value, int64_t *out_integer)
{
    if (out_integer == NULL || value.nature == NULL || value.nature->owner == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    if (value.nature != value.nature->owner->integer_nature || value.payload.kind != BS_PAYLOAD_INTEGER) {
        return BS_SETS_TYPE_ERROR;
    }
    if (!bs_internal_value_belongs_to_impl(value, value.nature->owner)) {
        return BS_INVALID_ARGUMENT;
    }
    if (!bs_int_to_i64(value.payload.as.integer, out_integer)) {
        return BS_NUM_OVERFLOW;
    }
    return BS_OK;
}

bs_status bs_value_impl_from_integer(
    const bs_universe *universe,
    int64_t integer,
    bs_value_impl *out_value
)
{
    if (universe == NULL || out_value == NULL || universe->integer_nature == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_int val = bs_int_from_i64(integer);
    bs_status status = integer_make_value(universe, val, out_value);
    bs_int_release(val);
    return status;
}

bs_status bs_value_impl_from_decimal(
    const bs_universe *universe,
    const char *decimal,
    bs_value_impl *out_value
)
{
    if (universe == NULL || decimal == NULL || out_value == NULL || universe->integer_nature == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    const char *cursor = decimal;
    if (*cursor == '+' || *cursor == '-') {
        cursor++;
    }
    if (*cursor == '\0') return BS_NUM_PARSE_ERROR;
    for (const char *p = cursor; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') return BS_NUM_PARSE_ERROR;
    }
    bs_int val;
    if (!bs_int_from_str(decimal, 10, &val)) {
        return BS_NUM_PARSE_ERROR;
    }
    bs_status status = integer_make_value(universe, val, out_value);
    bs_int_release(val);
    return status;
}

bs_status bs_value_impl_format_integer(
    bs_value_impl value,
    char *buffer,
    size_t capacity,
    size_t *out_required_capacity
)
{
    if (out_required_capacity == NULL || (buffer == NULL && capacity != 0) ||
        value.nature == NULL || value.nature->owner == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    if (value.nature != value.nature->owner->integer_nature || value.payload.kind != BS_PAYLOAD_INTEGER) {
        return BS_SETS_TYPE_ERROR;
    }
    if (!bs_internal_value_belongs_to_impl(value, value.nature->owner)) {
        return BS_INVALID_ARGUMENT;
    }
    char *str = bs_int_to_str(value.payload.as.integer, 10);
    if (str == NULL) {
        return BS_OUT_OF_MEMORY;
    }
    size_t len = strlen(str) + 1;
    *out_required_capacity = len;
    if (buffer == NULL && capacity == 0) {
        bs_free(str);
        return BS_OK;
    }
    if (capacity < len) {
        bs_free(str);
        return BS_SETS_OUT_OF_BOUNDS;
    }
    memcpy(buffer, str, len);
    bs_free(str);
    return BS_OK;
}

bs_status bs_nature_mint_finite(
    bs_universe *universe,
    uint64_t cardinality,
    const bs_nature **out_nature
)
{
    if (universe == NULL || out_nature == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_nature *nature = allocate_nature(
        universe,
        &minted_finite_vtable,
        BS_PAYLOAD_MINTED_INDEX,
        true,
        true,
        cardinality
    );
    if (nature == NULL) {
        return BS_OUT_OF_MEMORY;
    }
    *out_nature = nature;
    return BS_OK;
}

bs_status bs_nature_mint_nonfinite(
    bs_universe *universe,
    bs_nature **out_nature
)
{
    if (universe == NULL || out_nature == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_nature *nature = allocate_nature(
        universe,
        &minted_nonfinite_vtable,
        BS_PAYLOAD_MINTED_INDEX,
        false,
        true,
        0
    );
    if (nature == NULL) {
        return BS_OUT_OF_MEMORY;
    }
    *out_nature = nature;
    return BS_OK;
}

bs_status bs_nature_cardinality(
    const bs_nature *nature,
    bs_int *out_cardinality
)
{
    if (nature == NULL || out_cardinality == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    if (!nature->finite) {
        return BS_SETS_TYPE_ERROR;
    }
    bs_int card = bs_int_from_u64(nature->cardinality);
    if (bs_int_is_null(card)) {
        return BS_OUT_OF_MEMORY;
    }
    *out_cardinality = card;
    return BS_OK;
}

bs_status bs_nature_is_finite(
    const bs_nature *nature,
    bool *out_is_finite
)
{
    if (nature == NULL || out_is_finite == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    *out_is_finite = nature->finite;
    return BS_OK;
}

bs_status bs_nature_is_enumerable(
    const bs_nature *nature,
    bool *out_is_enumerable
)
{
    if (nature == NULL || out_is_enumerable == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    *out_is_enumerable = nature->enumerable;
    return BS_OK;
}

bs_status bs_nature_is_discrete(
    const bs_nature *nature,
    bool *out_is_discrete
)
{
    if (nature == NULL || out_is_discrete == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    *out_is_discrete = nature->discrete;
    return BS_OK;
}

const bs_universe *bs_nature_universe(const bs_nature *nature)
{
    return nature == NULL ? NULL : nature->owner;
}

const bs_nature *bs_universe_integer_nature(const bs_universe *universe)
{
    return universe == NULL ? NULL : universe->integer_nature;
}

const bs_nature *bs_value_impl_nature(bs_value_impl value)
{
    return value.nature;
}

bool bs_value_impl_equal(bs_value_impl left, bs_value_impl right)
{
    if (left.nature == NULL || right.nature == NULL) {
        return false;
    }
    if (left.nature != right.nature) {
        return false;
    }
    if (left.nature->owner == NULL ||
        !bs_internal_value_belongs_to_impl(left, left.nature->owner) ||
        !bs_internal_value_belongs_to_impl(right, right.nature->owner)) {
        return false;
    }
    if (left.nature->vtable != NULL && left.nature->vtable->equal != NULL) {
        return left.nature->vtable->equal(left, right);
    }
    return false;
}

bs_status bs_value_impl_compare(
    bs_value_impl left,
    bs_value_impl right,
    int *out_comparison
)
{
    if (out_comparison == NULL || left.nature == NULL || right.nature == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    if (left.nature != right.nature) {
        return BS_SETS_TYPE_ERROR;
    }
    if (left.nature->owner == NULL ||
        !bs_internal_value_belongs_to_impl(left, left.nature->owner) ||
        !bs_internal_value_belongs_to_impl(right, right.nature->owner)) {
        return BS_SETS_TYPE_ERROR;
    }
    if (left.nature->vtable != NULL && left.nature->vtable->compare != NULL) {
        return left.nature->vtable->compare(left, right, out_comparison);
    }
    return BS_INVALID_ARGUMENT;
}

uint64_t bs_value_impl_hash(bs_value_impl value)
{
    if (value.nature == NULL || value.nature->vtable == NULL || value.nature->vtable->hash == NULL) {
        return 0;
    }
    return value.nature->vtable->hash(value);
}

/* ========================================================================= */
/* Public API Export Wrappers                                                */
/* ========================================================================= */

bs_status bs_nature_as_value(const bs_nature *nature, bs_value *out_value)
{
    if (out_value == NULL) return BS_INVALID_ARGUMENT;
    bs_value_impl value;
    bs_status status = bs_nature_as_value_impl(nature, &value);
    if (status == BS_OK) bs_value_pack(value, out_value);
    return status;
}

bs_status bs_value_as_nature(bs_value value, const bs_nature **out_nature)
{
    return bs_value_impl_as_nature(bs_value_unpack(value), out_nature);
}

bs_status bs_value_as_minted_index(bs_value value, uint64_t *out_index)
{
    return bs_value_impl_as_minted_index(bs_value_unpack(value), out_index);
}

bs_status bs_value_as_integer(bs_value value, int64_t *out_integer)
{
    return bs_value_impl_as_integer(bs_value_unpack(value), out_integer);
}

bs_status bs_value_from_integer(const bs_universe *universe, int64_t integer, bs_value *out_value)
{
    if (out_value == NULL) return BS_INVALID_ARGUMENT;
    bs_value_impl value;
    bs_status status = bs_value_impl_from_integer(universe, integer, &value);
    if (status == BS_OK) bs_value_pack(value, out_value);
    return status;
}

bs_status bs_value_from_decimal(const bs_universe *universe, const char *decimal, bs_value *out_value)
{
    if (out_value == NULL) return BS_INVALID_ARGUMENT;
    bs_value_impl value;
    bs_status status = bs_value_impl_from_decimal(universe, decimal, &value);
    if (status == BS_OK) bs_value_pack(value, out_value);
    return status;
}

bs_status bs_value_format_integer(bs_value value, char *buffer, size_t capacity, size_t *out_required_capacity)
{
    return bs_value_impl_format_integer(bs_value_unpack(value), buffer, capacity, out_required_capacity);
}

bs_status bs_value_from_bs_int(const bs_universe *universe, bs_int integer, bs_value *out_value)
{
    if (universe == NULL || out_value == NULL || bs_int_is_null(integer) || universe->integer_nature == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_value_impl value;
    bs_status status = integer_make_value(universe, integer, &value);
    if (status == BS_OK) bs_value_pack(value, out_value);
    return status;
}

bs_status bs_value_as_bs_int(bs_value value, bs_int *out_integer)
{
    if (out_integer == NULL) return BS_INVALID_ARGUMENT;
    bs_value_impl impl = bs_value_unpack(value);
    if (impl.nature == NULL || impl.nature->owner == NULL) return BS_INVALID_ARGUMENT;
    if (impl.nature != impl.nature->owner->integer_nature || impl.payload.kind != BS_PAYLOAD_INTEGER) {
        return BS_SETS_TYPE_ERROR;
    }
    if (!bs_internal_value_belongs_to_impl(impl, impl.nature->owner)) {
        return BS_INVALID_ARGUMENT;
    }
    *out_integer = bs_int_retain(impl.payload.as.integer);
    return BS_OK;
}

bs_status bs_nature_get_minted_value(const bs_nature *nature, uint64_t index, bs_value *out_value)
{
    if (out_value == NULL) return BS_INVALID_ARGUMENT;
    bs_value_impl value;
    bs_status status = bs_nature_get_minted_value_impl(nature, index, &value);
    if (status == BS_OK) bs_value_pack(value, out_value);
    return status;
}

bs_status bs_nature_mint_value(bs_universe *universe, bs_nature *nature, bs_value *out_value)
{
    if (out_value == NULL) return BS_INVALID_ARGUMENT;
    bs_value_impl value;
    bs_status status = bs_nature_mint_value_impl(universe, nature, &value);
    if (status == BS_OK) bs_value_pack(value, out_value);
    return status;
}

const bs_nature *bs_value_nature(bs_value value)
{
    return bs_value_impl_nature(bs_value_unpack(value));
}

bool bs_value_equal(bs_value left, bs_value right)
{
    return bs_value_impl_equal(bs_value_unpack(left), bs_value_unpack(right));
}

bs_status bs_value_compare(bs_value left, bs_value right, int *out_comparison)
{
    return bs_value_impl_compare(bs_value_unpack(left), bs_value_unpack(right), out_comparison);
}

uint64_t bs_value_hash(bs_value value)
{
    return bs_value_impl_hash(bs_value_unpack(value));
}

bool bs_internal_value_belongs_to(bs_value value, const bs_universe *universe)
{
    return bs_internal_value_belongs_to_impl(bs_value_unpack(value), universe);
}

bs_status bs_internal_nature_iter_step(const bs_nature *nature, bs_nature_iter_state *state, bs_value *out_value)
{
    if (out_value == NULL) return BS_INVALID_ARGUMENT;
    bs_value_impl value;
    bs_status status = bs_internal_nature_iter_step_impl(nature, state, &value);
    if (status == BS_OK) bs_value_pack(value, out_value);
    return status;
}

bs_status bs_internal_nature_step(const bs_nature *nature, bs_value current, int direction, bs_value *out_value)
{
    if (out_value == NULL) return BS_INVALID_ARGUMENT;
    bs_value_impl value;
    bs_status status = bs_internal_nature_step_impl(nature, bs_value_unpack(current), direction, &value);
    if (status == BS_OK) bs_value_pack(value, out_value);
    return status;
}

bs_status bs_internal_nature_interval_cardinality(const bs_nature *nature, bs_value lower, bool lower_inclusive, bs_value upper, bool upper_inclusive, bs_int *out_cardinality)
{
    return bs_internal_nature_interval_cardinality_impl(nature, bs_value_unpack(lower), lower_inclusive, bs_value_unpack(upper), upper_inclusive, out_cardinality);
}
