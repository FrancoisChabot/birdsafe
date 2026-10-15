/**
 * @file chirp_vm.h
 * @brief Public API for Chirp Virtual Machine State.
 */

#ifndef CHIRP_VM_CHIRP_VM_H
#define CHIRP_VM_CHIRP_VM_H

#include "bs_core.h"
#include "bs_num.h"
#include "bs_sets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Opaque Types & Handles                                                    */
/* ========================================================================= */

typedef uint32_t chirp_symbol_t;
#define CHIRP_SYMBOL_NONE ((chirp_symbol_t)0)

typedef struct chirp_vm chirp_vm;
typedef struct chirp_fiber chirp_fiber;
typedef struct chirp_scope chirp_scope;
typedef struct chirp_binding chirp_binding;

/* ========================================================================= */
/* Binding Attributes & Layout                                               */
/* ========================================================================= */

typedef enum chirp_binding_flags {
    CHIRP_BINDING_NONE     = 0,
    CHIRP_BINDING_FINAL    = 1 << 0, /**< Unshadowable in descendant scopes. */
    CHIRP_BINDING_READONLY = 1 << 1, /**< Reassignment forbidden. */
} chirp_binding_flags;

struct chirp_binding {
    chirp_symbol_t name;
    const bs_set *fc;     /**< Fundamental constraint (NULL = unconstrained). */
    const bs_set *lc;     /**< Local constraint (NULL = matches fc). */
    bs_value cv;          /**< Current temporal value. */
    uint32_t flags;       /**< Bitmask of chirp_binding_flags. */
};

/* ========================================================================= */
/* Control Flow Signals                                                      */
/* ========================================================================= */

typedef enum chirp_signal {
    CHIRP_SIGNAL_NONE = 0,
    CHIRP_SIGNAL_BREAK,
    CHIRP_SIGNAL_RETURN,
    CHIRP_SIGNAL_ERROR,
} chirp_signal;

/* ========================================================================= */
/* Host Functions                                                            */
/* ========================================================================= */

typedef bs_status (*chirp_host_fn)(
    chirp_fiber *fiber,
    size_t argc,
    const bs_value *argv,
    bs_value *out_result,
    void *user_data
);

/* ========================================================================= */
/* VM Engine Lifecycle & Global State (Tier 2 Substrate)                     */
/* ========================================================================= */

/** Creates a VM engine substrate. If universe is NULL, an internal one is created. */
BS_API bs_status chirp_vm_create(bs_universe *universe, chirp_vm **out_vm);

/** Increments reference count of a VM engine substrate. */
BS_API chirp_vm *chirp_vm_retain(chirp_vm *vm);

/** Decrements reference count and destroys the VM engine when it reaches zero. */
BS_API void chirp_vm_release(chirp_vm *vm);

/**
 * Destroys a VM handle by releasing its reference.
 * The substrate resources remain valid until all referencing fibers are destroyed.
 */
BS_API void chirp_vm_destroy(chirp_vm *vm);

/** Returns the universe associated with the VM engine. */
BS_API bs_universe *chirp_vm_universe(const chirp_vm *vm);

/** Returns the root (global/boot) scope of the VM engine. */
BS_API chirp_scope *chirp_vm_root_scope(chirp_vm *vm);

/** Interns a string identifier as a unique canonical symbol. */
BS_API bs_status chirp_vm_intern_symbol(
    chirp_vm *vm,
    const char *name,
    chirp_symbol_t *out_symbol
);

/** Returns the string representation of an interned symbol. Returns NULL if invalid. */
BS_API const char *chirp_vm_symbol_name(const chirp_vm *vm, chirp_symbol_t symbol);

/** Registers a host intrinsic callback under format and key namespace. */
BS_API bs_status chirp_vm_register_host_fn(
    chirp_vm *vm,
    const char *format,
    const char *key,
    chirp_host_fn fn,
    void *user_data
);

/** Looks up a registered host intrinsic. Returns NULL if not found. */
BS_API chirp_host_fn chirp_vm_lookup_host_fn(
    const chirp_vm *vm,
    const char *format,
    const char *key,
    void **out_user_data
);

/* ========================================================================= */
/* Lexical Scopes & Bindings                                                 */
/* ========================================================================= */

/** Creates a child lexical scope. Safe to pass NULL for root scopes. */
BS_API bs_status chirp_scope_create(chirp_scope *parent, chirp_scope **out_scope);

/** Increments reference count of a scope (e.g. for closure capture). */
BS_API chirp_scope *chirp_scope_retain(chirp_scope *scope);

/** Decrements reference count and frees scope when it reaches zero. Safe for NULL. */
BS_API void chirp_scope_release(chirp_scope *scope);

/** Returns the parent enclosing lexical scope. */
BS_API chirp_scope *chirp_scope_parent(const chirp_scope *scope);

/** Defines a binding in this scope. Enforces cv in fc and unshadowable finality. */
BS_API bs_status chirp_scope_define(
    chirp_scope *scope,
    chirp_symbol_t name,
    const bs_set *fc,
    bs_value cv,
    uint32_t flags,
    chirp_binding **out_binding
);

/** Looks up a binding by walking lexically from inner to enclosing scopes. */
BS_API bs_status chirp_scope_lookup(
    chirp_scope *scope,
    chirp_symbol_t name,
    chirp_binding **out_binding
);

/** Looks up a binding strictly in this scope without checking parent scopes. */
BS_API bs_status chirp_scope_lookup_local(
    chirp_scope *scope,
    chirp_symbol_t name,
    chirp_binding **out_binding
);

/* ========================================================================= */
/* Execution Context / Fiber (Tier 3 Confined Arena)                         */
/* ========================================================================= */

/** Creates a thread-confined execution fiber. Retains the owning VM engine. */
BS_API bs_status chirp_fiber_create(chirp_vm *vm, chirp_fiber **out_fiber);

/** Destroys an execution fiber and its thread-confined set store. Releases the VM engine. */
BS_API void chirp_fiber_destroy(chirp_fiber *fiber);

/** Returns the VM engine associated with the fiber. */
BS_API chirp_vm *chirp_fiber_vm(const chirp_fiber *fiber);

/** Returns the thread-confined set store for this fiber. */
BS_API bs_set_store *chirp_fiber_set_store(const chirp_fiber *fiber);

/** Returns the currently active scope in this fiber. */
BS_API chirp_scope *chirp_fiber_current_scope(const chirp_fiber *fiber);

/** Pushes a new lexical scope onto the fiber's active scope chain. */
BS_API bs_status chirp_fiber_push_scope(chirp_fiber *fiber, chirp_scope *scope);

/** Pops the active scope from the fiber's scope chain. */
BS_API bs_status chirp_fiber_pop_scope(chirp_fiber *fiber);

/** Enters a do-block scope and yields a unique block ID for break targets. */
BS_API bs_status chirp_fiber_push_block(chirp_fiber *fiber, uint32_t *out_block_id);

/**
 * Exits a do-block scope, handling break signals matching this block.
 * Returns BS_VM_INVALID_BLOCK if the active scope is not a block matching block_id.
 */
BS_API bs_status chirp_fiber_pop_block(
    chirp_fiber *fiber,
    uint32_t block_id,
    bs_value fallback_val,
    bs_value *out_block_val
);

/**
 * Signals an early break out of an active block scope with a value.
 * Returns BS_VM_INVALID_BLOCK if block_id is not an active block target on the fiber.
 */
BS_API bs_status chirp_fiber_signal_break(
    chirp_fiber *fiber,
    uint32_t block_id,
    bs_value val
);

/** Queries the active signal on this fiber. */
BS_API chirp_signal chirp_fiber_signal(const chirp_fiber *fiber);

/** Retrieves the payload value associated with the active signal. */
BS_API bs_value chirp_fiber_signal_value(const chirp_fiber *fiber);

/** Clears any active control flow signal on this fiber. */
BS_API void chirp_fiber_clear_signal(chirp_fiber *fiber);

/** Defines a binding in the fiber's currently active scope. */
BS_API bs_status chirp_fiber_define_binding(
    chirp_fiber *fiber,
    chirp_symbol_t name,
    const bs_set *fc,
    bs_value cv,
    uint32_t flags,
    chirp_binding **out_binding
);

/** Reassigns a binding's cv. Validates mutability and cv in fc. */
BS_API bs_status chirp_fiber_assign(
    chirp_fiber *fiber,
    chirp_symbol_t name,
    bs_value new_cv
);

/** Looks up an active binding from the fiber's current scope. */
BS_API bs_status chirp_fiber_lookup(
    chirp_fiber *fiber,
    chirp_symbol_t name,
    chirp_binding **out_binding
);

/** Invokes a registered host intrinsic. */
BS_API bs_status chirp_fiber_invoke_host(
    chirp_fiber *fiber,
    const char *format,
    const char *key,
    size_t argc,
    const bs_value *argv,
    bs_value *out_result
);

#ifdef __cplusplus
}
#endif

#endif /* CHIRP_VM_CHIRP_VM_H */
