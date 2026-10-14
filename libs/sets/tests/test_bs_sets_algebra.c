#include "bs_sets.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct predicate_state {
    bs_status status;
    bs_truth result;
    size_t calls;
} predicate_state;

static bs_status fixed_predicate(
    void *context,
    bs_value candidate,
    bs_truth *out_result
)
{
    (void) candidate;
    predicate_state *state = context;
    ++state->calls;
    if (state->status != BS_OK) {
        return state->status;
    }
    *out_result = state->result;
    return BS_OK;
}

static const bs_set *make_predicate(
    bs_set_store *store,
    const bs_set *bound,
    predicate_state *state
)
{
    const bs_set *set = NULL;
    assert(bs_set_predicate(
        store, bound, fixed_predicate, state, NULL, &set
    ) == BS_OK);
    return set;
}

static bs_truth contains(const bs_set *set, bs_value value)
{
    bs_truth result = BS_UNDECIDED;
    assert(bs_set_contains(set, value, &result) == BS_OK);
    return result;
}

static bool assert_card_u64(bs_int card, uint64_t expected)
{
    uint64_t val = 0;
    bool ok = bs_int_to_u64(card, &val) && (val == expected);
    bs_int_release(card);
    return ok;
}

static void count_destructor(void *context)
{
    int *count = context;
    ++(*count);
}

static bs_status count_for_each(void *context, bs_value value, bool *out_continue)
{
    (void) value;
    int *count = context;
    ++(*count);
    if (*count >= 2) {
        *out_continue = false;
    }
    return BS_OK;
}

int main(void)
{
    /* Test status strings and domains */
    assert(bs_status_is_ok(BS_OK));
    assert(!bs_status_is_ok(BS_SETS_TYPE_ERROR));
    assert(bs_status_domain(BS_SETS_TYPE_ERROR) == BS_DOMAIN_SETS);
    assert(bs_status_code(BS_SETS_TYPE_ERROR) == 2);
    assert(bs_status_domain(BS_NUM_OVERFLOW) == BS_DOMAIN_NUM);
    assert(bs_status_code(BS_NUM_OVERFLOW) == 1);
    assert(strcmp(bs_status_domain_name(BS_SETS_TYPE_ERROR), "sets") == 0);
    assert(strcmp(bs_status_domain_name(BS_NUM_OVERFLOW), "num") == 0);

    assert(strcmp(bs_status_string(BS_OK), "OK") == 0);
    assert(strcmp(bs_status_string(BS_INVALID_ARGUMENT), "Invalid argument") == 0);
    assert(strcmp(bs_status_string(BS_SETS_TYPE_ERROR), "Type error") == 0);
    assert(strcmp(bs_status_string(BS_NUM_OVERFLOW), "Overflow") == 0);
    assert(strcmp(bs_status_string(BS_NUM_PARSE_ERROR), "Parse error") == 0);

    /* Test truth strings */
    assert(strcmp(bs_truth_string(BS_FALSE), "False") == 0);
    assert(strcmp(bs_truth_string(BS_TRUE), "True") == 0);
    assert(strcmp(bs_truth_string(BS_UNDECIDED), "Undecided") == 0);
    assert(strcmp(bs_truth_string((bs_truth) 99), "Unknown truth") == 0);

    bs_universe *universe = NULL;
    bs_universe *other_universe = NULL;
    bs_set_store *store = NULL;
    assert(bs_universe_create(&universe) == BS_OK);
    assert(bs_universe_create(&other_universe) == BS_OK);
    assert(bs_set_store_create(universe, &store) == BS_OK);

    const bs_nature *nature_of_natures = bs_universe_nature_of_natures(universe);
    const bs_nature *other_nature_of_natures = bs_universe_nature_of_natures(other_universe);
    assert(nature_of_natures != NULL);
    assert(other_nature_of_natures != NULL);
    assert(nature_of_natures != other_nature_of_natures);

    bool is_finite = true;
    assert(bs_nature_is_finite(nature_of_natures, &is_finite) == BS_OK);
    assert(!is_finite);

    bs_value meta_value;
    const bs_nature *round_trip_nature = NULL;
    assert(bs_nature_as_value(nature_of_natures, &meta_value) == BS_OK);
    assert(bs_value_nature(meta_value) == nature_of_natures);
    assert(bs_value_as_nature(meta_value, &round_trip_nature) == BS_OK);
    assert(round_trip_nature == nature_of_natures);
    assert(bs_value_nature(meta_value) == nature_of_natures);
    bs_int meta_cardinality = BS_INT_NULL;
    assert(bs_nature_cardinality(nature_of_natures, &meta_cardinality) == BS_SETS_TYPE_ERROR);
    assert(bs_nature_mint_value(universe, (bs_nature *) nature_of_natures, &meta_value) == BS_SETS_TYPE_ERROR);

    const bs_nature *nature = NULL;
    assert(bs_nature_mint_finite(universe, 3, &nature) == BS_OK);
    assert(bs_nature_is_finite(nature, &is_finite) == BS_OK);
    assert(is_finite);
    bs_int cardinality = BS_INT_NULL;
    assert(bs_nature_cardinality(nature, &cardinality) == BS_OK);
    assert(assert_card_u64(cardinality, 3));

    bs_value values[3];
    for (uint64_t i = 0; i < 3; ++i) {
        assert(bs_nature_get_minted_value(nature, i, &values[i]) == BS_OK);
        uint64_t extracted_index = 999;
        assert(bs_value_as_minted_index(values[i], &extracted_index) == BS_OK);
        assert(extracted_index == i);
    }
    int comparison = 0;
    uint64_t dummy_index = 0;

    const bs_nature *integer_nature = NULL;
    integer_nature = bs_universe_integer_nature(universe);
    assert(integer_nature != NULL);
    bs_value integer_min, integer_max, integer_zero, integer_negative;
    assert(bs_value_from_integer(universe, INT64_MIN, &integer_min) == BS_OK);
    assert(bs_value_from_integer(universe, INT64_MAX, &integer_max) == BS_OK);
    assert(bs_value_from_integer(universe, 0, &integer_zero) == BS_OK);
    assert(bs_value_from_integer(universe, -7, &integer_negative) == BS_OK);
    assert(bs_value_nature(integer_min) == integer_nature &&
           bs_value_nature(integer_max) == integer_nature);
    int64_t extracted_integer = 0;
    assert(bs_value_as_integer(integer_min, &extracted_integer) == BS_OK);
    assert(extracted_integer == INT64_MIN);
    assert(bs_value_as_integer(integer_max, &extracted_integer) == BS_OK);
    assert(extracted_integer == INT64_MAX);
    assert(bs_value_as_minted_index(integer_zero, &dummy_index) == BS_SETS_TYPE_ERROR);
    assert(bs_value_compare(integer_min, integer_max, &comparison) == BS_OK);
    assert(comparison < 0);
    bool integer_finite = true;
    assert(bs_nature_is_finite(integer_nature, &integer_finite) == BS_OK);
    assert(!integer_finite);
    bool integer_enumerable = false;
    assert(bs_nature_is_enumerable(integer_nature, &integer_enumerable) == BS_OK);
    assert(integer_enumerable);
    bool integer_discrete = false;
    assert(bs_nature_is_discrete(integer_nature, &integer_discrete) == BS_OK);
    assert(integer_discrete);
    bs_int integer_cardinality = BS_INT_NULL;
    assert(bs_nature_cardinality(integer_nature, &integer_cardinality) == BS_SETS_TYPE_ERROR);

    bs_value big_positive, big_negative, parsed_again, small_again;
    assert(bs_value_from_decimal(universe, "9223372036854775808", &big_positive) == BS_OK);
    assert(bs_value_from_decimal(universe, "-9223372036854775809", &big_negative) == BS_OK);
    assert(bs_value_as_integer(big_positive, &extracted_integer) == BS_NUM_OVERFLOW);
    assert(bs_value_compare(integer_max, big_positive, &comparison) == BS_OK && comparison < 0);
    assert(bs_value_compare(big_negative, integer_min, &comparison) == BS_OK && comparison < 0);
    assert(bs_value_from_decimal(universe, "+00000042", &parsed_again) == BS_OK);
    assert(bs_value_from_integer(universe, 42, &small_again) == BS_OK);
    assert(bs_value_equal(parsed_again, small_again));
    assert(bs_value_from_decimal(universe, "-0", &parsed_again) == BS_OK);
    assert(bs_value_equal(parsed_again, integer_zero));
    assert(bs_value_from_decimal(universe, "", &parsed_again) == BS_NUM_PARSE_ERROR);
    assert(bs_value_from_decimal(universe, "+", &parsed_again) == BS_NUM_PARSE_ERROR);
    assert(bs_value_from_decimal(universe, " 1", &parsed_again) == BS_NUM_PARSE_ERROR);
    assert(bs_value_from_decimal(universe, "1x", &parsed_again) == BS_NUM_PARSE_ERROR);
    size_t required = 0;
    assert(bs_value_format_integer(big_positive, NULL, 0, &required) == BS_OK);
    assert(required == strlen("9223372036854775808") + 1);
    char decimal_buffer[64];
    assert(bs_value_format_integer(big_positive, decimal_buffer, sizeof(decimal_buffer), &required) == BS_OK);
    assert(strcmp(decimal_buffer, "9223372036854775808") == 0);
    assert(bs_value_format_integer(big_negative, decimal_buffer, sizeof(decimal_buffer), &required) == BS_OK);
    assert(strcmp(decimal_buffer, "-9223372036854775809") == 0);
    assert(bs_value_format_integer(big_positive, decimal_buffer, 2, &required) == BS_SETS_OUT_OF_BOUNDS);

    const bs_set *integer_nature_set = NULL;
    assert(bs_set_nature(store, integer_nature, &integer_nature_set) == BS_OK);
    bs_truth nature_set_finite = BS_TRUE;
    assert(bs_set_is_finite(integer_nature_set, &nature_set_finite) == BS_OK);
    assert(nature_set_finite == BS_FALSE);
    assert(bs_set_cardinality(integer_nature_set, &integer_cardinality) == BS_SETS_TYPE_ERROR);
    bs_iterator *integer_nature_iterator = NULL;
    assert(bs_iterator_create(integer_nature_set, &integer_nature_iterator) == BS_OK);
    bs_value first_integer, iterated_integer;
    assert(bs_iterator_next(integer_nature_iterator, &first_integer) == BS_OK);
    assert(bs_value_equal(first_integer, integer_zero));
    assert(bs_iterator_next(integer_nature_iterator, &first_integer) == BS_OK);
    assert(bs_value_as_integer(first_integer, &extracted_integer) == BS_OK && extracted_integer == 1);
    assert(bs_iterator_next(integer_nature_iterator, &first_integer) == BS_OK);
    assert(bs_value_as_integer(first_integer, &extracted_integer) == BS_OK && extracted_integer == -1);
    bs_iterator_destroy(integer_nature_iterator);

    const bs_set *integer_interval = NULL;
    bs_value integer_low, integer_high;
    assert(bs_value_from_integer(universe, -2, &integer_low) == BS_OK);
    assert(bs_value_from_integer(universe, 2, &integer_high) == BS_OK);
    assert(bs_set_interval(store, &integer_low, true, &integer_high, true,
                               &integer_interval) == BS_OK);
    assert(bs_set_cardinality(integer_interval, &integer_cardinality) == BS_OK);
    assert(assert_card_u64(integer_cardinality, 5));
    const bs_set *wide_integer_interval = NULL;
    assert(bs_set_interval(store, &integer_min, true, &integer_max, true,
                               &wide_integer_interval) == BS_OK);
    assert(bs_set_cardinality(wide_integer_interval, &integer_cardinality) == BS_OK);
    bs_int expected_2_64 = BS_INT_NULL;
    assert(bs_int_from_str("18446744073709551616", 10, &expected_2_64));
    assert(bs_int_eq(integer_cardinality, expected_2_64));
    bs_int_release(expected_2_64);
    bs_int_release(integer_cardinality);
    bs_value cardinality_upper;
    const bs_set *max_uint64_cardinality_interval = NULL;
    assert(bs_value_from_decimal(universe, "18446744073709551614", &cardinality_upper) == BS_OK);
    assert(bs_set_interval(store, &integer_zero, true, &cardinality_upper, true,
                               &max_uint64_cardinality_interval) == BS_OK);
    assert(bs_set_cardinality(max_uint64_cardinality_interval, &integer_cardinality) == BS_OK);
    assert(assert_card_u64(integer_cardinality, UINT64_MAX));
    assert(bs_value_from_decimal(universe, "18446744073709551615", &cardinality_upper) == BS_OK);
    assert(bs_set_interval(store, &integer_zero, true, &cardinality_upper, true,
                               &max_uint64_cardinality_interval) == BS_OK);
    assert(bs_set_cardinality(max_uint64_cardinality_interval, &integer_cardinality) == BS_OK);
    assert(bs_int_from_str("18446744073709551616", 10, &expected_2_64));
    assert(bs_int_eq(integer_cardinality, expected_2_64));
    bs_int_release(expected_2_64);
    bs_int_release(integer_cardinality);

    /* Huge interval with bignum bounds and cardinality */
    bs_value huge_val;
    const bs_set *huge_interval = NULL;
    assert(bs_value_from_decimal(universe, "1000000000000000000000000000000", &huge_val) == BS_OK);
    assert(bs_set_interval(store, &integer_zero, true, &huge_val, true, &huge_interval) == BS_OK);
    assert(bs_set_cardinality(huge_interval, &integer_cardinality) == BS_OK);
    bs_int expected_huge = BS_INT_NULL;
    assert(bs_int_from_str("1000000000000000000000000000001", 10, &expected_huge));
    assert(bs_int_eq(integer_cardinality, expected_huge));
    bs_int_release(expected_huge);
    bs_int_release(integer_cardinality);

    const bs_set *integer_min_prefix = NULL;
    assert(bs_set_interval(store, NULL, false, &integer_min, true,
                               &integer_min_prefix) == BS_OK);
    bs_truth prefix_finite = BS_TRUE;
    assert(bs_set_is_finite(integer_min_prefix, &prefix_finite) == BS_OK);
    assert(prefix_finite == BS_FALSE);
    bs_iterator *integer_iterator = NULL;
    assert(bs_iterator_create(integer_min_prefix, &integer_iterator) == BS_OK);
    assert(bs_iterator_next(integer_iterator, &iterated_integer) == BS_OK);
    assert(bs_value_equal(iterated_integer, integer_min));
    assert(bs_iterator_next(integer_iterator, &iterated_integer) == BS_OK);
    assert(bs_value_from_decimal(universe, "-9223372036854775809", &big_negative) == BS_OK);
    assert(bs_value_equal(iterated_integer, big_negative));
    bs_iterator_destroy(integer_iterator);

    const bs_set *negative_integer_ray = NULL;
    bs_value negative_ray_upper;
    assert(bs_value_from_integer(universe, -3, &negative_ray_upper) == BS_OK);
    assert(bs_set_interval(store, NULL, false, &negative_ray_upper, true,
                               &negative_integer_ray) == BS_OK);
    assert(bs_iterator_create(negative_integer_ray, &integer_iterator) == BS_OK);
    assert(bs_iterator_next(integer_iterator, &iterated_integer) == BS_OK);
    assert(bs_value_as_integer(iterated_integer, &extracted_integer) == BS_OK && extracted_integer == -3);
    assert(bs_iterator_next(integer_iterator, &iterated_integer) == BS_OK);
    assert(bs_value_as_integer(iterated_integer, &extracted_integer) == BS_OK && extracted_integer == -4);
    bs_iterator_destroy(integer_iterator);

    const bs_set *big_interval = NULL;
    bs_value big_low, big_high;
    assert(bs_value_from_decimal(universe, "999999999999999999999", &big_low) == BS_OK);
    assert(bs_value_from_decimal(universe, "1000000000000000000001", &big_high) == BS_OK);
    assert(bs_set_interval(store, &big_low, true, &big_high, true, &big_interval) == BS_OK);
    assert(bs_set_cardinality(big_interval, &integer_cardinality) == BS_OK);
    assert(assert_card_u64(integer_cardinality, 3));
    assert(bs_iterator_create(big_interval, &integer_iterator) == BS_OK);
    assert(bs_iterator_next(integer_iterator, &iterated_integer) == BS_OK);
    assert(bs_value_equal(iterated_integer, big_low));
    assert(bs_iterator_next(integer_iterator, &iterated_integer) == BS_OK);
    assert(bs_value_from_decimal(universe, "1000000000000000000000", &parsed_again) == BS_OK);
    assert(bs_value_equal(iterated_integer, parsed_again));
    bs_iterator_destroy(integer_iterator);

    /* Test bs_value_compare */
    assert(bs_value_compare(values[0], values[1], &comparison) == BS_OK);
    assert(comparison < 0);
    assert(bs_value_compare(values[2], values[1], &comparison) == BS_OK);
    assert(comparison > 0);
    assert(bs_value_compare(values[1], values[1], &comparison) == BS_OK);
    assert(comparison == 0);
    assert(bs_value_compare(values[0], meta_value, &comparison) == BS_SETS_TYPE_ERROR);
    assert(bs_value_compare(meta_value, meta_value, &comparison) == BS_OK);
    assert(comparison == 0);

    bs_value nature_value;
    assert(bs_nature_as_value(nature, &nature_value) == BS_OK);
    assert(bs_value_as_nature(nature_value, &round_trip_nature) == BS_OK);
    assert(round_trip_nature == nature);
    assert(!bs_value_equal(nature_value, meta_value));
    assert(bs_value_as_nature(values[0], &round_trip_nature) == BS_SETS_TYPE_ERROR);
    assert(bs_value_as_minted_index(nature_value, &dummy_index) == BS_SETS_TYPE_ERROR);
    assert(bs_value_compare(nature_value, meta_value, &comparison) == BS_SETS_TYPE_ERROR);

    const bs_nature *other_universe_nature = NULL;
    assert(bs_nature_mint_finite(other_universe, 1, &other_universe_nature) == BS_OK);
    bs_value other_universe_nature_value;
    assert(bs_nature_as_value(other_universe_nature, &other_universe_nature_value) == BS_OK);
    assert(!bs_value_equal(nature_value, other_universe_nature_value));
    assert(bs_value_nature(nature_value) == nature_of_natures);
    bool eq = bs_value_equal(values[1], values[1]);
    assert(eq);
    assert(!bs_value_equal(values[0], values[1]));

    bs_nature *open_nature = NULL;
    bs_value open_values[2];
    assert(bs_nature_mint_nonfinite(universe, &open_nature) == BS_OK);
    assert(bs_nature_is_finite(open_nature, &is_finite) == BS_OK);
    assert(!is_finite);
    assert(bs_nature_mint_value(universe, open_nature, &open_values[0]) == BS_OK);
    assert(bs_nature_mint_value(universe, open_nature, &open_values[1]) == BS_OK);
    uint64_t open_index = 0;
    assert(bs_value_as_minted_index(open_values[0], &open_index) == BS_OK && open_index == 0);
    assert(bs_value_as_minted_index(open_values[1], &open_index) == BS_OK && open_index == 1);
    assert(bs_value_compare(open_values[0], open_values[1], &comparison) == BS_OK);
    assert(comparison < 0);

    const bs_set *empty = NULL;
    const bs_set *enumerated = NULL;
    const bs_set *singleton = NULL;
    assert(bs_set_enumerated(store, NULL, 0, &empty) == BS_OK);
    assert(bs_set_enumerated(store, values, 3, &enumerated) == BS_OK);
    const bs_set *nature_values = NULL;
    assert(bs_set_enumerated(store, &nature_value, 1, &nature_values) == BS_OK);
    assert(contains(nature_values, nature_value) == BS_TRUE);
    assert(contains(nature_values, meta_value) == BS_FALSE);
    bs_value duplicate_values[] = { values[1], values[1] };
    assert(bs_set_enumerated(store, duplicate_values, 2, &singleton) == BS_OK);
    assert(contains(empty, values[0]) == BS_FALSE);
    assert(contains(enumerated, values[2]) == BS_TRUE);
    assert(contains(singleton, values[1]) == BS_TRUE);
    assert(contains(singleton, values[0]) == BS_FALSE);

    /* Exercise enumerated sets through their semantic interface. */
    bs_int enum_cardinality = BS_INT_NULL;
    assert(bs_set_cardinality(enumerated, &enum_cardinality) == BS_OK);
    assert(assert_card_u64(enum_cardinality, 3));
    bs_iterator *enum_iterator = NULL;
    bs_value enum_value;
    assert(bs_iterator_create(enumerated, &enum_iterator) == BS_OK);
    assert(bs_iterator_next(enum_iterator, &enum_value) == BS_OK);
    assert(bs_value_equal(enum_value, values[0]));
    bs_iterator_destroy(enum_iterator);

    /* Test bs_set_nature (First-Class Nature Set) */
    const bs_set *nature_set = NULL;
    assert(bs_set_nature(store, nature, &nature_set) == BS_OK);
    assert(contains(nature_set, values[0]) == BS_TRUE);
    assert(contains(nature_set, values[1]) == BS_TRUE);
    assert(contains(nature_set, values[2]) == BS_TRUE);
    assert(contains(nature_set, open_values[0]) == BS_FALSE);
    assert(contains(nature_set, meta_value) == BS_FALSE);

    const bs_set *mixed_types = NULL;
    assert(bs_set_union(store, enumerated, nature_values, &mixed_types) == BS_OK);
    const bs_set *disjoint_intersection = NULL;
    assert(bs_set_intersection(store, enumerated, nature_values, &disjoint_intersection) == BS_OK);
    assert(contains(disjoint_intersection, values[0]) == BS_FALSE);

    const bs_nature *other_nature = NULL;
    bs_value other_value;
    assert(bs_nature_mint_finite(universe, 1, &other_nature) == BS_OK);
    assert(bs_nature_get_minted_value(other_nature, 0, &other_value) == BS_OK);
    assert(contains(singleton, other_value) == BS_FALSE);

    const bs_set *not_one = NULL;
    const bs_set *not_not_one = NULL;
    assert(bs_set_complement(store, singleton, &not_one) == BS_OK);
    assert(bs_set_complement(store, not_one, &not_not_one) == BS_OK);
    assert(contains(not_one, values[1]) == BS_FALSE);
    assert(contains(not_one, values[0]) == BS_TRUE);
    assert(contains(not_not_one, values[1]) == BS_TRUE);
    assert(contains(not_not_one, values[0]) == BS_FALSE);

    predicate_state true_pred = { .status = BS_OK, .result = BS_TRUE };
    predicate_state false_pred = { .status = BS_OK, .result = BS_FALSE };
    predicate_state undecided_pred = { .status = BS_OK, .result = BS_UNDECIDED };
    const bs_set *pred_true = make_predicate(store, NULL, &true_pred);
    const bs_set *pred_false = make_predicate(store, NULL, &false_pred);
    const bs_set *pred_undecided = make_predicate(store, NULL, &undecided_pred);

    const bs_set *truth_sets[] = { pred_false, pred_true, pred_undecided };
    const bs_truth truth_values[] = { BS_FALSE, BS_TRUE, BS_UNDECIDED };
    const bs_truth union_expected[3][3] = {
        { BS_FALSE, BS_TRUE, BS_UNDECIDED },
        { BS_TRUE, BS_TRUE, BS_TRUE },
        { BS_UNDECIDED, BS_TRUE, BS_UNDECIDED }
    };
    const bs_truth intersection_expected[3][3] = {
        { BS_FALSE, BS_FALSE, BS_FALSE },
        { BS_FALSE, BS_TRUE, BS_UNDECIDED },
        { BS_FALSE, BS_UNDECIDED, BS_UNDECIDED }
    };
    const bs_truth difference_expected[3][3] = {
        { BS_FALSE, BS_FALSE, BS_FALSE },
        { BS_TRUE, BS_FALSE, BS_UNDECIDED },
        { BS_UNDECIDED, BS_FALSE, BS_UNDECIDED }
    };
    for (size_t left = 0; left < 3; ++left) {
        for (size_t right = 0; right < 3; ++right) {
            const bs_set *combined = NULL;
            assert(bs_set_union(store, truth_sets[left], truth_sets[right], &combined) == BS_OK);
            assert(contains(combined, values[0]) == union_expected[left][right]);
            assert(bs_set_intersection(store, truth_sets[left], truth_sets[right], &combined) == BS_OK);
            assert(contains(combined, values[0]) == intersection_expected[left][right]);
            assert(bs_set_difference(store, truth_sets[left], truth_sets[right], &combined) == BS_OK);
            assert(contains(combined, values[0]) == difference_expected[left][right]);
        }
        const bs_set *complement = NULL;
        assert(bs_set_complement(store, truth_sets[left], &complement) == BS_OK);
        bs_truth expected_complement = truth_values[left] == BS_TRUE ? BS_FALSE
                                      : truth_values[left] == BS_FALSE ? BS_TRUE
                                      : BS_UNDECIDED;
        assert(contains(complement, values[0]) == expected_complement);
    }

    const bs_set *union_set = NULL;
    const bs_set *intersection_set = NULL;
    size_t undecided_calls_before = undecided_pred.calls;
    assert(bs_set_union(store, pred_true, pred_undecided, &union_set) == BS_OK);
    assert(contains(union_set, values[0]) == BS_TRUE);
    assert(undecided_pred.calls == undecided_calls_before); /* true short-circuits union */
    assert(bs_set_union(store, pred_false, pred_undecided, &union_set) == BS_OK);
    assert(contains(union_set, values[0]) == BS_UNDECIDED);
    assert(bs_set_union(store, pred_false, empty, &union_set) == BS_OK);
    assert(contains(union_set, values[0]) == BS_FALSE);

    assert(bs_set_intersection(store, pred_false, pred_undecided, &intersection_set) == BS_OK);
    size_t undecided_calls = undecided_pred.calls;
    assert(contains(intersection_set, values[0]) == BS_FALSE);
    assert(undecided_pred.calls == undecided_calls); /* false short-circuits intersection */
    assert(bs_set_intersection(store, pred_true, pred_undecided, &intersection_set) == BS_OK);
    assert(contains(intersection_set, values[0]) == BS_UNDECIDED);
    assert(bs_set_intersection(store, pred_true, pred_true, &intersection_set) == BS_OK);
    assert(contains(intersection_set, values[0]) == BS_TRUE);

    const bs_set *not_undecided = NULL;
    assert(bs_set_complement(store, pred_undecided, &not_undecided) == BS_OK);
    assert(contains(not_undecided, values[0]) == BS_UNDECIDED);

    predicate_state bound_undecided = {
        .status = BS_OK, .result = BS_UNDECIDED
    };
    const bs_set *bound_u = make_predicate(store, NULL, &bound_undecided);
    const bs_set *bounded_false = make_predicate(store, bound_u, &false_pred);
    assert(contains(bounded_false, values[0]) == BS_FALSE);

    const bs_set *bounded_true = make_predicate(store, singleton, &true_pred);
    size_t true_calls = true_pred.calls;
    assert(contains(bounded_true, values[0]) == BS_FALSE);
    assert(true_pred.calls == true_calls);
    assert(contains(bounded_true, values[1]) == BS_TRUE);
    assert(true_pred.calls == true_calls + 1);
    size_t false_calls = false_pred.calls;
    assert(contains(bounded_true, values[0]) == BS_FALSE);
    assert(false_pred.calls == false_calls);

    predicate_state error_pred = {
        .status = BS_SETS_CALLBACK_ERROR, .result = BS_TRUE
    };
    const bs_set *pred_error = make_predicate(store, NULL, &error_pred);
    bs_truth result = BS_UNDECIDED;
    assert(bs_set_contains(pred_error, values[0], &result) == BS_SETS_CALLBACK_ERROR);

    bs_set_store *other_store = NULL;
    assert(bs_set_store_create(other_universe, &other_store) == BS_OK);
    const bs_set *invalid_composite = NULL;
    assert(bs_set_union(other_store, empty, singleton, &invalid_composite) == BS_INVALID_ARGUMENT);
    const bs_set *cross_universe_values = NULL;
    assert(bs_set_enumerated(other_store, values, 1, &cross_universe_values) == BS_INVALID_ARGUMENT);
    bs_set_store_destroy(other_store);

    /* Test context destructor in bs_set_predicate */
    bs_set_store *destructor_test_store = NULL;
    assert(bs_set_store_create(universe, &destructor_test_store) == BS_OK);
    int destructor_invocations = 0;
    const bs_set *pred_with_dtor = NULL;
    assert(bs_set_predicate(
        destructor_test_store,
        NULL,
        fixed_predicate,
        &destructor_invocations,
        count_destructor,
        &pred_with_dtor
    ) == BS_OK);
    assert(destructor_invocations == 0);
    bs_set_store_destroy(destructor_test_store);
    assert(destructor_invocations == 1);

    /* Test bs_nature_is_enumerable */
    bool nat_enum = false;
    assert(bs_nature_is_enumerable(nature, &nat_enum) == BS_OK);
    assert(nat_enum);
    assert(bs_nature_is_enumerable(open_nature, &nat_enum) == BS_OK);
    assert(nat_enum);

    /* Test bs_set_is_finite and bs_set_is_enumerable */
    const bs_set *open_nature_set = NULL;
    assert(bs_set_nature(store, open_nature, &open_nature_set) == BS_OK);

    bs_truth query_truth = BS_UNDECIDED;
    assert(bs_set_is_finite(empty, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);
    assert(bs_set_is_enumerable(empty, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);

    assert(bs_set_is_finite(enumerated, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);
    assert(bs_set_is_enumerable(enumerated, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);

    assert(bs_set_is_finite(nature_set, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);
    assert(bs_set_is_enumerable(nature_set, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);

    assert(bs_set_is_finite(open_nature_set, &query_truth) == BS_OK);
    assert(query_truth == BS_FALSE);
    assert(bs_set_is_enumerable(open_nature_set, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);

    assert(bs_set_is_finite(bounded_true, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);
    assert(bs_set_is_enumerable(bounded_true, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);

    assert(bs_set_is_finite(pred_true, &query_truth) == BS_OK);
    assert(query_truth == BS_UNDECIDED);
    assert(bs_set_is_enumerable(pred_true, &query_truth) == BS_OK);
    assert(query_truth == BS_FALSE);

    assert(bs_set_is_enumerable(not_one, &query_truth) == BS_OK);
    assert(query_truth == BS_FALSE);

    /* Test bs_set_is_discrete */
    const bs_set *meta_nature_set = NULL;
    assert(bs_set_nature(store, nature_of_natures, &meta_nature_set) == BS_OK);
    assert(bs_set_is_discrete(empty, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);
    assert(bs_set_is_discrete(enumerated, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);
    assert(bs_set_is_discrete(nature_set, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);
    assert(bs_set_is_discrete(open_nature_set, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);
    assert(bs_set_is_discrete(integer_nature_set, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);
    assert(bs_set_is_discrete(meta_nature_set, &query_truth) == BS_OK);
    assert(query_truth == BS_FALSE);
    assert(bs_set_is_discrete(bounded_true, &query_truth) == BS_OK);
    assert(query_truth == BS_TRUE);
    assert(bs_set_is_discrete(pred_true, &query_truth) == BS_OK);
    assert(query_truth == BS_UNDECIDED);
    assert(bs_set_is_discrete(not_one, &query_truth) == BS_OK);
    assert(query_truth == BS_UNDECIDED);
    assert(bs_set_is_discrete(NULL, &query_truth) == BS_INVALID_ARGUMENT);
    assert(bs_set_is_discrete(empty, NULL) == BS_INVALID_ARGUMENT);

    /* Test bs_set_cardinality */
    bs_int set_card = BS_INT_NULL;
    assert(bs_set_cardinality(empty, &set_card) == BS_OK);
    assert(assert_card_u64(set_card, 0));
    assert(bs_set_cardinality(singleton, &set_card) == BS_OK);
    assert(assert_card_u64(set_card, 1));
    assert(bs_set_cardinality(enumerated, &set_card) == BS_OK);
    assert(assert_card_u64(set_card, 3));
    assert(bs_set_cardinality(nature_set, &set_card) == BS_OK);
    assert(assert_card_u64(set_card, 3));
    assert(bs_set_cardinality(bounded_true, &set_card) == BS_OK);
    assert(assert_card_u64(set_card, 1));
    assert(bs_set_cardinality(open_nature_set, &set_card) == BS_SETS_TYPE_ERROR);

    /* Test Iterator: Empty set */
    bs_iterator *it = NULL;
    assert(bs_iterator_create(empty, &it) == BS_OK);
    bs_value it_val;
    assert(bs_iterator_next(it, &it_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(it);

    /* Test Iterator: Enumerated set */
    assert(bs_iterator_create(enumerated, &it) == BS_OK);
    for (size_t i = 0; i < 3; ++i) {
        assert(bs_iterator_next(it, &it_val) == BS_OK);
        assert(bs_value_equal(it_val, values[i]));
    }
    assert(bs_iterator_next(it, &it_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(it);

    /* Test Iterator: Nature set (finite) */
    assert(bs_iterator_create(nature_set, &it) == BS_OK);
    for (size_t i = 0; i < 3; ++i) {
        assert(bs_iterator_next(it, &it_val) == BS_OK);
        assert(bs_value_equal(it_val, values[i]));
    }
    assert(bs_iterator_next(it, &it_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(it);

    /* Test Iterator: Open Nature set (infinite-yet-enumerable) */
    assert(bs_iterator_create(open_nature_set, &it) == BS_OK);
    for (uint64_t i = 0; i < 5; ++i) {
        assert(bs_iterator_next(it, &it_val) == BS_OK);
        assert(bs_value_nature(it_val) == open_nature);
        uint64_t idx = 999;
        assert(bs_value_as_minted_index(it_val, &idx) == BS_OK);
        assert(idx == i);
    }
    bs_iterator_destroy(it);

    /* Test Iterator: Bounded predicate set */
    assert(bs_iterator_create(bounded_true, &it) == BS_OK);
    assert(bs_iterator_next(it, &it_val) == BS_OK);
    assert(bs_value_equal(it_val, values[1]));
    assert(bs_iterator_next(it, &it_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(it);

    /* Test Iterator: Union of enumerable sets */
    assert(bs_iterator_create(mixed_types, &it) == BS_OK);
    size_t union_count = 0;
    while (bs_iterator_next(it, &it_val) == BS_OK) {
        ++union_count;
    }
    assert(union_count == 4); /* 3 values from enumerated + 1 from nature_values */
    bs_iterator_destroy(it);

    /* Test Iterator: Intersection of infinite enumerable set and finite set (both orders) */
    const bs_set *singleton_int = NULL;
    assert(bs_set_enumerated(store, &integer_negative, 1, &singleton_int) == BS_OK);

    /* Infinite on left, finite on right */
    const bs_set *isect_inf_left = NULL;
    assert(bs_set_intersection(store, integer_nature_set, singleton_int, &isect_inf_left) == BS_OK);
    bs_truth isect_finite = BS_UNDECIDED;
    assert(bs_set_is_finite(isect_inf_left, &isect_finite) == BS_OK);
    assert(isect_finite == BS_TRUE);
    bs_int isect_card = BS_INT_NULL;
    assert(bs_set_cardinality(isect_inf_left, &isect_card) == BS_OK);
    assert(assert_card_u64(isect_card, 1));
    assert(bs_iterator_create(isect_inf_left, &it) == BS_OK);
    assert(bs_iterator_next(it, &it_val) == BS_OK);
    assert(bs_value_equal(it_val, integer_negative));
    assert(bs_iterator_next(it, &it_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(it);

    /* Finite on left, infinite on right */
    const bs_set *isect_inf_right = NULL;
    assert(bs_set_intersection(store, singleton_int, integer_nature_set, &isect_inf_right) == BS_OK);
    assert(bs_set_cardinality(isect_inf_right, &isect_card) == BS_OK);
    assert(assert_card_u64(isect_card, 1));
    assert(bs_iterator_create(isect_inf_right, &it) == BS_OK);
    assert(bs_iterator_next(it, &it_val) == BS_OK);
    assert(bs_value_equal(it_val, integer_negative));
    assert(bs_iterator_next(it, &it_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(it);

    /* Unbounded ray on left, enumerated set on right */
    bs_value sample_ints[3];
    assert(bs_value_from_integer(universe, -5, &sample_ints[0]) == BS_OK);
    assert(bs_value_from_integer(universe, 0, &sample_ints[1]) == BS_OK);
    assert(bs_value_from_integer(universe, 5, &sample_ints[2]) == BS_OK);
    const bs_set *sample_int_set = NULL;
    assert(bs_set_enumerated(store, sample_ints, 3, &sample_int_set) == BS_OK);

    const bs_set *isect_ray = NULL;
    assert(bs_set_intersection(store, negative_integer_ray, sample_int_set, &isect_ray) == BS_OK);
    assert(bs_set_cardinality(isect_ray, &isect_card) == BS_OK);
    assert(assert_card_u64(isect_card, 1)); /* only -5 is <= -3 */
    assert(bs_iterator_create(isect_ray, &it) == BS_OK);
    assert(bs_iterator_next(it, &it_val) == BS_OK);
    assert(bs_value_equal(it_val, sample_ints[0]));
    assert(bs_iterator_next(it, &it_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(it);

    /* Both sides finite: interval [-2, 2] and sample ints {-5, 0, 5} */
    const bs_set *isect_both_finite = NULL;
    assert(bs_set_intersection(store, integer_interval, sample_int_set, &isect_both_finite) == BS_OK);
    assert(bs_set_cardinality(isect_both_finite, &isect_card) == BS_OK);
    assert(assert_card_u64(isect_card, 1)); /* only 0 is in [-2, 2] */
    assert(bs_iterator_create(isect_both_finite, &it) == BS_OK);
    assert(bs_iterator_next(it, &it_val) == BS_OK);
    assert(bs_value_equal(it_val, sample_ints[1]));
    assert(bs_iterator_next(it, &it_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(it);

    /* Test Iterator: Non-enumerable complement & unbounded predicate fails */
    assert(bs_iterator_create(not_one, &it) == BS_SETS_TYPE_ERROR);
    assert(bs_iterator_create(pred_true, &it) == BS_SETS_TYPE_ERROR);

    /* Test bs_set_for_each */
    int for_each_items = 0;
    assert(bs_set_for_each(enumerated, count_for_each, &for_each_items) == BS_OK);
    assert(for_each_items == 2); /* broke early after 2 */

    /* Test bs_set_interval */
    const bs_set *interval_closed = NULL;
    const bs_set *interval_open = NULL;
    const bs_set *interval_half_left = NULL;
    const bs_set *interval_half_right = NULL;
    assert(bs_set_interval(store, &values[0], true, &values[2], true, &interval_closed) == BS_OK);
    assert(bs_set_interval(store, &values[0], false, &values[2], false, &interval_open) == BS_OK);
    assert(bs_set_interval(store, &values[0], true, &values[2], false, &interval_half_left) == BS_OK);
    assert(bs_set_interval(store, &values[0], false, &values[2], true, &interval_half_right) == BS_OK);

    /* Membership */
    assert(contains(interval_closed, values[0]) == BS_TRUE);
    assert(contains(interval_closed, values[1]) == BS_TRUE);
    assert(contains(interval_closed, values[2]) == BS_TRUE);
    assert(contains(interval_closed, open_values[0]) == BS_FALSE);

    assert(contains(interval_open, values[0]) == BS_FALSE);
    assert(contains(interval_open, values[1]) == BS_TRUE);
    assert(contains(interval_open, values[2]) == BS_FALSE);

    assert(contains(interval_half_left, values[0]) == BS_TRUE);
    assert(contains(interval_half_left, values[2]) == BS_FALSE);

    assert(contains(interval_half_right, values[0]) == BS_FALSE);
    assert(contains(interval_half_right, values[2]) == BS_TRUE);

    /* Finiteness and enumerability */
    bs_truth int_truth = BS_UNDECIDED;
    assert(bs_set_is_finite(interval_closed, &int_truth) == BS_OK);
    assert(int_truth == BS_TRUE);
    assert(bs_set_is_enumerable(interval_closed, &int_truth) == BS_OK);
    assert(int_truth == BS_TRUE);

    /* Cardinality */
    bs_int int_card = BS_INT_NULL;
    assert(bs_set_cardinality(interval_closed, &int_card) == BS_OK);
    assert(assert_card_u64(int_card, 3));
    assert(bs_set_cardinality(interval_open, &int_card) == BS_OK);
    assert(assert_card_u64(int_card, 1));
    assert(bs_set_cardinality(interval_half_left, &int_card) == BS_OK);
    assert(assert_card_u64(int_card, 2));
    assert(bs_set_cardinality(interval_half_right, &int_card) == BS_OK);
    assert(assert_card_u64(int_card, 2));

    /* Empty interval */
    const bs_set *empty_interval = NULL;
    assert(bs_set_interval(store, &values[1], false, &values[1], false, &empty_interval) == BS_OK);
    assert(bs_set_cardinality(empty_interval, &int_card) == BS_OK);
    assert(assert_card_u64(int_card, 0));
    assert(contains(empty_interval, values[1]) == BS_FALSE);

    /* Iteration over interval */
    bs_iterator *int_it = NULL;
    assert(bs_iterator_create(interval_closed, &int_it) == BS_OK);
    for (size_t i = 0; i < 3; ++i) {
        assert(bs_iterator_next(int_it, &it_val) == BS_OK);
        assert(bs_value_equal(it_val, values[i]));
    }
    assert(bs_iterator_next(int_it, &it_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(int_it);

    /* Unbounded interval from above on open_nature: [1, +infinity) */
    const bs_set *unbounded_above = NULL;
    assert(bs_set_interval(store, &open_values[1], true, NULL, false, &unbounded_above) == BS_OK);
    assert(bs_set_is_finite(unbounded_above, &int_truth) == BS_OK);
    assert(int_truth == BS_FALSE);
    assert(bs_set_is_enumerable(unbounded_above, &int_truth) == BS_OK);
    assert(int_truth == BS_TRUE);
    assert(bs_set_cardinality(unbounded_above, &int_card) == BS_SETS_TYPE_ERROR);
    assert(contains(unbounded_above, open_values[0]) == BS_FALSE);
    assert(contains(unbounded_above, open_values[1]) == BS_TRUE);

    assert(bs_iterator_create(unbounded_above, &int_it) == BS_OK);
    for (uint64_t i = 1; i <= 5; ++i) {
        assert(bs_iterator_next(int_it, &it_val) == BS_OK);
        uint64_t idx = 0;
        assert(bs_value_as_minted_index(it_val, &idx) == BS_OK);
        assert(idx == i);
    }
    bs_iterator_destroy(int_it);

    /* Unbounded interval from below: (-infinity, 1] */
    const bs_set *unbounded_below = NULL;
    assert(bs_set_interval(store, NULL, false, &open_values[1], true, &unbounded_below) == BS_OK);
    assert(bs_set_is_finite(unbounded_below, &int_truth) == BS_OK);
    assert(int_truth == BS_FALSE);
    assert(bs_set_is_enumerable(unbounded_below, &int_truth) == BS_OK);
    assert(int_truth == BS_FALSE);
    assert(bs_iterator_create(unbounded_below, &int_it) == BS_SETS_TYPE_ERROR);

    /* Invalid interval: NULL, NULL */
    const bs_set *invalid_int = NULL;
    assert(bs_set_interval(store, NULL, false, NULL, false, &invalid_int) == BS_INVALID_ARGUMENT);
    /* Incompatible natures */
    assert(bs_set_interval(store, &values[0], true, &open_values[0], true, &invalid_int) == BS_SETS_TYPE_ERROR);
    /* Inverted bounds: [2, 0] */
    assert(bs_set_interval(store, &values[2], true, &values[0], true, &invalid_int) == BS_INVALID_ARGUMENT);

    /* Test discreteness */
    bool is_discrete = false;
    assert(bs_nature_is_discrete(nature, &is_discrete) == BS_OK);
    assert(is_discrete);
    assert(bs_nature_is_discrete(open_nature, &is_discrete) == BS_OK);
    assert(is_discrete);
    assert(bs_nature_is_discrete(nature_of_natures, &is_discrete) == BS_OK);
    assert(!is_discrete);
    assert(bs_nature_is_discrete(NULL, &is_discrete) == BS_INVALID_ARGUMENT);

    /* Test handle-to-universe queries */
    assert(bs_nature_universe(nature) == universe);
    assert(bs_nature_universe(nature_of_natures) == universe);
    assert(bs_nature_universe(other_universe_nature) == other_universe);
    assert(bs_nature_universe(NULL) == NULL);

    assert(bs_set_store_universe(store) == universe);
    assert(bs_set_store_universe(NULL) == NULL);

    assert(bs_set_universe(interval_closed) == universe);
    assert(bs_set_universe(NULL) == NULL);

    /* Test named built-in Nature accessors. */
    assert(bs_universe_nature_of_natures(universe) == nature_of_natures);
    assert(bs_universe_integer_nature(universe) == integer_nature);
    assert(bs_universe_integer_nature(NULL) == NULL);

    /* Composite sets are checked through membership alone. */
    const bs_set *test_union = NULL;
    assert(bs_set_union(store, empty, singleton, &test_union) == BS_OK);
    assert(contains(test_union, values[1]) == BS_TRUE);
    assert(contains(test_union, values[0]) == BS_FALSE);

    const bs_set *test_isect = NULL;
    assert(bs_set_intersection(store, empty, singleton, &test_isect) == BS_OK);
    assert(contains(test_isect, values[1]) == BS_FALSE);

    const bs_set *test_comp = NULL;
    assert(bs_set_complement(store, singleton, &test_comp) == BS_OK);
    assert(contains(test_comp, values[1]) == BS_FALSE);
    assert(contains(test_comp, values[0]) == BS_TRUE);

    const bs_set *test_pred = make_predicate(store, singleton, &true_pred);
    assert(contains(test_pred, values[1]) == BS_TRUE);
    assert(contains(test_pred, values[0]) == BS_FALSE);

    /* Test bs_set_difference */
    const bs_set *test_diff = NULL;
    assert(bs_set_difference(store, enumerated, singleton, &test_diff) == BS_OK);
    assert(contains(test_diff, values[0]) == BS_TRUE);
    assert(contains(test_diff, values[1]) == BS_FALSE);
    assert(contains(test_diff, values[2]) == BS_TRUE);

    /* Finiteness, enumerability, discreteness, cardinality, and iterator of difference */
    bs_truth diff_finite = BS_UNDECIDED;
    assert(bs_set_is_finite(test_diff, &diff_finite) == BS_OK && diff_finite == BS_TRUE);
    bs_truth diff_enum = BS_UNDECIDED;
    assert(bs_set_is_enumerable(test_diff, &diff_enum) == BS_OK && diff_enum == BS_TRUE);
    bs_truth diff_disc = BS_UNDECIDED;
    assert(bs_set_is_discrete(test_diff, &diff_disc) == BS_OK && diff_disc == BS_TRUE);

    bs_int diff_card = BS_INT_NULL;
    assert(bs_set_cardinality(test_diff, &diff_card) == BS_OK);
    assert(assert_card_u64(diff_card, 2));

    bs_iterator *diff_it = NULL;
    assert(bs_iterator_create(test_diff, &diff_it) == BS_OK);
    bs_value diff_val;
    assert(bs_iterator_next(diff_it, &diff_val) == BS_OK && bs_value_equal(diff_val, values[0]));
    assert(bs_iterator_next(diff_it, &diff_val) == BS_OK && bs_value_equal(diff_val, values[2]));
    assert(bs_iterator_next(diff_it, &diff_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(diff_it);

    /* Difference resulting in empty set */
    const bs_set *empty_diff = NULL;
    assert(bs_set_difference(store, singleton, enumerated, &empty_diff) == BS_OK);
    assert(bs_set_cardinality(empty_diff, &diff_card) == BS_OK);
    assert(assert_card_u64(diff_card, 0));
    assert(bs_iterator_create(empty_diff, &diff_it) == BS_OK);
    assert(bs_iterator_next(diff_it, &diff_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(diff_it);

    /* Infinite minus finite difference */
    const bs_set *inf_diff = NULL;
    assert(bs_set_difference(store, integer_nature_set, singleton_int, &inf_diff) == BS_OK);
    assert(bs_set_is_finite(inf_diff, &diff_finite) == BS_OK && diff_finite == BS_FALSE);
    assert(bs_set_is_enumerable(inf_diff, &diff_enum) == BS_OK && diff_enum == BS_TRUE);
    assert(bs_set_is_discrete(inf_diff, &diff_disc) == BS_OK && diff_disc == BS_TRUE);
    assert(contains(inf_diff, integer_negative) == BS_FALSE);
    assert(contains(inf_diff, integer_zero) == BS_TRUE);

    /* Invalid arguments for bs_set_difference */
    const bs_set *invalid_diff = NULL;
    assert(bs_set_difference(NULL, enumerated, singleton, &invalid_diff) == BS_INVALID_ARGUMENT);
    assert(bs_set_difference(store, NULL, singleton, &invalid_diff) == BS_INVALID_ARGUMENT);
    assert(bs_set_difference(store, enumerated, NULL, &invalid_diff) == BS_INVALID_ARGUMENT);
    assert(bs_set_difference(store, enumerated, singleton, NULL) == BS_INVALID_ARGUMENT);

    /* Test interval edge cases (generic stepping & interval cardinality) */
    /* Empty interval: [values[1], values[1]) */
    const bs_set *empty_interval_half_open = NULL;
    assert(bs_set_interval(store, &values[1], true, &values[1], false, &empty_interval_half_open) == BS_OK);
    bs_int empty_card = BS_INT_NULL;
    assert(bs_set_cardinality(empty_interval_half_open, &empty_card) == BS_OK);
    assert(assert_card_u64(empty_card, 0));
    bs_iterator *empty_it = NULL;
    assert(bs_iterator_create(empty_interval_half_open, &empty_it) == BS_OK);
    bs_value dummy_val;
    assert(bs_iterator_next(empty_it, &dummy_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(empty_it);

    /* Open interval (values[0], values[2]) = { values[1] } */
    const bs_set *single_interval = NULL;
    assert(bs_set_interval(store, &values[0], false, &values[2], false, &single_interval) == BS_OK);
    bs_int single_card = BS_INT_NULL;
    assert(bs_set_cardinality(single_interval, &single_card) == BS_OK);
    assert(assert_card_u64(single_card, 1));
    bs_iterator *single_it = NULL;
    assert(bs_iterator_create(single_interval, &single_it) == BS_OK);
    assert(bs_iterator_next(single_it, &dummy_val) == BS_OK);
    assert(bs_value_equal(dummy_val, values[1]));
    assert(bs_iterator_next(single_it, &dummy_val) == BS_SETS_EXHAUSTED);
    bs_iterator_destroy(single_it);

    bs_set_store_destroy(store);
    bs_universe_destroy(other_universe);
    bs_universe_destroy(universe);
    return 0;
}
