/**
 * @file chirp_parser.c
 * @brief Recursive-descent and Pratt parser implementation for Chirp.
 */

#include "chirp_frontend/chirp_parser.h"
#include "chirp_frontend/chirp_lexer.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct chirp_parser {
    const chirp_token *tokens;
    size_t count;
    size_t current;
    chirp_ast_arena *arena;
    chirp_parser_error *error;
    bool had_error;
    bs_status error_status;
} chirp_parser;

typedef struct parser_checkpoint {
    size_t current;
    bool had_error;
    bs_status error_status;
} parser_checkpoint;

static parser_checkpoint parser_save(const chirp_parser *parser) {
    parser_checkpoint cp;
    cp.current = parser->current;
    cp.had_error = parser->had_error;
    cp.error_status = parser->error_status;
    return cp;
}

static void parser_restore(chirp_parser *parser, parser_checkpoint cp) {
    parser->current = cp.current;
    parser->had_error = cp.had_error;
    parser->error_status = cp.error_status;
}

static void parser_fail(chirp_parser *parser, chirp_token token, const char *msg) {
    if (!parser->had_error) {
        parser->had_error = true;
        parser->error_status = BS_FRONTEND_SYNTAX_ERROR;
        if (parser->error != NULL) {
            parser->error->token = token;
            snprintf(parser->error->message, sizeof(parser->error->message), "%s", msg);
        }
    }
}

static const chirp_token *peek(const chirp_parser *p);

static void parser_oom(chirp_parser *parser) {
    if (!parser->had_error) {
        parser->had_error = true;
        parser->error_status = BS_OUT_OF_MEMORY;
        if (parser->error != NULL) {
            parser->error->token = *peek(parser);
            snprintf(parser->error->message, sizeof(parser->error->message), "Out of memory during parsing");
        }
    }
}

static inline chirp_expr *check_expr_alloc(chirp_parser *p, chirp_expr *e) {
    if (e == NULL && !p->had_error) {
        parser_oom(p);
    }
    return e;
}

static inline chirp_stmt *check_stmt_alloc(chirp_parser *p, chirp_stmt *s) {
    if (s == NULL && !p->had_error) {
        parser_oom(p);
    }
    return s;
}

static const chirp_token *peek(const chirp_parser *p) {
    if (p->current < p->count) {
        return &p->tokens[p->current];
    }
    return &p->tokens[p->count - 1]; // EOF token
}

static const chirp_token *peek_next(const chirp_parser *p) {
    if (p->current + 1 < p->count) {
        return &p->tokens[p->current + 1];
    }
    return &p->tokens[p->count - 1]; // EOF token
}

static const chirp_token *previous(const chirp_parser *p) {
    assert(p->current > 0);
    return &p->tokens[p->current - 1];
}

static bool is_at_end(const chirp_parser *p) {
    return peek(p)->type == CHIRP_TOK_EOF;
}

static bool check(const chirp_parser *p, chirp_token_type type) {
    if (is_at_end(p)) return false;
    return peek(p)->type == type;
}

static const chirp_token *advance(chirp_parser *p) {
    if (!is_at_end(p)) {
        p->current++;
    }
    return previous(p);
}

static bool match(chirp_parser *p, chirp_token_type type) {
    if (check(p, type)) {
        advance(p);
        return true;
    }
    return false;
}

static const chirp_token *consume(chirp_parser *p, chirp_token_type type, const char *msg) {
    if (check(p, type)) {
        return advance(p);
    }
    parser_fail(p, *peek(p), msg);
    return peek(p);
}

/* ========================================================================= */
/* Dynamic Buffer & Vector Helpers                                           */
/* ========================================================================= */

static bool buf_grow(chirp_parser *p, void **buf, size_t *cap, size_t count, size_t elem_size) {
    if (count < *cap) return true;
    if (elem_size == 0 || *cap > (SIZE_MAX / elem_size) / 2) {
        parser_oom(p);
        return false;
    }
    size_t new_cap = *cap == 0 ? 4 : *cap * 2;
    void *new_buf = bs_realloc(*buf, new_cap * elem_size);
    if (new_buf == NULL) {
        parser_oom(p);
        return false;
    }
    *buf = new_buf;
    *cap = new_cap;
    return true;
}

typedef struct ptr_vec {
    void **items;
    size_t count;
    size_t cap;
} ptr_vec;

static void ptr_vec_init(ptr_vec *v) {
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static bool ptr_vec_push(chirp_parser *p, ptr_vec *v, void *item) {
    if (v->count >= v->cap) {
        if (v->cap > (SIZE_MAX / sizeof(void *)) / 2) {
            parser_oom(p);
            return false;
        }
        size_t new_cap = v->cap == 0 ? 8 : v->cap * 2;
        void **new_items = (void **)bs_realloc(v->items, new_cap * sizeof(void *));
        if (new_items == NULL) {
            parser_oom(p);
            return false;
        }
        v->items = new_items;
        v->cap = new_cap;
    }
    v->items[v->count++] = item;
    return true;
}

static void ptr_vec_free(ptr_vec *v) {
    if (v->items != NULL) {
        bs_free(v->items);
        v->items = NULL;
    }
    v->count = 0;
    v->cap = 0;
}

/* ========================================================================= */
/* Forward Declarations of Grammar Levels                                    */
/* ========================================================================= */

static chirp_stmt *statement(chirp_parser *p);
static chirp_stmt *let_declaration(chirp_parser *p);
static chirp_stmt *break_statement(chirp_parser *p);
static chirp_stmt *debug_statement(chirp_parser *p);
static chirp_stmt *if_leading_statement(chirp_parser *p);
static chirp_stmt *if_statement(chirp_parser *p);
static chirp_stmt *statement_branch(chirp_parser *p);
static chirp_stmt *expression_statement(chirp_parser *p);

static chirp_expr *expression(chirp_parser *p);
static chirp_expr *logic_or(chirp_parser *p);
static chirp_expr *logic_and(chirp_parser *p);
static chirp_expr *equality(chirp_parser *p);
static chirp_expr *comparison(chirp_parser *p);
static chirp_expr *range_expr(chirp_parser *p);
static chirp_expr *term(chirp_parser *p);
static chirp_expr *factor(chirp_parser *p);
static chirp_expr *unary(chirp_parser *p);
static chirp_expr *postfix(chirp_parser *p);
static chirp_expr *primary(chirp_parser *p);

/* ========================================================================= */
/* Binding Parser                                                            */
/* ========================================================================= */

static bool is_binding_name_token(chirp_token_type type) {
    return type == CHIRP_TOK_IDENTIFIER || type == CHIRP_TOK_INTRINSIC;
}

static bool is_contextual_binding_modifier(const chirp_token *tok) {
    if (tok->type != CHIRP_TOK_IDENTIFIER) return false;
    return chirp_str_view_equals_cstr(tok->lexeme, "mut") ||
           chirp_str_view_equals_cstr(tok->lexeme, "final") ||
           chirp_str_view_equals_cstr(tok->lexeme, "pub");
}

static bool modifier_has_binding_name_after(const chirp_parser *p) {
    if (!is_contextual_binding_modifier(peek(p))) return false;
    return is_binding_name_token(peek_next(p)->type);
}

static chirp_token consume_binding_name(chirp_parser *p) {
    if (is_binding_name_token(peek(p)->type)) {
        return *advance(p);
    }
    parser_fail(p, *peek(p), "Expect identifier for binding.");
    return *peek(p);
}

static chirp_named_binding parse_binding(
    chirp_parser *p,
    bool require_initializer,
    bool allow_initializer,
    bool allow_function_sugar,
    bool *out_is_public
) {
    chirp_named_binding binding;
    memset(&binding, 0, sizeof(binding));

    bool is_mut = false;
    bool is_final = false;

    while (modifier_has_binding_name_after(p)) {
        const chirp_token *modifier = advance(p);
        if (chirp_str_view_equals_cstr(modifier->lexeme, "mut")) {
            if (is_mut) {
                parser_fail(p, *modifier, "Duplicate 'mut' binding modifier.");
                return binding;
            }
            is_mut = true;
        } else if (chirp_str_view_equals_cstr(modifier->lexeme, "final")) {
            if (is_final) {
                parser_fail(p, *modifier, "Duplicate 'final' binding modifier.");
                return binding;
            }
            is_final = true;
        } else if (chirp_str_view_equals_cstr(modifier->lexeme, "pub")) {
            if (out_is_public != NULL) {
                if (*out_is_public) {
                    parser_fail(p, *modifier, "Duplicate 'pub' binding modifier.");
                    return binding;
                }
                *out_is_public = true;
            } else {
                parser_fail(p, *modifier, "Modifier 'pub' is not allowed in this context.");
                return binding;
            }
        }
    }

    chirp_token name = consume_binding_name(p);
    if (p->had_error) return binding;

    bool is_function = false;
    chirp_binding_list lambda_params = { .items = NULL, .count = 0 };
    chirp_token paren;
    memset(&paren, 0, sizeof(paren));

    if (allow_function_sugar && check(p, CHIRP_TOK_LPAREN)) {
        paren = *peek(p);
        match(p, CHIRP_TOK_LPAREN);
        is_function = true;

        if (!check(p, CHIRP_TOK_RPAREN)) {
            size_t cap = 0;
            chirp_named_binding *param_buf = NULL;
            size_t count = 0;

            do {
                if (!buf_grow(p, (void **)&param_buf, &cap, count, sizeof(chirp_named_binding))) {
                    bs_free(param_buf);
                    return binding;
                }
                param_buf[count++] = parse_binding(p, false, false, false, NULL);
                if (p->had_error) {
                    bs_free(param_buf);
                    return binding;
                }
            } while (match(p, CHIRP_TOK_COMMA));

            chirp_named_binding *arena_params = NULL;
            if (count > 0) {
                arena_params = (chirp_named_binding *)chirp_ast_arena_alloc_array(
                    p->arena, count, sizeof(chirp_named_binding)
                );
                if (arena_params == NULL) {
                    bs_free(param_buf);
                    parser_oom(p);
                    return binding;
                }
                memcpy(arena_params, param_buf, count * sizeof(chirp_named_binding));
            }
            bs_free(param_buf);

            lambda_params.items = arena_params;
            lambda_params.count = count;
        }
        consume(p, CHIRP_TOK_RPAREN, "Expect ')' after function parameters.");
        if (p->had_error) return binding;
    }

    chirp_expr *type_bound = NULL;
    if (match(p, CHIRP_TOK_COLON)) {
        type_bound = expression(p);
        if (p->had_error) return binding;
    }

    chirp_expr *initializer = NULL;
    if (match(p, CHIRP_TOK_EQUAL)) {
        if (!allow_initializer) {
            parser_fail(p, *previous(p), "Initializer not allowed in this binding context.");
            return binding;
        }
        initializer = expression(p);
        if (p->had_error) return binding;
    } else if (require_initializer) {
        parser_fail(p, *peek(p), "Expect '=' and initializer for binding.");
        return binding;
    }

    if (is_function) {
        if (initializer == NULL) {
            parser_fail(p, *peek(p), "Function binding must have a body.");
            return binding;
        }
        initializer = check_expr_alloc(p, chirp_ast_new_lambda(p->arena, lambda_params, type_bound, initializer, paren));
        if (initializer == NULL) return binding;
        type_bound = NULL;
    }

    binding.name = name;
    binding.is_mut = is_mut;
    binding.is_final = is_final;
    binding.type_bound = type_bound;
    binding.initializer = initializer;
    return binding;
}

/* ========================================================================= */
/* Statements                                                                */
/* ========================================================================= */

static chirp_stmt *statement(chirp_parser *p) {
    if (p->had_error) return NULL;
    if (match(p, CHIRP_TOK_KW_LET)) return let_declaration(p);
    if (match(p, CHIRP_TOK_KW_BREAK)) return break_statement(p);
    if (match(p, CHIRP_TOK_KW_DEBUG)) return debug_statement(p);
    if (check(p, CHIRP_TOK_KW_IF)) return if_leading_statement(p);
    return expression_statement(p);
}

static chirp_stmt *debug_statement(chirp_parser *p) {
    chirp_token debug_tok = *previous(p);
    consume(p, CHIRP_TOK_LBRACE, "Expect '{' after 'debug'.");
    if (p->had_error) return NULL;

    ptr_vec stmts;
    ptr_vec_init(&stmts);

    while (!check(p, CHIRP_TOK_RBRACE) && !is_at_end(p)) {
        chirp_stmt *s = statement(p);
        if (p->had_error || s == NULL) {
            ptr_vec_free(&stmts);
            return NULL;
        }
        if (s->kind == CHIRP_STMT_LET) {
            ptr_vec_free(&stmts);
            parser_fail(p, s->diagnostic_token, "Cannot use 'let' inside a 'debug' block.");
            return NULL;
        }
        if (!ptr_vec_push(p, &stmts, s)) {
            ptr_vec_free(&stmts);
            return NULL;
        }
    }
    consume(p, CHIRP_TOK_RBRACE, "Expect '}' after debug block.");
    if (p->had_error) {
        ptr_vec_free(&stmts);
        return NULL;
    }

    chirp_stmt **arena_stmts = NULL;
    if (stmts.count > 0) {
        arena_stmts = (chirp_stmt **)chirp_ast_arena_alloc_array(p->arena, stmts.count, sizeof(chirp_stmt *));
        if (arena_stmts == NULL) {
            ptr_vec_free(&stmts);
            parser_oom(p);
            return NULL;
        }
        memcpy(arena_stmts, stmts.items, stmts.count * sizeof(chirp_stmt *));
    }
    size_t count = stmts.count;
    ptr_vec_free(&stmts);

    chirp_stmt_list list = { .items = arena_stmts, .count = count };
    return check_stmt_alloc(p, chirp_ast_new_debug_stmt(p->arena, list, debug_tok));
}

static chirp_stmt *let_declaration(chirp_parser *p) {
    chirp_token let_tok = *previous(p);
    bool is_public = false;
    chirp_named_binding binding = parse_binding(p, true, true, true, &is_public);
    if (p->had_error) return NULL;
    consume(p, CHIRP_TOK_SEMICOLON, "Expect ';' after let declaration.");
    if (p->had_error) return NULL;
    return chirp_ast_new_let_stmt(p->arena, binding, is_public, let_tok);
}

static chirp_stmt *break_statement(chirp_parser *p) {
    chirp_token break_tok = *previous(p);
    chirp_expr *value = NULL;

    if (!match(p, CHIRP_TOK_SEMICOLON)) {
        value = expression(p);
        if (p->had_error) return NULL;
        consume(p, CHIRP_TOK_SEMICOLON, "Expect ';' after break value.");
        if (p->had_error) return NULL;
    }
    return chirp_ast_new_break_stmt(p->arena, value, break_tok);
}

static chirp_stmt *if_leading_statement(chirp_parser *p) {
    parser_checkpoint cp = parser_save(p);
    chirp_stmt *candidate = expression_statement(p);
    if (!p->had_error && candidate != NULL && candidate->kind == CHIRP_STMT_EXPR) {
        return candidate;
    }

    parser_restore(p, cp);
    match(p, CHIRP_TOK_KW_IF);
    return if_statement(p);
}

static chirp_stmt *statement_branch(chirp_parser *p) {
    if (check(p, CHIRP_TOK_LBRACE) || check(p, CHIRP_TOK_KW_DO)) {
        chirp_token start_tok = *peek(p);
        chirp_expr *expr = expression(p);
        if (p->had_error) return NULL;
        return chirp_ast_new_expr_stmt(p->arena, expr, start_tok);
    }
    return statement(p);
}

static chirp_stmt *if_statement(chirp_parser *p) {
    chirp_token if_tok = *previous(p);
    consume(p, CHIRP_TOK_LPAREN, "Expect '(' after 'if'.");
    chirp_expr *cond = expression(p);
    consume(p, CHIRP_TOK_RPAREN, "Expect ')' after if condition.");
    if (p->had_error) return NULL;

    chirp_stmt *then_branch = statement_branch(p);
    if (p->had_error) return NULL;

    chirp_stmt *else_branch = NULL;
    if (match(p, CHIRP_TOK_KW_ELSE)) {
        else_branch = statement_branch(p);
        if (p->had_error) return NULL;
    }
    match(p, CHIRP_TOK_SEMICOLON); // optional trailing semicolon

    return chirp_ast_new_if_stmt(p->arena, cond, then_branch, else_branch, if_tok);
}

static bool is_assignment_operator(chirp_token_type type) {
    return type == CHIRP_TOK_EQUAL ||
           type == CHIRP_TOK_PLUS_EQUAL ||
           type == CHIRP_TOK_MINUS_EQUAL ||
           type == CHIRP_TOK_STAR_EQUAL ||
           type == CHIRP_TOK_SLASH_EQUAL ||
           type == CHIRP_TOK_PERCENT_EQUAL;
}

static chirp_stmt *expression_statement(chirp_parser *p) {
    chirp_token start_tok = *peek(p);
    chirp_expr *expr = expression(p);
    if (p->had_error) return NULL;

    if (is_assignment_operator(peek(p)->type)) {
        chirp_token op = *advance(p);
        chirp_expr *val = expression(p);
        if (p->had_error) return NULL;
        consume(p, CHIRP_TOK_SEMICOLON, "Expect ';' after assignment.");
        if (p->had_error) return NULL;
        return chirp_ast_new_assign_stmt(p->arena, expr, op, val, start_tok);
    }

    consume(p, CHIRP_TOK_SEMICOLON, "Expect ';' after expression.");
    if (p->had_error) return NULL;
    return chirp_ast_new_expr_stmt(p->arena, expr, start_tok);
}

/* ========================================================================= */
/* Expressions (Pratt Precedence Climbing / Layered Recursive Descent)       */
/* ========================================================================= */

static chirp_expr *expression(chirp_parser *p) {
    return logic_or(p);
}

static chirp_expr *logic_or(chirp_parser *p) {
    chirp_expr *expr = logic_and(p);
    while (match(p, CHIRP_TOK_OR_OR)) {
        chirp_token op_tok = *previous(p);
        chirp_expr *right = logic_and(p);
        if (p->had_error) return NULL;
        expr = chirp_ast_new_binary(p->arena, expr, CHIRP_BINOP_OR, right, op_tok);
    }
    return expr;
}

static chirp_expr *logic_and(chirp_parser *p) {
    chirp_expr *expr = equality(p);
    while (match(p, CHIRP_TOK_AND_AND)) {
        chirp_token op_tok = *previous(p);
        chirp_expr *right = equality(p);
        if (p->had_error) return NULL;
        expr = chirp_ast_new_binary(p->arena, expr, CHIRP_BINOP_AND, right, op_tok);
    }
    return expr;
}

static chirp_binary_op get_binary_op(chirp_token_type t) {
    switch (t) {
        case CHIRP_TOK_PLUS:              return CHIRP_BINOP_ADD;
        case CHIRP_TOK_MINUS:             return CHIRP_BINOP_SUB;
        case CHIRP_TOK_STAR:              return CHIRP_BINOP_MUL;
        case CHIRP_TOK_SLASH:             return CHIRP_BINOP_DIV;
        case CHIRP_TOK_PERCENT:           return CHIRP_BINOP_MOD;
        case CHIRP_TOK_EQUAL_EQUAL:       return CHIRP_BINOP_EQ;
        case CHIRP_TOK_BANG_EQUAL:        return CHIRP_BINOP_NEQ;
        case CHIRP_TOK_LESS:              return CHIRP_BINOP_LT;
        case CHIRP_TOK_LESS_EQUAL:        return CHIRP_BINOP_LTE;
        case CHIRP_TOK_GREATER:           return CHIRP_BINOP_GT;
        case CHIRP_TOK_GREATER_EQUAL:     return CHIRP_BINOP_GTE;
        case CHIRP_TOK_AND_AND:           return CHIRP_BINOP_AND;
        case CHIRP_TOK_OR_OR:             return CHIRP_BINOP_OR;
        case CHIRP_TOK_IN:                return CHIRP_BINOP_IN;
        case CHIRP_TOK_NOT_IN:            return CHIRP_BINOP_NOT_IN;
        case CHIRP_TOK_UNION:             return CHIRP_BINOP_UNION;
        case CHIRP_TOK_INTERSECTION:      return CHIRP_BINOP_INTERSECTION;
        case CHIRP_TOK_DOT_DOT:           return CHIRP_BINOP_RANGE;
        case CHIRP_TOK_DOT_DOT_EQUAL:     return CHIRP_BINOP_RANGE_INCLUSIVE_END;
        case CHIRP_TOK_DOT:               return CHIRP_BINOP_DOT;
        default:                          return CHIRP_BINOP_ADD;
    }
}

static chirp_unary_op get_unary_op(chirp_token_type t) {
    switch (t) {
        case CHIRP_TOK_BANG:  return CHIRP_UNOP_NOT;
        case CHIRP_TOK_MINUS: return CHIRP_UNOP_NEGATE;
        case CHIRP_TOK_STAR:  return CHIRP_UNOP_DEREF;
        case CHIRP_TOK_TILDE: return CHIRP_UNOP_COMPLEMENT;
        default:              return CHIRP_UNOP_NOT;
    }
}

static chirp_expr *equality(chirp_parser *p) {
    chirp_expr *expr = comparison(p);
    while (match(p, CHIRP_TOK_EQUAL_EQUAL) || match(p, CHIRP_TOK_BANG_EQUAL)) {
        chirp_token op_tok = *previous(p);
        chirp_expr *right = comparison(p);
        if (p->had_error) return NULL;
        expr = chirp_ast_new_binary(p->arena, expr, get_binary_op(op_tok.type), right, op_tok);
    }
    return expr;
}

static chirp_expr *comparison(chirp_parser *p) {
    chirp_expr *expr = range_expr(p);
    while (match(p, CHIRP_TOK_GREATER) || match(p, CHIRP_TOK_GREATER_EQUAL) ||
           match(p, CHIRP_TOK_LESS) || match(p, CHIRP_TOK_LESS_EQUAL) ||
           match(p, CHIRP_TOK_IN) || match(p, CHIRP_TOK_NOT_IN)) {
        chirp_token op_tok = *previous(p);
        chirp_expr *right = range_expr(p);
        if (p->had_error) return NULL;
        expr = chirp_ast_new_binary(p->arena, expr, get_binary_op(op_tok.type), right, op_tok);
    }
    return expr;
}

static chirp_expr *range_expr(chirp_parser *p) {
    chirp_expr *expr = term(p);
    while (match(p, CHIRP_TOK_DOT_DOT) || match(p, CHIRP_TOK_DOT_DOT_EQUAL)) {
        chirp_token op_tok = *previous(p);
        chirp_expr *right = term(p);
        if (p->had_error) return NULL;
        expr = chirp_ast_new_binary(p->arena, expr, get_binary_op(op_tok.type), right, op_tok);
    }
    return expr;
}

static chirp_expr *term(chirp_parser *p) {
    chirp_expr *expr = factor(p);
    while (match(p, CHIRP_TOK_MINUS) || match(p, CHIRP_TOK_PLUS) || match(p, CHIRP_TOK_UNION)) {
        chirp_token op_tok = *previous(p);
        chirp_expr *right = factor(p);
        if (p->had_error) return NULL;
        expr = chirp_ast_new_binary(p->arena, expr, get_binary_op(op_tok.type), right, op_tok);
    }
    return expr;
}

static chirp_expr *factor(chirp_parser *p) {
    chirp_expr *expr = unary(p);
    while (match(p, CHIRP_TOK_SLASH) || match(p, CHIRP_TOK_STAR) ||
           match(p, CHIRP_TOK_PERCENT) || match(p, CHIRP_TOK_INTERSECTION)) {
        chirp_token op_tok = *previous(p);
        chirp_expr *right = unary(p);
        if (p->had_error) return NULL;
        expr = chirp_ast_new_binary(p->arena, expr, get_binary_op(op_tok.type), right, op_tok);
    }
    return expr;
}

static chirp_expr *unary(chirp_parser *p) {
    if (match(p, CHIRP_TOK_AMPERSAND) || match(p, CHIRP_TOK_AMPERSAND_MUT)) {
        chirp_token op_tok = *previous(p);
        chirp_unary_op op = op_tok.type == CHIRP_TOK_AMPERSAND_MUT
            ? CHIRP_UNOP_MUTABLE_ADDRESS_OF
            : CHIRP_UNOP_ADDRESS_OF;
        chirp_expr *right = unary(p);
        if (p->had_error) return NULL;
        return chirp_ast_new_unary(p->arena, op, right, op_tok);
    }

    if (match(p, CHIRP_TOK_ARROW) || match(p, CHIRP_TOK_ARROW_MUT)) {
        chirp_token op_tok = *previous(p);
        chirp_unary_op op = op_tok.type == CHIRP_TOK_ARROW_MUT
            ? CHIRP_UNOP_MUTABLE_POINTER_TYPE
            : CHIRP_UNOP_POINTER_TYPE;
        chirp_expr *right = unary(p);
        if (p->had_error) return NULL;
        return chirp_ast_new_unary(p->arena, op, right, op_tok);
    }

    if (match(p, CHIRP_TOK_BANG) || match(p, CHIRP_TOK_MINUS) ||
        match(p, CHIRP_TOK_STAR) || match(p, CHIRP_TOK_TILDE)) {
        chirp_token op_tok = *previous(p);
        chirp_expr *right = unary(p);
        if (p->had_error) return NULL;
        return chirp_ast_new_unary(p->arena, get_unary_op(op_tok.type), right, op_tok);
    }

    return postfix(p);
}

static chirp_expr *postfix(chirp_parser *p) {
    chirp_expr *expr = primary(p);
    if (p->had_error) return NULL;

    while (true) {
        if (match(p, CHIRP_TOK_DOT)) {
            chirp_token op_tok = *previous(p);
            chirp_expr *right = primary(p);
            if (p->had_error) return NULL;
            expr = chirp_ast_new_binary(p->arena, expr, CHIRP_BINOP_DOT, right, op_tok);
        } else if (match(p, CHIRP_TOK_LPAREN)) {
            chirp_token paren = *previous(p);
            bool has_named = false;
            bool has_positional = false;

            size_t cap = 0;
            chirp_ast_arg *arg_buf = NULL;
            size_t count = 0;

            if (!check(p, CHIRP_TOK_RPAREN)) {
                do {
                    if (!buf_grow(p, (void **)&arg_buf, &cap, count, sizeof(chirp_ast_arg))) {
                        bs_free(arg_buf);
                        return NULL;
                    }
                    if (check(p, CHIRP_TOK_IDENTIFIER) && peek_next(p)->type == CHIRP_TOK_EQUAL) {
                        chirp_token name = *advance(p);
                        advance(p); // consume '='
                        chirp_expr *val = expression(p);
                        if (p->had_error || val == NULL) { bs_free(arg_buf); return NULL; }

                        arg_buf[count].name = name;
                        arg_buf[count].has_name = true;
                        arg_buf[count].value = val;
                        count++;
                        has_named = true;
                    } else {
                        chirp_expr *val = expression(p);
                        if (p->had_error || val == NULL) { bs_free(arg_buf); return NULL; }

                        memset(&arg_buf[count].name, 0, sizeof(chirp_token));
                        arg_buf[count].has_name = false;
                        arg_buf[count].value = val;
                        count++;
                        has_positional = true;
                    }
                } while (match(p, CHIRP_TOK_COMMA));
            }
            consume(p, CHIRP_TOK_RPAREN, "Expect ')' after arguments.");
            if (p->had_error) {
                bs_free(arg_buf);
                return NULL;
            }

            if (has_named && has_positional) {
                bs_free(arg_buf);
                parser_fail(p, paren, "Cannot mix named and positional arguments.");
                return NULL;
            }

            chirp_ast_arg *arena_args = NULL;
            if (count > 0) {
                arena_args = (chirp_ast_arg *)chirp_ast_arena_alloc_array(p->arena, count, sizeof(chirp_ast_arg));
                if (arena_args == NULL) {
                    bs_free(arg_buf);
                    parser_oom(p);
                    return NULL;
                }
                memcpy(arena_args, arg_buf, count * sizeof(chirp_ast_arg));
            }
            bs_free(arg_buf);

            chirp_ast_arg_list args = { .items = arena_args, .count = count };
            expr = check_expr_alloc(p, chirp_ast_new_call(p->arena, expr, args, paren));
            if (p->had_error) return NULL;
        } else if (match(p, CHIRP_TOK_LBRACKET)) {
            chirp_token bracket = *previous(p);
            size_t cap = 0;
            chirp_ast_arg *arg_buf = NULL;
            size_t count = 0;

            if (!check(p, CHIRP_TOK_RBRACKET)) {
                do {
                    if (!buf_grow(p, (void **)&arg_buf, &cap, count, sizeof(chirp_ast_arg))) {
                        bs_free(arg_buf);
                        return NULL;
                    }
                    chirp_expr *val = expression(p);
                    if (p->had_error || val == NULL) { bs_free(arg_buf); return NULL; }

                    memset(&arg_buf[count].name, 0, sizeof(chirp_token));
                    arg_buf[count].has_name = false;
                    arg_buf[count].value = val;
                    count++;
                } while (match(p, CHIRP_TOK_COMMA));
            } else {
                parser_fail(p, bracket, "Expect at least one argument for indexing.");
                return NULL;
            }
            consume(p, CHIRP_TOK_RBRACKET, "Expect ']' after index arguments.");
            if (p->had_error) {
                bs_free(arg_buf);
                return NULL;
            }

            chirp_ast_arg *arena_args = (chirp_ast_arg *)chirp_ast_arena_alloc_array(p->arena, count, sizeof(chirp_ast_arg));
            if (arena_args == NULL) {
                bs_free(arg_buf);
                parser_oom(p);
                return NULL;
            }
            memcpy(arena_args, arg_buf, count * sizeof(chirp_ast_arg));
            bs_free(arg_buf);

            chirp_ast_arg_list args = { .items = arena_args, .count = count };
            expr = check_expr_alloc(p, chirp_ast_new_index(p->arena, expr, args, bracket));
            if (p->had_error) return NULL;
        } else {
            break;
        }
    }
    return expr;
}

/* Scans ahead to check if (params) is followed by => or -> */
static bool is_lambda_or_signature_ahead(const chirp_parser *p) {
    size_t temp = p->current;
    int parens = 0;

    while (temp < p->count) {
        if (p->tokens[temp].type == CHIRP_TOK_LPAREN) {
            parens++;
        } else if (p->tokens[temp].type == CHIRP_TOK_RPAREN) {
            parens--;
            if (parens == 0) {
                temp++;
                break;
            }
        }
        temp++;
    }

    if (parens != 0) return false;

    if (temp < p->count) {
        if (p->tokens[temp].type == CHIRP_TOK_FAT_ARROW || p->tokens[temp].type == CHIRP_TOK_ARROW) {
            return true;
        }
        if (p->tokens[temp].type == CHIRP_TOK_COLON) {
            size_t look = temp + 1;
            while (look < p->count) {
                if (p->tokens[look].type == CHIRP_TOK_FAT_ARROW || p->tokens[look].type == CHIRP_TOK_ARROW) {
                    return true;
                }
                if (p->tokens[look].type == CHIRP_TOK_SEMICOLON ||
                    p->tokens[look].type == CHIRP_TOK_RBRACE ||
                    p->tokens[look].type == CHIRP_TOK_RPAREN ||
                    p->tokens[look].type == CHIRP_TOK_EOF) {
                    break;
                }
                look++;
            }
        }
    }
    return false;
}

static chirp_expr *primary(chirp_parser *p) {
    if (p->had_error) return NULL;

    if (match(p, CHIRP_TOK_KW_STRUCT)) {
        chirp_token struct_tok = *previous(p);
        consume(p, CHIRP_TOK_LBRACE, "Expect '{' after 'struct'.");
        if (p->had_error) return NULL;

        size_t cap = 0;
        chirp_named_binding *field_buf = NULL;
        size_t count = 0;

        if (!check(p, CHIRP_TOK_RBRACE)) {
            while (true) {
                if (!buf_grow(p, (void **)&field_buf, &cap, count, sizeof(chirp_named_binding))) {
                    bs_free(field_buf);
                    return NULL;
                }
                field_buf[count++] = parse_binding(p, false, true, true, NULL);
                if (p->had_error) { bs_free(field_buf); return NULL; }
                if (!match(p, CHIRP_TOK_COMMA)) break;
                if (check(p, CHIRP_TOK_RBRACE)) break;
            }
        }
        consume(p, CHIRP_TOK_RBRACE, "Expect '}' after struct fields.");
        if (p->had_error) { bs_free(field_buf); return NULL; }

        chirp_named_binding *arena_fields = NULL;
        if (count > 0) {
            arena_fields = (chirp_named_binding *)chirp_ast_arena_alloc_array(p->arena, count, sizeof(chirp_named_binding));
            if (arena_fields == NULL) {
                bs_free(field_buf);
                parser_oom(p);
                return NULL;
            }
            memcpy(arena_fields, field_buf, count * sizeof(chirp_named_binding));
        }
        bs_free(field_buf);

        chirp_binding_list fields = { .items = arena_fields, .count = count };
        return check_expr_alloc(p, chirp_ast_new_struct(p->arena, fields, struct_tok));
    }

    if (match(p, CHIRP_TOK_KW_ENUM)) {
        chirp_token enum_tok = *previous(p);
        consume(p, CHIRP_TOK_LBRACE, "Expect '{' after 'enum'.");
        if (p->had_error) return NULL;

        size_t cap = 0;
        chirp_str_view *var_buf = NULL;
        size_t count = 0;

        if (!check(p, CHIRP_TOK_RBRACE)) {
            while (true) {
                const chirp_token *var_tok = consume(p, CHIRP_TOK_IDENTIFIER, "Expect enum variant identifier.");
                if (p->had_error) { bs_free(var_buf); return NULL; }
                if (!buf_grow(p, (void **)&var_buf, &cap, count, sizeof(chirp_str_view))) {
                    bs_free(var_buf);
                    return NULL;
                }
                var_buf[count++] = var_tok->lexeme;
                if (!match(p, CHIRP_TOK_COMMA)) break;
                if (check(p, CHIRP_TOK_RBRACE)) break;
            }
        }
        consume(p, CHIRP_TOK_RBRACE, "Expect '}' after enum variants.");
        if (p->had_error) { bs_free(var_buf); return NULL; }

        chirp_str_view *arena_vars = NULL;
        if (count > 0) {
            arena_vars = (chirp_str_view *)chirp_ast_arena_alloc_array(p->arena, count, sizeof(chirp_str_view));
            if (arena_vars == NULL) {
                bs_free(var_buf);
                parser_oom(p);
                return NULL;
            }
            memcpy(arena_vars, var_buf, count * sizeof(chirp_str_view));
        }
        bs_free(var_buf);

        static uint64_t next_enum_id = 1;
        chirp_str_view_list variants = { .items = arena_vars, .count = count };
        return check_expr_alloc(p, chirp_ast_new_enum(p->arena, variants, next_enum_id++, enum_tok));
    }

    if (match(p, CHIRP_TOK_KW_FOR)) {
        chirp_token for_tok = *previous(p);
        consume(p, CHIRP_TOK_LPAREN, "Expect '(' after 'for'.");
        chirp_named_binding binding = parse_binding(p, false, false, false, NULL);
        consume(p, CHIRP_TOK_IN, "Expect '∈' after for loop iterator.");
        chirp_expr *iterable = expression(p);
        consume(p, CHIRP_TOK_RPAREN, "Expect ')' after for loop iterable.");
        chirp_expr *body = expression(p);
        if (p->had_error) return NULL;
        return check_expr_alloc(p, chirp_ast_new_for(p->arena, binding, iterable, body, for_tok));
    }

    if (match(p, CHIRP_TOK_KW_WHILE)) {
        chirp_token while_tok = *previous(p);
        consume(p, CHIRP_TOK_LPAREN, "Expect '(' after 'while'.");
        chirp_expr *cond = expression(p);
        consume(p, CHIRP_TOK_RPAREN, "Expect ')' after while condition.");
        chirp_expr *body = expression(p);
        if (p->had_error) return NULL;
        return check_expr_alloc(p, chirp_ast_new_while(p->arena, cond, body, while_tok));
    }

    if (match(p, CHIRP_TOK_KW_MATCH)) {
        chirp_token match_tok = *previous(p);
        chirp_expr *subject = expression(p);
        consume(p, CHIRP_TOK_LBRACE, "Expect '{' after match subject.");
        if (p->had_error) return NULL;

        size_t cap = 0;
        chirp_match_arm *arm_buf = NULL;
        size_t count = 0;

        if (!check(p, CHIRP_TOK_RBRACE)) {
            do {
                if (!buf_grow(p, (void **)&arm_buf, &cap, count, sizeof(chirp_match_arm))) {
                    bs_free(arm_buf);
                    return NULL;
                }
                chirp_expr *pattern = expression(p);
                consume(p, CHIRP_TOK_FAT_ARROW, "Expect '=>' after match pattern.");
                chirp_expr *body = expression(p);
                if (p->had_error) { bs_free(arm_buf); return NULL; }

                arm_buf[count].pattern = pattern;
                arm_buf[count].body = body;
                count++;
            } while (match(p, CHIRP_TOK_COMMA) && !check(p, CHIRP_TOK_RBRACE));
        }
        consume(p, CHIRP_TOK_RBRACE, "Expect '}' after match arms.");
        if (p->had_error) { bs_free(arm_buf); return NULL; }

        chirp_match_arm *arena_arms = NULL;
        if (count > 0) {
            arena_arms = (chirp_match_arm *)chirp_ast_arena_alloc_array(p->arena, count, sizeof(chirp_match_arm));
            if (arena_arms == NULL) {
                bs_free(arm_buf);
                parser_oom(p);
                return NULL;
            }
            memcpy(arena_arms, arm_buf, count * sizeof(chirp_match_arm));
        }
        bs_free(arm_buf);

        chirp_match_arm_list arms = { .items = arena_arms, .count = count };
        return check_expr_alloc(p, chirp_ast_new_match(p->arena, subject, arms, match_tok));
    }

    if (match(p, CHIRP_TOK_KW_IF)) {
        chirp_token if_tok = *previous(p);
        consume(p, CHIRP_TOK_LPAREN, "Expect '(' after 'if'.");
        chirp_expr *condition = expression(p);
        consume(p, CHIRP_TOK_RPAREN, "Expect ')' after if condition.");
        chirp_expr *then_branch = expression(p);
        consume(p, CHIRP_TOK_KW_ELSE, "Expect 'else' branch for if expression.");
        chirp_expr *else_branch = expression(p);
        if (p->had_error) return NULL;
        return check_expr_alloc(p, chirp_ast_new_if(p->arena, condition, then_branch, else_branch, if_tok));
    }

    if (match(p, CHIRP_TOK_LBRACKET)) {
        chirp_token bracket_tok = *previous(p);
        ptr_vec elements;
        ptr_vec_init(&elements);

        if (!check(p, CHIRP_TOK_RBRACKET)) {
            do {
                chirp_expr *el = expression(p);
                if (p->had_error || el == NULL) { ptr_vec_free(&elements); return NULL; }
                if (!ptr_vec_push(p, &elements, el)) { ptr_vec_free(&elements); return NULL; }
            } while (match(p, CHIRP_TOK_COMMA) && !check(p, CHIRP_TOK_RBRACKET));
        }
        consume(p, CHIRP_TOK_RBRACKET, "Expect ']' after list elements.");
        if (p->had_error) { ptr_vec_free(&elements); return NULL; }

        chirp_expr **arena_els = NULL;
        if (elements.count > 0) {
            arena_els = (chirp_expr **)chirp_ast_arena_alloc_array(p->arena, elements.count, sizeof(chirp_expr *));
            if (arena_els == NULL) {
                ptr_vec_free(&elements);
                parser_oom(p);
                return NULL;
            }
            memcpy(arena_els, elements.items, elements.count * sizeof(chirp_expr *));
        }
        size_t cnt = elements.count;
        ptr_vec_free(&elements);

        chirp_expr_list list = { .items = arena_els, .count = cnt };
        return check_expr_alloc(p, chirp_ast_new_list(p->arena, list, bracket_tok));
    }

    if (match(p, CHIRP_TOK_NUMBER)) return check_expr_alloc(p, chirp_ast_new_number(p->arena, previous(p)->lexeme, *previous(p)));
    if (match(p, CHIRP_TOK_STRING)) return check_expr_alloc(p, chirp_ast_new_string(p->arena, previous(p)->lexeme, *previous(p)));
    if (match(p, CHIRP_TOK_CHARACTER)) return check_expr_alloc(p, chirp_ast_new_char(p->arena, previous(p)->lexeme, *previous(p)));
    if (match(p, CHIRP_TOK_SYMBOLIC_CONSTANT)) return check_expr_alloc(p, chirp_ast_new_symbolic_constant(p->arena, previous(p)->lexeme, *previous(p)));
    if (match(p, CHIRP_TOK_IDENTIFIER)) return check_expr_alloc(p, chirp_ast_new_identifier(p->arena, previous(p)->lexeme, *previous(p)));
    if (match(p, CHIRP_TOK_INTRINSIC)) return check_expr_alloc(p, chirp_ast_new_intrinsic(p->arena, previous(p)->lexeme, *previous(p)));

    if (match(p, CHIRP_TOK_FSTRING_LITERAL)) {
        chirp_token lit_tok = *previous(p);
        chirp_expr **part = (chirp_expr **)chirp_ast_arena_alloc_array(p->arena, 1, sizeof(chirp_expr *));
        if (part == NULL) {
            parser_oom(p);
            return NULL;
        }
        part[0] = check_expr_alloc(p, chirp_ast_new_string(p->arena, lit_tok.lexeme, lit_tok));
        if (part[0] == NULL) return NULL;
        chirp_expr_list parts = { .items = part, .count = 1 };
        return check_expr_alloc(p, chirp_ast_new_fstring(p->arena, parts, lit_tok));
    }

    if (match(p, CHIRP_TOK_FSTRING_HEAD)) {
        chirp_token head_tok = *previous(p);
        ptr_vec parts;
        ptr_vec_init(&parts);

        chirp_expr *head_str = check_expr_alloc(p, chirp_ast_new_string(p->arena, head_tok.lexeme, head_tok));
        if (head_str == NULL || !ptr_vec_push(p, &parts, head_str)) {
            ptr_vec_free(&parts);
            return NULL;
        }

        while (true) {
            chirp_expr *expr = expression(p);
            if (p->had_error || expr == NULL) { ptr_vec_free(&parts); return NULL; }
            if (!ptr_vec_push(p, &parts, expr)) { ptr_vec_free(&parts); return NULL; }

            if (match(p, CHIRP_TOK_FSTRING_MIDDLE)) {
                chirp_token mid_tok = *previous(p);
                chirp_expr *mid_str = check_expr_alloc(p, chirp_ast_new_string(p->arena, mid_tok.lexeme, mid_tok));
                if (mid_str == NULL || !ptr_vec_push(p, &parts, mid_str)) {
                    ptr_vec_free(&parts);
                    return NULL;
                }
            } else if (match(p, CHIRP_TOK_FSTRING_TAIL)) {
                chirp_token tail_tok = *previous(p);
                chirp_expr *tail_str = check_expr_alloc(p, chirp_ast_new_string(p->arena, tail_tok.lexeme, tail_tok));
                if (tail_str == NULL || !ptr_vec_push(p, &parts, tail_str)) {
                    ptr_vec_free(&parts);
                    return NULL;
                }
                break;
            } else {
                ptr_vec_free(&parts);
                parser_fail(p, *peek(p), "Expect fstring middle or tail.");
                return NULL;
            }
        }

        chirp_expr **arena_parts = NULL;
        if (parts.count > 0) {
            arena_parts = (chirp_expr **)chirp_ast_arena_alloc_array(p->arena, parts.count, sizeof(chirp_expr *));
            if (arena_parts == NULL) {
                ptr_vec_free(&parts);
                parser_oom(p);
                return NULL;
            }
            memcpy(arena_parts, parts.items, parts.count * sizeof(chirp_expr *));
        }
        size_t count = parts.count;
        ptr_vec_free(&parts);

        chirp_expr_list plist = { .items = arena_parts, .count = count };
        return check_expr_alloc(p, chirp_ast_new_fstring(p->arena, plist, head_tok));
    }

    if (check(p, CHIRP_TOK_LPAREN)) {
        if (is_lambda_or_signature_ahead(p)) {
            chirp_token paren_tok = *advance(p);
            size_t cap = 0;
            chirp_named_binding *param_buf = NULL;
            size_t count = 0;

            if (!check(p, CHIRP_TOK_RPAREN)) {
                do {
                    if (!buf_grow(p, (void **)&param_buf, &cap, count, sizeof(chirp_named_binding))) {
                        bs_free(param_buf);
                        return NULL;
                    }
                    param_buf[count++] = parse_binding(p, false, false, false, NULL);
                    if (p->had_error) { bs_free(param_buf); return NULL; }
                } while (match(p, CHIRP_TOK_COMMA));
            }
            consume(p, CHIRP_TOK_RPAREN, "Expect ')' after lambda/signature parameters.");
            if (p->had_error) { bs_free(param_buf); return NULL; }

            chirp_named_binding *arena_params = NULL;
            if (count > 0) {
                arena_params = (chirp_named_binding *)chirp_ast_arena_alloc_array(p->arena, count, sizeof(chirp_named_binding));
                if (arena_params == NULL) {
                    bs_free(param_buf);
                    parser_oom(p);
                    return NULL;
                }
                memcpy(arena_params, param_buf, count * sizeof(chirp_named_binding));
            }
            bs_free(param_buf);
            chirp_binding_list params = { .items = arena_params, .count = count };

            chirp_expr *return_bound = NULL;
            if (match(p, CHIRP_TOK_COLON)) {
                return_bound = expression(p);
                if (p->had_error) return NULL;
            }

            if (match(p, CHIRP_TOK_FAT_ARROW)) {
                chirp_expr *body = expression(p);
                if (p->had_error) return NULL;
                return check_expr_alloc(p, chirp_ast_new_lambda(p->arena, params, return_bound, body, paren_tok));
            } else {
                consume(p, CHIRP_TOK_ARROW, "Expect '=>' or '->' after parameters.");
                chirp_expr *return_constraint = expression(p);
                if (p->had_error) return NULL;
                return check_expr_alloc(p, chirp_ast_new_signature(p->arena, params, return_constraint, paren_tok));
            }
        } else {
            advance(p); // consume '('
            chirp_expr *expr = expression(p);
            consume(p, CHIRP_TOK_RPAREN, "Expect ')' after grouping expression.");
            if (p->had_error) return NULL;
            return check_expr_alloc(p, chirp_ast_new_grouping(p->arena, expr, *previous(p)));
        }
    }

    if (match(p, CHIRP_TOK_KW_DO)) {
        chirp_token do_tok = *previous(p);
        consume(p, CHIRP_TOK_LBRACE, "Expect '{' after 'do'.");
        if (p->had_error) return NULL;

        ptr_vec stmts;
        ptr_vec_init(&stmts);

        while (!check(p, CHIRP_TOK_RBRACE) && !is_at_end(p)) {
            if (match(p, CHIRP_TOK_KW_LET)) {
                chirp_stmt *s = let_declaration(p);
                if (p->had_error || s == NULL) { ptr_vec_free(&stmts); return NULL; }
                if (!ptr_vec_push(p, &stmts, s)) { ptr_vec_free(&stmts); return NULL; }
            } else if (match(p, CHIRP_TOK_KW_BREAK)) {
                chirp_stmt *s = break_statement(p);
                if (p->had_error || s == NULL) { ptr_vec_free(&stmts); return NULL; }
                if (!ptr_vec_push(p, &stmts, s)) { ptr_vec_free(&stmts); return NULL; }
            } else {
                parser_checkpoint cp = parser_save(p);
                chirp_token expr_start_tok = *peek(p);
                chirp_expr *expr = expression(p);

                if (!p->had_error && expr != NULL) {
                    if (check(p, CHIRP_TOK_RBRACE)) {
                        // Trailing expression lowered to break expr;
                        chirp_stmt *s = check_stmt_alloc(p, chirp_ast_new_break_stmt(p->arena, expr, expr_start_tok));
                        if (s == NULL || !ptr_vec_push(p, &stmts, s)) {
                            ptr_vec_free(&stmts);
                            return NULL;
                        }
                        continue;
                    }
                    if (is_assignment_operator(peek(p)->type)) {
                        chirp_token op = *advance(p);
                        chirp_expr *val = expression(p);
                        if (!p->had_error && val != NULL) {
                            consume(p, CHIRP_TOK_SEMICOLON, "Expect ';' after assignment.");
                            if (!p->had_error) {
                                chirp_stmt *s = check_stmt_alloc(p, chirp_ast_new_assign_stmt(p->arena, expr, op, val, expr_start_tok));
                                if (s == NULL || !ptr_vec_push(p, &stmts, s)) {
                                    ptr_vec_free(&stmts);
                                    return NULL;
                                }
                                continue;
                            }
                        }
                    } else if (match(p, CHIRP_TOK_SEMICOLON)) {
                        chirp_stmt *s = check_stmt_alloc(p, chirp_ast_new_expr_stmt(p->arena, expr, expr_start_tok));
                        if (s == NULL || !ptr_vec_push(p, &stmts, s)) {
                            ptr_vec_free(&stmts);
                            return NULL;
                        }
                        continue;
                    }
                }

                // If speculative parse failed, rewind and parse statement
                parser_restore(p, cp);
                chirp_stmt *s = statement(p);
                if (p->had_error || s == NULL) { ptr_vec_free(&stmts); return NULL; }
                if (!ptr_vec_push(p, &stmts, s)) { ptr_vec_free(&stmts); return NULL; }
            }
        }
        consume(p, CHIRP_TOK_RBRACE, "Expect '}' after block.");
        if (p->had_error) { ptr_vec_free(&stmts); return NULL; }

        chirp_stmt **arena_stmts = NULL;
        if (stmts.count > 0) {
            arena_stmts = (chirp_stmt **)chirp_ast_arena_alloc_array(p->arena, stmts.count, sizeof(chirp_stmt *));
            if (arena_stmts == NULL) {
                ptr_vec_free(&stmts);
                parser_oom(p);
                return NULL;
            }
            memcpy(arena_stmts, stmts.items, stmts.count * sizeof(chirp_stmt *));
        }
        size_t count = stmts.count;
        ptr_vec_free(&stmts);

        chirp_stmt_list list = { .items = arena_stmts, .count = count };
        return check_expr_alloc(p, chirp_ast_new_block(p->arena, list, do_tok));
    }

    if (match(p, CHIRP_TOK_LBRACE)) {
        chirp_token brace_tok = *previous(p);

        // 1. Empty set: {}
        if (match(p, CHIRP_TOK_RBRACE)) {
            chirp_expr_list empty_list = { .items = NULL, .count = 0 };
            return check_expr_alloc(p, chirp_ast_new_enumerated_set(p->arena, empty_list, brace_tok));
        }

        // 2. Constructed set: { x : bound | cond } or { x | cond }
        if (check(p, CHIRP_TOK_IDENTIFIER) &&
            (peek_next(p)->type == CHIRP_TOK_PIPE || peek_next(p)->type == CHIRP_TOK_COLON)) {
            chirp_named_binding binding = parse_binding(p, false, false, false, NULL);
            consume(p, CHIRP_TOK_PIPE, "Expect '|' in constructed set.");
            chirp_expr *cond = expression(p);
            consume(p, CHIRP_TOK_RBRACE, "Expect '}' after constructed set condition.");
            if (p->had_error) return NULL;
            return check_expr_alloc(p, chirp_ast_new_constructed_set(p->arena, binding, cond, brace_tok));
        }

        // 3. Anonymous struct literal: { x = 1, y = 2 }
        if (check(p, CHIRP_TOK_IDENTIFIER) && peek_next(p)->type == CHIRP_TOK_EQUAL) {
            size_t cap = 0;
            chirp_ast_arg *field_buf = NULL;
            size_t count = 0;

            do {
                if (!buf_grow(p, (void **)&field_buf, &cap, count, sizeof(chirp_ast_arg))) {
                    bs_free(field_buf);
                    return NULL;
                }
                const chirp_token *name = consume(p, CHIRP_TOK_IDENTIFIER, "Expect field name in anonymous struct literal.");
                consume(p, CHIRP_TOK_EQUAL, "Expect '=' after anonymous struct field name.");
                chirp_expr *val = expression(p);
                if (p->had_error || val == NULL) { bs_free(field_buf); return NULL; }

                field_buf[count].name = *name;
                field_buf[count].has_name = true;
                field_buf[count].value = val;
                count++;
            } while (match(p, CHIRP_TOK_COMMA) && !check(p, CHIRP_TOK_RBRACE));

            consume(p, CHIRP_TOK_RBRACE, "Expect '}' after anonymous struct literal.");
            if (p->had_error) { bs_free(field_buf); return NULL; }

            chirp_ast_arg *arena_fields = NULL;
            if (count > 0) {
                arena_fields = (chirp_ast_arg *)chirp_ast_arena_alloc_array(p->arena, count, sizeof(chirp_ast_arg));
                if (arena_fields == NULL) {
                    bs_free(field_buf);
                    parser_oom(p);
                    return NULL;
                }
                memcpy(arena_fields, field_buf, count * sizeof(chirp_ast_arg));
            }
            bs_free(field_buf);

            chirp_ast_arg_list fields = { .items = arena_fields, .count = count };
            return check_expr_alloc(p, chirp_ast_new_anon_struct(p->arena, fields, brace_tok));
        }

        // 4. Enumerated set: { expr, ... }
        ptr_vec elements;
        ptr_vec_init(&elements);

        chirp_expr *first = expression(p);
        if (p->had_error || first == NULL) { ptr_vec_free(&elements); return NULL; }
        if (!ptr_vec_push(p, &elements, first)) { ptr_vec_free(&elements); return NULL; }

        while (match(p, CHIRP_TOK_COMMA)) {
            if (check(p, CHIRP_TOK_RBRACE)) break;
            chirp_expr *next = expression(p);
            if (p->had_error || next == NULL) { ptr_vec_free(&elements); return NULL; }
            if (!ptr_vec_push(p, &elements, next)) { ptr_vec_free(&elements); return NULL; }
        }
        consume(p, CHIRP_TOK_RBRACE, "Expect '}' after enumerated set elements.");
        if (p->had_error) { ptr_vec_free(&elements); return NULL; }

        chirp_expr **arena_els = NULL;
        if (elements.count > 0) {
            arena_els = (chirp_expr **)chirp_ast_arena_alloc_array(p->arena, elements.count, sizeof(chirp_expr *));
            if (arena_els == NULL) {
                ptr_vec_free(&elements);
                parser_oom(p);
                return NULL;
            }
            memcpy(arena_els, elements.items, elements.count * sizeof(chirp_expr *));
        }
        size_t count = elements.count;
        ptr_vec_free(&elements);

        chirp_expr_list elist = { .items = arena_els, .count = count };
        return check_expr_alloc(p, chirp_ast_new_enumerated_set(p->arena, elist, brace_tok));
    }

    parser_fail(p, *peek(p), "Expect expression.");
    return NULL;
}

/* ========================================================================= */
/* Public Parser API                                                         */
/* ========================================================================= */

bs_status chirp_parse_expression(
    chirp_ast_arena *arena,
    const chirp_token_list *tokens,
    chirp_expr **out_expr,
    chirp_parser_error *out_error
) {
    assert(arena != NULL);
    assert(tokens != NULL);
    assert(out_expr != NULL);

    if (tokens->count == 0 || tokens->tokens[0].type == CHIRP_TOK_EOF) {
        *out_expr = NULL;
        return BS_OK;
    }

    chirp_parser parser;
    parser.tokens = tokens->tokens;
    parser.count = tokens->count;
    parser.current = 0;
    parser.arena = arena;
    parser.error = out_error;
    parser.had_error = false;
    parser.error_status = BS_OK;

    chirp_expr *expr = expression(&parser);
    if (parser.had_error || expr == NULL) {
        if (!parser.had_error) parser_oom(&parser);
        *out_expr = NULL;
        return parser.error_status;
    }

    if (!is_at_end(&parser)) {
        parser_fail(&parser, *peek(&parser), "Unexpected token after expression.");
        *out_expr = NULL;
        return parser.error_status;
    }

    *out_expr = expr;
    return BS_OK;
}

bs_status chirp_parse_program(
    chirp_ast_arena *arena,
    const chirp_token_list *tokens,
    chirp_program *out_program,
    chirp_parser_error *out_error
) {
    assert(arena != NULL);
    assert(tokens != NULL);
    assert(out_program != NULL);

    out_program->statements.items = NULL;
    out_program->statements.count = 0;

    if (tokens->count == 0 || tokens->tokens[0].type == CHIRP_TOK_EOF) {
        return BS_OK;
    }

    chirp_parser parser;
    parser.tokens = tokens->tokens;
    parser.count = tokens->count;
    parser.current = 0;
    parser.arena = arena;
    parser.error = out_error;
    parser.had_error = false;
    parser.error_status = BS_OK;

    ptr_vec stmts;
    ptr_vec_init(&stmts);

    while (!is_at_end(&parser)) {
        chirp_stmt *s = statement(&parser);
        if (parser.had_error || s == NULL) {
            ptr_vec_free(&stmts);
            return parser.error_status != BS_OK ? parser.error_status : BS_FRONTEND_SYNTAX_ERROR;
        }
        if (!ptr_vec_push(&parser, &stmts, s)) {
            ptr_vec_free(&stmts);
            return parser.error_status;
        }
    }

    chirp_stmt **arena_stmts = NULL;
    if (stmts.count > 0) {
        arena_stmts = (chirp_stmt **)chirp_ast_arena_alloc_array(arena, stmts.count, sizeof(chirp_stmt *));
        if (arena_stmts == NULL) {
            ptr_vec_free(&stmts);
            parser_oom(&parser);
            return parser.error_status;
        }
        memcpy(arena_stmts, stmts.items, stmts.count * sizeof(chirp_stmt *));
    }
    out_program->statements.items = arena_stmts;
    out_program->statements.count = stmts.count;

    ptr_vec_free(&stmts);
    return BS_OK;
}

bs_status chirp_parse_expression_cstr(
    chirp_ast_arena *arena,
    const char *source,
    chirp_expr **out_expr,
    chirp_parser_error *out_error
) {
    chirp_token_list tokens;
    bs_status st = chirp_tokenize_cstr(source, &tokens);
    if (st != BS_OK) return st;

    st = chirp_parse_expression(arena, &tokens, out_expr, out_error);
    chirp_token_list_free(&tokens);
    return st;
}

bs_status chirp_parse_program_cstr(
    chirp_ast_arena *arena,
    const char *source,
    chirp_program *out_program,
    chirp_parser_error *out_error
) {
    chirp_token_list tokens;
    bs_status st = chirp_tokenize_cstr(source, &tokens);
    if (st != BS_OK) return st;

    st = chirp_parse_program(arena, &tokens, out_program, out_error);
    chirp_token_list_free(&tokens);
    return st;
}
