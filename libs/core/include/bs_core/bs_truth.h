/**
 * @file bs_truth.h
 * @brief Three-valued logic representation for BirdSafe.
 */

#ifndef BS_CORE_BS_TRUTH_H
#define BS_CORE_BS_TRUTH_H

#include "bs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Three-valued logic states: false, true, or undecided/unknown. */
typedef enum bs_truth {
    BS_FALSE = 0,
    BS_TRUE = 1,
    BS_UNDECIDED = 2
} bs_truth;

/** Returns a static string representation ("false", "true", or "undecided"). */
BS_API const char *bs_truth_string(bs_truth truth);

#ifdef __cplusplus
}
#endif

#endif /* BS_CORE_BS_TRUTH_H */
