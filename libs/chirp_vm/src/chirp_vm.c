/**
 * @file chirp_vm.c
 * @brief VM Engine Lifecycle and Intrinsic Registry.
 */

#include "chirp_vm_internal.h"
#include "bs_core/bs_alloc.h"
#include <string.h>

static char *chirp_strdup(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char *dst = (char *)bs_alloc(len + 1);
    if (dst) {
        memcpy(dst, src, len + 1);
    }
    return dst;
}

BS_API bs_status chirp_vm_create(bs_universe *universe, chirp_vm **out_vm) {
    assert(out_vm != NULL);

    chirp_vm *vm = (chirp_vm *)bs_alloc(sizeof(chirp_vm));
    if (!vm) {
        return BS_OUT_OF_MEMORY;
    }

    if (universe != NULL) {
        vm->universe = universe;
        vm->owns_universe = false;
    } else {
        bs_status status = bs_universe_create(&vm->universe);
        if (status != BS_OK) {
            bs_free(vm);
            return status;
        }
        vm->owns_universe = true;
    }

    vm->ref_count = 1;
    vm->root_scope = NULL;
    bs_status status = chirp_scope_create(NULL, &vm->root_scope);
    if (status != BS_OK) {
        if (vm->owns_universe) {
            bs_universe_destroy(vm->universe);
        }
        bs_free(vm);
        return status;
    }

    vm->symbol_names = NULL;
    vm->symbol_count = 0;
    vm->symbol_capacity = 0;

    vm->host_entries = NULL;
    vm->host_count = 0;
    vm->host_capacity = 0;

    *out_vm = vm;
    return BS_OK;
}

BS_API chirp_vm *chirp_vm_retain(chirp_vm *vm) {
    if (vm) {
        assert(vm->ref_count > 0);
        vm->ref_count++;
    }
    return vm;
}

BS_API void chirp_vm_release(chirp_vm *vm) {
    if (!vm) {
        return;
    }

    assert(vm->ref_count > 0);
    vm->ref_count--;
    if (vm->ref_count > 0) {
        return;
    }

    /* Free host intrinsic entries */
    for (size_t i = 0; i < vm->host_count; ++i) {
        bs_free(vm->host_entries[i].format);
        bs_free(vm->host_entries[i].key);
    }
    bs_free(vm->host_entries);

    /* Release root scope */
    if (vm->root_scope) {
        chirp_scope_release(vm->root_scope);
    }

    /* Free interned symbols */
    for (size_t i = 0; i < vm->symbol_count; ++i) {
        bs_free(vm->symbol_names[i]);
    }
    bs_free(vm->symbol_names);

    /* Destroy owned universe */
    if (vm->owns_universe && vm->universe) {
        bs_universe_destroy(vm->universe);
    }

    bs_free(vm);
}

BS_API void chirp_vm_destroy(chirp_vm *vm) {
    chirp_vm_release(vm);
}

BS_API bs_universe *chirp_vm_universe(const chirp_vm *vm) {
    assert(vm != NULL);
    return vm->universe;
}

BS_API chirp_scope *chirp_vm_root_scope(chirp_vm *vm) {
    assert(vm != NULL);
    return vm->root_scope;
}

BS_API bs_status chirp_vm_register_host_fn(
    chirp_vm *vm,
    const char *format,
    const char *key,
    chirp_host_fn fn,
    void *user_data
) {
    assert(vm != NULL);
    assert(format != NULL);
    assert(key != NULL);
    assert(fn != NULL);

    /* Update existing entry if already registered */
    for (size_t i = 0; i < vm->host_count; ++i) {
        if (strcmp(vm->host_entries[i].format, format) == 0 &&
            strcmp(vm->host_entries[i].key, key) == 0) {
            vm->host_entries[i].fn = fn;
            vm->host_entries[i].user_data = user_data;
            return BS_OK;
        }
    }

    /* Grow host entries array */
    if (vm->host_count >= vm->host_capacity) {
        size_t new_cap = vm->host_capacity == 0 ? 16 : vm->host_capacity * 2;
        chirp_host_entry *new_entries = (chirp_host_entry *)bs_realloc(
            vm->host_entries,
            new_cap * sizeof(chirp_host_entry)
        );
        if (!new_entries) {
            return BS_OUT_OF_MEMORY;
        }
        vm->host_entries = new_entries;
        vm->host_capacity = new_cap;
    }

    char *fmt_dup = chirp_strdup(format);
    char *key_dup = chirp_strdup(key);
    if (!fmt_dup || !key_dup) {
        bs_free(fmt_dup);
        bs_free(key_dup);
        return BS_OUT_OF_MEMORY;
    }

    chirp_host_entry *entry = &vm->host_entries[vm->host_count++];
    entry->format = fmt_dup;
    entry->key = key_dup;
    entry->fn = fn;
    entry->user_data = user_data;

    return BS_OK;
}

BS_API chirp_host_fn chirp_vm_lookup_host_fn(
    const chirp_vm *vm,
    const char *format,
    const char *key,
    void **out_user_data
) {
    assert(vm != NULL);
    assert(format != NULL);
    assert(key != NULL);

    for (size_t i = 0; i < vm->host_count; ++i) {
        if (strcmp(vm->host_entries[i].format, format) == 0 &&
            strcmp(vm->host_entries[i].key, key) == 0) {
            if (out_user_data) {
                *out_user_data = vm->host_entries[i].user_data;
            }
            return vm->host_entries[i].fn;
        }
    }
    return NULL;
}
