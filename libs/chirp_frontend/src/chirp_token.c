/**
 * @file chirp_token.c
 * @brief Implementation of chirp_token helper functions and token list management.
 */

#include "chirp_frontend/chirp_token.h"
#include <assert.h>
#include <stdlib.h>

const char *chirp_token_type_name(chirp_token_type type) {
    switch (type) {
        case CHIRP_TOK_LPAREN:            return "(";
        case CHIRP_TOK_RPAREN:            return ")";
        case CHIRP_TOK_LBRACE:            return "{";
        case CHIRP_TOK_RBRACE:            return "}";
        case CHIRP_TOK_LBRACKET:          return "[";
        case CHIRP_TOK_RBRACKET:          return "]";
        case CHIRP_TOK_COMMA:             return ",";
        case CHIRP_TOK_DOT:               return ".";
        case CHIRP_TOK_SEMICOLON:         return ";";
        case CHIRP_TOK_COLON:             return ":";
        case CHIRP_TOK_PIPE:              return "|";

        case CHIRP_TOK_PLUS:              return "+";
        case CHIRP_TOK_MINUS:             return "-";
        case CHIRP_TOK_STAR:              return "*";
        case CHIRP_TOK_SLASH:             return "/";
        case CHIRP_TOK_PERCENT:           return "%";

        case CHIRP_TOK_PLUS_EQUAL:        return "+=";
        case CHIRP_TOK_MINUS_EQUAL:       return "-=";
        case CHIRP_TOK_STAR_EQUAL:        return "*=";
        case CHIRP_TOK_SLASH_EQUAL:       return "/=";
        case CHIRP_TOK_PERCENT_EQUAL:     return "%=";

        case CHIRP_TOK_EQUAL:             return "=";

        case CHIRP_TOK_EQUAL_EQUAL:       return "==";
        case CHIRP_TOK_BANG_EQUAL:        return "!=";
        case CHIRP_TOK_LESS:              return "<";
        case CHIRP_TOK_LESS_EQUAL:        return "<=";
        case CHIRP_TOK_GREATER:           return ">";
        case CHIRP_TOK_GREATER_EQUAL:     return ">=";

        case CHIRP_TOK_BANG:              return "!";
        case CHIRP_TOK_TILDE:             return "~";
        case CHIRP_TOK_AMPERSAND:         return "&";
        case CHIRP_TOK_AND_AND:           return "&&";
        case CHIRP_TOK_OR_OR:             return "||";

        case CHIRP_TOK_AMPERSAND_MUT:     return "&mut";
        case CHIRP_TOK_ARROW:             return "->";
        case CHIRP_TOK_ARROW_MUT:         return "->mut";
        case CHIRP_TOK_FAT_ARROW:         return "=>";
        case CHIRP_TOK_DOT_DOT:           return "..";
        case CHIRP_TOK_DOT_DOT_EQUAL:     return "..=";

        case CHIRP_TOK_IN:                return "∈";
        case CHIRP_TOK_NOT_IN:            return "∉";
        case CHIRP_TOK_UNION:             return "∪";
        case CHIRP_TOK_INTERSECTION:      return "∩";

        case CHIRP_TOK_IDENTIFIER:        return "identifier";
        case CHIRP_TOK_INTRINSIC:         return "intrinsic";
        case CHIRP_TOK_SYMBOLIC_CONSTANT: return "symbolic_constant";
        case CHIRP_TOK_NUMBER:            return "number";
        case CHIRP_TOK_STRING:            return "string";
        case CHIRP_TOK_CHARACTER:         return "character";

        case CHIRP_TOK_FSTRING_LITERAL:   return "fstring_literal";
        case CHIRP_TOK_FSTRING_HEAD:      return "fstring_head";
        case CHIRP_TOK_FSTRING_MIDDLE:    return "fstring_middle";
        case CHIRP_TOK_FSTRING_TAIL:      return "fstring_tail";

        case CHIRP_TOK_KW_LET:            return "kw_let";
        case CHIRP_TOK_KW_STRUCT:         return "kw_struct";
        case CHIRP_TOK_KW_IF:             return "kw_if";
        case CHIRP_TOK_KW_ELSE:           return "kw_else";
        case CHIRP_TOK_KW_WHILE:          return "kw_while";
        case CHIRP_TOK_KW_FOR:            return "kw_for";
        case CHIRP_TOK_KW_BREAK:          return "kw_break";
        case CHIRP_TOK_KW_MATCH:          return "kw_match";
        case CHIRP_TOK_KW_DO:             return "kw_do";
        case CHIRP_TOK_KW_DEBUG:          return "kw_debug";
        case CHIRP_TOK_KW_ENUM:           return "kw_enum";

        case CHIRP_TOK_EOF:               return "eof";
        case CHIRP_TOK_ERROR:             return "error";

        default:                          return "unknown";
    }
}

void chirp_token_list_init(chirp_token_list *list) {
    assert(list != NULL);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

void chirp_token_list_free(chirp_token_list *list) {
    if (list == NULL) return;
    if (list->tokens != NULL) {
        bs_free(list->tokens);
        list->tokens = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

bs_status chirp_token_list_append(chirp_token_list *list, chirp_token token) {
    assert(list != NULL);
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity == 0 ? 32 : list->capacity * 2;
        chirp_token *new_tokens = (chirp_token *)bs_realloc(list->tokens, new_cap * sizeof(chirp_token));
        if (new_tokens == NULL) {
            return BS_OUT_OF_MEMORY;
        }
        list->tokens = new_tokens;
        list->capacity = new_cap;
    }
    list->tokens[list->count++] = token;
    return BS_OK;
}
