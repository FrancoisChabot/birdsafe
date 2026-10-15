/**
 * @file chirp_lexer.h
 * @brief Public interface for the Chirp UTF-8 lexer.
 */

#ifndef CHIRP_FRONTEND_CHIRP_LEXER_H
#define CHIRP_FRONTEND_CHIRP_LEXER_H

#include "bs_core.h"
#include "chirp_frontend/chirp_token.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Tokenizes a UTF-8 source buffer into a token list.
 * The token lexemes and trivia reference slices within the provided source buffer.
 * Caller owns out_tokens and must call chirp_token_list_free() when done.
 */
BS_API bs_status chirp_tokenize(const char *source, size_t length, chirp_token_list *out_tokens);

/**
 * Tokenizes a null-terminated UTF-8 C string into a token list.
 */
BS_API bs_status chirp_tokenize_cstr(const char *source, chirp_token_list *out_tokens);

/**
 * Formats Chirp source code by normalizing ASCII operator aliases (`in, `notin, `or, `and)
 * to their standard Unicode mathematical representations (∈, ∉, ∪, ∩).
 * Caller owns the returned string and must release it with bs_free().
 */
BS_API bs_status chirp_format_text(const char *source, size_t length, char **out_formatted, size_t *out_length);

#ifdef __cplusplus
}
#endif

#endif /* CHIRP_FRONTEND_CHIRP_LEXER_H */
