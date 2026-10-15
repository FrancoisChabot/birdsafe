/**
 * @file chirp_symbols.c
 * @brief Interned symbol table for Chirp Virtual Machine State.
 */

#include "chirp_vm_internal.h"
#include "bs_core/bs_alloc.h"
#include <string.h>

BS_API bs_status chirp_vm_intern_symbol(
    chirp_vm *vm,
    const char *name,
    chirp_symbol_t *out_symbol
) {
    assert(vm != NULL);
    assert(name != NULL);
    assert(out_symbol != NULL);

    /* Linear search for existing symbol */
    for (size_t i = 0; i < vm->symbol_count; ++i) {
        if (strcmp(vm->symbol_names[i], name) == 0) {
            *out_symbol = (chirp_symbol_t)(i + 1);
            return BS_OK;
        }
    }

    /* Grow symbol array if needed */
    if (vm->symbol_count >= vm->symbol_capacity) {
        size_t new_cap = vm->symbol_capacity == 0 ? 32 : vm->symbol_capacity * 2;
        char **new_names = (char **)bs_realloc(vm->symbol_names, new_cap * sizeof(char *));
        if (!new_names) {
            return BS_OUT_OF_MEMORY;
        }
        vm->symbol_names = new_names;
        vm->symbol_capacity = new_cap;
    }

    size_t len = strlen(name);
    char *copy = (char *)bs_alloc(len + 1);
    if (!copy) {
        return BS_OUT_OF_MEMORY;
    }
    memcpy(copy, name, len + 1);

    vm->symbol_names[vm->symbol_count] = copy;
    vm->symbol_count++;
    *out_symbol = (chirp_symbol_t)vm->symbol_count;
    return BS_OK;
}

BS_API const char *chirp_vm_symbol_name(const chirp_vm *vm, chirp_symbol_t symbol) {
    assert(vm != NULL);
    if (symbol == CHIRP_SYMBOL_NONE || (size_t)symbol > vm->symbol_count) {
        return NULL;
    }
    return vm->symbol_names[symbol - 1];
}
