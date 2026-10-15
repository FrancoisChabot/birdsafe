/**
 * @file chirp_parser.h
 * @brief Public interface for the Chirp parser.
 */

#ifndef CHIRP_FRONTEND_CHIRP_PARSER_H
#define CHIRP_FRONTEND_CHIRP_PARSER_H

#include "bs_core.h"
#include "chirp_frontend/chirp_token.h"
#include "chirp_frontend/chirp_ast_arena.h"
#include "chirp_frontend/chirp_ast.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Diagnostic error payload populated when parsing fails. */
typedef struct chirp_parser_error {
    char message[128];
    chirp_token token;
} chirp_parser_error;

/**
 * Parses a token stream into an expression AST node in the given arena.
 * If out_error is non-NULL and parsing fails, diagnostic details are recorded.
 */
BS_API bs_status chirp_parse_expression(
    chirp_ast_arena *arena,
    const chirp_token_list *tokens,
    chirp_expr **out_expr,
    chirp_parser_error *out_error
);

/**
 * Parses a token stream into a complete program AST in the given arena.
 * If out_error is non-NULL and parsing fails, diagnostic details are recorded.
 */
BS_API bs_status chirp_parse_program(
    chirp_ast_arena *arena,
    const chirp_token_list *tokens,
    chirp_program *out_program,
    chirp_parser_error *out_error
);

/**
 * Convenience helper: tokenizes and parses source text into an expression.
 */
BS_API bs_status chirp_parse_expression_cstr(
    chirp_ast_arena *arena,
    const char *source,
    chirp_expr **out_expr,
    chirp_parser_error *out_error
);

/**
 * Convenience helper: tokenizes and parses source text into a program.
 */
BS_API bs_status chirp_parse_program_cstr(
    chirp_ast_arena *arena,
    const char *source,
    chirp_program *out_program,
    chirp_parser_error *out_error
);

#ifdef __cplusplus
}
#endif

#endif /* CHIRP_FRONTEND_CHIRP_PARSER_H */
