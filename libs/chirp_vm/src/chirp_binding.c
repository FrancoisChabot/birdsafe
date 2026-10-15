/**
 * @file chirp_binding.c
 * @brief Scope and Binding management for Chirp Virtual Machine State.
 */

#include "chirp_vm_internal.h"
#include "bs_core/bs_alloc.h"

BS_API bs_status chirp_scope_create(chirp_scope *parent, chirp_scope **out_scope) {
    assert(out_scope != NULL);

    chirp_scope *scope = (chirp_scope *)bs_alloc(sizeof(chirp_scope));
    if (!scope) {
        return BS_OUT_OF_MEMORY;
    }

    scope->parent = parent ? chirp_scope_retain(parent) : NULL;
    scope->bindings = NULL;
    scope->count = 0;
    scope->capacity = 0;
    scope->ref_count = 1;
    scope->is_block = false;
    scope->block_id = 0;

    *out_scope = scope;
    return BS_OK;
}

BS_API chirp_scope *chirp_scope_retain(chirp_scope *scope) {
    if (scope) {
        assert(scope->ref_count > 0);
        scope->ref_count++;
    }
    return scope;
}

BS_API void chirp_scope_release(chirp_scope *scope) {
    while (scope) {
        assert(scope->ref_count > 0);
        scope->ref_count--;
        if (scope->ref_count > 0) {
            break;
        }

        if (scope->bindings) {
            for (size_t i = 0; i < scope->count; ++i) {
                bs_free(scope->bindings[i]);
            }
            bs_free(scope->bindings);
        }

        chirp_scope *parent = scope->parent;
        bs_free(scope);
        scope = parent;
    }
}

BS_API chirp_scope *chirp_scope_parent(const chirp_scope *scope) {
    assert(scope != NULL);
    return scope->parent;
}

BS_API bs_status chirp_scope_lookup_local(
    chirp_scope *scope,
    chirp_symbol_t name,
    chirp_binding **out_binding
) {
    assert(scope != NULL);
    assert(out_binding != NULL);

    for (size_t i = 0; i < scope->count; ++i) {
        if (scope->bindings[i]->name == name) {
            *out_binding = scope->bindings[i];
            return BS_OK;
        }
    }
    return BS_VM_UNDEFINED_SYMBOL;
}

BS_API bs_status chirp_scope_lookup(
    chirp_scope *scope,
    chirp_symbol_t name,
    chirp_binding **out_binding
) {
    assert(scope != NULL);
    assert(out_binding != NULL);

    for (chirp_scope *cur = scope; cur != NULL; cur = cur->parent) {
        for (size_t i = 0; i < cur->count; ++i) {
            if (cur->bindings[i]->name == name) {
                *out_binding = cur->bindings[i];
                return BS_OK;
            }
        }
    }
    return BS_VM_UNDEFINED_SYMBOL;
}

BS_API bs_status chirp_scope_define(
    chirp_scope *scope,
    chirp_symbol_t name,
    const bs_set *fc,
    bs_value cv,
    uint32_t flags,
    chirp_binding **out_binding
) {
    assert(scope != NULL);
    assert(name != CHIRP_SYMBOL_NONE);

    /* Enforce unshadowable finality: check enclosing scopes */
    for (chirp_scope *cur = scope->parent; cur != NULL; cur = cur->parent) {
        for (size_t i = 0; i < cur->count; ++i) {
            if (cur->bindings[i]->name == name &&
                (cur->bindings[i]->flags & CHIRP_BINDING_FINAL) != 0) {
                return BS_VM_SHADOW_FORBIDDEN;
            }
        }
    }

    /* Check redefinition within the same local scope */
    for (size_t i = 0; i < scope->count; ++i) {
        if (scope->bindings[i]->name == name) {
            return BS_VM_REDEFINITION;
        }
    }

    /* Enforce fundamental constraint cv in fc */
    if (fc != NULL) {
        bs_truth truth = BS_FALSE;
        bs_status status = bs_set_contains(fc, cv, &truth);
        if (status != BS_OK) {
            return status;
        }
        if (truth != BS_TRUE) {
            return BS_VM_CONSTRAINT_VIOLATION;
        }
    }

    /* Grow bindings pointer array if needed */
    if (scope->count >= scope->capacity) {
        size_t new_cap = scope->capacity == 0 ? 8 : scope->capacity * 2;
        chirp_binding **new_bindings = (chirp_binding **)bs_realloc(
            scope->bindings,
            new_cap * sizeof(chirp_binding *)
        );
        if (!new_bindings) {
            return BS_OUT_OF_MEMORY;
        }
        scope->bindings = new_bindings;
        scope->capacity = new_cap;
    }

    chirp_binding *b = (chirp_binding *)bs_alloc(sizeof(chirp_binding));
    if (!b) {
        return BS_OUT_OF_MEMORY;
    }

    b->name = name;
    b->fc = fc;
    b->lc = fc; /* Initial spatial constraint matches fc */
    b->cv = cv;
    b->flags = flags;

    scope->bindings[scope->count++] = b;

    if (out_binding) {
        *out_binding = b;
    }
    return BS_OK;
}
