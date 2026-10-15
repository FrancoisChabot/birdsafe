/**
 * @file chirp_token.h
 * @brief Token definitions and token stream data structures for Chirp.
 */

#ifndef CHIRP_FRONTEND_CHIRP_TOKEN_H
#define CHIRP_FRONTEND_CHIRP_TOKEN_H

#include "bs_core.h"
#include "chirp_frontend/chirp_str_view.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Chirp token classifications per the lexical specification. */
typedef enum chirp_token_type {
    /* Delimiters & Groupings */
    CHIRP_TOK_LPAREN = 0,    /**< '(' */
    CHIRP_TOK_RPAREN,        /**< ')' */
    CHIRP_TOK_LBRACE,        /**< '{' */
    CHIRP_TOK_RBRACE,        /**< '}' */
    CHIRP_TOK_LBRACKET,      /**< '[' */
    CHIRP_TOK_RBRACKET,      /**< ']' */
    CHIRP_TOK_COMMA,         /**< ',' */
    CHIRP_TOK_DOT,           /**< '.' */
    CHIRP_TOK_SEMICOLON,     /**< ';' */
    CHIRP_TOK_COLON,         /**< ':' */
    CHIRP_TOK_PIPE,          /**< '|' */

    /* Arithmetic Operators */
    CHIRP_TOK_PLUS,          /**< '+' */
    CHIRP_TOK_MINUS,         /**< '-' */
    CHIRP_TOK_STAR,          /**< '*' */
    CHIRP_TOK_SLASH,         /**< '/' */
    CHIRP_TOK_PERCENT,       /**< '%' */

    /* Compound Assignment Operators */
    CHIRP_TOK_PLUS_EQUAL,    /**< '+=' */
    CHIRP_TOK_MINUS_EQUAL,   /**< '-=' */
    CHIRP_TOK_STAR_EQUAL,    /**< '*=' */
    CHIRP_TOK_SLASH_EQUAL,   /**< '/=' */
    CHIRP_TOK_PERCENT_EQUAL, /**< '%=' */

    /* Assignment */
    CHIRP_TOK_EQUAL,         /**< '=' */

    /* Equality & Relational Comparisons */
    CHIRP_TOK_EQUAL_EQUAL,   /**< '==' */
    CHIRP_TOK_BANG_EQUAL,    /**< '!=' */
    CHIRP_TOK_LESS,          /**< '<' */
    CHIRP_TOK_LESS_EQUAL,    /**< '<=' */
    CHIRP_TOK_GREATER,       /**< '>' */
    CHIRP_TOK_GREATER_EQUAL, /**< '>=' */

    /* Logical & Bitwise Operators */
    CHIRP_TOK_BANG,          /**< '!' */
    CHIRP_TOK_TILDE,         /**< '~' */
    CHIRP_TOK_AMPERSAND,     /**< '&' */
    CHIRP_TOK_AND_AND,       /**< '&&' */
    CHIRP_TOK_OR_OR,         /**< '||' */

    /* Fused Operators */
    CHIRP_TOK_AMPERSAND_MUT, /**< '&mut' */
    CHIRP_TOK_ARROW,         /**< '->' */
    CHIRP_TOK_ARROW_MUT,     /**< '->mut' */
    CHIRP_TOK_FAT_ARROW,     /**< '=>' */
    CHIRP_TOK_DOT_DOT,       /**< '..' */
    CHIRP_TOK_DOT_DOT_EQUAL, /**< '..=' */

    /* Set & Belonging Operators */
    CHIRP_TOK_IN,            /**< '∈' or '`in' */
    CHIRP_TOK_NOT_IN,        /**< '∉' or '`notin' */
    CHIRP_TOK_UNION,         /**< '∪' or '`or' */
    CHIRP_TOK_INTERSECTION,  /**< '∩' or '`and' */

    /* Literals & Names */
    CHIRP_TOK_IDENTIFIER,        /**< Identifier (including contextual mut/final/pub) */
    CHIRP_TOK_INTRINSIC,         /**< Backtick intrinsic (e.g. `any) */
    CHIRP_TOK_SYMBOLIC_CONSTANT, /**< Hash-prefixed constant (e.g. #foo) */
    CHIRP_TOK_NUMBER,            /**< Numeric literal */
    CHIRP_TOK_STRING,            /**< Quoted string literal */
    CHIRP_TOK_CHARACTER,         /**< Character literal */

    /* Format String Segments */
    CHIRP_TOK_FSTRING_LITERAL,   /**< f"..." with no interpolations */
    CHIRP_TOK_FSTRING_HEAD,      /**< f"...{" */
    CHIRP_TOK_FSTRING_MIDDLE,    /**< "}...{" */
    CHIRP_TOK_FSTRING_TAIL,      /**< "}..." */

    /* Reserved Keywords */
    CHIRP_TOK_KW_LET,            /**< 'let' */
    CHIRP_TOK_KW_STRUCT,         /**< 'struct' */
    CHIRP_TOK_KW_IF,             /**< 'if' */
    CHIRP_TOK_KW_ELSE,           /**< 'else' */
    CHIRP_TOK_KW_WHILE,          /**< 'while' */
    CHIRP_TOK_KW_FOR,            /**< 'for' */
    CHIRP_TOK_KW_BREAK,          /**< 'break' */
    CHIRP_TOK_KW_MATCH,          /**< 'match' */
    CHIRP_TOK_KW_DO,             /**< 'do' */
    CHIRP_TOK_KW_DEBUG,          /**< 'debug' */
    CHIRP_TOK_KW_ENUM,           /**< 'enum' */

    /* Stream Boundary & Diagnostics */
    CHIRP_TOK_EOF,               /**< End of source text */
    CHIRP_TOK_ERROR,             /**< Lexical error token */

    CHIRP_TOK_COUNT
} chirp_token_type;

/** A single lexical token in the source text. */
typedef struct chirp_token {
    chirp_token_type type;
    chirp_str_view lexeme;
    uint32_t line;
    uint32_t column;
    chirp_str_view leading_trivia;
} chirp_token;

/** Dynamic list of scanned tokens. */
typedef struct chirp_token_list {
    chirp_token *tokens;
    size_t count;
    size_t capacity;
} chirp_token_list;

/** Returns a static string name for a token type (e.g., "kw_let", "+", "identifier"). */
BS_API const char *chirp_token_type_name(chirp_token_type type);

/** Initializes an empty token list. */
BS_API void chirp_token_list_init(chirp_token_list *list);

/** Releases memory allocated by a token list. Safe to pass NULL. */
BS_API void chirp_token_list_free(chirp_token_list *list);

/** Appends a token to the list, expanding capacity if needed. Returns BS_OK or BS_OUT_OF_MEMORY. */
BS_API bs_status chirp_token_list_append(chirp_token_list *list, chirp_token token);

#ifdef __cplusplus
}
#endif

#endif /* CHIRP_FRONTEND_CHIRP_TOKEN_H */
