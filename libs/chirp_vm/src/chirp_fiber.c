/**
 * @file chirp_fiber.c
 * @brief Fiber execution context for Chirp Virtual Machine State.
 */

#include "chirp_vm_internal.h"
#include "bs_core/bs_alloc.h"

BS_API bs_status chirp_fiber_create(chirp_vm *vm, chirp_fiber **out_fiber) {
    assert(vm != NULL);
    assert(out_fiber != NULL);

    chirp_fiber *fiber = (chirp_fiber *)bs_alloc(sizeof(chirp_fiber));
    if (!fiber) {
        return BS_OUT_OF_MEMORY;
    }

    fiber->vm = chirp_vm_retain(vm);
    fiber->store = NULL;
    bs_status status = bs_set_store_create(vm->universe, &fiber->store);
    if (status != BS_OK) {
        chirp_vm_release(fiber->vm);
        bs_free(fiber);
        return status;
    }

    fiber->scope_capacity = 8;
    fiber->scope_stack = (chirp_scope **)bs_alloc(fiber->scope_capacity * sizeof(chirp_scope *));
    if (!fiber->scope_stack) {
        bs_set_store_destroy(fiber->store);
        chirp_vm_release(fiber->vm);
        bs_free(fiber);
        return BS_OUT_OF_MEMORY;
    }

    fiber->scope_stack[0] = chirp_scope_retain(vm->root_scope);
    fiber->scope_count = 1;

    fiber->next_block_id = 0;
    fiber->signal = CHIRP_SIGNAL_NONE;
    fiber->signal_block_id = 0;
    memset(&fiber->signal_val, 0, sizeof(bs_value));

    *out_fiber = fiber;
    return BS_OK;
}

BS_API void chirp_fiber_destroy(chirp_fiber *fiber) {
    if (!fiber) {
        return;
    }

    for (size_t i = 0; i < fiber->scope_count; ++i) {
        chirp_scope_release(fiber->scope_stack[i]);
    }
    bs_free(fiber->scope_stack);

    if (fiber->store) {
        bs_set_store_destroy(fiber->store);
    }

    chirp_vm *vm = fiber->vm;
    bs_free(fiber);
    chirp_vm_release(vm);
}

BS_API chirp_vm *chirp_fiber_vm(const chirp_fiber *fiber) {
    assert(fiber != NULL);
    return fiber->vm;
}

BS_API bs_set_store *chirp_fiber_set_store(const chirp_fiber *fiber) {
    assert(fiber != NULL);
    return fiber->store;
}

BS_API chirp_scope *chirp_fiber_current_scope(const chirp_fiber *fiber) {
    assert(fiber != NULL);
    assert(fiber->scope_count > 0);
    return fiber->scope_stack[fiber->scope_count - 1];
}

BS_API bs_status chirp_fiber_push_scope(chirp_fiber *fiber, chirp_scope *scope) {
    assert(fiber != NULL);
    assert(scope != NULL);

    if (fiber->scope_count >= fiber->scope_capacity) {
        size_t new_cap = fiber->scope_capacity * 2;
        chirp_scope **new_stack = (chirp_scope **)bs_realloc(
            fiber->scope_stack,
            new_cap * sizeof(chirp_scope *)
        );
        if (!new_stack) {
            return BS_OUT_OF_MEMORY;
        }
        fiber->scope_stack = new_stack;
        fiber->scope_capacity = new_cap;
    }

    fiber->scope_stack[fiber->scope_count++] = chirp_scope_retain(scope);
    return BS_OK;
}

BS_API bs_status chirp_fiber_pop_scope(chirp_fiber *fiber) {
    assert(fiber != NULL);
    if (fiber->scope_count <= 1) {
        return BS_VM_FRAME_STACK_UNDERFLOW;
    }

    fiber->scope_count--;
    chirp_scope_release(fiber->scope_stack[fiber->scope_count]);
    return BS_OK;
}

BS_API bs_status chirp_fiber_push_block(chirp_fiber *fiber, uint32_t *out_block_id) {
    assert(fiber != NULL);
    assert(out_block_id != NULL);

    chirp_scope *current = chirp_fiber_current_scope(fiber);
    chirp_scope *block_scope = NULL;
    bs_status status = chirp_scope_create(current, &block_scope);
    if (status != BS_OK) {
        return status;
    }

    fiber->next_block_id++;
    uint32_t block_id = fiber->next_block_id;

    block_scope->is_block = true;
    block_scope->block_id = block_id;

    status = chirp_fiber_push_scope(fiber, block_scope);
    chirp_scope_release(block_scope); /* push_scope added its own retain */
    if (status != BS_OK) {
        return status;
    }

    *out_block_id = block_id;
    return BS_OK;
}

BS_API bs_status chirp_fiber_pop_block(
    chirp_fiber *fiber,
    uint32_t block_id,
    bs_value fallback_val,
    bs_value *out_block_val
) {
    assert(fiber != NULL);

    chirp_scope *current = chirp_fiber_current_scope(fiber);
    if (!current->is_block || current->block_id != block_id) {
        return BS_VM_INVALID_BLOCK;
    }

    if (fiber->signal == CHIRP_SIGNAL_BREAK && fiber->signal_block_id == block_id) {
        if (out_block_val) {
            *out_block_val = fiber->signal_val;
        }
        fiber->signal = CHIRP_SIGNAL_NONE;
        fiber->signal_block_id = 0;
        memset(&fiber->signal_val, 0, sizeof(bs_value));
    } else {
        if (out_block_val) {
            *out_block_val = fallback_val;
        }
    }

    return chirp_fiber_pop_scope(fiber);
}

BS_API bs_status chirp_fiber_signal_break(
    chirp_fiber *fiber,
    uint32_t block_id,
    bs_value val
) {
    assert(fiber != NULL);

    if (block_id == 0) {
        return BS_VM_INVALID_BLOCK;
    }

    /* Verify that block_id corresponds to an active block scope in this fiber */
    bool found = false;
    for (size_t i = 0; i < fiber->scope_count; ++i) {
        chirp_scope *scope = fiber->scope_stack[i];
        if (scope->is_block && scope->block_id == block_id) {
            found = true;
            break;
        }
    }

    if (!found) {
        return BS_VM_INVALID_BLOCK;
    }

    fiber->signal = CHIRP_SIGNAL_BREAK;
    fiber->signal_block_id = block_id;
    fiber->signal_val = val;
    return BS_OK;
}

BS_API chirp_signal chirp_fiber_signal(const chirp_fiber *fiber) {
    assert(fiber != NULL);
    return fiber->signal;
}

BS_API bs_value chirp_fiber_signal_value(const chirp_fiber *fiber) {
    assert(fiber != NULL);
    return fiber->signal_val;
}

BS_API void chirp_fiber_clear_signal(chirp_fiber *fiber) {
    assert(fiber != NULL);
    fiber->signal = CHIRP_SIGNAL_NONE;
    fiber->signal_block_id = 0;
}

BS_API bs_status chirp_fiber_define_binding(
    chirp_fiber *fiber,
    chirp_symbol_t name,
    const bs_set *fc,
    bs_value cv,
    uint32_t flags,
    chirp_binding **out_binding
) {
    assert(fiber != NULL);
    chirp_scope *current = chirp_fiber_current_scope(fiber);
    return chirp_scope_define(current, name, fc, cv, flags, out_binding);
}

BS_API bs_status chirp_fiber_assign(
    chirp_fiber *fiber,
    chirp_symbol_t name,
    bs_value new_cv
) {
    assert(fiber != NULL);
    chirp_binding *binding = NULL;
    bs_status status = chirp_fiber_lookup(fiber, name, &binding);
    if (status != BS_OK) {
        return status;
    }

    if ((binding->flags & CHIRP_BINDING_READONLY) != 0) {
        return BS_VM_IMMUTABLE_BINDING;
    }

    if (binding->fc != NULL) {
        bs_truth truth = BS_FALSE;
        status = bs_set_contains(binding->fc, new_cv, &truth);
        if (status != BS_OK) {
            return status;
        }
        if (truth != BS_TRUE) {
            return BS_VM_CONSTRAINT_VIOLATION;
        }
    }

    binding->cv = new_cv;
    return BS_OK;
}

BS_API bs_status chirp_fiber_lookup(
    chirp_fiber *fiber,
    chirp_symbol_t name,
    chirp_binding **out_binding
) {
    assert(fiber != NULL);
    chirp_scope *current = chirp_fiber_current_scope(fiber);
    return chirp_scope_lookup(current, name, out_binding);
}

BS_API bs_status chirp_fiber_invoke_host(
    chirp_fiber *fiber,
    const char *format,
    const char *key,
    size_t argc,
    const bs_value *argv,
    bs_value *out_result
) {
    assert(fiber != NULL);
    assert(format != NULL);
    assert(key != NULL);

    void *user_data = NULL;
    chirp_host_fn fn = chirp_vm_lookup_host_fn(fiber->vm, format, key, &user_data);
    if (!fn) {
        return BS_VM_UNDEFINED_SYMBOL;
    }

    return fn(fiber, argc, argv, out_result, user_data);
}
