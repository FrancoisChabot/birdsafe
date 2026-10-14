/**
 * @file bs_sets.h
 * @brief Public API for bs_sets - BirdSafe Sets, Natures, and Value Model.
 */

#ifndef BS_SETS_BS_SETS_H
#define BS_SETS_BS_SETS_H

#include "bs_core/bs_alloc.h"
#include "bs_core/bs_config.h"
#include "bs_core/bs_status.h"
#include "bs_core/bs_truth.h"
#include "bs_num/bs_int.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Set Subsystem Status Codes                                                */
/* ========================================================================= */

/** Status and error codes specific to set domain operations. */
typedef enum bs_sets_status {
    BS_SETS_EXHAUSTED      = BS_STATUS_MAKE(BS_DOMAIN_SETS, 1),
    BS_SETS_TYPE_ERROR     = BS_STATUS_MAKE(BS_DOMAIN_SETS, 2),
    BS_SETS_OUT_OF_BOUNDS  = BS_STATUS_MAKE(BS_DOMAIN_SETS, 3),
    BS_SETS_CALLBACK_ERROR = BS_STATUS_MAKE(BS_DOMAIN_SETS, 4),
} bs_sets_status;

/* ========================================================================= */
/* Opaque Types & Core Handles                                               */
/* ========================================================================= */

typedef struct bs_nature bs_nature;
typedef struct bs_universe bs_universe;
typedef struct bs_set bs_set;
typedef struct bs_set_store bs_set_store;
typedef struct bs_iterator bs_iterator;

/**
 * Lightweight, copyable value handle.
 * Nature and payload remain valid until the owning Universe is destroyed.
 * Treat storage as private: inspect and manipulate values only via API.
 */
typedef struct bs_value {
    union {
        uint64_t _opaque_alignment;
        unsigned char _opaque_storage[3 * sizeof(uint64_t)];
    };
} bs_value;

/** Strong three-valued predicate callback. */
typedef bs_status (*bs_predicate_fn)(
    void *context,
    bs_value candidate,
    bs_truth *out_result
);

/** Destructor callback for predicate context when store is destroyed. */
typedef void (*bs_context_destroy_fn)(void *context);

/* ========================================================================= */
/* Universe Lifecycle & Predefined Natures                                   */
/* ========================================================================= */

/** Creates a thread-safe universe container. */
BS_API bs_status bs_universe_create(bs_universe **out_universe);

/**
 * Destroys a universe and all natures and values tied to it.
 * All stores created in this universe must be destroyed prior to calling this function.
 */
BS_API void bs_universe_destroy(bs_universe *universe);

/** Returns the meta-nature (Nature of Natures) for a universe. */
BS_API const bs_nature *bs_universe_nature_of_natures(const bs_universe *universe);

/** Returns the predefined Integer nature for a universe. */
BS_API const bs_nature *bs_universe_integer_nature(const bs_universe *universe);

/** Returns the universe that owns the given nature. */
BS_API const bs_universe *bs_nature_universe(const bs_nature *nature);

/* ========================================================================= */
/* Value Constructors & Inspections                                          */
/* ========================================================================= */

/** Wraps a nature handle as a first-class value. */
BS_API bs_status bs_nature_as_value(const bs_nature *nature, bs_value *out_value);

/** Unpacks a nature handle from a value. */
BS_API bs_status bs_value_as_nature(bs_value value, const bs_nature **out_nature);

/** Extracts the ordinal index from a minted value. */
BS_API bs_status bs_value_as_minted_index(bs_value value, uint64_t *out_index);

/** Extracts a 64-bit signed integer from an integer value. */
BS_API bs_status bs_value_as_integer(bs_value value, int64_t *out_integer);

/** Creates an integer value from a 64-bit signed integer. */
BS_API bs_status bs_value_from_integer(
    const bs_universe *universe,
    int64_t integer,
    bs_value *out_value
);

/** Creates an integer value by parsing a decimal string. */
BS_API bs_status bs_value_from_decimal(
    const bs_universe *universe,
    const char *decimal,
    bs_value *out_value
);

/** Formats an integer value into a character buffer. */
BS_API bs_status bs_value_format_integer(
    bs_value value,
    char *buffer,
    size_t capacity,
    size_t *out_required_capacity
);

/** Creates an integer value from a bs_int handle. */
BS_API bs_status bs_value_from_bs_int(
    const bs_universe *universe,
    bs_int integer,
    bs_value *out_value
);

/** Extracts a bs_int handle from an integer value. */
BS_API bs_status bs_value_as_bs_int(
    bs_value value,
    bs_int *out_integer
);

/* ========================================================================= */
/* Minting Natures                                                           */
/* ========================================================================= */

/** Mints a new finite nature with a fixed cardinality. Thread-safe across threads. */
BS_API bs_status bs_nature_mint_finite(
    bs_universe *universe,
    uint64_t cardinality,
    const bs_nature **out_nature
);

/** Retrieves a minted value by index from a finite nature. */
BS_API bs_status bs_nature_get_minted_value(
    const bs_nature *nature,
    uint64_t index,
    bs_value *out_value
);

/** Mints a new nonfinite (open) nature. Thread-safe across threads. */
BS_API bs_status bs_nature_mint_nonfinite(
    bs_universe *universe,
    bs_nature **out_nature
);

/** Mints a new unique value in a nonfinite nature. Thread-safe and lock-free across threads. */
BS_API bs_status bs_nature_mint_value(
    bs_universe *universe,
    bs_nature *nature,
    bs_value *out_value
);

/* ========================================================================= */
/* Nature Properties & Value Comparisons                                     */
/* ========================================================================= */

/** Retrieves the cardinality of a finite nature (output owned). */
BS_API bs_status bs_nature_cardinality(
    const bs_nature *nature,
    bs_int *out_cardinality
);

/** Checks whether a nature is finite. */
BS_API bs_status bs_nature_is_finite(
    const bs_nature *nature,
    bool *out_is_finite
);

/** Checks whether a nature is enumerable. */
BS_API bs_status bs_nature_is_enumerable(
    const bs_nature *nature,
    bool *out_is_enumerable
);

/** Checks whether a nature is discrete. */
BS_API bs_status bs_nature_is_discrete(
    const bs_nature *nature,
    bool *out_is_discrete
);

/** Returns the nature of a value. */
BS_API const bs_nature *bs_value_nature(bs_value value);

/** Tests structural equality between two values. */
BS_API bool bs_value_equal(bs_value left, bs_value right);

/** Three-way comparison between two values. */
BS_API bs_status bs_value_compare(
    bs_value left,
    bs_value right,
    int *out_comparison
);

/** Computes a 64-bit hash of a value. */
BS_API uint64_t bs_value_hash(bs_value value);

/* ========================================================================= */
/* Set Store Lifecycle & Construction                                        */
/* ========================================================================= */

/** Creates a set store container. Stores are thread-confined workspaces. */
BS_API bs_status bs_set_store_create(
    const bs_universe *universe,
    bs_set_store **out_store
);

/** Destroys a set store and releases all sets managed by it. */
BS_API void bs_set_store_destroy(bs_set_store *store);

/** Returns the universe associated with the store. */
BS_API const bs_universe *bs_set_store_universe(const bs_set_store *store);

/** Returns the universe associated with the set. */
BS_API const bs_universe *bs_set_universe(const bs_set *set);

/** Constructs an enumerated set from an explicit array of values. */
BS_API bs_status bs_set_enumerated(
    bs_set_store *store,
    const bs_value *values,
    size_t count,
    const bs_set **out_set
);

/** Constructs a set representing an entire nature. */
BS_API bs_status bs_set_nature(
    bs_set_store *store,
    const bs_nature *nature,
    const bs_set **out_set
);

/** Constructs an interval set between lower and upper bounds. */
BS_API bs_status bs_set_interval(
    bs_set_store *store,
    const bs_value *lower,
    bool lower_inclusive,
    const bs_value *upper,
    bool upper_inclusive,
    const bs_set **out_set
);

/** Constructs the union of two sets. */
BS_API bs_status bs_set_union(
    bs_set_store *store,
    const bs_set *left,
    const bs_set *right,
    const bs_set **out_set
);

/** Constructs the intersection of two sets. */
BS_API bs_status bs_set_intersection(
    bs_set_store *store,
    const bs_set *left,
    const bs_set *right,
    const bs_set **out_set
);

/** Constructs the set difference (left \ right). */
BS_API bs_status bs_set_difference(
    bs_set_store *store,
    const bs_set *left,
    const bs_set *right,
    const bs_set **out_set
);

/** Constructs the complement of a set within its universe. */
BS_API bs_status bs_set_complement(
    bs_set_store *store,
    const bs_set *inner,
    const bs_set **out_set
);

/** Constructs a set filtered by a three-valued predicate. */
BS_API bs_status bs_set_predicate(
    bs_set_store *store,
    const bs_set *bound,
    bs_predicate_fn predicate,
    void *context,
    bs_context_destroy_fn context_destroy,
    const bs_set **out_set
);

/* ========================================================================= */
/* Set Queries                                                               */
/* ========================================================================= */

/** Evaluates membership of a candidate value in a set (three-valued logic). */
BS_API bs_status bs_set_contains(
    const bs_set *set,
    bs_value candidate,
    bs_truth *out_result
);

/** Queries whether a set is finite. */
BS_API bs_status bs_set_is_finite(
    const bs_set *set,
    bs_truth *out_is_finite
);

/** Queries whether a set is enumerable. */
BS_API bs_status bs_set_is_enumerable(
    const bs_set *set,
    bs_truth *out_is_enumerable
);

/** Queries whether a set is discrete. */
BS_API bs_status bs_set_is_discrete(
    const bs_set *set,
    bs_truth *out_is_discrete
);

/** Retrieves the cardinality of a finite set (output owned). */
BS_API bs_status bs_set_cardinality(
    const bs_set *set,
    bs_int *out_cardinality
);

/* ========================================================================= */
/* Set Iteration                                                             */
/* ========================================================================= */

/** Creates an iterator over an enumerable set. */
BS_API bs_status bs_iterator_create(
    const bs_set *set,
    bs_iterator **out_iterator
);

/** Advances iterator and yields next value. */
BS_API bs_status bs_iterator_next(
    bs_iterator *iterator,
    bs_value *out_value
);

/** Destroys an iterator. */
BS_API void bs_iterator_destroy(bs_iterator *iterator);

/** Callback for element traversal in bs_set_for_each. Set *out_continue to false to stop. */
typedef bs_status (*bs_each_fn)(
    void *context,
    bs_value value,
    bool *out_continue
);

/** Iterates over elements of a set, invoking callback on each value. */
BS_API bs_status bs_set_for_each(
    const bs_set *set,
    bs_each_fn callback,
    void *context
);

#ifdef __cplusplus
}
#endif

#endif /* BS_SETS_BS_SETS_H */
