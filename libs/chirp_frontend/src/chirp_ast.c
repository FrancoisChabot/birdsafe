/**
 * @file chirp_ast.c
 * @brief Implementation of AST constructors.
 */

#include "chirp_frontend/chirp_ast.h"
#include <assert.h>
#include <string.h>

chirp_expr *chirp_ast_new_expr(chirp_ast_arena *arena, chirp_expr_kind kind, chirp_token diag) {
    assert(arena != NULL);
    chirp_expr *node = (chirp_expr *)chirp_ast_arena_alloc(arena, sizeof(chirp_expr), sizeof(void *));
    if (node == NULL) return NULL;
    memset(node, 0, sizeof(chirp_expr));
    node->kind = kind;
    node->diagnostic_token = diag;
    return node;
}

chirp_stmt *chirp_ast_new_stmt(chirp_ast_arena *arena, chirp_stmt_kind kind, chirp_token diag) {
    assert(arena != NULL);
    chirp_stmt *node = (chirp_stmt *)chirp_ast_arena_alloc(arena, sizeof(chirp_stmt), sizeof(void *));
    if (node == NULL) return NULL;
    memset(node, 0, sizeof(chirp_stmt));
    node->kind = kind;
    node->diagnostic_token = diag;
    return node;
}

chirp_expr *chirp_ast_new_binary(chirp_ast_arena *arena, chirp_expr *left, chirp_binary_op op, chirp_expr *right, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_BINARY, diag);
    if (e == NULL) return NULL;
    e->as.binary.left = left;
    e->as.binary.op = op;
    e->as.binary.right = right;
    return e;
}

chirp_expr *chirp_ast_new_unary(chirp_ast_arena *arena, chirp_unary_op op, chirp_expr *right, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_UNARY, diag);
    if (e == NULL) return NULL;
    e->as.unary.op = op;
    e->as.unary.right = right;
    return e;
}

chirp_expr *chirp_ast_new_grouping(chirp_ast_arena *arena, chirp_expr *inner, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_GROUPING, diag);
    if (e == NULL) return NULL;
    e->as.grouping.expression = inner;
    return e;
}

chirp_expr *chirp_ast_new_number(chirp_ast_arena *arena, chirp_str_view value, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_NUMBER, diag);
    if (e == NULL) return NULL;
    e->as.literal.value = value;
    return e;
}

chirp_expr *chirp_ast_new_string(chirp_ast_arena *arena, chirp_str_view value, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_STRING, diag);
    if (e == NULL) return NULL;
    e->as.literal.value = value;
    return e;
}

chirp_expr *chirp_ast_new_char(chirp_ast_arena *arena, chirp_str_view value, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_CHAR, diag);
    if (e == NULL) return NULL;
    e->as.literal.value = value;
    return e;
}

chirp_expr *chirp_ast_new_identifier(chirp_ast_arena *arena, chirp_str_view name, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_IDENTIFIER, diag);
    if (e == NULL) return NULL;
    e->as.literal.value = name;
    return e;
}

chirp_expr *chirp_ast_new_intrinsic(chirp_ast_arena *arena, chirp_str_view name, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_INTRINSIC, diag);
    if (e == NULL) return NULL;
    e->as.literal.value = name;
    return e;
}

chirp_expr *chirp_ast_new_symbolic_constant(chirp_ast_arena *arena, chirp_str_view value, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_SYMBOLIC_CONSTANT, diag);
    if (e == NULL) return NULL;
    e->as.literal.value = value;
    return e;
}

chirp_expr *chirp_ast_new_fstring(chirp_ast_arena *arena, chirp_expr_list parts, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_FSTRING, diag);
    if (e == NULL) return NULL;
    e->as.fstring.parts = parts;
    return e;
}

chirp_expr *chirp_ast_new_enumerated_set(chirp_ast_arena *arena, chirp_expr_list elements, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_ENUMERATED_SET, diag);
    if (e == NULL) return NULL;
    e->as.enumerated_set.elements = elements;
    return e;
}

chirp_expr *chirp_ast_new_constructed_set(chirp_ast_arena *arena, chirp_named_binding binding, chirp_expr *condition, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_CONSTRUCTED_SET, diag);
    if (e == NULL) return NULL;
    e->as.constructed_set.binding = binding;
    e->as.constructed_set.condition = condition;
    return e;
}

chirp_expr *chirp_ast_new_anon_struct(chirp_ast_arena *arena, chirp_ast_arg_list fields, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_ANON_STRUCT, diag);
    if (e == NULL) return NULL;
    e->as.anon_struct.fields = fields;
    return e;
}

chirp_expr *chirp_ast_new_if(chirp_ast_arena *arena, chirp_expr *cond, chirp_expr *then_branch, chirp_expr *else_branch, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_IF, diag);
    if (e == NULL) return NULL;
    e->as.if_expr.condition = cond;
    e->as.if_expr.then_branch = then_branch;
    e->as.if_expr.else_branch = else_branch;
    return e;
}

chirp_expr *chirp_ast_new_while(chirp_ast_arena *arena, chirp_expr *cond, chirp_expr *body, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_WHILE, diag);
    if (e == NULL) return NULL;
    e->as.while_expr.condition = cond;
    e->as.while_expr.body = body;
    return e;
}

chirp_expr *chirp_ast_new_for(chirp_ast_arena *arena, chirp_named_binding iter, chirp_expr *iterable, chirp_expr *body, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_FOR, diag);
    if (e == NULL) return NULL;
    e->as.for_expr.iterator_binding = iter;
    e->as.for_expr.iterable = iterable;
    e->as.for_expr.body = body;
    return e;
}

chirp_expr *chirp_ast_new_lambda(chirp_ast_arena *arena, chirp_binding_list params, chirp_expr *return_bound, chirp_expr *body, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_LAMBDA, diag);
    if (e == NULL) return NULL;
    e->as.lambda.parameters = params;
    e->as.lambda.return_bound = return_bound;
    e->as.lambda.body = body;
    return e;
}

chirp_expr *chirp_ast_new_signature(chirp_ast_arena *arena, chirp_binding_list params, chirp_expr *return_bound, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_SIGNATURE, diag);
    if (e == NULL) return NULL;
    e->as.signature.parameters = params;
    e->as.signature.return_bound = return_bound;
    return e;
}

chirp_expr *chirp_ast_new_block(chirp_ast_arena *arena, chirp_stmt_list statements, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_BLOCK, diag);
    if (e == NULL) return NULL;
    e->as.block.statements = statements;
    return e;
}

chirp_expr *chirp_ast_new_struct(chirp_ast_arena *arena, chirp_binding_list fields, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_STRUCT, diag);
    if (e == NULL) return NULL;
    e->as.struct_expr.fields = fields;
    return e;
}

chirp_expr *chirp_ast_new_enum(chirp_ast_arena *arena, chirp_str_view_list variants, uint64_t node_id, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_ENUM, diag);
    if (e == NULL) return NULL;
    e->as.enum_expr.variants = variants;
    e->as.enum_expr.node_id = node_id;
    return e;
}

chirp_expr *chirp_ast_new_call(chirp_ast_arena *arena, chirp_expr *callee, chirp_ast_arg_list args, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_CALL, diag);
    if (e == NULL) return NULL;
    e->as.call.callee = callee;
    e->as.call.args = args;
    return e;
}

chirp_expr *chirp_ast_new_index(chirp_ast_arena *arena, chirp_expr *target, chirp_ast_arg_list args, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_INDEX, diag);
    if (e == NULL) return NULL;
    e->as.index.target = target;
    e->as.index.args = args;
    return e;
}

chirp_expr *chirp_ast_new_list(chirp_ast_arena *arena, chirp_expr_list elements, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_LIST, diag);
    if (e == NULL) return NULL;
    e->as.list.elements = elements;
    return e;
}

chirp_expr *chirp_ast_new_match(chirp_ast_arena *arena, chirp_expr *subject, chirp_match_arm_list arms, chirp_token diag) {
    chirp_expr *e = chirp_ast_new_expr(arena, CHIRP_EXPR_MATCH, diag);
    if (e == NULL) return NULL;
    e->as.match.subject = subject;
    e->as.match.arms = arms;
    return e;
}

chirp_stmt *chirp_ast_new_expr_stmt(chirp_ast_arena *arena, chirp_expr *expr, chirp_token diag) {
    chirp_stmt *s = chirp_ast_new_stmt(arena, CHIRP_STMT_EXPR, diag);
    if (s == NULL) return NULL;
    s->as.expr_stmt.expression = expr;
    return s;
}

chirp_stmt *chirp_ast_new_let_stmt(chirp_ast_arena *arena, chirp_named_binding binding, bool is_public, chirp_token diag) {
    chirp_stmt *s = chirp_ast_new_stmt(arena, CHIRP_STMT_LET, diag);
    if (s == NULL) return NULL;
    s->as.let_stmt.binding = binding;
    s->as.let_stmt.is_public = is_public;
    return s;
}

chirp_stmt *chirp_ast_new_break_stmt(chirp_ast_arena *arena, chirp_expr *value, chirp_token diag) {
    chirp_stmt *s = chirp_ast_new_stmt(arena, CHIRP_STMT_BREAK, diag);
    if (s == NULL) return NULL;
    s->as.break_stmt.value = value;
    return s;
}

chirp_stmt *chirp_ast_new_assign_stmt(chirp_ast_arena *arena, chirp_expr *target, chirp_token op, chirp_expr *value, chirp_token diag) {
    chirp_stmt *s = chirp_ast_new_stmt(arena, CHIRP_STMT_ASSIGN, diag);
    if (s == NULL) return NULL;
    s->as.assign_stmt.target = target;
    s->as.assign_stmt.op = op;
    s->as.assign_stmt.value = value;
    return s;
}

chirp_stmt *chirp_ast_new_if_stmt(chirp_ast_arena *arena, chirp_expr *cond, chirp_stmt *then_branch, chirp_stmt *else_branch, chirp_token diag) {
    chirp_stmt *s = chirp_ast_new_stmt(arena, CHIRP_STMT_IF, diag);
    if (s == NULL) return NULL;
    s->as.if_stmt.condition = cond;
    s->as.if_stmt.then_branch = then_branch;
    s->as.if_stmt.else_branch = else_branch;
    return s;
}

chirp_stmt *chirp_ast_new_debug_stmt(chirp_ast_arena *arena, chirp_stmt_list statements, chirp_token diag) {
    chirp_stmt *s = chirp_ast_new_stmt(arena, CHIRP_STMT_DEBUG, diag);
    if (s == NULL) return NULL;
    s->as.debug_stmt.statements = statements;
    return s;
}
