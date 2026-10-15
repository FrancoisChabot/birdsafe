/**
 * @file chirp_ast.h
 * @brief Abstract Syntax Tree (AST) definitions and constructors for Chirp.
 */

#ifndef CHIRP_FRONTEND_CHIRP_AST_H
#define CHIRP_FRONTEND_CHIRP_AST_H

#include "bs_core.h"
#include "chirp_frontend/chirp_str_view.h"
#include "chirp_frontend/chirp_token.h"
#include "chirp_frontend/chirp_ast_arena.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct chirp_expr chirp_expr;
typedef struct chirp_stmt chirp_stmt;

/* ========================================================================= */
/* Operator Classifications                                                  */
/* ========================================================================= */

typedef enum chirp_binary_op {
    CHIRP_BINOP_ADD = 0,
    CHIRP_BINOP_SUB,
    CHIRP_BINOP_MUL,
    CHIRP_BINOP_DIV,
    CHIRP_BINOP_MOD,
    CHIRP_BINOP_EQ,
    CHIRP_BINOP_NEQ,
    CHIRP_BINOP_LT,
    CHIRP_BINOP_LTE,
    CHIRP_BINOP_GT,
    CHIRP_BINOP_GTE,
    CHIRP_BINOP_AND,
    CHIRP_BINOP_OR,
    CHIRP_BINOP_IN,
    CHIRP_BINOP_NOT_IN,
    CHIRP_BINOP_UNION,
    CHIRP_BINOP_INTERSECTION,
    CHIRP_BINOP_RANGE,              /**< '..' */
    CHIRP_BINOP_RANGE_INCLUSIVE_END,/**< '..=' */
    CHIRP_BINOP_DOT                 /**< '.' */
} chirp_binary_op;

typedef enum chirp_unary_op {
    CHIRP_UNOP_NOT = 0,             /**< '!' */
    CHIRP_UNOP_NEGATE,              /**< '-' */
    CHIRP_UNOP_ADDRESS_OF,          /**< '&' */
    CHIRP_UNOP_MUTABLE_ADDRESS_OF,  /**< '&mut' */
    CHIRP_UNOP_DEREF,               /**< '*' */
    CHIRP_UNOP_POINTER_TYPE,        /**< '->' */
    CHIRP_UNOP_MUTABLE_POINTER_TYPE,/**< '->mut' */
    CHIRP_UNOP_COMPLEMENT           /**< '~' */
} chirp_unary_op;

/* ========================================================================= */
/* Supporting AST Structures & Lists                                         */
/* ========================================================================= */

typedef struct chirp_expr_list {
    chirp_expr **items;
    size_t count;
} chirp_expr_list;

typedef struct chirp_stmt_list {
    chirp_stmt **items;
    size_t count;
} chirp_stmt_list;

typedef struct chirp_str_view_list {
    chirp_str_view *items;
    size_t count;
} chirp_str_view_list;

typedef struct chirp_named_binding {
    chirp_token name;
    bool is_mut;
    bool is_final;
    chirp_expr *type_bound;   /**< NULL if unconstrained */
    chirp_expr *initializer;  /**< NULL if uninitialized */
} chirp_named_binding;

typedef struct chirp_binding_list {
    chirp_named_binding *items;
    size_t count;
} chirp_binding_list;

typedef struct chirp_ast_arg {
    chirp_token name;         /**< Name token for named argument (has_name=true) */
    bool has_name;
    chirp_expr *value;
} chirp_ast_arg;

typedef struct chirp_ast_arg_list {
    chirp_ast_arg *items;
    size_t count;
} chirp_ast_arg_list;

typedef struct chirp_match_arm {
    chirp_expr *pattern;
    chirp_expr *body;
} chirp_match_arm;

typedef struct chirp_match_arm_list {
    chirp_match_arm *items;
    size_t count;
} chirp_match_arm_list;

/* ========================================================================= */
/* Expression AST Nodes                                                      */
/* ========================================================================= */

typedef enum chirp_expr_kind {
    CHIRP_EXPR_BINARY = 0,
    CHIRP_EXPR_UNARY,
    CHIRP_EXPR_GROUPING,
    CHIRP_EXPR_NUMBER,
    CHIRP_EXPR_STRING,
    CHIRP_EXPR_CHAR,
    CHIRP_EXPR_IDENTIFIER,
    CHIRP_EXPR_INTRINSIC,
    CHIRP_EXPR_SYMBOLIC_CONSTANT,
    CHIRP_EXPR_ENUMERATED_SET,
    CHIRP_EXPR_CONSTRUCTED_SET,
    CHIRP_EXPR_ANON_STRUCT,
    CHIRP_EXPR_IF,
    CHIRP_EXPR_WHILE,
    CHIRP_EXPR_FOR,
    CHIRP_EXPR_LAMBDA,
    CHIRP_EXPR_SIGNATURE,
    CHIRP_EXPR_BLOCK,
    CHIRP_EXPR_STRUCT,
    CHIRP_EXPR_ENUM,
    CHIRP_EXPR_CALL,
    CHIRP_EXPR_INDEX,
    CHIRP_EXPR_LIST,
    CHIRP_EXPR_MATCH,
    CHIRP_EXPR_FSTRING
} chirp_expr_kind;

struct chirp_expr {
    chirp_expr_kind kind;
    chirp_token diagnostic_token;
    union {
        struct {
            chirp_expr *left;
            chirp_binary_op op;
            chirp_expr *right;
        } binary;
        struct {
            chirp_unary_op op;
            chirp_expr *right;
        } unary;
        struct {
            chirp_expr *expression;
        } grouping;
        struct {
            chirp_str_view value;
        } literal;
        struct {
            chirp_expr_list parts;
        } fstring;
        struct {
            chirp_expr_list elements;
        } enumerated_set;
        struct {
            chirp_named_binding binding;
            chirp_expr *condition;
        } constructed_set;
        struct {
            chirp_ast_arg_list fields;
        } anon_struct;
        struct {
            chirp_expr *condition;
            chirp_expr *then_branch;
            chirp_expr *else_branch; /**< NULL if none */
        } if_expr;
        struct {
            chirp_expr *condition;
            chirp_expr *body;
        } while_expr;
        struct {
            chirp_named_binding iterator_binding;
            chirp_expr *iterable;
            chirp_expr *body;
        } for_expr;
        struct {
            chirp_binding_list parameters;
            chirp_expr *return_bound; /**< NULL if none */
            chirp_expr *body;
        } lambda;
        struct {
            chirp_binding_list parameters;
            chirp_expr *return_bound;
        } signature;
        struct {
            chirp_stmt_list statements;
        } block;
        struct {
            chirp_binding_list fields;
        } struct_expr;
        struct {
            chirp_str_view_list variants;
            uint64_t node_id;
        } enum_expr;
        struct {
            chirp_expr *callee;
            chirp_ast_arg_list args;
        } call;
        struct {
            chirp_expr *target;
            chirp_ast_arg_list args;
        } index;
        struct {
            chirp_expr_list elements;
        } list;
        struct {
            chirp_expr *subject;
            chirp_match_arm_list arms;
        } match;
    } as;
};

/* ========================================================================= */
/* Statement AST Nodes                                                       */
/* ========================================================================= */

typedef enum chirp_stmt_kind {
    CHIRP_STMT_EXPR = 0,
    CHIRP_STMT_LET,
    CHIRP_STMT_BREAK,
    CHIRP_STMT_ASSIGN,
    CHIRP_STMT_IF,
    CHIRP_STMT_DEBUG
} chirp_stmt_kind;

struct chirp_stmt {
    chirp_stmt_kind kind;
    chirp_token diagnostic_token;
    union {
        struct {
            chirp_expr *expression;
        } expr_stmt;
        struct {
            chirp_named_binding binding;
            bool is_public;
        } let_stmt;
        struct {
            chirp_expr *value; /**< NULL for bare break; */
        } break_stmt;
        struct {
            chirp_expr *target;
            chirp_token op;
            chirp_expr *value;
        } assign_stmt;
        struct {
            chirp_expr *condition;
            chirp_stmt *then_branch;
            chirp_stmt *else_branch; /**< NULL if no else */
        } if_stmt;
        struct {
            chirp_stmt_list statements;
        } debug_stmt;
    } as;
};

/** Complete parsed program containing top-level statements. */
typedef struct chirp_program {
    chirp_stmt_list statements;
} chirp_program;

/* ========================================================================= */
/* Constructors (Arena-allocated)                                            */
/* ========================================================================= */

BS_API chirp_expr *chirp_ast_new_expr(chirp_ast_arena *arena, chirp_expr_kind kind, chirp_token diag);
BS_API chirp_stmt *chirp_ast_new_stmt(chirp_ast_arena *arena, chirp_stmt_kind kind, chirp_token diag);

BS_API chirp_expr *chirp_ast_new_binary(chirp_ast_arena *arena, chirp_expr *left, chirp_binary_op op, chirp_expr *right, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_unary(chirp_ast_arena *arena, chirp_unary_op op, chirp_expr *right, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_grouping(chirp_ast_arena *arena, chirp_expr *inner, chirp_token diag);

BS_API chirp_expr *chirp_ast_new_number(chirp_ast_arena *arena, chirp_str_view value, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_string(chirp_ast_arena *arena, chirp_str_view value, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_char(chirp_ast_arena *arena, chirp_str_view value, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_identifier(chirp_ast_arena *arena, chirp_str_view name, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_intrinsic(chirp_ast_arena *arena, chirp_str_view name, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_symbolic_constant(chirp_ast_arena *arena, chirp_str_view value, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_fstring(chirp_ast_arena *arena, chirp_expr_list parts, chirp_token diag);

BS_API chirp_expr *chirp_ast_new_enumerated_set(chirp_ast_arena *arena, chirp_expr_list elements, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_constructed_set(chirp_ast_arena *arena, chirp_named_binding binding, chirp_expr *condition, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_anon_struct(chirp_ast_arena *arena, chirp_ast_arg_list fields, chirp_token diag);

BS_API chirp_expr *chirp_ast_new_if(chirp_ast_arena *arena, chirp_expr *cond, chirp_expr *then_branch, chirp_expr *else_branch, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_while(chirp_ast_arena *arena, chirp_expr *cond, chirp_expr *body, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_for(chirp_ast_arena *arena, chirp_named_binding iter, chirp_expr *iterable, chirp_expr *body, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_lambda(chirp_ast_arena *arena, chirp_binding_list params, chirp_expr *return_bound, chirp_expr *body, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_signature(chirp_ast_arena *arena, chirp_binding_list params, chirp_expr *return_bound, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_block(chirp_ast_arena *arena, chirp_stmt_list statements, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_struct(chirp_ast_arena *arena, chirp_binding_list fields, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_enum(chirp_ast_arena *arena, chirp_str_view_list variants, uint64_t node_id, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_call(chirp_ast_arena *arena, chirp_expr *callee, chirp_ast_arg_list args, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_index(chirp_ast_arena *arena, chirp_expr *target, chirp_ast_arg_list args, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_list(chirp_ast_arena *arena, chirp_expr_list elements, chirp_token diag);
BS_API chirp_expr *chirp_ast_new_match(chirp_ast_arena *arena, chirp_expr *subject, chirp_match_arm_list arms, chirp_token diag);

BS_API chirp_stmt *chirp_ast_new_expr_stmt(chirp_ast_arena *arena, chirp_expr *expr, chirp_token diag);
BS_API chirp_stmt *chirp_ast_new_let_stmt(chirp_ast_arena *arena, chirp_named_binding binding, bool is_public, chirp_token diag);
BS_API chirp_stmt *chirp_ast_new_break_stmt(chirp_ast_arena *arena, chirp_expr *value, chirp_token diag);
BS_API chirp_stmt *chirp_ast_new_assign_stmt(chirp_ast_arena *arena, chirp_expr *target, chirp_token op, chirp_expr *value, chirp_token diag);
BS_API chirp_stmt *chirp_ast_new_if_stmt(chirp_ast_arena *arena, chirp_expr *cond, chirp_stmt *then_branch, chirp_stmt *else_branch, chirp_token diag);
BS_API chirp_stmt *chirp_ast_new_debug_stmt(chirp_ast_arena *arena, chirp_stmt_list statements, chirp_token diag);

/* ========================================================================= */
/* S-Expression AST Formatter (Testing & Diagnostics)                        */
/* ========================================================================= */

/** Formats an expression into a null-terminated string. Caller frees with bs_free(). */
BS_API bs_status chirp_ast_print_expr(const chirp_expr *expr, char **out_str);

/** Formats a statement into a null-terminated string. Caller frees with bs_free(). */
BS_API bs_status chirp_ast_print_stmt(const chirp_stmt *stmt, char **out_str);

/** Formats a program into a null-terminated string. Caller frees with bs_free(). */
BS_API bs_status chirp_ast_print_program(const chirp_program *program, char **out_str);

#ifdef __cplusplus
}
#endif

#endif /* CHIRP_FRONTEND_CHIRP_AST_H */
