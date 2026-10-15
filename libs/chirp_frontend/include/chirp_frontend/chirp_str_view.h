/**
 * @file chirp_str_view.h
 * @brief Non-owning UTF-8 string slice for the Chirp frontend.
 */

#ifndef CHIRP_FRONTEND_CHIRP_STR_VIEW_H
#define CHIRP_FRONTEND_CHIRP_STR_VIEW_H

#include "bs_core.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Non-owning UTF-8 string slice. */
typedef struct chirp_str_view {
    const char *ptr;
    size_t len;
} chirp_str_view;

/** Creates a string view from a pointer and byte length. */
static inline chirp_str_view chirp_str_view_create(const char *ptr, size_t len) {
    chirp_str_view sv;
    sv.ptr = ptr;
    sv.len = len;
    return sv;
}

/** Creates a string view from a null-terminated C string. Safe for NULL (produces len 0). */
BS_API chirp_str_view chirp_str_view_from_cstr(const char *cstr);

/** Checks whether two string views have identical length and byte content. */
BS_API bool chirp_str_view_equals(chirp_str_view a, chirp_str_view b);

/** Checks whether a string view equals a null-terminated C string. */
BS_API bool chirp_str_view_equals_cstr(chirp_str_view a, const char *cstr);

/** Checks whether a string view starts with the given prefix. */
BS_API bool chirp_str_view_starts_with(chirp_str_view a, chirp_str_view prefix);

/** Three-way lexicographical comparison of two string views. */
BS_API int chirp_str_view_compare(chirp_str_view a, chirp_str_view b);

#ifdef __cplusplus
}
#endif

#endif /* CHIRP_FRONTEND_CHIRP_STR_VIEW_H */
