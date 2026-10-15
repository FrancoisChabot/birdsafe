/**
 * @file test_chirp_parser.c
 * @brief Comprehensive test suite for the Chirp parser.
 */

#include "chirp_frontend.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_binary_precedence(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_expr *expr = NULL;
    assert(chirp_parse_expression_cstr(&arena, "x % 2 == 0", &expr, NULL) == BS_OK);
    assert(expr != NULL);

    char *printed = NULL;
    assert(chirp_ast_print_expr(expr, &printed) == BS_OK);
    assert(strcmp(printed, "(== (% x 2) 0)") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_symbolic_constant(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_expr *expr = NULL;
    assert(chirp_parse_expression_cstr(&arena, "#red == #blue", &expr, NULL) == BS_OK);
    assert(expr != NULL);

    char *printed = NULL;
    assert(chirp_ast_print_expr(expr, &printed) == BS_OK);
    assert(strcmp(printed, "(== #red #blue)") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_character_literal_unicode_escape(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_expr *expr = NULL;
    assert(chirp_parse_expression_cstr(&arena, "'\\u00AB'", &expr, NULL) == BS_OK);
    assert(expr != NULL);

    char *printed = NULL;
    assert(chirp_ast_print_expr(expr, &printed) == BS_OK);
    assert(strcmp(printed, "'\\u00AB'") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_logical_and_comparison(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_expr *expr = NULL;
    assert(chirp_parse_expression_cstr(&arena, "x ∈ int && y > 3", &expr, NULL) == BS_OK);
    assert(expr != NULL);

    char *printed = NULL;
    assert(chirp_ast_print_expr(expr, &printed) == BS_OK);
    assert(strcmp(printed, "(&& (in x int) (> y 3))") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_unary_and_range(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_expr *expr = NULL;
    assert(chirp_parse_expression_cstr(&arena, "!0..3", &expr, NULL) == BS_OK);
    assert(expr != NULL);

    char *printed = NULL;
    assert(chirp_ast_print_expr(expr, &printed) == BS_OK);
    assert(strcmp(printed, "(.. (! 0) 3)") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_pointer_mutability_operators(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    char *printed = NULL;
    chirp_expr *e = NULL;

    assert(chirp_parse_expression_cstr(&arena, "&mut x", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(&mut x)") == 0);
    bs_free(printed);

    assert(chirp_parse_expression_cstr(&arena, "->mut int", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(->mut int)") == 0);
    bs_free(printed);

    assert(chirp_parse_expression_cstr(&arena, "->mut ->int", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(->mut (-> int))") == 0);
    bs_free(printed);

    assert(chirp_parse_expression_cstr(&arena, "-> ->mut int", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(-> (->mut int))") == 0);
    bs_free(printed);

    assert(chirp_parse_expression_cstr(&arena, "->mut ->mut int", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(->mut (->mut int))") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_spaced_mut_is_not_pointer_mutability_operator(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    char *printed = NULL;
    chirp_expr *e = NULL;

    assert(chirp_parse_expression_cstr(&arena, "& mut", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(& mut)") == 0);
    bs_free(printed);

    assert(chirp_parse_expression_cstr(&arena, "-> mut", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(-> mut)") == 0);
    bs_free(printed);

    chirp_parser_error err;
    assert(chirp_parse_expression_cstr(&arena, "& mut x", &e, &err) != BS_OK);
    assert(chirp_parse_expression_cstr(&arena, "-> mut int", &e, &err) != BS_OK);

    chirp_ast_arena_destroy(&arena);
}

static void test_contextual_binding_modifiers(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_program prog;
    char *printed = NULL;

    assert(chirp_parse_program_cstr(&arena, "let final true = `import(\"true_val\", \"__chirp_boot\");", &prog, NULL) == BS_OK);
    assert(prog.statements.count == 1);
    assert(chirp_ast_print_stmt(prog.statements.items[0], &printed) == BS_OK);
    assert(strcmp(printed, "(let final true = (call `import \"true_val\" \"__chirp_boot\"))") == 0);
    bs_free(printed);

    assert(chirp_parse_program_cstr(&arena, "let final = 1;", &prog, NULL) == BS_OK);
    assert(prog.statements.count == 1);
    assert(chirp_ast_print_stmt(prog.statements.items[0], &printed) == BS_OK);
    assert(strcmp(printed, "(let final = 1)") == 0);
    bs_free(printed);

    assert(chirp_parse_program_cstr(&arena, "let mut = 2;", &prog, NULL) == BS_OK);
    assert(prog.statements.count == 1);
    assert(chirp_ast_print_stmt(prog.statements.items[0], &printed) == BS_OK);
    assert(strcmp(printed, "(let mut = 2)") == 0);
    bs_free(printed);

    assert(chirp_parse_program_cstr(&arena, "let mut final x : int = 3;", &prog, NULL) == BS_OK);
    assert(prog.statements.count == 1);
    assert(chirp_ast_print_stmt(prog.statements.items[0], &printed) == BS_OK);
    assert(strcmp(printed, "(let final mut x:int = 3)") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_public_let_is_contextual(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_program prog;
    char *printed = NULL;

    assert(chirp_parse_program_cstr(&arena, "let pub x = 1;", &prog, NULL) == BS_OK);
    assert(chirp_ast_print_stmt(prog.statements.items[0], &printed) == BS_OK);
    assert(strcmp(printed, "(let pub x = 1)") == 0);
    bs_free(printed);

    assert(chirp_parse_program_cstr(&arena, "let pub final `thing = 2;", &prog, NULL) == BS_OK);
    assert(chirp_ast_print_stmt(prog.statements.items[0], &printed) == BS_OK);
    assert(strcmp(printed, "(let pub final `thing = 2)") == 0);
    bs_free(printed);

    assert(chirp_parse_program_cstr(&arena, "let pub = 3;", &prog, NULL) == BS_OK);
    assert(chirp_ast_print_stmt(prog.statements.items[0], &printed) == BS_OK);
    assert(strcmp(printed, "(let pub = 3)") == 0);
    bs_free(printed);

    assert(chirp_parse_program_cstr(&arena, "pub;", &prog, NULL) == BS_OK);
    assert(chirp_ast_print_stmt(prog.statements.items[0], &printed) == BS_OK);
    assert(strcmp(printed, "(expr_stmt pub)") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_indirected_mutation_statement(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_program prog;
    char *printed = NULL;

    assert(chirp_parse_program_cstr(&arena, "let p: ->mut int = &mut x; *p = 1;", &prog, NULL) == BS_OK);
    assert(prog.statements.count == 2);

    assert(chirp_ast_print_stmt(prog.statements.items[0], &printed) == BS_OK);
    assert(strcmp(printed, "(let p:(->mut int) = (&mut x))") == 0);
    bs_free(printed);

    assert(chirp_ast_print_stmt(prog.statements.items[1], &printed) == BS_OK);
    assert(strcmp(printed, "(= (* p) 1)") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_range_operators(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_expr *e = NULL;
    char *printed = NULL;

    assert(chirp_parse_expression_cstr(&arena, "1..10", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(.. 1 10)") == 0);
    bs_free(printed);

    assert(chirp_parse_expression_cstr(&arena, "1..=10", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(..= 1 10)") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_sets_and_brace_disambiguation(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_expr *e = NULL;
    char *printed = NULL;

    // 1. Empty set: {}
    assert(chirp_parse_expression_cstr(&arena, "{}", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(set)") == 0);
    bs_free(printed);

    // 2. Enumerated set: {1, 2, 3}
    assert(chirp_parse_expression_cstr(&arena, "{1, 2, 3}", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(set 1 2 3)") == 0);
    bs_free(printed);

    // 3. Constructed set: { x : int | x > 0 }
    assert(chirp_parse_expression_cstr(&arena, "{ x : int | x > 0 }", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(c_set x : int | (> x 0))") == 0);
    bs_free(printed);

    // 4. Constructed set without type bound: { x | x > 0 }
    assert(chirp_parse_expression_cstr(&arena, "{ x | x > 0 }", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(c_set x | (> x 0))") == 0);
    bs_free(printed);

    // 5. Anonymous struct literal: { x = 1, y = 2 }
    assert(chirp_parse_expression_cstr(&arena, "{ x = 1, y = 2 }", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(anon_struct x=1 y=2)") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_blocks_and_trailing_expr(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_expr *e = NULL;
    char *printed = NULL;

    assert(chirp_parse_expression_cstr(&arena, "do { let x = 5; x * 2 }", &e, NULL) == BS_OK);
    assert(chirp_ast_print_expr(e, &printed) == BS_OK);
    assert(strcmp(printed, "(block (let x = 5) (break (* x 2)))") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_function_sugar(void) {
    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_program prog;
    char *printed = NULL;

    assert(chirp_parse_program_cstr(&arena, "let f(x: int): int = x + 1;", &prog, NULL) == BS_OK);
    assert(prog.statements.count == 1);
    assert(chirp_ast_print_stmt(prog.statements.items[0], &printed) == BS_OK);
    assert(strcmp(printed, "(let f = (lambda (x:int):int (+ x 1)))") == 0);
    bs_free(printed);

    chirp_ast_arena_destroy(&arena);
}

static void test_parse_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("Note: %s not found, skipping file read.\n", path);
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = (char *)malloc(size + 1);
    assert(source != NULL);
    size_t read_bytes = fread(source, 1, size, f);
    source[read_bytes] = '\0';
    fclose(f);

    chirp_ast_arena arena;
    chirp_ast_arena_init(&arena, 0);

    chirp_program prog;
    chirp_parser_error err;
    memset(&err, 0, sizeof(err));

    bs_status st = chirp_parse_program_cstr(&arena, source, &prog, &err);
    if (st != BS_OK) {
        printf("Parse error in %s: %s at line %u:%u (lexeme: '%.*s')\n",
               path, err.message, err.token.line, err.token.column,
               (int)err.token.lexeme.len, err.token.lexeme.ptr);
    }
    assert(st == BS_OK);

    printf("Successfully parsed %s (%zu top-level statements)\n", path, prog.statements.count);

    chirp_ast_arena_destroy(&arena);
    free(source);
}

static void test_parse_boot_files(void) {
    const char *boot_files[] = {
        "for_reference/chirp/lib/chirp/boot/00_fundamental.chirp",
        "for_reference/chirp/lib/chirp/boot/01_scoping.chirp",
        "for_reference/chirp/lib/chirp/boot/02_memory.chirp",
        "for_reference/chirp/lib/chirp/boot/03_operators.chirp",
        "for_reference/chirp/lib/chirp/boot/04_conveniences.chirp",
        "for_reference/chirp/lib/chirp/boot/05_testing.chirp",
        "for_reference/chirp/lib/chirp/boot/99_dev_compromises.chirp",
        "for_reference/chirp/tests/core/integration/fizzbuzz.chirp",
        NULL
    };

    for (size_t i = 0; boot_files[i] != NULL; ++i) {
        test_parse_file(boot_files[i]);
    }
}

int main(void) {
    printf("Running chirp_parser tests...\n");

    test_binary_precedence();
    test_symbolic_constant();
    test_character_literal_unicode_escape();
    test_logical_and_comparison();
    test_unary_and_range();
    test_pointer_mutability_operators();
    test_spaced_mut_is_not_pointer_mutability_operator();
    test_contextual_binding_modifiers();
    test_public_let_is_contextual();
    test_indirected_mutation_statement();
    test_range_operators();
    test_sets_and_brace_disambiguation();
    test_blocks_and_trailing_expr();
    test_function_sugar();
    test_parse_boot_files();

    printf("All chirp_parser tests passed successfully!\n");
    return 0;
}
