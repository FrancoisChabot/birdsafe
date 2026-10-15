/**
 * @file chirp_ast_printer.c
 * @brief S-expression AST printer for debugging and test verification.
 */

#include "chirp_frontend/chirp_ast.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct str_buf {
    char *data;
    size_t len;
    size_t cap;
    bool oom;
} str_buf;

static void buf_init(str_buf *buf) {
    buf->cap = 256;
    buf->data = (char *)bs_alloc(buf->cap);
    buf->len = 0;
    buf->oom = buf->data == NULL;
    if (buf->data != NULL) {
        buf->data[0] = '\0';
    }
}

static void buf_append_str(str_buf *buf, const char *str, size_t str_len) {
    if (buf->oom || str == NULL || str_len == 0) return;
    if (buf->len + str_len + 1 > buf->cap) {
        size_t new_cap = buf->cap * 2;
        while (new_cap < buf->len + str_len + 1) {
            new_cap *= 2;
        }
        char *new_data = (char *)bs_realloc(buf->data, new_cap);
        if (new_data == NULL) {
            buf->oom = true;
            return;
        }
        buf->data = new_data;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, str, str_len);
    buf->len += str_len;
    buf->data[buf->len] = '\0';
}

static void buf_append_cstr(str_buf *buf, const char *cstr) {
    if (cstr == NULL) return;
    buf_append_str(buf, cstr, strlen(cstr));
}

static void buf_append_view(str_buf *buf, chirp_str_view sv) {
    buf_append_str(buf, sv.ptr, sv.len);
}

static void print_expr(str_buf *buf, const chirp_expr *expr);
static void print_stmt(str_buf *buf, const chirp_stmt *stmt);

static const char *binary_op_name(chirp_binary_op op) {
    switch (op) {
        case CHIRP_BINOP_ADD: return "+";
        case CHIRP_BINOP_SUB: return "-";
        case CHIRP_BINOP_MUL: return "*";
        case CHIRP_BINOP_DIV: return "/";
        case CHIRP_BINOP_MOD: return "%";
        case CHIRP_BINOP_EQ: return "==";
        case CHIRP_BINOP_NEQ: return "!=";
        case CHIRP_BINOP_LT: return "<";
        case CHIRP_BINOP_LTE: return "<=";
        case CHIRP_BINOP_GT: return ">";
        case CHIRP_BINOP_GTE: return ">=";
        case CHIRP_BINOP_AND: return "&&";
        case CHIRP_BINOP_OR: return "||";
        case CHIRP_BINOP_IN: return "in";
        case CHIRP_BINOP_NOT_IN: return "not in";
        case CHIRP_BINOP_UNION: return "union";
        case CHIRP_BINOP_INTERSECTION: return "intersection";
        case CHIRP_BINOP_RANGE: return "..";
        case CHIRP_BINOP_RANGE_INCLUSIVE_END: return "..=";
        case CHIRP_BINOP_DOT: return ".";
        default: return "?";
    }
}

static const char *unary_op_name(chirp_unary_op op) {
    switch (op) {
        case CHIRP_UNOP_NOT: return "!";
        case CHIRP_UNOP_NEGATE: return "-";
        case CHIRP_UNOP_ADDRESS_OF: return "&";
        case CHIRP_UNOP_MUTABLE_ADDRESS_OF: return "&mut";
        case CHIRP_UNOP_DEREF: return "*";
        case CHIRP_UNOP_POINTER_TYPE: return "->";
        case CHIRP_UNOP_MUTABLE_POINTER_TYPE: return "->mut";
        case CHIRP_UNOP_COMPLEMENT: return "~";
        default: return "?";
    }
}

static void print_expr(str_buf *buf, const chirp_expr *expr) {
    if (expr == NULL) {
        buf_append_cstr(buf, "null");
        return;
    }

    switch (expr->kind) {
        case CHIRP_EXPR_BINARY:
            buf_append_cstr(buf, "(");
            buf_append_cstr(buf, binary_op_name(expr->as.binary.op));
            buf_append_cstr(buf, " ");
            print_expr(buf, expr->as.binary.left);
            buf_append_cstr(buf, " ");
            print_expr(buf, expr->as.binary.right);
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_UNARY:
            buf_append_cstr(buf, "(");
            buf_append_cstr(buf, unary_op_name(expr->as.unary.op));
            buf_append_cstr(buf, " ");
            print_expr(buf, expr->as.unary.right);
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_GROUPING:
            buf_append_cstr(buf, "(group ");
            print_expr(buf, expr->as.grouping.expression);
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_NUMBER:
        case CHIRP_EXPR_STRING:
        case CHIRP_EXPR_CHAR:
        case CHIRP_EXPR_IDENTIFIER:
        case CHIRP_EXPR_INTRINSIC:
        case CHIRP_EXPR_SYMBOLIC_CONSTANT:
            buf_append_view(buf, expr->as.literal.value);
            break;

        case CHIRP_EXPR_FSTRING:
            buf_append_cstr(buf, "(fstring");
            for (size_t i = 0; i < expr->as.fstring.parts.count; i++) {
                buf_append_cstr(buf, " ");
                print_expr(buf, expr->as.fstring.parts.items[i]);
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_ENUMERATED_SET:
            buf_append_cstr(buf, "(set");
            for (size_t i = 0; i < expr->as.enumerated_set.elements.count; i++) {
                buf_append_cstr(buf, " ");
                print_expr(buf, expr->as.enumerated_set.elements.items[i]);
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_CONSTRUCTED_SET:
            buf_append_cstr(buf, "(c_set ");
            if (expr->as.constructed_set.binding.is_final) buf_append_cstr(buf, "final ");
            if (expr->as.constructed_set.binding.is_mut) buf_append_cstr(buf, "mut ");
            buf_append_view(buf, expr->as.constructed_set.binding.name.lexeme);
            if (expr->as.constructed_set.binding.type_bound) {
                buf_append_cstr(buf, " : ");
                print_expr(buf, expr->as.constructed_set.binding.type_bound);
            }
            buf_append_cstr(buf, " | ");
            print_expr(buf, expr->as.constructed_set.condition);
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_ANON_STRUCT:
            buf_append_cstr(buf, "(anon_struct");
            for (size_t i = 0; i < expr->as.anon_struct.fields.count; i++) {
                buf_append_cstr(buf, " ");
                const chirp_ast_arg *f = &expr->as.anon_struct.fields.items[i];
                if (f->has_name) {
                    buf_append_view(buf, f->name.lexeme);
                    buf_append_cstr(buf, "=");
                }
                print_expr(buf, f->value);
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_IF:
            buf_append_cstr(buf, "(if ");
            print_expr(buf, expr->as.if_expr.condition);
            buf_append_cstr(buf, " ");
            print_expr(buf, expr->as.if_expr.then_branch);
            buf_append_cstr(buf, " ");
            print_expr(buf, expr->as.if_expr.else_branch);
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_WHILE:
            buf_append_cstr(buf, "(while ");
            print_expr(buf, expr->as.while_expr.condition);
            buf_append_cstr(buf, " ");
            print_expr(buf, expr->as.while_expr.body);
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_FOR:
            buf_append_cstr(buf, "(for ");
            if (expr->as.for_expr.iterator_binding.is_final) buf_append_cstr(buf, "final ");
            if (expr->as.for_expr.iterator_binding.is_mut) buf_append_cstr(buf, "mut ");
            buf_append_view(buf, expr->as.for_expr.iterator_binding.name.lexeme);
            if (expr->as.for_expr.iterator_binding.type_bound) {
                buf_append_cstr(buf, ":");
                print_expr(buf, expr->as.for_expr.iterator_binding.type_bound);
            }
            buf_append_cstr(buf, " in ");
            print_expr(buf, expr->as.for_expr.iterable);
            buf_append_cstr(buf, " ");
            print_expr(buf, expr->as.for_expr.body);
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_LAMBDA:
            buf_append_cstr(buf, "(lambda (");
            for (size_t i = 0; i < expr->as.lambda.parameters.count; i++) {
                if (i > 0) buf_append_cstr(buf, " ");
                const chirp_named_binding *p = &expr->as.lambda.parameters.items[i];
                if (p->is_final) buf_append_cstr(buf, "final ");
                if (p->is_mut) buf_append_cstr(buf, "mut ");
                buf_append_view(buf, p->name.lexeme);
                if (p->type_bound) {
                    buf_append_cstr(buf, ":");
                    print_expr(buf, p->type_bound);
                }
            }
            buf_append_cstr(buf, ")");
            if (expr->as.lambda.return_bound) {
                buf_append_cstr(buf, ":");
                print_expr(buf, expr->as.lambda.return_bound);
            }
            buf_append_cstr(buf, " ");
            print_expr(buf, expr->as.lambda.body);
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_SIGNATURE:
            buf_append_cstr(buf, "(signature (");
            for (size_t i = 0; i < expr->as.signature.parameters.count; i++) {
                buf_append_cstr(buf, "(");
                const chirp_named_binding *p = &expr->as.signature.parameters.items[i];
                buf_append_view(buf, p->name.lexeme);
                if (p->type_bound) {
                    buf_append_cstr(buf, " ");
                    print_expr(buf, p->type_bound);
                }
                buf_append_cstr(buf, ")");
                if (i + 1 < expr->as.signature.parameters.count) {
                    buf_append_cstr(buf, " ");
                }
            }
            buf_append_cstr(buf, ") ");
            if (expr->as.signature.return_bound) {
                print_expr(buf, expr->as.signature.return_bound);
            } else {
                buf_append_cstr(buf, "`any");
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_BLOCK:
            buf_append_cstr(buf, "(block");
            for (size_t i = 0; i < expr->as.block.statements.count; i++) {
                buf_append_cstr(buf, " ");
                print_stmt(buf, expr->as.block.statements.items[i]);
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_STRUCT:
            buf_append_cstr(buf, "(struct");
            for (size_t i = 0; i < expr->as.struct_expr.fields.count; i++) {
                buf_append_cstr(buf, " (field ");
                const chirp_named_binding *f = &expr->as.struct_expr.fields.items[i];
                if (f->is_final) buf_append_cstr(buf, "final ");
                if (f->is_mut) buf_append_cstr(buf, "mut ");
                buf_append_view(buf, f->name.lexeme);
                if (f->type_bound) {
                    buf_append_cstr(buf, ":");
                    print_expr(buf, f->type_bound);
                }
                if (f->initializer) {
                    buf_append_cstr(buf, " = ");
                    print_expr(buf, f->initializer);
                }
                buf_append_cstr(buf, ")");
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_ENUM:
            buf_append_cstr(buf, "(enum");
            for (size_t i = 0; i < expr->as.enum_expr.variants.count; i++) {
                buf_append_cstr(buf, " ");
                buf_append_view(buf, expr->as.enum_expr.variants.items[i]);
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_CALL:
            buf_append_cstr(buf, "(call ");
            print_expr(buf, expr->as.call.callee);
            for (size_t i = 0; i < expr->as.call.args.count; i++) {
                buf_append_cstr(buf, " ");
                const chirp_ast_arg *arg = &expr->as.call.args.items[i];
                if (arg->has_name) {
                    buf_append_view(buf, arg->name.lexeme);
                    buf_append_cstr(buf, "=");
                }
                print_expr(buf, arg->value);
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_INDEX:
            buf_append_cstr(buf, "(index ");
            print_expr(buf, expr->as.index.target);
            for (size_t i = 0; i < expr->as.index.args.count; i++) {
                buf_append_cstr(buf, " ");
                print_expr(buf, expr->as.index.args.items[i].value);
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_LIST:
            buf_append_cstr(buf, "(list");
            for (size_t i = 0; i < expr->as.list.elements.count; i++) {
                buf_append_cstr(buf, " ");
                print_expr(buf, expr->as.list.elements.items[i]);
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_EXPR_MATCH:
            buf_append_cstr(buf, "(match ");
            print_expr(buf, expr->as.match.subject);
            for (size_t i = 0; i < expr->as.match.arms.count; i++) {
                buf_append_cstr(buf, " (=> ");
                print_expr(buf, expr->as.match.arms.items[i].pattern);
                buf_append_cstr(buf, " ");
                print_expr(buf, expr->as.match.arms.items[i].body);
                buf_append_cstr(buf, ")");
            }
            buf_append_cstr(buf, ")");
            break;
    }
}

static void print_stmt(str_buf *buf, const chirp_stmt *stmt) {
    if (stmt == NULL) {
        buf_append_cstr(buf, "null");
        return;
    }

    switch (stmt->kind) {
        case CHIRP_STMT_EXPR:
            buf_append_cstr(buf, "(expr_stmt ");
            print_expr(buf, stmt->as.expr_stmt.expression);
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_STMT_LET:
            buf_append_cstr(buf, stmt->as.let_stmt.is_public ? "(let pub " : "(let ");
            if (stmt->as.let_stmt.binding.is_final) buf_append_cstr(buf, "final ");
            if (stmt->as.let_stmt.binding.is_mut) buf_append_cstr(buf, "mut ");
            buf_append_view(buf, stmt->as.let_stmt.binding.name.lexeme);
            if (stmt->as.let_stmt.binding.type_bound) {
                buf_append_cstr(buf, ":");
                print_expr(buf, stmt->as.let_stmt.binding.type_bound);
            }
            if (stmt->as.let_stmt.binding.initializer) {
                buf_append_cstr(buf, " = ");
                print_expr(buf, stmt->as.let_stmt.binding.initializer);
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_STMT_BREAK:
            buf_append_cstr(buf, "(break ");
            if (stmt->as.break_stmt.value) {
                print_expr(buf, stmt->as.break_stmt.value);
            } else {
                buf_append_cstr(buf, "`void");
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_STMT_ASSIGN:
            buf_append_cstr(buf, "(");
            buf_append_view(buf, stmt->as.assign_stmt.op.lexeme);
            buf_append_cstr(buf, " ");
            print_expr(buf, stmt->as.assign_stmt.target);
            buf_append_cstr(buf, " ");
            print_expr(buf, stmt->as.assign_stmt.value);
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_STMT_IF:
            buf_append_cstr(buf, "(if_stmt ");
            print_expr(buf, stmt->as.if_stmt.condition);
            buf_append_cstr(buf, " ");
            print_stmt(buf, stmt->as.if_stmt.then_branch);
            if (stmt->as.if_stmt.else_branch) {
                buf_append_cstr(buf, " ");
                print_stmt(buf, stmt->as.if_stmt.else_branch);
            }
            buf_append_cstr(buf, ")");
            break;

        case CHIRP_STMT_DEBUG:
            buf_append_cstr(buf, "(debug_stmt");
            for (size_t i = 0; i < stmt->as.debug_stmt.statements.count; i++) {
                buf_append_cstr(buf, " ");
                print_stmt(buf, stmt->as.debug_stmt.statements.items[i]);
            }
            buf_append_cstr(buf, ")");
            break;
    }
}

bs_status chirp_ast_print_expr(const chirp_expr *expr, char **out_str) {
    assert(out_str != NULL);
    str_buf buf;
    buf_init(&buf);
    if (buf.oom) return BS_OUT_OF_MEMORY;

    print_expr(&buf, expr);
    if (buf.oom) {
        bs_free(buf.data);
        return BS_OUT_OF_MEMORY;
    }

    *out_str = buf.data;
    return BS_OK;
}

bs_status chirp_ast_print_stmt(const chirp_stmt *stmt, char **out_str) {
    assert(out_str != NULL);
    str_buf buf;
    buf_init(&buf);
    if (buf.oom) return BS_OUT_OF_MEMORY;

    print_stmt(&buf, stmt);
    if (buf.oom) {
        bs_free(buf.data);
        return BS_OUT_OF_MEMORY;
    }

    *out_str = buf.data;
    return BS_OK;
}

bs_status chirp_ast_print_program(const chirp_program *program, char **out_str) {
    assert(out_str != NULL);
    str_buf buf;
    buf_init(&buf);
    if (buf.oom) return BS_OUT_OF_MEMORY;

    if (program != NULL) {
        for (size_t i = 0; i < program->statements.count; i++) {
            print_stmt(&buf, program->statements.items[i]);
            buf_append_cstr(&buf, "\n");
        }
    }

    if (buf.oom) {
        bs_free(buf.data);
        return BS_OUT_OF_MEMORY;
    }

    *out_str = buf.data;
    return BS_OK;
}
