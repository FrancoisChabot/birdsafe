/**
 * @file chirp_vm_internal.h
 * @brief Internal declarations and layouts for Chirp Virtual Machine State.
 */

#ifndef CHIRP_VM_INTERNAL_H
#define CHIRP_VM_INTERNAL_H

#include "chirp_vm/chirp_vm.h"
#include <assert.h>
#include <string.h>

struct chirp_scope {
    chirp_scope *parent;
    chirp_binding **bindings;
    size_t count;
    size_t capacity;
    uint32_t ref_count;
    bool is_block;
    uint32_t block_id;
};

typedef struct chirp_host_entry {
    char *format;
    char *key;
    chirp_host_fn fn;
    void *user_data;
} chirp_host_entry;

struct chirp_vm {
    uint32_t ref_count;
    bs_universe *universe;
    bool owns_universe;
    chirp_scope *root_scope;

    /* Symbol interner table */
    char **symbol_names;
    size_t symbol_count;
    size_t symbol_capacity;

    /* Host intrinsic registry */
    chirp_host_entry *host_entries;
    size_t host_count;
    size_t host_capacity;
};

struct chirp_fiber {
    chirp_vm *vm;
    bs_set_store *store;

    chirp_scope **scope_stack;
    size_t scope_count;
    size_t scope_capacity;

    uint32_t next_block_id;

    chirp_signal signal;
    bs_value signal_val;
    uint32_t signal_block_id;
};

#endif /* CHIRP_VM_INTERNAL_H */
