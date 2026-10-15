/**
 * @file chirp_str_view.c
 * @brief Implementation of chirp_str_view utilities.
 */

#include "chirp_frontend/chirp_str_view.h"
#include <string.h>

chirp_str_view chirp_str_view_from_cstr(const char *cstr) {
    chirp_str_view sv;
    if (cstr == NULL) {
        sv.ptr = "";
        sv.len = 0;
    } else {
        sv.ptr = cstr;
        sv.len = strlen(cstr);
    }
    return sv;
}

bool chirp_str_view_equals(chirp_str_view a, chirp_str_view b) {
    if (a.len != b.len) {
        return false;
    }
    if (a.len == 0) {
        return true;
    }
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

bool chirp_str_view_equals_cstr(chirp_str_view a, const char *cstr) {
    if (cstr == NULL) {
        return a.len == 0;
    }
    size_t cstr_len = strlen(cstr);
    if (a.len != cstr_len) {
        return false;
    }
    return memcmp(a.ptr, cstr, a.len) == 0;
}

bool chirp_str_view_starts_with(chirp_str_view a, chirp_str_view prefix) {
    if (a.len < prefix.len) {
        return false;
    }
    if (prefix.len == 0) {
        return true;
    }
    return memcmp(a.ptr, prefix.ptr, prefix.len) == 0;
}

int chirp_str_view_compare(chirp_str_view a, chirp_str_view b) {
    size_t min_len = a.len < b.len ? a.len : b.len;
    if (min_len > 0) {
        int cmp = memcmp(a.ptr, b.ptr, min_len);
        if (cmp != 0) {
            return cmp;
        }
    }
    if (a.len < b.len) return -1;
    if (a.len > b.len) return 1;
    return 0;
}
