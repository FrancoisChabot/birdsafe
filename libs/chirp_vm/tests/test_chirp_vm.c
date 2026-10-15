/**
 * @file test_chirp_vm.c
 * @brief Test suite for Chirp Virtual Machine State.
 */

#include "chirp_vm.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bs_status host_mock_same(
    chirp_fiber *fiber,
    size_t argc,
    const bs_value *argv,
    bs_value *out_result,
    void *user_data
) {
    (void)fiber;
    (void)user_data;
    assert(argc == 2);
    assert(out_result != NULL);

    int cmp = 0;
    bs_status st = bs_value_compare(argv[0], argv[1], &cmp);
    if (st != BS_OK) return st;

    /* Return integer 1 for true, 0 for false in this mock */
    return bs_value_from_integer(chirp_vm_universe(chirp_fiber_vm(fiber)), cmp == 0 ? 1 : 0, out_result);
}

static void test_symbols(chirp_vm *vm) {
    chirp_symbol_t s_x = CHIRP_SYMBOL_NONE;
    chirp_symbol_t s_y = CHIRP_SYMBOL_NONE;
    chirp_symbol_t s_x2 = CHIRP_SYMBOL_NONE;

    assert(chirp_vm_intern_symbol(vm, "x", &s_x) == BS_OK);
    assert(chirp_vm_intern_symbol(vm, "y", &s_y) == BS_OK);
    assert(chirp_vm_intern_symbol(vm, "x", &s_x2) == BS_OK);

    assert(s_x != CHIRP_SYMBOL_NONE);
    assert(s_y != CHIRP_SYMBOL_NONE);
    assert(s_x != s_y);
    assert(s_x == s_x2);

    assert(strcmp(chirp_vm_symbol_name(vm, s_x), "x") == 0);
    assert(strcmp(chirp_vm_symbol_name(vm, s_y), "y") == 0);
}

static void test_scopes_and_shadowing(chirp_vm *vm, chirp_fiber *fiber) {
    chirp_symbol_t sym_foo, sym_final_var;
    assert(chirp_vm_intern_symbol(vm, "foo", &sym_foo) == BS_OK);
    assert(chirp_vm_intern_symbol(vm, "final_var", &sym_final_var) == BS_OK);

    bs_value v10, v20, v30;
    assert(bs_value_from_integer(chirp_vm_universe(vm), 10, &v10) == BS_OK);
    assert(bs_value_from_integer(chirp_vm_universe(vm), 20, &v20) == BS_OK);
    assert(bs_value_from_integer(chirp_vm_universe(vm), 30, &v30) == BS_OK);

    /* Define in root scope */
    assert(chirp_fiber_define_binding(fiber, sym_foo, NULL, v10, CHIRP_BINDING_NONE, NULL) == BS_OK);
    assert(chirp_fiber_define_binding(fiber, sym_final_var, NULL, v20, CHIRP_BINDING_FINAL, NULL) == BS_OK);

    /* Redefinition in same scope must fail */
    assert(chirp_fiber_define_binding(fiber, sym_foo, NULL, v10, CHIRP_BINDING_NONE, NULL) == BS_VM_REDEFINITION);

    /* Push child scope */
    chirp_scope *child_scope = NULL;
    assert(chirp_scope_create(chirp_fiber_current_scope(fiber), &child_scope) == BS_OK);
    assert(chirp_fiber_push_scope(fiber, child_scope) == BS_OK);
    chirp_scope_release(child_scope);

    /* Shadow non-final foo -> should succeed */
    assert(chirp_fiber_define_binding(fiber, sym_foo, NULL, v30, CHIRP_BINDING_NONE, NULL) == BS_OK);

    /* Lookup foo in child scope -> should see child's value (30) */
    chirp_binding *b_foo = NULL;
    assert(chirp_fiber_lookup(fiber, sym_foo, &b_foo) == BS_OK);
    int64_t val = 0;
    assert(bs_value_as_integer(b_foo->cv, &val) == BS_OK);
    assert(val == 30);

    /* Attempt to shadow final_var -> MUST fail with BS_VM_SHADOW_FORBIDDEN */
    assert(chirp_fiber_define_binding(fiber, sym_final_var, NULL, v30, CHIRP_BINDING_NONE, NULL) == BS_VM_SHADOW_FORBIDDEN);

    /* Pop child scope */
    assert(chirp_fiber_pop_scope(fiber) == BS_OK);

    /* In root scope again, foo must be 10 */
    assert(chirp_fiber_lookup(fiber, sym_foo, &b_foo) == BS_OK);
    assert(bs_value_as_integer(b_foo->cv, &val) == BS_OK);
    assert(val == 10);
}

static void test_constraint_enforcement(chirp_vm *vm, chirp_fiber *fiber) {
    chirp_symbol_t sym_bounded;
    assert(chirp_vm_intern_symbol(vm, "bounded", &sym_bounded) == BS_OK);

    bs_set_store *store = chirp_fiber_set_store(fiber);
    const bs_universe *u = chirp_vm_universe(vm);

    bs_value v_lower, v_upper;
    assert(bs_value_from_integer(u, 0, &v_lower) == BS_OK);
    assert(bs_value_from_integer(u, 10, &v_upper) == BS_OK);

    const bs_set *interval = NULL;
    assert(bs_set_interval(store, &v_lower, true, &v_upper, true, &interval) == BS_OK);

    bs_value v_valid, v_invalid, v_valid_update;
    assert(bs_value_from_integer(u, 5, &v_valid) == BS_OK);
    assert(bs_value_from_integer(u, 15, &v_invalid) == BS_OK);
    assert(bs_value_from_integer(u, 8, &v_valid_update) == BS_OK);

    /* Defining with invalid initial value must fail */
    assert(chirp_fiber_define_binding(fiber, sym_bounded, interval, v_invalid, CHIRP_BINDING_NONE, NULL) == BS_VM_CONSTRAINT_VIOLATION);

    /* Defining with valid initial value succeeds */
    assert(chirp_fiber_define_binding(fiber, sym_bounded, interval, v_valid, CHIRP_BINDING_NONE, NULL) == BS_OK);

    /* Assigning valid value in [0, 10] succeeds */
    assert(chirp_fiber_assign(fiber, sym_bounded, v_valid_update) == BS_OK);

    /* Assigning invalid value outside [0, 10] fails */
    assert(chirp_fiber_assign(fiber, sym_bounded, v_invalid) == BS_VM_CONSTRAINT_VIOLATION);

    /* Value must remain 8 */
    chirp_binding *b = NULL;
    assert(chirp_fiber_lookup(fiber, sym_bounded, &b) == BS_OK);
    int64_t val = 0;
    assert(bs_value_as_integer(b->cv, &val) == BS_OK);
    assert(val == 8);
}

static void test_immutability(chirp_vm *vm, chirp_fiber *fiber) {
    chirp_symbol_t sym_const;
    assert(chirp_vm_intern_symbol(vm, "const_val", &sym_const) == BS_OK);

    bs_value v1, v2;
    assert(bs_value_from_integer(chirp_vm_universe(vm), 42, &v1) == BS_OK);
    assert(bs_value_from_integer(chirp_vm_universe(vm), 99, &v2) == BS_OK);

    assert(chirp_fiber_define_binding(fiber, sym_const, NULL, v1, CHIRP_BINDING_READONLY, NULL) == BS_OK);
    assert(chirp_fiber_assign(fiber, sym_const, v2) == BS_VM_IMMUTABLE_BINDING);
}

static void test_block_and_break(chirp_vm *vm, chirp_fiber *fiber) {
    chirp_symbol_t sym_in_block;
    assert(chirp_vm_intern_symbol(vm, "in_block", &sym_in_block) == BS_OK);

    uint32_t block_id = 0;
    assert(chirp_fiber_push_block(fiber, &block_id) == BS_OK);
    assert(block_id > 0);

    bs_value v_block_local, v_break_val, v_fallback;
    assert(bs_value_from_integer(chirp_vm_universe(vm), 111, &v_block_local) == BS_OK);
    assert(bs_value_from_integer(chirp_vm_universe(vm), 777, &v_break_val) == BS_OK);
    assert(bs_value_from_integer(chirp_vm_universe(vm), 0, &v_fallback) == BS_OK);

    /* Define binding inside block */
    assert(chirp_fiber_define_binding(fiber, sym_in_block, NULL, v_block_local, CHIRP_BINDING_NONE, NULL) == BS_OK);

    /* Push a nested inner block */
    uint32_t inner_block_id = 0;
    assert(chirp_fiber_push_block(fiber, &inner_block_id) == BS_OK);

    /* Reject signaling break to an invalid / nonexistent block ID */
    assert(chirp_fiber_signal_break(fiber, 99999, v_break_val) == BS_VM_INVALID_BLOCK);
    assert(chirp_fiber_signal(fiber) == CHIRP_SIGNAL_NONE);

    /* Reject popping outer block while inner block is active (mismatched ID) */
    bs_value dummy_result;
    assert(chirp_fiber_pop_block(fiber, block_id, v_fallback, &dummy_result) == BS_VM_INVALID_BLOCK);

    /* Inner block remains active and functional */
    assert(chirp_fiber_signal_break(fiber, inner_block_id, v_break_val) == BS_OK);
    assert(chirp_fiber_signal(fiber) == CHIRP_SIGNAL_BREAK);

    /* Pop inner block with matching ID */
    assert(chirp_fiber_pop_block(fiber, inner_block_id, v_fallback, &dummy_result) == BS_OK);
    assert(chirp_fiber_signal(fiber) == CHIRP_SIGNAL_NONE);
    int64_t inner_res = 0;
    assert(bs_value_as_integer(dummy_result, &inner_res) == BS_OK);
    assert(inner_res == 777);

    /* Now that inner block has exited, signaling break to inner_block_id must be rejected */
    assert(chirp_fiber_signal_break(fiber, inner_block_id, v_break_val) == BS_VM_INVALID_BLOCK);
    assert(chirp_fiber_signal(fiber) == CHIRP_SIGNAL_NONE);

    /* Signal break on outer block */
    assert(chirp_fiber_signal_break(fiber, block_id, v_break_val) == BS_OK);
    assert(chirp_fiber_signal(fiber) == CHIRP_SIGNAL_BREAK);

    /* Pop outer block: catches break, clears signal, and outputs 777 */
    bs_value result;
    assert(chirp_fiber_pop_block(fiber, block_id, v_fallback, &result) == BS_OK);
    assert(chirp_fiber_signal(fiber) == CHIRP_SIGNAL_NONE);

    int64_t res_val = 0;
    assert(bs_value_as_integer(result, &res_val) == BS_OK);
    assert(res_val == 777);

    /* in_block must no longer be visible */
    chirp_binding *b = NULL;
    assert(chirp_fiber_lookup(fiber, sym_in_block, &b) == BS_VM_UNDEFINED_SYMBOL);
}

static void test_vm_lifetime_with_fiber(void) {
    chirp_vm *vm = NULL;
    assert(chirp_vm_create(NULL, &vm) == BS_OK);

    chirp_fiber *fiber = NULL;
    assert(chirp_fiber_create(vm, &fiber) == BS_OK);

    chirp_symbol_t sym_alive;
    assert(chirp_vm_intern_symbol(vm, "alive", &sym_alive) == BS_OK);

    bs_value v123, v456;
    assert(bs_value_from_integer(chirp_vm_universe(vm), 123, &v123) == BS_OK);
    assert(bs_value_from_integer(chirp_vm_universe(vm), 456, &v456) == BS_OK);

    assert(chirp_fiber_define_binding(fiber, sym_alive, NULL, v123, CHIRP_BINDING_NONE, NULL) == BS_OK);

    /* Destroy VM while fiber is still alive */
    chirp_vm_destroy(vm);

    /* Fiber must remain completely functional and safe from dangling memory */
    chirp_binding *b = NULL;
    assert(chirp_fiber_lookup(fiber, sym_alive, &b) == BS_OK);
    int64_t val = 0;
    assert(bs_value_as_integer(b->cv, &val) == BS_OK);
    assert(val == 123);

    assert(chirp_fiber_assign(fiber, sym_alive, v456) == BS_OK);
    assert(chirp_fiber_lookup(fiber, sym_alive, &b) == BS_OK);
    assert(bs_value_as_integer(b->cv, &val) == BS_OK);
    assert(val == 456);

    uint32_t block_id = 0;
    assert(chirp_fiber_push_block(fiber, &block_id) == BS_OK);
    bs_value block_out;
    assert(chirp_fiber_pop_block(fiber, block_id, v123, &block_out) == BS_OK);

    /* Clean destruction of fiber also releases the underlying VM cleanly */
    chirp_fiber_destroy(fiber);
}

static void test_host_intrinsics(chirp_vm *vm, chirp_fiber *fiber) {
    assert(chirp_vm_register_host_fn(
        vm,
        "__chirp_boot",
        "values.same",
        host_mock_same,
        NULL
    ) == BS_OK);

    bs_value v1, v2;
    assert(bs_value_from_integer(chirp_vm_universe(vm), 42, &v1) == BS_OK);
    assert(bs_value_from_integer(chirp_vm_universe(vm), 42, &v2) == BS_OK);

    bs_value args[2] = { v1, v2 };
    bs_value result;
    assert(chirp_fiber_invoke_host(fiber, "__chirp_boot", "values.same", 2, args, &result) == BS_OK);

    int64_t is_same = 0;
    assert(bs_value_as_integer(result, &is_same) == BS_OK);
    assert(is_same == 1);
}

static void test_stable_binding_pointers(chirp_vm *vm, chirp_fiber *fiber) {
    chirp_symbol_t sym_initial;
    assert(chirp_vm_intern_symbol(vm, "initial_var", &sym_initial) == BS_OK);

    bs_value v100, v999;
    assert(bs_value_from_integer(chirp_vm_universe(vm), 100, &v100) == BS_OK);
    assert(bs_value_from_integer(chirp_vm_universe(vm), 999, &v999) == BS_OK);

    chirp_binding *saved_ptr = NULL;
    assert(chirp_fiber_define_binding(fiber, sym_initial, NULL, v100, CHIRP_BINDING_NONE, &saved_ptr) == BS_OK);
    assert(saved_ptr != NULL);

    /* Define 50 more bindings in this same scope, forcing multiple reallocations of the pointer array */
    for (int i = 0; i < 50; ++i) {
        char name_buf[32];
        snprintf(name_buf, sizeof(name_buf), "bulk_var_%d", i);

        chirp_symbol_t sym;
        assert(chirp_vm_intern_symbol(vm, name_buf, &sym) == BS_OK);

        bs_value v;
        assert(bs_value_from_integer(chirp_vm_universe(vm), i, &v) == BS_OK);

        chirp_binding *bulk_ptr = NULL;
        assert(chirp_fiber_define_binding(fiber, sym, NULL, v, CHIRP_BINDING_NONE, &bulk_ptr) == BS_OK);
        assert(bulk_ptr != NULL);
        assert(bulk_ptr != saved_ptr);
    }

    /* Verify saved_ptr was never relocated or corrupted */
    assert(saved_ptr->name == sym_initial);
    int64_t val = 0;
    assert(bs_value_as_integer(saved_ptr->cv, &val) == BS_OK);
    assert(val == 100);

    /* Reassigning via fiber updates saved_ptr target */
    assert(chirp_fiber_assign(fiber, sym_initial, v999) == BS_OK);
    assert(bs_value_as_integer(saved_ptr->cv, &val) == BS_OK);
    assert(val == 999);

    /* Lookup confirms same address */
    chirp_binding *lookup_ptr = NULL;
    assert(chirp_fiber_lookup(fiber, sym_initial, &lookup_ptr) == BS_OK);
    assert(lookup_ptr == saved_ptr);
}

int main(void) {
    printf("Running chirp_vm tests...\n");

    chirp_vm *vm = NULL;
    assert(chirp_vm_create(NULL, &vm) == BS_OK);
    assert(vm != NULL);

    chirp_fiber *fiber = NULL;
    assert(chirp_fiber_create(vm, &fiber) == BS_OK);
    assert(fiber != NULL);

    test_symbols(vm);
    test_scopes_and_shadowing(vm, fiber);
    test_constraint_enforcement(vm, fiber);
    test_immutability(vm, fiber);
    test_stable_binding_pointers(vm, fiber);
    test_block_and_break(vm, fiber);
    test_host_intrinsics(vm, fiber);

    chirp_fiber_destroy(fiber);
    chirp_vm_destroy(vm);

    test_vm_lifetime_with_fiber();

    printf("All chirp_vm tests passed successfully!\n");
    return 0;
}
