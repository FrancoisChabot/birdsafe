/**
 * @file test_chirp_ast.c
 * @brief Test suite for Chirp AST nodes, arena allocator, and S-expression printer.
 */

#include "chirp_frontend.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_ast_arena_lifecycle(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 256); // small chunk size to force chunk allocations

    void *p1 = chirp_ast_arena_alloc(&arena, 120, 8);
    assert(p1 != NULL);

    void *p2 = chirp_ast_arena_alloc(&arena, 150, 8); // forces 2nd chunk
    assert(p2 != NULL);
    assert(p1 != p2);

    // Large allocation larger than default chunk
    void *p3 = chirp_ast_arena_alloc(&arena, 1024, 16);
    assert(p3 != NULL);

    // Array allocation
    int *arr = (int *)chirp_ast_arena_alloc_array(&arena, 100, sizeof(int));
    assert(arr != NULL);
    for (int i = 0; i < 100; i++) {
        arr[i] = i * 2;
    }
    for (int i = 0; i < 100; i++) {
        assert(arr[i] == i * 2);
    }

    chirp_ast_arena_destroy(&arena);
}

static void test_binary_and_unary_ast(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_token dummy_tok = {
        .type = CHIRP_TOK_PLUS,
        .lexeme = chirp_str_view_from_cstr("+"),
        .line = 1,
        .column = 3,
        .leading_trivia = chirp_str_view_from_cstr("")
    };

    chirp_expr *one = chirp_ast_new_number(&arena, chirp_str_view_from_cstr("1"), dummy_tok);
    chirp_expr *two = chirp_ast_new_number(&arena, chirp_str_view_from_cstr("2"), dummy_tok);
    chirp_expr *bin = chirp_ast_new_binary(&arena, one, CHIRP_BINOP_ADD, two, dummy_tok);

    char *printed = NULL;
    assert(chirp_ast_print_expr(bin, &printed) == BS_OK);
    assert(strcmp(printed, "(+ 1 2)") == 0);
    bs_free(printed);

    chirp_expr *un = chirp_ast_new_unary(&arena, CHIRP_UNOP_NEGATE, bin, dummy_tok);
    assert(chirp_ast_print_expr(un, &printed) == BS_OK);
    assert(strcmp(printed, "(- (+ 1 2))") == 0);
    bs_free(printed);

    chirp_expr *grp = chirp_ast_new_grouping(&arena, un, dummy_tok);
    assert(chirp_ast_print_expr(grp, &printed) == BS_OK);
    assert(strcmp(printed, "(group (- (+ 1 2)))") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_statements_ast(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_token dummy = {
        .type = CHIRP_TOK_IDENTIFIER,
        .lexeme = chirp_str_view_from_cstr("x"),
        .line = 1,
        .column = 1,
        .leading_trivia = chirp_str_view_from_cstr("")
    };

    // let pub mut x: int = 42;
    chirp_named_binding binding = {
        .name = dummy,
        .is_mut = true,
        .is_final = false,
        .type_bound = chirp_ast_new_identifier(&arena, chirp_str_view_from_cstr("int"), dummy),
        .initializer = chirp_ast_new_number(&arena, chirp_str_view_from_cstr("42"), dummy)
    };

    chirp_stmt *let_stmt = chirp_ast_new_let_stmt(&arena, binding, true, dummy);
    char *printed = NULL;
    assert(chirp_ast_print_stmt(let_stmt, &printed) == BS_OK);
    assert(strcmp(printed, "(let pub mut x:int = 42)") == 0);
    bs_free(printed);

    // break 100;
    chirp_stmt *break_stmt = chirp_ast_new_break_stmt(
        &arena,
        chirp_ast_new_number(&arena, chirp_str_view_from_cstr("100"), dummy),
        dummy
    );
    assert(chirp_ast_print_stmt(break_stmt, &printed) == BS_OK);
    assert(strcmp(printed, "(break 100)") == 0);
    bs_free(printed);

    // bare break;
    chirp_stmt *bare_break = chirp_ast_new_break_stmt(&arena, NULL, dummy);
    assert(chirp_ast_print_stmt(bare_break, &printed) == BS_OK);
    assert(strcmp(printed, "(break `void)") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_control_flow_and_data_ast(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_token dummy = {
        .type = CHIRP_TOK_IDENTIFIER,
        .lexeme = chirp_str_view_from_cstr("x"),
        .line = 1,
        .column = 1,
        .leading_trivia = chirp_str_view_from_cstr("")
    };

    // if (cond) then_expr else else_expr
    chirp_expr *cond = chirp_ast_new_identifier(&arena, chirp_str_view_from_cstr("flag"), dummy);
    chirp_expr *then_b = chirp_ast_new_number(&arena, chirp_str_view_from_cstr("1"), dummy);
    chirp_expr *else_b = chirp_ast_new_number(&arena, chirp_str_view_from_cstr("0"), dummy);
    chirp_expr *if_expr = chirp_ast_new_if(&arena, cond, then_b, else_b, dummy);

    char *printed = NULL;
    assert(chirp_ast_print_expr(if_expr, &printed) == BS_OK);
    assert(strcmp(printed, "(if flag 1 0)") == 0);
    bs_free(printed);

    // Block expression
    chirp_stmt **stmts = (chirp_stmt **)chirp_ast_arena_alloc_array(&arena, 2, sizeof(chirp_stmt *));
    chirp_named_binding b = {
        .name = dummy,
        .is_mut = false,
        .is_final = false,
        .type_bound = NULL,
        .initializer = chirp_ast_new_number(&arena, chirp_str_view_from_cstr("5"), dummy)
    };
    stmts[0] = chirp_ast_new_let_stmt(&arena, b, false, dummy);
    stmts[1] = chirp_ast_new_expr_stmt(&arena, chirp_ast_new_identifier(&arena, chirp_str_view_from_cstr("x"), dummy), dummy);
    chirp_stmt_list stmt_list = { .items = stmts, .count = 2 };

    chirp_expr *blk = chirp_ast_new_block(&arena, stmt_list, dummy);
    assert(chirp_ast_print_expr(blk, &printed) == BS_OK);
    assert(strcmp(printed, "(block (let x = 5) (expr_stmt x))") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

int main(void) {
    printf("Running chirp_ast tests...\n");

    test_ast_arena_lifecycle();
    test_binary_and_unary_ast();
    test_statements_ast();
    test_control_flow_and_data_ast();

    printf("All chirp_ast tests passed successfully!\n");
    return 0;
}
