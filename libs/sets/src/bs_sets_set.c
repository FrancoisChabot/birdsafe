#include "bs_sets_internal.h"
#include "bs_core/bs_alloc.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef enum set_kind {
    SET_ENUMERATED,
    SET_INTERVAL,
    SET_UNION,
    SET_INTERSECTION,
    SET_DIFFERENCE,
    SET_COMPLEMENT,
    SET_PREDICATE,
    SET_NATURE
} set_kind;

struct bs_set {
    struct bs_set *next;
    const bs_set_store *store;
    set_kind kind;
    union {
        struct {
            bs_value *values;
            size_t count;
        } enumerated;
        struct {
            bool has_lower;
            bs_value lower;
            bool lower_inclusive;
            bool has_upper;
            bs_value upper;
            bool upper_inclusive;
            const bs_nature *nature;
        } interval;
        struct {
            const bs_set *left;
            const bs_set *right;
        } binary;
        const bs_set *inner;
        struct {
            const bs_set *bound;
            bs_predicate_fn predicate;
            void *context;
            bs_context_destroy_fn context_destroy;
        } predicate;
        const bs_nature *nature;
    } as;
};

struct bs_set_store {
    const bs_universe *universe;
    bs_set *sets;
};

static bool valid_truth(bs_truth truth)
{
    return truth == BS_FALSE || truth == BS_TRUE || truth == BS_UNDECIDED;
}

static bool owns_set(const bs_set_store *store, const bs_set *set)
{
    if (store == NULL || set == NULL) {
        return false;
    }
    return set->store == store;
}

static bs_status allocate_set(
    bs_set_store *store,
    set_kind kind,
    bs_set **out_set
)
{
    bs_set *set = bs_calloc(1, sizeof(*set));
    if (set == NULL) {
        return BS_OUT_OF_MEMORY;
    }
    set->store = store;
    set->kind = kind;
    set->next = store->sets;
    store->sets = set;
    *out_set = set;
    return BS_OK;
}

bs_status bs_set_store_create(
    const bs_universe *universe,
    bs_set_store **out_store
)
{
    if (universe == NULL || out_store == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_set_store *store = bs_calloc(1, sizeof(*store));
    if (store == NULL) {
        return BS_OUT_OF_MEMORY;
    }
    store->universe = universe;
    bs_internal_universe_register_store(universe);
    *out_store = store;
    return BS_OK;
}

void bs_set_store_destroy(bs_set_store *store)
{
    if (store == NULL) {
        return;
    }
    bs_internal_universe_unregister_store(store->universe);
    bs_set *set = store->sets;
    while (set != NULL) {
        bs_set *next = set->next;
        if (set->kind == SET_ENUMERATED) {
            bs_free(set->as.enumerated.values);
        } else if (set->kind == SET_PREDICATE) {
            if (set->as.predicate.context_destroy != NULL) {
                set->as.predicate.context_destroy(set->as.predicate.context);
            }
        }
        bs_free(set);
        set = next;
    }
    bs_free(store);
}

const bs_universe *bs_set_store_universe(const bs_set_store *store)
{
    return store == NULL ? NULL : store->universe;
}

const bs_universe *bs_set_universe(const bs_set *set)
{
    return (set == NULL || set->store == NULL) ? NULL : set->store->universe;
}

bs_status bs_set_enumerated(
    bs_set_store *store,
    const bs_value *values,
    size_t count,
    const bs_set **out_set
)
{
    if (store == NULL || out_set == NULL || (count != 0 && values == NULL)) {
        return BS_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!bs_internal_value_belongs_to(values[i], store->universe)) {
            return BS_INVALID_ARGUMENT;
        }
    }

    bs_value *copy = NULL;
    if (count != 0) {
        if (count > SIZE_MAX / sizeof(*copy)) {
            return BS_OUT_OF_MEMORY;
        }
        copy = bs_alloc(count * sizeof(*copy));
        if (copy == NULL) {
            return BS_OUT_OF_MEMORY;
        }
        memcpy(copy, values, count * sizeof(*copy));
    }

    bs_set *set;
    bs_status status = allocate_set(store, SET_ENUMERATED, &set);
    if (status != BS_OK) {
        bs_free(copy);
        return status;
    }
    set->as.enumerated.values = copy;
    set->as.enumerated.count = count;
    *out_set = set;
    return BS_OK;
}

bs_status bs_set_nature(
    bs_set_store *store,
    const bs_nature *nature,
    const bs_set **out_set
)
{
    if (store == NULL || nature == NULL || out_set == NULL ||
        !bs_internal_universe_owns_nature(store->universe, nature)) {
        return BS_INVALID_ARGUMENT;
    }
    bs_set *set;
    bs_status status = allocate_set(store, SET_NATURE, &set);
    if (status != BS_OK) {
        return status;
    }
    set->as.nature = nature;
    *out_set = set;
    return BS_OK;
}

bs_status bs_set_interval(
    bs_set_store *store,
    const bs_value *lower,
    bool lower_inclusive,
    const bs_value *upper,
    bool upper_inclusive,
    const bs_set **out_set
)
{
    if (store == NULL || out_set == NULL || (lower == NULL && upper == NULL)) {
        return BS_INVALID_ARGUMENT;
    }
    const bs_nature *nature = NULL;
    if (lower != NULL) {
        if (!bs_internal_value_belongs_to(*lower, store->universe)) {
            return BS_INVALID_ARGUMENT;
        }
        nature = bs_value_nature(*lower);
    }
    if (upper != NULL) {
        if (!bs_internal_value_belongs_to(*upper, store->universe)) {
            return BS_INVALID_ARGUMENT;
        }
        if (nature != NULL && bs_value_nature(*upper) != nature) {
            return BS_SETS_TYPE_ERROR;
        }
        nature = bs_value_nature(*upper);
    }
    if (lower != NULL && upper != NULL) {
        int cmp = 0;
        bs_status s = bs_value_compare(*lower, *upper, &cmp);
        if (s != BS_OK) {
            return s;
        }
        if (cmp > 0) {
            return BS_INVALID_ARGUMENT;
        }
    }

    bs_set *set = NULL;
    bs_status status = allocate_set(store, SET_INTERVAL, &set);
    if (status != BS_OK) {
        return status;
    }
    set->as.interval.nature = nature;
    if (lower != NULL) {
        set->as.interval.has_lower = true;
        set->as.interval.lower = *lower;
        set->as.interval.lower_inclusive = lower_inclusive;
    }
    if (upper != NULL) {
        set->as.interval.has_upper = true;
        set->as.interval.upper = *upper;
        set->as.interval.upper_inclusive = upper_inclusive;
    }
    *out_set = set;
    return BS_OK;
}

static bs_status make_binary_set(
    bs_set_store *store,
    const bs_set *left,
    const bs_set *right,
    set_kind kind,
    const bs_set **out_set
)
{
    if (store == NULL || out_set == NULL ||
        !owns_set(store, left) || !owns_set(store, right)) {
        return BS_INVALID_ARGUMENT;
    }
    bs_set *set;
    bs_status status = allocate_set(store, kind, &set);
    if (status != BS_OK) {
        return status;
    }
    set->as.binary.left = left;
    set->as.binary.right = right;
    *out_set = set;
    return BS_OK;
}

bs_status bs_set_union(
    bs_set_store *store,
    const bs_set *left,
    const bs_set *right,
    const bs_set **out_set
)
{
    return make_binary_set(store, left, right, SET_UNION, out_set);
}

bs_status bs_set_intersection(
    bs_set_store *store,
    const bs_set *left,
    const bs_set *right,
    const bs_set **out_set
)
{
    return make_binary_set(store, left, right, SET_INTERSECTION, out_set);
}

bs_status bs_set_difference(
    bs_set_store *store,
    const bs_set *left,
    const bs_set *right,
    const bs_set **out_set
)
{
    return make_binary_set(store, left, right, SET_DIFFERENCE, out_set);
}

bs_status bs_set_complement(
    bs_set_store *store,
    const bs_set *inner,
    const bs_set **out_set
)
{
    if (store == NULL || out_set == NULL || !owns_set(store, inner)) {
        return BS_INVALID_ARGUMENT;
    }
    bs_set *set;
    bs_status status = allocate_set(store, SET_COMPLEMENT, &set);
    if (status != BS_OK) {
        return status;
    }
    set->as.inner = inner;
    *out_set = set;
    return BS_OK;
}

bs_status bs_set_predicate(
    bs_set_store *store,
    const bs_set *bound,
    bs_predicate_fn predicate,
    void *context,
    bs_context_destroy_fn context_destroy,
    const bs_set **out_set
)
{
    if (store == NULL || predicate == NULL || out_set == NULL ||
        (bound != NULL && !owns_set(store, bound))) {
        return BS_INVALID_ARGUMENT;
    }
    bs_set *set;
    bs_status status = allocate_set(store, SET_PREDICATE, &set);
    if (status != BS_OK) {
        return status;
    }
    set->as.predicate.bound = bound;
    set->as.predicate.predicate = predicate;
    set->as.predicate.context = context;
    set->as.predicate.context_destroy = context_destroy;
    *out_set = set;
    return BS_OK;
}

static bs_status evaluate(
    const bs_set *set,
    bs_value candidate,
    bs_truth *out_result
)
{
    bs_truth left;
    bs_truth right;
    bs_status status;

    switch (set->kind) {
    case SET_ENUMERATED:
        for (size_t i = 0; i < set->as.enumerated.count; ++i) {
            if (bs_value_equal(set->as.enumerated.values[i], candidate)) {
                *out_result = BS_TRUE;
                return BS_OK;
            }
        }
        *out_result = BS_FALSE;
        return BS_OK;
    case SET_NATURE:
        *out_result = (bs_value_nature(candidate) == set->as.nature) ? BS_TRUE : BS_FALSE;
        return BS_OK;
    case SET_INTERVAL: {
        if (bs_value_nature(candidate) != set->as.interval.nature) {
            *out_result = BS_FALSE;
            return BS_OK;
        }
        if (set->as.interval.has_lower) {
            int cmp = 0;
            status = bs_value_compare(candidate, set->as.interval.lower, &cmp);
            if (status != BS_OK) {
                return status;
            }
            if (cmp < 0 || (cmp == 0 && !set->as.interval.lower_inclusive)) {
                *out_result = BS_FALSE;
                return BS_OK;
            }
        }
        if (set->as.interval.has_upper) {
            int cmp = 0;
            status = bs_value_compare(candidate, set->as.interval.upper, &cmp);
            if (status != BS_OK) {
                return status;
            }
            if (cmp > 0 || (cmp == 0 && !set->as.interval.upper_inclusive)) {
                *out_result = BS_FALSE;
                return BS_OK;
            }
        }
        *out_result = BS_TRUE;
        return BS_OK;
    }
    case SET_UNION:
        status = evaluate(set->as.binary.left, candidate, &left);
        if (status != BS_OK) {
            return status;
        }
        if (left == BS_TRUE) {
            *out_result = BS_TRUE;
            return BS_OK;
        }
        status = evaluate(set->as.binary.right, candidate, &right);
        if (status != BS_OK) {
            return status;
        }
        if (right == BS_TRUE) {
            *out_result = BS_TRUE;
        } else if (left == BS_FALSE && right == BS_FALSE) {
            *out_result = BS_FALSE;
        } else {
            *out_result = BS_UNDECIDED;
        }
        return BS_OK;
    case SET_INTERSECTION:
        status = evaluate(set->as.binary.left, candidate, &left);
        if (status != BS_OK) {
            return status;
        }
        if (left == BS_FALSE) {
            *out_result = BS_FALSE;
            return BS_OK;
        }
        status = evaluate(set->as.binary.right, candidate, &right);
        if (status != BS_OK) {
            return status;
        }
        if (right == BS_FALSE) {
            *out_result = BS_FALSE;
        } else if (left == BS_TRUE && right == BS_TRUE) {
            *out_result = BS_TRUE;
        } else {
            *out_result = BS_UNDECIDED;
        }
        return BS_OK;
    case SET_DIFFERENCE:
        status = evaluate(set->as.binary.left, candidate, &left);
        if (status != BS_OK) {
            return status;
        }
        if (left == BS_FALSE) {
            *out_result = BS_FALSE;
            return BS_OK;
        }
        status = evaluate(set->as.binary.right, candidate, &right);
        if (status != BS_OK) {
            return status;
        }
        if (right == BS_TRUE) {
            *out_result = BS_FALSE;
        } else if (left == BS_TRUE && right == BS_FALSE) {
            *out_result = BS_TRUE;
        } else {
            *out_result = BS_UNDECIDED;
        }
        return BS_OK;
    case SET_COMPLEMENT:
        status = evaluate(set->as.inner, candidate, &left);
        if (status != BS_OK) {
            return status;
        }
        *out_result = left == BS_TRUE ? BS_FALSE
                   : left == BS_FALSE ? BS_TRUE
                   : BS_UNDECIDED;
        return BS_OK;
    case SET_PREDICATE:
        if (set->as.predicate.bound != NULL) {
            status = evaluate(set->as.predicate.bound, candidate, &left);
            if (status != BS_OK) {
                return status;
            }
            if (left == BS_FALSE) {
                *out_result = BS_FALSE;
                return BS_OK;
            }
        } else {
            left = BS_TRUE;
        }

        status = set->as.predicate.predicate(
            set->as.predicate.context,
            candidate,
            &right
        );
        if (status != BS_OK) {
            return status;
        }
        if (!valid_truth(right)) {
            return BS_SETS_CALLBACK_ERROR;
        }
        if (left == BS_TRUE) {
            *out_result = right;
        } else if (right == BS_FALSE) {
            *out_result = BS_FALSE;
        } else {
            *out_result = BS_UNDECIDED;
        }
        return BS_OK;
    }
    return BS_INVALID_ARGUMENT;
}

bs_status bs_set_contains(
    const bs_set *set,
    bs_value candidate,
    bs_truth *out_result
)
{
    if (set == NULL || out_result == NULL ||
        !bs_internal_value_belongs_to(candidate, set->store->universe)) {
        return BS_INVALID_ARGUMENT;
    }
    return evaluate(set, candidate, out_result);
}

static bs_truth truth_and(bs_truth left, bs_truth right)
{
    if (left == BS_FALSE || right == BS_FALSE) {
        return BS_FALSE;
    }
    if (left == BS_TRUE && right == BS_TRUE) {
        return BS_TRUE;
    }
    return BS_UNDECIDED;
}

bs_status bs_set_is_finite(
    const bs_set *set,
    bs_truth *out_is_finite
)
{
    if (set == NULL || out_is_finite == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_truth left;
    bs_truth right;
    bs_status s;

    switch (set->kind) {
    case SET_ENUMERATED:
        *out_is_finite = BS_TRUE;
        return BS_OK;
    case SET_NATURE: {
        bool finite = false;
        s = bs_nature_is_finite(set->as.nature, &finite);
        if (s != BS_OK) {
            return s;
        }
        *out_is_finite = finite ? BS_TRUE : BS_FALSE;
        return BS_OK;
    }
    case SET_INTERVAL: {
        if (set->as.interval.has_lower && set->as.interval.has_upper) {
            bool finite = false;
            s = bs_nature_is_finite(set->as.interval.nature, &finite);
            if (s != BS_OK) {
                return s;
            }
            if (finite) {
                *out_is_finite = BS_TRUE;
                return BS_OK;
            }
            bool discrete = false;
            s = bs_nature_is_discrete(set->as.interval.nature, &discrete);
            if (s != BS_OK) {
                return s;
            }
            *out_is_finite = discrete ? BS_TRUE : BS_FALSE;
            return BS_OK;
        }
        bool nat_finite = false;
        s = bs_nature_is_finite(set->as.interval.nature, &nat_finite);
        if (s != BS_OK) {
            return s;
        }
        *out_is_finite = nat_finite ? BS_TRUE : BS_FALSE;
        return BS_OK;
    }
    case SET_UNION:
        s = bs_set_is_finite(set->as.binary.left, &left);
        if (s != BS_OK) {
            return s;
        }
        s = bs_set_is_finite(set->as.binary.right, &right);
        if (s != BS_OK) {
            return s;
        }
        *out_is_finite = truth_and(left, right);
        return BS_OK;
    case SET_INTERSECTION:
        s = bs_set_is_finite(set->as.binary.left, &left);
        if (s != BS_OK) {
            return s;
        }
        if (left == BS_TRUE) {
            *out_is_finite = BS_TRUE;
            return BS_OK;
        }
        s = bs_set_is_finite(set->as.binary.right, &right);
        if (s != BS_OK) {
            return s;
        }
        if (right == BS_TRUE) {
            *out_is_finite = BS_TRUE;
            return BS_OK;
        }
        *out_is_finite = BS_UNDECIDED;
        return BS_OK;
    case SET_DIFFERENCE:
        s = bs_set_is_finite(set->as.binary.left, &left);
        if (s != BS_OK) {
            return s;
        }
        if (left == BS_TRUE) {
            *out_is_finite = BS_TRUE;
            return BS_OK;
        }
        s = bs_set_is_finite(set->as.binary.right, &right);
        if (s != BS_OK) {
            return s;
        }
        if (left == BS_FALSE && right == BS_TRUE) {
            *out_is_finite = BS_FALSE;
            return BS_OK;
        }
        *out_is_finite = BS_UNDECIDED;
        return BS_OK;
    case SET_COMPLEMENT:
        *out_is_finite = BS_UNDECIDED;
        return BS_OK;
    case SET_PREDICATE:
        if (set->as.predicate.bound != NULL) {
            s = bs_set_is_finite(set->as.predicate.bound, &left);
            if (s != BS_OK) {
                return s;
            }
            if (left == BS_TRUE) {
                *out_is_finite = BS_TRUE;
                return BS_OK;
            }
        }
        *out_is_finite = BS_UNDECIDED;
        return BS_OK;
    }
    return BS_INVALID_ARGUMENT;
}

bs_status bs_set_is_enumerable(
    const bs_set *set,
    bs_truth *out_is_enumerable
)
{
    if (set == NULL || out_is_enumerable == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_truth left;
    bs_truth right;
    bs_status s;

    switch (set->kind) {
    case SET_ENUMERATED:
        *out_is_enumerable = BS_TRUE;
        return BS_OK;
    case SET_NATURE: {
        bool enumerable = false;
        s = bs_nature_is_enumerable(set->as.nature, &enumerable);
        if (s != BS_OK) {
            return s;
        }
        *out_is_enumerable = enumerable ? BS_TRUE : BS_FALSE;
        return BS_OK;
    }
    case SET_INTERVAL: {
        bool nat_enum = false;
        s = bs_nature_is_enumerable(set->as.interval.nature, &nat_enum);
        if (s != BS_OK) {
            return s;
        }
        if (!nat_enum) {
            *out_is_enumerable = BS_FALSE;
            return BS_OK;
        }
        bool discrete = false;
        s = bs_nature_is_discrete(set->as.interval.nature, &discrete);
        if (s != BS_OK) {
            return s;
        }
        const bs_nature *integer_nature =
            bs_universe_integer_nature(set->store->universe);
        if (discrete && (set->as.interval.has_lower ||
                         set->as.interval.nature == integer_nature)) {
            *out_is_enumerable = BS_TRUE;
            return BS_OK;
        }
        *out_is_enumerable = BS_FALSE;
        return BS_OK;
    }
    case SET_UNION:
        s = bs_set_is_enumerable(set->as.binary.left, &left);
        if (s != BS_OK) {
            return s;
        }
        s = bs_set_is_enumerable(set->as.binary.right, &right);
        if (s != BS_OK) {
            return s;
        }
        *out_is_enumerable = truth_and(left, right);
        return BS_OK;
    case SET_INTERSECTION:
        s = bs_set_is_enumerable(set->as.binary.left, &left);
        if (s != BS_OK) {
            return s;
        }
        if (left == BS_TRUE) {
            *out_is_enumerable = BS_TRUE;
            return BS_OK;
        }
        s = bs_set_is_enumerable(set->as.binary.right, &right);
        if (s != BS_OK) {
            return s;
        }
        if (right == BS_TRUE) {
            *out_is_enumerable = BS_TRUE;
            return BS_OK;
        }
        *out_is_enumerable = BS_UNDECIDED;
        return BS_OK;
    case SET_DIFFERENCE:
        s = bs_set_is_enumerable(set->as.binary.left, &left);
        if (s != BS_OK) {
            return s;
        }
        if (left == BS_TRUE) {
            *out_is_enumerable = BS_TRUE;
            return BS_OK;
        }
        s = bs_set_is_finite(set->as.binary.right, &right);
        if (s != BS_OK) {
            return s;
        }
        if (left == BS_FALSE && right == BS_TRUE) {
            *out_is_enumerable = BS_FALSE;
            return BS_OK;
        }
        *out_is_enumerable = BS_UNDECIDED;
        return BS_OK;
    case SET_COMPLEMENT:
        *out_is_enumerable = BS_FALSE;
        return BS_OK;
    case SET_PREDICATE:
        if (set->as.predicate.bound != NULL) {
            return bs_set_is_enumerable(set->as.predicate.bound, out_is_enumerable);
        }
        *out_is_enumerable = BS_FALSE;
        return BS_OK;
    }
    return BS_INVALID_ARGUMENT;
}

bs_status bs_set_is_discrete(
    const bs_set *set,
    bs_truth *out_is_discrete
)
{
    if (set == NULL || out_is_discrete == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_truth left;
    bs_truth right;
    bs_status s;

    switch (set->kind) {
    case SET_ENUMERATED:
        *out_is_discrete = BS_TRUE;
        return BS_OK;
    case SET_NATURE: {
        bool discrete = false;
        s = bs_nature_is_discrete(set->as.nature, &discrete);
        if (s != BS_OK) {
            return s;
        }
        *out_is_discrete = discrete ? BS_TRUE : BS_FALSE;
        return BS_OK;
    }
    case SET_INTERVAL: {
        bool discrete = false;
        s = bs_nature_is_discrete(set->as.interval.nature, &discrete);
        if (s != BS_OK) {
            return s;
        }
        *out_is_discrete = discrete ? BS_TRUE : BS_FALSE;
        return BS_OK;
    }
    case SET_UNION:
        s = bs_set_is_discrete(set->as.binary.left, &left);
        if (s != BS_OK) {
            return s;
        }
        s = bs_set_is_discrete(set->as.binary.right, &right);
        if (s != BS_OK) {
            return s;
        }
        *out_is_discrete = truth_and(left, right);
        return BS_OK;
    case SET_INTERSECTION:
        s = bs_set_is_discrete(set->as.binary.left, &left);
        if (s != BS_OK) {
            return s;
        }
        if (left == BS_TRUE) {
            *out_is_discrete = BS_TRUE;
            return BS_OK;
        }
        s = bs_set_is_discrete(set->as.binary.right, &right);
        if (s != BS_OK) {
            return s;
        }
        if (right == BS_TRUE) {
            *out_is_discrete = BS_TRUE;
            return BS_OK;
        }
        *out_is_discrete = BS_UNDECIDED;
        return BS_OK;
    case SET_DIFFERENCE:
        s = bs_set_is_discrete(set->as.binary.left, &left);
        if (s != BS_OK) {
            return s;
        }
        if (left == BS_TRUE) {
            *out_is_discrete = BS_TRUE;
            return BS_OK;
        }
        *out_is_discrete = BS_UNDECIDED;
        return BS_OK;
    case SET_COMPLEMENT:
        *out_is_discrete = BS_UNDECIDED;
        return BS_OK;
    case SET_PREDICATE:
        if (set->as.predicate.bound != NULL) {
            return bs_set_is_discrete(set->as.predicate.bound, out_is_discrete);
        }
        *out_is_discrete = BS_UNDECIDED;
        return BS_OK;
    }
    return BS_INVALID_ARGUMENT;
}

struct bs_iterator {
    const bs_set *set;
    set_kind kind;
    union {
        struct {
            size_t index;
        } enumerated;
        struct {
            bs_nature_iter_state state;
        } nature;
        struct {
            bs_value current;
            bs_value end;
            bool has_end;
            bool descending;
            bool exhausted;
        } interval;
        struct {
            bs_iterator *bound_iter;
        } predicate;
        struct {
            bs_iterator *left_iter;
            bs_iterator *right_iter;
            bool left_exhausted;
            bool right_exhausted;
            bool step_left;
        } union_state;
        struct {
            bs_iterator *source_iter;
            const bs_set *filter_set;
        } intersection_state;
        struct {
            bs_iterator *source_iter;
            const bs_set *filter_set;
        } difference_state;
    } as;
};

static bool has_known_cardinality(const bs_set *set, bs_int *out_card)
{
    if (set == NULL || out_card == NULL) {
        return false;
    }
    if (set->kind == SET_ENUMERATED) {
        bs_int c = bs_int_from_u64((uint64_t) set->as.enumerated.count);
        if (bs_int_is_null(c)) return false;
        *out_card = c;
        return true;
    }
    if (set->kind == SET_NATURE) {
        return bs_nature_cardinality(set->as.nature, out_card) == BS_OK;
    }
    if (set->kind == SET_INTERVAL && set->as.interval.has_lower && set->as.interval.has_upper) {
        bool is_discrete = false;
        if (bs_nature_is_discrete(set->as.interval.nature, &is_discrete) == BS_OK && is_discrete) {
            return bs_internal_nature_interval_cardinality(
                set->as.interval.nature,
                set->as.interval.lower,
                set->as.interval.lower_inclusive,
                set->as.interval.upper,
                set->as.interval.upper_inclusive,
                out_card
            ) == BS_OK;
        }
    }
    return false;
}

bs_status bs_iterator_create(
    const bs_set *set,
    bs_iterator **out_iterator
)
{
    if (set == NULL || out_iterator == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_truth enumerable = BS_UNDECIDED;
    bs_status s = bs_set_is_enumerable(set, &enumerable);
    if (s != BS_OK) {
        return s;
    }
    if (enumerable != BS_TRUE) {
        return BS_SETS_TYPE_ERROR;
    }

    bs_iterator *it = bs_calloc(1, sizeof(*it));
    if (it == NULL) {
        return BS_OUT_OF_MEMORY;
    }
    it->set = set;
    it->kind = set->kind;

    switch (set->kind) {
    case SET_ENUMERATED:
        it->as.enumerated.index = 0;
        break;

    case SET_NATURE:
        /* bs_calloc initializes the Nature iterator state to ordinal zero. */
        break;

    case SET_INTERVAL: {
        bool has_lower = set->as.interval.has_lower;
        bs_value lower = set->as.interval.lower;
        bool lower_inclusive = set->as.interval.lower_inclusive;
        bool has_upper = set->as.interval.has_upper;
        bs_value upper = set->as.interval.upper;
        bool upper_inclusive = set->as.interval.upper_inclusive;
        const bs_nature *integer_nature =
            bs_universe_integer_nature(set->store->universe);
        bool descending = !has_lower && has_upper &&
                          set->as.interval.nature == integer_nature;
        if (!has_lower && !descending) {
            bs_free(it);
            return BS_SETS_TYPE_ERROR;
        }
        bool is_discrete = false;
        s = bs_nature_is_discrete(set->as.interval.nature, &is_discrete);
        if (s != BS_OK || !is_discrete) {
            bs_free(it);
            return BS_SETS_TYPE_ERROR;
        }
        bs_value cur = descending ? upper : lower;
        bool cur_inclusive = descending ? upper_inclusive : lower_inclusive;
        if (!cur_inclusive) {
            s = bs_internal_nature_step(set->as.interval.nature, cur,
                                            descending ? -1 : 1, &cur);
            if (s == BS_SETS_EXHAUSTED) {
                it->as.interval.exhausted = true;
            } else if (s != BS_OK) {
                bs_free(it);
                return s;
            }
        }
        it->as.interval.current = cur;
        if (has_upper && !descending) {
            bs_value end = upper;
            if (!upper_inclusive) {
                s = bs_internal_nature_step(set->as.interval.nature, end, -1, &end);
                if (s == BS_SETS_EXHAUSTED) {
                    it->as.interval.exhausted = true;
                } else if (s != BS_OK) {
                    bs_free(it);
                    return s;
                }
            }
            it->as.interval.end = end;
            it->as.interval.has_end = true;
            if (!it->as.interval.exhausted) {
                int cmp = 0;
                s = bs_value_compare(it->as.interval.current, it->as.interval.end, &cmp);
                if (s != BS_OK) {
                    bs_free(it);
                    return s;
                }
                if (cmp > 0) {
                    it->as.interval.exhausted = true;
                }
            }
        } else {
            it->as.interval.has_end = false;
        }
        it->as.interval.descending = descending;
        break;
    }

    case SET_PREDICATE:
        s = bs_iterator_create(set->as.predicate.bound, &it->as.predicate.bound_iter);
        if (s != BS_OK) {
            bs_free(it);
            return s;
        }
        break;

    case SET_INTERSECTION: {
        bs_truth left_enum = BS_UNDECIDED;
        bs_truth right_enum = BS_UNDECIDED;
        s = bs_set_is_enumerable(set->as.binary.left, &left_enum);
        if (s != BS_OK) {
            bs_free(it);
            return s;
        }
        s = bs_set_is_enumerable(set->as.binary.right, &right_enum);
        if (s != BS_OK) {
            bs_free(it);
            return s;
        }

        bool choose_left = true;
        if (left_enum == BS_TRUE && right_enum != BS_TRUE) {
            choose_left = true;
        } else if (right_enum == BS_TRUE && left_enum != BS_TRUE) {
            choose_left = false;
        } else if (left_enum == BS_TRUE && right_enum == BS_TRUE) {
            bs_truth left_finite = BS_UNDECIDED;
            bs_truth right_finite = BS_UNDECIDED;
            s = bs_set_is_finite(set->as.binary.left, &left_finite);
            if (s != BS_OK) {
                bs_free(it);
                return s;
            }
            s = bs_set_is_finite(set->as.binary.right, &right_finite);
            if (s != BS_OK) {
                bs_free(it);
                return s;
            }

            if (left_finite == BS_TRUE && right_finite != BS_TRUE) {
                choose_left = true;
            } else if (right_finite == BS_TRUE && left_finite != BS_TRUE) {
                choose_left = false;
            } else if (left_finite == BS_TRUE && right_finite == BS_TRUE) {
                bs_int card_l = BS_INT_NULL;
                bs_int card_r = BS_INT_NULL;
                bool has_l = has_known_cardinality(set->as.binary.left, &card_l);
                bool has_r = has_known_cardinality(set->as.binary.right, &card_r);
                if (has_l && has_r) {
                    choose_left = (bs_int_cmp(card_l, card_r) <= 0);
                } else if (has_l) {
                    choose_left = true;
                } else if (has_r) {
                    choose_left = false;
                } else {
                    choose_left = true;
                }
                if (has_l) bs_int_release(card_l);
                if (has_r) bs_int_release(card_r);
            } else {
                choose_left = true;
            }
        } else {
            bs_free(it);
            return BS_SETS_TYPE_ERROR;
        }

        const bs_set *source_set = choose_left ? set->as.binary.left : set->as.binary.right;
        const bs_set *filter_set = choose_left ? set->as.binary.right : set->as.binary.left;

        s = bs_iterator_create(source_set, &it->as.intersection_state.source_iter);
        if (s != BS_OK) {
            bs_free(it);
            return s;
        }
        it->as.intersection_state.filter_set = filter_set;
        break;
    }

    case SET_DIFFERENCE: {
        s = bs_iterator_create(set->as.binary.left, &it->as.difference_state.source_iter);
        if (s != BS_OK) {
            bs_free(it);
            return s;
        }
        it->as.difference_state.filter_set = set->as.binary.right;
        break;
    }

    case SET_UNION:
        s = bs_iterator_create(set->as.binary.left, &it->as.union_state.left_iter);
        if (s != BS_OK) {
            bs_free(it);
            return s;
        }
        s = bs_iterator_create(set->as.binary.right, &it->as.union_state.right_iter);
        if (s != BS_OK) {
            bs_iterator_destroy(it->as.union_state.left_iter);
            bs_free(it);
            return s;
        }
        it->as.union_state.step_left = true;
        break;

    case SET_COMPLEMENT:
        bs_free(it);
        return BS_SETS_TYPE_ERROR;
    }

    *out_iterator = it;
    return BS_OK;
}

bs_status bs_iterator_next(
    bs_iterator *iterator,
    bs_value *out_value
)
{
    if (iterator == NULL || out_value == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    switch (iterator->kind) {
    case SET_ENUMERATED:
        while (iterator->as.enumerated.index < iterator->set->as.enumerated.count) {
            bs_value candidate = iterator->set->as.enumerated.values[iterator->as.enumerated.index++];
            bool dup = false;
            for (size_t j = 0; j < iterator->as.enumerated.index - 1; ++j) {
                if (bs_value_equal(iterator->set->as.enumerated.values[j], candidate)) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                *out_value = candidate;
                return BS_OK;
            }
        }
        return BS_SETS_EXHAUSTED;

    case SET_NATURE:
        return bs_internal_nature_iter_step(
            iterator->set->as.nature,
            &iterator->as.nature.state,
            out_value
        );

    case SET_INTERVAL: {
        if (iterator->as.interval.exhausted) {
            return BS_SETS_EXHAUSTED;
        }
        *out_value = iterator->as.interval.current;
        if (iterator->as.interval.has_end) {
            int cmp = 0;
            bs_status s = bs_value_compare(
                iterator->as.interval.current,
                iterator->as.interval.end,
                &cmp
            );
            if (s != BS_OK) {
                return s;
            }
            if ((!iterator->as.interval.descending && cmp >= 0) ||
                (iterator->as.interval.descending && cmp <= 0)) {
                iterator->as.interval.exhausted = true;
                return BS_OK;
            }
        }
        bs_value next_val;
        bs_status s = bs_internal_nature_step(
            iterator->set->as.interval.nature,
            iterator->as.interval.current,
            iterator->as.interval.descending ? -1 : 1,
            &next_val
        );
        if (s == BS_SETS_EXHAUSTED) {
            iterator->as.interval.exhausted = true;
        } else if (s == BS_OK) {
            iterator->as.interval.current = next_val;
        } else {
            return s;
        }
        return BS_OK;
    }

    case SET_PREDICATE:
        while (true) {
            bs_value candidate;
            bs_status s = bs_iterator_next(iterator->as.predicate.bound_iter, &candidate);
            if (s != BS_OK) {
                return s;
            }
            bs_truth truth = BS_UNDECIDED;
            s = iterator->set->as.predicate.predicate(
                iterator->set->as.predicate.context,
                candidate,
                &truth
            );
            if (s != BS_OK) {
                return s;
            }
            if (!valid_truth(truth)) {
                return BS_SETS_CALLBACK_ERROR;
            }
            if (truth == BS_TRUE) {
                *out_value = candidate;
                return BS_OK;
            }
        }

    case SET_INTERSECTION:
        while (true) {
            bs_value candidate;
            bs_status s = bs_iterator_next(
                iterator->as.intersection_state.source_iter,
                &candidate
            );
            if (s != BS_OK) {
                return s;
            }
            bs_truth truth = BS_UNDECIDED;
            s = evaluate(iterator->as.intersection_state.filter_set, candidate, &truth);
            if (s != BS_OK) {
                return s;
            }
            if (truth == BS_TRUE) {
                *out_value = candidate;
                return BS_OK;
            }
        }

    case SET_DIFFERENCE:
        while (true) {
            bs_value candidate;
            bs_status s = bs_iterator_next(
                iterator->as.difference_state.source_iter,
                &candidate
            );
            if (s != BS_OK) {
                return s;
            }
            bs_truth truth = BS_UNDECIDED;
            s = evaluate(iterator->as.difference_state.filter_set, candidate, &truth);
            if (s != BS_OK) {
                return s;
            }
            if (truth == BS_FALSE) {
                *out_value = candidate;
                return BS_OK;
            }
        }

    case SET_UNION:
        while (!iterator->as.union_state.left_exhausted ||
               !iterator->as.union_state.right_exhausted) {
            bool pull_left = false;
            if (!iterator->as.union_state.left_exhausted &&
                !iterator->as.union_state.right_exhausted) {
                pull_left = iterator->as.union_state.step_left;
                iterator->as.union_state.step_left = !iterator->as.union_state.step_left;
            } else if (!iterator->as.union_state.left_exhausted) {
                pull_left = true;
            } else {
                pull_left = false;
            }

            if (pull_left) {
                bs_value candidate;
                bs_status s = bs_iterator_next(
                    iterator->as.union_state.left_iter,
                    &candidate
                );
                if (s == BS_SETS_EXHAUSTED) {
                    iterator->as.union_state.left_exhausted = true;
                    continue;
                }
                if (s != BS_OK) {
                    return s;
                }
                *out_value = candidate;
                return BS_OK;
            } else {
                bs_value candidate;
                bs_status s = bs_iterator_next(
                    iterator->as.union_state.right_iter,
                    &candidate
                );
                if (s == BS_SETS_EXHAUSTED) {
                    iterator->as.union_state.right_exhausted = true;
                    continue;
                }
                if (s != BS_OK) {
                    return s;
                }
                bs_truth in_left = BS_UNDECIDED;
                s = evaluate(iterator->set->as.binary.left, candidate, &in_left);
                if (s != BS_OK) {
                    return s;
                }
                if (in_left == BS_TRUE) {
                    continue;
                }
                *out_value = candidate;
                return BS_OK;
            }
        }
        return BS_SETS_EXHAUSTED;

    case SET_COMPLEMENT:
        return BS_SETS_TYPE_ERROR;
    }

    return BS_INVALID_ARGUMENT;
}

void bs_iterator_destroy(bs_iterator *iterator)
{
    if (iterator == NULL) {
        return;
    }
    switch (iterator->kind) {
    case SET_NATURE:
        bs_internal_nature_iter_state_destroy(
            iterator->set->as.nature, &iterator->as.nature.state
        );
        break;
    case SET_PREDICATE:
        bs_iterator_destroy(iterator->as.predicate.bound_iter);
        break;
    case SET_INTERSECTION:
        bs_iterator_destroy(iterator->as.intersection_state.source_iter);
        break;
    case SET_DIFFERENCE:
        bs_iterator_destroy(iterator->as.difference_state.source_iter);
        break;
    case SET_UNION:
        bs_iterator_destroy(iterator->as.union_state.left_iter);
        bs_iterator_destroy(iterator->as.union_state.right_iter);
        break;
    default:
        break;
    }
    bs_free(iterator);
}

bs_status bs_set_cardinality(
    const bs_set *set,
    bs_int *out_cardinality
)
{
    if (set == NULL || out_cardinality == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_truth is_finite = BS_UNDECIDED;
    bs_status s = bs_set_is_finite(set, &is_finite);
    if (s != BS_OK) {
        return s;
    }
    if (is_finite != BS_TRUE) {
        return BS_SETS_TYPE_ERROR;
    }
    if (set->kind == SET_ENUMERATED) {
        size_t unique = 0;
        for (size_t i = 0; i < set->as.enumerated.count; ++i) {
            bool seen = false;
            for (size_t j = 0; j < i; ++j) {
                if (bs_value_equal(set->as.enumerated.values[i], set->as.enumerated.values[j])) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                ++unique;
            }
        }
        bs_int card = bs_int_from_u64((uint64_t)unique);
        if (bs_int_is_null(card)) {
            return BS_OUT_OF_MEMORY;
        }
        *out_cardinality = card;
        return BS_OK;
    }
    if (set->kind == SET_INTERVAL) {
        bs_value lower = set->as.interval.lower;
        bs_value upper = set->as.interval.upper;
        bool lower_inclusive = set->as.interval.lower_inclusive;
        bool upper_inclusive = set->as.interval.upper_inclusive;
        bool has_lower = set->as.interval.has_lower;
        bool has_upper = set->as.interval.has_upper;
        if (!has_lower || !has_upper) {
            return BS_SETS_TYPE_ERROR;
        }
        bool is_discrete = false;
        bs_status s_disc = bs_nature_is_discrete(set->as.interval.nature, &is_discrete);
        if (s_disc != BS_OK) {
            return s_disc;
        }
        if (!is_discrete) {
            return BS_SETS_TYPE_ERROR;
        }
        return bs_internal_nature_interval_cardinality(
            set->as.interval.nature,
            lower,
            lower_inclusive,
            upper,
            upper_inclusive,
            out_cardinality
        );
    }
    if (set->kind == SET_NATURE) {
        return bs_nature_cardinality(set->as.nature, out_cardinality);
    }
    /* For other finite sets, iterate to count unique elements */
    bs_iterator *it = NULL;
    s = bs_iterator_create(set, &it);
    if (s != BS_OK) {
        return s;
    }
    uint64_t count = 0;
    bs_value val;
    while ((s = bs_iterator_next(it, &val)) == BS_OK) {
        ++count;
    }
    bs_iterator_destroy(it);
    if (s == BS_SETS_EXHAUSTED) {
        bs_int card = bs_int_from_u64(count);
        if (bs_int_is_null(card)) {
            return BS_OUT_OF_MEMORY;
        }
        *out_cardinality = card;
        return BS_OK;
    }
    return s;
}

bs_status bs_set_for_each(
    const bs_set *set,
    bs_each_fn callback,
    void *context
)
{
    if (set == NULL || callback == NULL) {
        return BS_INVALID_ARGUMENT;
    }
    bs_iterator *it = NULL;
    bs_status s = bs_iterator_create(set, &it);
    if (s != BS_OK) {
        return s;
    }
    bs_value val;
    while ((s = bs_iterator_next(it, &val)) == BS_OK) {
        bool proceed = true;
        s = callback(context, val, &proceed);
        if (s != BS_OK || !proceed) {
            break;
        }
    }
    bs_iterator_destroy(it);
    return (s == BS_SETS_EXHAUSTED) ? BS_OK : s;
}
