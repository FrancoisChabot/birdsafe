/**
 * @file test_chirp_lexer.c
 * @brief Test suite for the Chirp UTF-8 lexer.
 */

#include "chirp_frontend.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_str_views(void) {
    chirp_str_view a = chirp_str_view_from_cstr("hello");
    chirp_str_view b = chirp_str_view_create("hello world", 5);
    chirp_str_view c = chirp_str_view_from_cstr("world");

    assert(chirp_str_view_equals(a, b));
    assert(!chirp_str_view_equals(a, c));
    assert(chirp_str_view_equals_cstr(a, "hello"));
    assert(!chirp_str_view_equals_cstr(a, "world"));
    assert(chirp_str_view_starts_with(chirp_str_view_from_cstr("hello world"), a));
    assert(!chirp_str_view_starts_with(a, chirp_str_view_from_cstr("hello world")));
    assert(chirp_str_view_compare(a, c) < 0);
    assert(chirp_str_view_compare(c, a) > 0);
    assert(chirp_str_view_compare(a, b) == 0);
}

static void test_basic_tokens(void) {
    chirp_token_list tokens;
    assert(chirp_tokenize_cstr("let mut x = 0..3;", &tokens) == BS_OK);
    assert(tokens.count == 9);

    assert(tokens.tokens[0].type == CHIRP_TOK_KW_LET);
    assert(tokens.tokens[1].type == CHIRP_TOK_IDENTIFIER);
    assert(chirp_str_view_equals_cstr(tokens.tokens[1].lexeme, "mut"));
    assert(tokens.tokens[2].type == CHIRP_TOK_IDENTIFIER);
    assert(chirp_str_view_equals_cstr(tokens.tokens[2].lexeme, "x"));
    assert(tokens.tokens[3].type == CHIRP_TOK_EQUAL);
    assert(tokens.tokens[4].type == CHIRP_TOK_NUMBER);
    assert(chirp_str_view_equals_cstr(tokens.tokens[4].lexeme, "0"));
    assert(tokens.tokens[5].type == CHIRP_TOK_DOT_DOT);
    assert(tokens.tokens[6].type == CHIRP_TOK_NUMBER);
    assert(chirp_str_view_equals_cstr(tokens.tokens[6].lexeme, "3"));
    assert(tokens.tokens[7].type == CHIRP_TOK_SEMICOLON);
    assert(tokens.tokens[8].type == CHIRP_TOK_EOF);

    chirp_token_list_free(&tokens);
}

static void test_unicode_operators_and_columns(void) {
    chirp_token_list tokens;
    assert(chirp_tokenize_cstr("x ∈ int && y ∪ z", &tokens) == BS_OK);
    assert(tokens.count == 8);

    assert(chirp_str_view_equals_cstr(tokens.tokens[0].lexeme, "x"));
    assert(tokens.tokens[0].column == 1);

    assert(tokens.tokens[1].type == CHIRP_TOK_IN);
    assert(tokens.tokens[1].column == 3);

    assert(chirp_str_view_equals_cstr(tokens.tokens[2].lexeme, "int"));
    assert(tokens.tokens[2].column == 5);

    assert(tokens.tokens[3].type == CHIRP_TOK_AND_AND);
    assert(tokens.tokens[4].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[5].type == CHIRP_TOK_UNION);
    assert(tokens.tokens[6].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[7].type == CHIRP_TOK_EOF);

    chirp_token_list_free(&tokens);
}

static void test_intrinsics(void) {
    chirp_token_list tokens;
    assert(chirp_tokenize_cstr("`any `type `in `notin `or `and `", &tokens) == BS_OK);
    assert(tokens.count == 8);

    assert(tokens.tokens[0].type == CHIRP_TOK_INTRINSIC);
    assert(chirp_str_view_equals_cstr(tokens.tokens[0].lexeme, "`any"));

    assert(tokens.tokens[1].type == CHIRP_TOK_INTRINSIC);
    assert(chirp_str_view_equals_cstr(tokens.tokens[1].lexeme, "`type"));

    assert(tokens.tokens[2].type == CHIRP_TOK_IN);
    assert(tokens.tokens[3].type == CHIRP_TOK_NOT_IN);
    assert(tokens.tokens[4].type == CHIRP_TOK_UNION);
    assert(tokens.tokens[5].type == CHIRP_TOK_INTERSECTION);

    assert(tokens.tokens[6].type == CHIRP_TOK_ERROR);
    assert(tokens.tokens[7].type == CHIRP_TOK_EOF);

    chirp_token_list_free(&tokens);
}

static void test_range_operators(void) {
    chirp_token_list tokens;
    assert(chirp_tokenize_cstr("0..1 0..=1", &tokens) == BS_OK);
    assert(tokens.count == 7);

    assert(tokens.tokens[1].type == CHIRP_TOK_DOT_DOT);
    assert(tokens.tokens[4].type == CHIRP_TOK_DOT_DOT_EQUAL);

    chirp_token_list_free(&tokens);
}

static void test_decimal_numbers_require_fraction_digits(void) {
    chirp_token_list tokens;
    assert(chirp_tokenize_cstr("1.2 1.foo 1.;", &tokens) == BS_OK);
    assert(tokens.count == 8);

    assert(tokens.tokens[0].type == CHIRP_TOK_NUMBER);
    assert(chirp_str_view_equals_cstr(tokens.tokens[0].lexeme, "1.2"));

    assert(tokens.tokens[1].type == CHIRP_TOK_NUMBER);
    assert(chirp_str_view_equals_cstr(tokens.tokens[1].lexeme, "1"));
    assert(tokens.tokens[2].type == CHIRP_TOK_DOT);
    assert(tokens.tokens[3].type == CHIRP_TOK_IDENTIFIER);
    assert(chirp_str_view_equals_cstr(tokens.tokens[3].lexeme, "foo"));

    assert(tokens.tokens[4].type == CHIRP_TOK_NUMBER);
    assert(chirp_str_view_equals_cstr(tokens.tokens[4].lexeme, "1"));
    assert(tokens.tokens[5].type == CHIRP_TOK_DOT);
    assert(tokens.tokens[6].type == CHIRP_TOK_SEMICOLON);
    assert(tokens.tokens[7].type == CHIRP_TOK_EOF);

    chirp_token_list_free(&tokens);
}

static void test_character_literals_valid(void) {
    chirp_token_list tokens;
    assert(chirp_tokenize_cstr("'a' '«' '😀' '\\n' '\\'' '\\\\' '\\u00AB'", &tokens) == BS_OK);
    assert(tokens.count == 8);

    for (size_t i = 0; i < 7; i++) {
        assert(tokens.tokens[i].type == CHIRP_TOK_CHARACTER);
    }
    assert(chirp_str_view_equals_cstr(tokens.tokens[0].lexeme, "'a'"));
    assert(chirp_str_view_equals_cstr(tokens.tokens[1].lexeme, "'«'"));
    assert(chirp_str_view_equals_cstr(tokens.tokens[2].lexeme, "'😀'"));
    assert(chirp_str_view_equals_cstr(tokens.tokens[3].lexeme, "'\\n'"));
    assert(chirp_str_view_equals_cstr(tokens.tokens[4].lexeme, "'\\''"));
    assert(chirp_str_view_equals_cstr(tokens.tokens[5].lexeme, "'\\\\'"));
    assert(chirp_str_view_equals_cstr(tokens.tokens[6].lexeme, "'\\u00AB'"));

    chirp_token_list_free(&tokens);
}

static void test_character_literals_reject_malformed(void) {
    chirp_token_list tokens;
    assert(chirp_tokenize_cstr("'' 'ab' '«»' '\\u00G1' '\\uD800' '\\x' 'a", &tokens) == BS_OK);
    assert(tokens.count == 8);

    for (size_t i = 0; i < 7; i++) {
        assert(tokens.tokens[i].type == CHIRP_TOK_ERROR);
    }
    assert(tokens.tokens[7].type == CHIRP_TOK_EOF);

    chirp_token_list_free(&tokens);
}

static void test_keywords_and_contextual_identifiers(void) {
    chirp_token_list tokens;
    assert(chirp_tokenize_cstr(
        "do let mut pub struct if else while for true false undecided break match",
        &tokens
    ) == BS_OK);
    assert(tokens.count == 15);

    assert(tokens.tokens[0].type == CHIRP_TOK_KW_DO);
    assert(tokens.tokens[1].type == CHIRP_TOK_KW_LET);
    assert(tokens.tokens[2].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[3].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[4].type == CHIRP_TOK_KW_STRUCT);
    assert(tokens.tokens[5].type == CHIRP_TOK_KW_IF);
    assert(tokens.tokens[6].type == CHIRP_TOK_KW_ELSE);
    assert(tokens.tokens[7].type == CHIRP_TOK_KW_WHILE);
    assert(tokens.tokens[8].type == CHIRP_TOK_KW_FOR);
    assert(tokens.tokens[9].type == CHIRP_TOK_IDENTIFIER);  // true is not a keyword
    assert(tokens.tokens[10].type == CHIRP_TOK_IDENTIFIER); // false is not a keyword
    assert(tokens.tokens[11].type == CHIRP_TOK_IDENTIFIER); // undecided is not a keyword
    assert(tokens.tokens[12].type == CHIRP_TOK_KW_BREAK);
    assert(tokens.tokens[13].type == CHIRP_TOK_KW_MATCH);
    assert(tokens.tokens[14].type == CHIRP_TOK_EOF);

    chirp_token_list_free(&tokens);
}

static void test_mutable_pointer_operators_fused(void) {
    chirp_token_list tokens;
    assert(chirp_tokenize_cstr(
        "&mut x & mut y &mutx ->mut int -> mut int ->mutx",
        &tokens
    ) == BS_OK);
    assert(tokens.count == 15);

    assert(tokens.tokens[0].type == CHIRP_TOK_AMPERSAND_MUT);
    assert(tokens.tokens[1].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[2].type == CHIRP_TOK_AMPERSAND);
    assert(tokens.tokens[3].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[4].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[5].type == CHIRP_TOK_AMPERSAND);
    assert(tokens.tokens[6].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[7].type == CHIRP_TOK_ARROW_MUT);
    assert(tokens.tokens[8].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[9].type == CHIRP_TOK_ARROW);
    assert(tokens.tokens[10].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[11].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[12].type == CHIRP_TOK_ARROW);
    assert(tokens.tokens[13].type == CHIRP_TOK_IDENTIFIER);
    assert(tokens.tokens[14].type == CHIRP_TOK_EOF);

    chirp_token_list_free(&tokens);
}

static void test_locations_and_comments(void) {
    chirp_token_list tokens;
    assert(chirp_tokenize_cstr("let x = 1;\n// comment\nx = 2;", &tokens) == BS_OK);
    assert(tokens.count == 10);

    assert(tokens.tokens[0].line == 1);
    assert(tokens.tokens[0].column == 1);

    // x = 2; should be on line 3
    assert(tokens.tokens[5].type == CHIRP_TOK_IDENTIFIER);
    assert(chirp_str_view_equals_cstr(tokens.tokens[5].lexeme, "x"));
    assert(tokens.tokens[5].line == 3);
    assert(tokens.tokens[5].column == 1);

    // Verify trivia on x = 2; contains the newline and comment
    assert(chirp_str_view_starts_with(tokens.tokens[5].leading_trivia, chirp_str_view_from_cstr("\n// comment\n")));

    chirp_token_list_free(&tokens);
}

static void test_symbolic_constants(void) {
    chirp_token_list tokens;
    assert(chirp_tokenize_cstr("#foo #_bar #1", &tokens) == BS_OK);
    assert(tokens.count == 5);

    assert(tokens.tokens[0].type == CHIRP_TOK_SYMBOLIC_CONSTANT);
    assert(chirp_str_view_equals_cstr(tokens.tokens[0].lexeme, "#foo"));

    assert(tokens.tokens[1].type == CHIRP_TOK_SYMBOLIC_CONSTANT);
    assert(chirp_str_view_equals_cstr(tokens.tokens[1].lexeme, "#_bar"));

    // #1 is invalid per spec: first char after # must be alpha or _, so # is error and 1 is number
    assert(tokens.tokens[2].type == CHIRP_TOK_ERROR);
    assert(chirp_str_view_equals_cstr(tokens.tokens[2].lexeme, "#"));

    assert(tokens.tokens[3].type == CHIRP_TOK_NUMBER);
    assert(chirp_str_view_equals_cstr(tokens.tokens[3].lexeme, "1"));

    assert(tokens.tokens[4].type == CHIRP_TOK_EOF);

    chirp_token_list_free(&tokens);
}

static void test_format_strings(void) {
    // 1. Literal f-string without interpolation
    {
        chirp_token_list tokens;
        assert(chirp_tokenize_cstr("f\"hello world\"", &tokens) == BS_OK);
        assert(tokens.count == 2);
        assert(tokens.tokens[0].type == CHIRP_TOK_FSTRING_LITERAL);
        assert(chirp_str_view_equals_cstr(tokens.tokens[0].lexeme, "f\"hello world\""));
        chirp_token_list_free(&tokens);
    }

    // 2. Simple interpolation
    {
        chirp_token_list tokens;
        assert(chirp_tokenize_cstr("f\"Hello {name}!\"", &tokens) == BS_OK);
        assert(tokens.count == 4);
        assert(tokens.tokens[0].type == CHIRP_TOK_FSTRING_HEAD);
        assert(chirp_str_view_equals_cstr(tokens.tokens[0].lexeme, "f\"Hello {"));
        assert(tokens.tokens[1].type == CHIRP_TOK_IDENTIFIER);
        assert(chirp_str_view_equals_cstr(tokens.tokens[1].lexeme, "name"));
        assert(tokens.tokens[2].type == CHIRP_TOK_FSTRING_TAIL);
        assert(chirp_str_view_equals_cstr(tokens.tokens[2].lexeme, "}!\""));
        chirp_token_list_free(&tokens);
    }

    // 3. Multiple interpolations
    {
        chirp_token_list tokens;
        assert(chirp_tokenize_cstr("f\"a={a}, b={b}\"", &tokens) == BS_OK);
        assert(tokens.count == 6);
        assert(tokens.tokens[0].type == CHIRP_TOK_FSTRING_HEAD);
        assert(tokens.tokens[1].type == CHIRP_TOK_IDENTIFIER);
        assert(tokens.tokens[2].type == CHIRP_TOK_FSTRING_MIDDLE);
        assert(tokens.tokens[3].type == CHIRP_TOK_IDENTIFIER);
        assert(tokens.tokens[4].type == CHIRP_TOK_FSTRING_TAIL);
        assert(tokens.tokens[5].type == CHIRP_TOK_EOF);
        chirp_token_list_free(&tokens);
    }

    // 4. Nested f-strings: f"This is {f"a {inner} string"}!"
    {
        chirp_token_list tokens;
        assert(chirp_tokenize_cstr("f\"This is {f\"a {inner} string\"}!\"", &tokens) == BS_OK);
        assert(tokens.count == 6);
        assert(tokens.tokens[0].type == CHIRP_TOK_FSTRING_HEAD); // f"This is {
        assert(tokens.tokens[1].type == CHIRP_TOK_FSTRING_HEAD); // f"a {
        assert(tokens.tokens[2].type == CHIRP_TOK_IDENTIFIER);   // inner
        assert(tokens.tokens[3].type == CHIRP_TOK_FSTRING_TAIL); // } string"
        assert(tokens.tokens[4].type == CHIRP_TOK_FSTRING_TAIL); // }!"
        assert(tokens.tokens[5].type == CHIRP_TOK_EOF);
        chirp_token_list_free(&tokens);
    }

    // 5. Nested braces in f-string: f"Value: {do { let x = 5; }}"
    {
        chirp_token_list tokens;
        assert(chirp_tokenize_cstr("f\"Value: {do { let x = 5; }}\"", &tokens) == BS_OK);
        // Expect: HEAD, do, LBRACE, let, x, =, 5, SEMICOLON, RBRACE, TAIL, EOF
        assert(tokens.tokens[0].type == CHIRP_TOK_FSTRING_HEAD);
        assert(tokens.tokens[1].type == CHIRP_TOK_KW_DO);
        assert(tokens.tokens[2].type == CHIRP_TOK_LBRACE);
        assert(tokens.tokens[3].type == CHIRP_TOK_KW_LET);
        assert(tokens.tokens[4].type == CHIRP_TOK_IDENTIFIER);
        assert(tokens.tokens[5].type == CHIRP_TOK_EQUAL);
        assert(tokens.tokens[6].type == CHIRP_TOK_NUMBER);
        assert(tokens.tokens[7].type == CHIRP_TOK_SEMICOLON);
        assert(tokens.tokens[8].type == CHIRP_TOK_RBRACE);
        assert(tokens.tokens[9].type == CHIRP_TOK_FSTRING_TAIL);
        assert(tokens.tokens[10].type == CHIRP_TOK_EOF);
        chirp_token_list_free(&tokens);
    }

    // 6. Unclosed f-string produces error token
    {
        chirp_token_list tokens;
        assert(chirp_tokenize_cstr("f\"Hello {world", &tokens) == BS_OK);
        assert(tokens.count == 4);
        assert(tokens.tokens[0].type == CHIRP_TOK_FSTRING_HEAD);
        assert(tokens.tokens[1].type == CHIRP_TOK_IDENTIFIER);
        assert(tokens.tokens[2].type == CHIRP_TOK_ERROR);
        assert(tokens.tokens[3].type == CHIRP_TOK_EOF);
        chirp_token_list_free(&tokens);
    }

    // 7. Unclosed f-string with dangling quote as in conformance test
    {
        chirp_token_list tokens;
        assert(chirp_tokenize_cstr("let broken = f\"Hello {world\";", &tokens) == BS_OK);
        // let, broken, =, f"Hello {, world, ERROR (from unclosed string ";), EOF
        assert(tokens.tokens[0].type == CHIRP_TOK_KW_LET);
        assert(tokens.tokens[1].type == CHIRP_TOK_IDENTIFIER);
        assert(tokens.tokens[2].type == CHIRP_TOK_EQUAL);
        assert(tokens.tokens[3].type == CHIRP_TOK_FSTRING_HEAD);
        assert(tokens.tokens[4].type == CHIRP_TOK_IDENTIFIER);
        assert(tokens.tokens[5].type == CHIRP_TOK_ERROR);
        chirp_token_list_free(&tokens);
    }
}

static void test_format_text(void) {
    const char *source = "let a = b `and c;\nlet d = e `or f;\nlet j = k `in l;\nlet m = n `notin o;";
    const char *expected = "let a = b ∩ c;\nlet d = e ∪ f;\nlet j = k ∈ l;\nlet m = n ∉ o;";

    char *formatted = NULL;
    size_t len = 0;
    assert(chirp_format_text(source, strlen(source), &formatted, &len) == BS_OK);
    assert(formatted != NULL);
    assert(strcmp(formatted, expected) == 0);
    bs_free(formatted);

    // Verify already formatted or unrelated stuff untouched
    const char *source2 = "let x = 1; // comment\nlet y = `any;";
    assert(chirp_format_text(source2, strlen(source2), &formatted, &len) == BS_OK);
    assert(strcmp(formatted, source2) == 0);
    bs_free(formatted);

    // Overflow protection on length
    assert(chirp_format_text(source, SIZE_MAX - 10, &formatted, &len) == BS_OUT_OF_MEMORY);
}

int main(void) {
    printf("Running chirp_lexer tests...\n");

    test_str_views();
    test_basic_tokens();
    test_unicode_operators_and_columns();
    test_intrinsics();
    test_range_operators();
    test_decimal_numbers_require_fraction_digits();
    test_character_literals_valid();
    test_character_literals_reject_malformed();
    test_keywords_and_contextual_identifiers();
    test_mutable_pointer_operators_fused();
    test_locations_and_comments();
    test_symbolic_constants();
    test_format_strings();
    test_format_text();

    printf("All chirp_lexer tests passed successfully!\n");
    return 0;
}
