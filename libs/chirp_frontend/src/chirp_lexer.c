/**
 * @file chirp_lexer.c
 * @brief UTF-8 lexer implementation for the Chirp frontend.
 */

#include "chirp_frontend/chirp_lexer.h"
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct chirp_lexer {
    const char *source;
    size_t length;
    size_t start;
    size_t current;
    uint32_t line;
    uint32_t column;
    uint32_t start_line;
    uint32_t start_column;
    chirp_str_view pending_trivia;
    chirp_token_list *tokens;

    uint32_t *template_brace_stack;
    size_t template_brace_top;
    size_t template_brace_cap;
    uint32_t template_brace_local[32];

    bs_status error_status;
} chirp_lexer;

static bool is_at_end(const chirp_lexer *lexer) {
    return lexer->current >= lexer->length;
}

static char advance(chirp_lexer *lexer) {
    char c = lexer->source[lexer->current++];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else if (((unsigned char)c & 0xC0) != 0x80) {
        // Only increment column on starting bytes of UTF-8 sequences
        lexer->column++;
    }
    return c;
}

static char peek(const chirp_lexer *lexer) {
    if (lexer->current >= lexer->length) return '\0';
    return lexer->source[lexer->current];
}

static char peek_next(const chirp_lexer *lexer) {
    if (lexer->current + 1 >= lexer->length) return '\0';
    return lexer->source[lexer->current + 1];
}

static bool match_char(chirp_lexer *lexer, char expected) {
    if (lexer->current >= lexer->length) return false;
    if (lexer->source[lexer->current] != expected) return false;
    advance(lexer);
    return true;
}

static bool match_str(chirp_lexer *lexer, const char *str, size_t str_len) {
    if (lexer->current + str_len > lexer->length) return false;
    if (memcmp(lexer->source + lexer->current, str, str_len) == 0) {
        for (size_t i = 0; i < str_len; i++) {
            advance(lexer);
        }
        return true;
    }
    return false;
}

static bool match_word(chirp_lexer *lexer, const char *word, size_t word_len) {
    if (lexer->current + word_len > lexer->length) return false;
    if (memcmp(lexer->source + lexer->current, word, word_len) != 0) return false;
    if (lexer->current + word_len < lexer->length) {
        unsigned char next = (unsigned char)lexer->source[lexer->current + word_len];
        if (isalnum(next) || next == '_') return false;
    }
    for (size_t i = 0; i < word_len; i++) {
        advance(lexer);
    }
    return true;
}

static bool push_template_brace(chirp_lexer *lexer, uint32_t val) {
    if (lexer->template_brace_top >= lexer->template_brace_cap) {
        size_t new_cap = lexer->template_brace_cap * 2;
        uint32_t *new_stack = (uint32_t *)bs_alloc(new_cap * sizeof(uint32_t));
        if (new_stack == NULL) {
            lexer->error_status = BS_OUT_OF_MEMORY;
            return false;
        }
        memcpy(new_stack, lexer->template_brace_stack, lexer->template_brace_top * sizeof(uint32_t));
        if (lexer->template_brace_stack != lexer->template_brace_local) {
            bs_free(lexer->template_brace_stack);
        }
        lexer->template_brace_stack = new_stack;
        lexer->template_brace_cap = new_cap;
    }
    lexer->template_brace_stack[lexer->template_brace_top++] = val;
    return true;
}

static void add_token(chirp_lexer *lexer, chirp_token_type type) {
    chirp_token tok;
    tok.type = type;
    tok.lexeme.ptr = lexer->source + lexer->start;
    tok.lexeme.len = lexer->current - lexer->start;
    tok.line = lexer->start_line;
    tok.column = lexer->start_column;
    tok.leading_trivia = lexer->pending_trivia;

    lexer->pending_trivia.ptr = "";
    lexer->pending_trivia.len = 0;

    bs_status st = chirp_token_list_append(lexer->tokens, tok);
    if (st != BS_OK) {
        lexer->error_status = st;
    }
}

static void skip_whitespace(chirp_lexer *lexer) {
    while (true) {
        char c = peek(lexer);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
            case '\n':
                advance(lexer);
                break;
            case '/':
                if (peek_next(lexer) == '/') {
                    while (peek(lexer) != '\n' && !is_at_end(lexer)) {
                        advance(lexer);
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static void scan_identifier(chirp_lexer *lexer) {
    while (isalnum((unsigned char)peek(lexer)) || peek(lexer) == '_') {
        advance(lexer);
    }
    chirp_str_view text = chirp_str_view_create(
        lexer->source + lexer->start,
        lexer->current - lexer->start
    );
    chirp_token_type type = CHIRP_TOK_IDENTIFIER;

    if (chirp_str_view_equals_cstr(text, "let")) type = CHIRP_TOK_KW_LET;
    else if (chirp_str_view_equals_cstr(text, "struct")) type = CHIRP_TOK_KW_STRUCT;
    else if (chirp_str_view_equals_cstr(text, "if")) type = CHIRP_TOK_KW_IF;
    else if (chirp_str_view_equals_cstr(text, "else")) type = CHIRP_TOK_KW_ELSE;
    else if (chirp_str_view_equals_cstr(text, "while")) type = CHIRP_TOK_KW_WHILE;
    else if (chirp_str_view_equals_cstr(text, "for")) type = CHIRP_TOK_KW_FOR;
    else if (chirp_str_view_equals_cstr(text, "break")) type = CHIRP_TOK_KW_BREAK;
    else if (chirp_str_view_equals_cstr(text, "match")) type = CHIRP_TOK_KW_MATCH;
    else if (chirp_str_view_equals_cstr(text, "do")) type = CHIRP_TOK_KW_DO;
    else if (chirp_str_view_equals_cstr(text, "debug")) type = CHIRP_TOK_KW_DEBUG;
    else if (chirp_str_view_equals_cstr(text, "enum")) type = CHIRP_TOK_KW_ENUM;

    add_token(lexer, type);
}

static void scan_intrinsic(chirp_lexer *lexer) {
    while (isalnum((unsigned char)peek(lexer)) || peek(lexer) == '_') {
        advance(lexer);
    }
    if (lexer->current - lexer->start == 1) {
        add_token(lexer, CHIRP_TOK_ERROR);
    } else {
        chirp_str_view text = chirp_str_view_create(
            lexer->source + lexer->start,
            lexer->current - lexer->start
        );
        if (chirp_str_view_equals_cstr(text, "`in")) add_token(lexer, CHIRP_TOK_IN);
        else if (chirp_str_view_equals_cstr(text, "`notin")) add_token(lexer, CHIRP_TOK_NOT_IN);
        else if (chirp_str_view_equals_cstr(text, "`or")) add_token(lexer, CHIRP_TOK_UNION);
        else if (chirp_str_view_equals_cstr(text, "`and")) add_token(lexer, CHIRP_TOK_INTERSECTION);
        else add_token(lexer, CHIRP_TOK_INTRINSIC);
    }
}

static void scan_number(chirp_lexer *lexer) {
    while (isdigit((unsigned char)peek(lexer))) {
        advance(lexer);
    }

    if (peek(lexer) == '.' && isdigit((unsigned char)peek_next(lexer))) {
        advance(lexer); // consume '.'
        while (isdigit((unsigned char)peek(lexer))) {
            advance(lexer);
        }
    }

    add_token(lexer, CHIRP_TOK_NUMBER);
}

static void scan_string(chirp_lexer *lexer) {
    while (peek(lexer) != '"' && !is_at_end(lexer)) {
        if (peek(lexer) == '\\') {
            advance(lexer);
        }
        advance(lexer);
    }
    if (is_at_end(lexer)) {
        add_token(lexer, CHIRP_TOK_ERROR);
        return;
    }
    advance(lexer); // consume '"'
    add_token(lexer, CHIRP_TOK_STRING);
}

static void scan_template_string_part(chirp_lexer *lexer, bool is_head) {
    while (peek(lexer) != '"' && peek(lexer) != '{' && !is_at_end(lexer)) {
        if (peek(lexer) == '\\') {
            advance(lexer);
            if (!is_at_end(lexer)) {
                advance(lexer);
            }
            continue;
        }
        advance(lexer);
    }
    if (is_at_end(lexer)) {
        add_token(lexer, CHIRP_TOK_ERROR);
        return;
    }
    if (peek(lexer) == '"') {
        advance(lexer); // consume '"'
        add_token(lexer, is_head ? CHIRP_TOK_FSTRING_LITERAL : CHIRP_TOK_FSTRING_TAIL);
    } else if (peek(lexer) == '{') {
        advance(lexer); // consume '{'
        add_token(lexer, is_head ? CHIRP_TOK_FSTRING_HEAD : CHIRP_TOK_FSTRING_MIDDLE);
        push_template_brace(lexer, 0);
    }
}

static bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static uint32_t hex_val(char c) {
    if (c >= '0' && c <= '9') return (uint32_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint32_t)(10 + c - 'a');
    return (uint32_t)(10 + c - 'A');
}

static bool is_continuation_byte(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

static unsigned char peek_byte(const chirp_lexer *lexer, size_t offset) {
    if (lexer->current + offset >= lexer->length) return '\0';
    return (unsigned char)lexer->source[lexer->current + offset];
}

static bool has_bytes(const chirp_lexer *lexer, size_t count) {
    return lexer->current + count <= lexer->length;
}

static bool consume_utf8_codepoint(chirp_lexer *lexer) {
    if (is_at_end(lexer)) return false;

    unsigned char c = peek_byte(lexer, 0);
    if (c == '\n' || c == '\r') return false;

    if (c <= 0x7F) {
        advance(lexer);
        return true;
    }

    size_t byte_count = 0;
    bool valid = false;

    if (c >= 0xC2 && c <= 0xDF) {
        byte_count = 2;
        valid = has_bytes(lexer, byte_count) && is_continuation_byte(peek_byte(lexer, 1));
    } else if (c == 0xE0) {
        byte_count = 3;
        valid = has_bytes(lexer, byte_count) &&
            peek_byte(lexer, 1) >= 0xA0 && peek_byte(lexer, 1) <= 0xBF &&
            is_continuation_byte(peek_byte(lexer, 2));
    } else if (c >= 0xE1 && c <= 0xEC) {
        byte_count = 3;
        valid = has_bytes(lexer, byte_count) &&
            is_continuation_byte(peek_byte(lexer, 1)) &&
            is_continuation_byte(peek_byte(lexer, 2));
    } else if (c == 0xED) {
        byte_count = 3;
        valid = has_bytes(lexer, byte_count) &&
            peek_byte(lexer, 1) >= 0x80 && peek_byte(lexer, 1) <= 0x9F &&
            is_continuation_byte(peek_byte(lexer, 2));
    } else if (c >= 0xEE && c <= 0xEF) {
        byte_count = 3;
        valid = has_bytes(lexer, byte_count) &&
            is_continuation_byte(peek_byte(lexer, 1)) &&
            is_continuation_byte(peek_byte(lexer, 2));
    } else if (c == 0xF0) {
        byte_count = 4;
        valid = has_bytes(lexer, byte_count) &&
            peek_byte(lexer, 1) >= 0x90 && peek_byte(lexer, 1) <= 0xBF &&
            is_continuation_byte(peek_byte(lexer, 2)) &&
            is_continuation_byte(peek_byte(lexer, 3));
    } else if (c >= 0xF1 && c <= 0xF3) {
        byte_count = 4;
        valid = has_bytes(lexer, byte_count) &&
            is_continuation_byte(peek_byte(lexer, 1)) &&
            is_continuation_byte(peek_byte(lexer, 2)) &&
            is_continuation_byte(peek_byte(lexer, 3));
    } else if (c == 0xF4) {
        byte_count = 4;
        valid = has_bytes(lexer, byte_count) &&
            peek_byte(lexer, 1) >= 0x80 && peek_byte(lexer, 1) <= 0x8F &&
            is_continuation_byte(peek_byte(lexer, 2)) &&
            is_continuation_byte(peek_byte(lexer, 3));
    }

    if (!valid) return false;

    for (size_t i = 0; i < byte_count; i++) {
        advance(lexer);
    }
    return true;
}

static bool consume_unicode_escape(chirp_lexer *lexer) {
    uint32_t codepoint = 0;
    for (int i = 0; i < 4; i++) {
        if (is_at_end(lexer) || !is_hex_digit(peek(lexer))) return false;
        codepoint = (codepoint << 4) | hex_val(peek(lexer));
        advance(lexer);
    }
    return codepoint < 0xD800 || codepoint > 0xDFFF;
}

static bool consume_escape_codepoint(chirp_lexer *lexer) {
    if (!match_char(lexer, '\\') || is_at_end(lexer)) return false;

    char c = advance(lexer);
    switch (c) {
        case '\\':
        case '\'':
        case '"':
        case 'n':
        case 'r':
        case 't':
        case '0':
            return true;
        case 'u':
            return consume_unicode_escape(lexer);
        default:
            return false;
    }
}

static void consume_invalid_character_literal(chirp_lexer *lexer) {
    while (peek(lexer) != '\'' && !is_at_end(lexer)) {
        advance(lexer);
    }
    if (!is_at_end(lexer)) {
        advance(lexer); // consume closing quote for error recovery
    }
    add_token(lexer, CHIRP_TOK_ERROR);
}

static void scan_character(chirp_lexer *lexer) {
    if (is_at_end(lexer) || peek(lexer) == '\'' || peek(lexer) == '\n' || peek(lexer) == '\r') {
        consume_invalid_character_literal(lexer);
        return;
    }

    bool valid = peek(lexer) == '\\'
        ? consume_escape_codepoint(lexer)
        : consume_utf8_codepoint(lexer);

    if (!valid || is_at_end(lexer) || peek(lexer) != '\'') {
        consume_invalid_character_literal(lexer);
        return;
    }

    advance(lexer); // consume closing quote
    add_token(lexer, CHIRP_TOK_CHARACTER);
}

static void scan_symbolic_constant(chirp_lexer *lexer) {
    // '#' has already been consumed
    char c = peek(lexer);
    if (!isalpha((unsigned char)c) && c != '_') {
        add_token(lexer, CHIRP_TOK_ERROR);
        return;
    }
    while (isalnum((unsigned char)peek(lexer)) || peek(lexer) == '_') {
        advance(lexer);
    }
    add_token(lexer, CHIRP_TOK_SYMBOLIC_CONSTANT);
}

static void scan_token(chirp_lexer *lexer) {
    size_t trivia_start = lexer->current;
    skip_whitespace(lexer);
    lexer->pending_trivia.ptr = lexer->source + trivia_start;
    lexer->pending_trivia.len = lexer->current - trivia_start;

    if (is_at_end(lexer)) return;

    lexer->start = lexer->current;
    lexer->start_line = lexer->line;
    lexer->start_column = lexer->column;

    /* Multi-byte Unicode operator checks */
    if (match_str(lexer, "∈", 3)) { add_token(lexer, CHIRP_TOK_IN); return; }
    if (match_str(lexer, "∉", 3)) { add_token(lexer, CHIRP_TOK_NOT_IN); return; }
    if (match_str(lexer, "∪", 3)) { add_token(lexer, CHIRP_TOK_UNION); return; }
    if (match_str(lexer, "∩", 3)) { add_token(lexer, CHIRP_TOK_INTERSECTION); return; }
    if (match_str(lexer, "..=", 3)) { add_token(lexer, CHIRP_TOK_DOT_DOT_EQUAL); return; }

    char c = advance(lexer);

    if (c == 'f' && peek(lexer) == '"') {
        advance(lexer); // consume '"'
        scan_template_string_part(lexer, true);
        return;
    }

    if (isalpha((unsigned char)c) || c == '_') {
        scan_identifier(lexer);
        return;
    }

    if (c == '`') {
        scan_intrinsic(lexer);
        return;
    }

    if (c == '~') {
        add_token(lexer, CHIRP_TOK_TILDE);
        return;
    }

    if (isdigit((unsigned char)c)) {
        scan_number(lexer);
        return;
    }

    switch (c) {
        case '(': add_token(lexer, CHIRP_TOK_LPAREN); break;
        case ')': add_token(lexer, CHIRP_TOK_RPAREN); break;
        case '{':
            if (lexer->template_brace_top > 0) {
                lexer->template_brace_stack[lexer->template_brace_top - 1]++;
            }
            add_token(lexer, CHIRP_TOK_LBRACE);
            break;
        case '}':
            if (lexer->template_brace_top > 0) {
                if (lexer->template_brace_stack[lexer->template_brace_top - 1] > 0) {
                    lexer->template_brace_stack[lexer->template_brace_top - 1]--;
                    add_token(lexer, CHIRP_TOK_RBRACE);
                } else {
                    lexer->template_brace_top--;
                    scan_template_string_part(lexer, false);
                }
            } else {
                add_token(lexer, CHIRP_TOK_RBRACE);
            }
            break;
        case '[': add_token(lexer, CHIRP_TOK_LBRACKET); break;
        case ']': add_token(lexer, CHIRP_TOK_RBRACKET); break;
        case ',': add_token(lexer, CHIRP_TOK_COMMA); break;
        case '.':
            if (match_char(lexer, '.')) add_token(lexer, CHIRP_TOK_DOT_DOT);
            else add_token(lexer, CHIRP_TOK_DOT);
            break;
        case '-':
            if (match_char(lexer, '>')) {
                if (match_word(lexer, "mut", 3)) add_token(lexer, CHIRP_TOK_ARROW_MUT);
                else add_token(lexer, CHIRP_TOK_ARROW);
            } else if (match_char(lexer, '=')) {
                add_token(lexer, CHIRP_TOK_MINUS_EQUAL);
            } else {
                add_token(lexer, CHIRP_TOK_MINUS);
            }
            break;
        case '+':
            if (match_char(lexer, '=')) add_token(lexer, CHIRP_TOK_PLUS_EQUAL);
            else add_token(lexer, CHIRP_TOK_PLUS);
            break;
        case ';': add_token(lexer, CHIRP_TOK_SEMICOLON); break;
        case ':': add_token(lexer, CHIRP_TOK_COLON); break;
        case '*':
            if (match_char(lexer, '=')) add_token(lexer, CHIRP_TOK_STAR_EQUAL);
            else add_token(lexer, CHIRP_TOK_STAR);
            break;
        case '%':
            if (match_char(lexer, '=')) add_token(lexer, CHIRP_TOK_PERCENT_EQUAL);
            else add_token(lexer, CHIRP_TOK_PERCENT);
            break;
        case '|':
            if (match_char(lexer, '|')) add_token(lexer, CHIRP_TOK_OR_OR);
            else add_token(lexer, CHIRP_TOK_PIPE);
            break;
        case '&':
            if (match_char(lexer, '&')) {
                add_token(lexer, CHIRP_TOK_AND_AND);
            } else if (match_word(lexer, "mut", 3)) {
                add_token(lexer, CHIRP_TOK_AMPERSAND_MUT);
            } else {
                add_token(lexer, CHIRP_TOK_AMPERSAND);
            }
            break;
        case '!':
            if (match_char(lexer, '=')) add_token(lexer, CHIRP_TOK_BANG_EQUAL);
            else add_token(lexer, CHIRP_TOK_BANG);
            break;
        case '=':
            if (match_char(lexer, '=')) add_token(lexer, CHIRP_TOK_EQUAL_EQUAL);
            else if (match_char(lexer, '>')) add_token(lexer, CHIRP_TOK_FAT_ARROW);
            else add_token(lexer, CHIRP_TOK_EQUAL);
            break;
        case '<':
            if (match_char(lexer, '=')) add_token(lexer, CHIRP_TOK_LESS_EQUAL);
            else add_token(lexer, CHIRP_TOK_LESS);
            break;
        case '>':
            if (match_char(lexer, '=')) add_token(lexer, CHIRP_TOK_GREATER_EQUAL);
            else add_token(lexer, CHIRP_TOK_GREATER);
            break;
        case '/':
            if (match_char(lexer, '=')) add_token(lexer, CHIRP_TOK_SLASH_EQUAL);
            else add_token(lexer, CHIRP_TOK_SLASH);
            break;
        case '"':
            scan_string(lexer);
            break;
        case '\'':
            scan_character(lexer);
            break;
        case '#':
            scan_symbolic_constant(lexer);
            break;
        default:
            add_token(lexer, CHIRP_TOK_ERROR);
            break;
    }
}

bs_status chirp_tokenize(const char *source, size_t length, chirp_token_list *out_tokens) {
    assert(out_tokens != NULL);
    if (source == NULL && length > 0) {
        return BS_INVALID_ARGUMENT;
    }

    chirp_token_list_init(out_tokens);

    chirp_lexer lexer;
    lexer.source = source != NULL ? source : "";
    lexer.length = length;
    lexer.start = 0;
    lexer.current = 0;
    lexer.line = 1;
    lexer.column = 1;
    lexer.start_line = 1;
    lexer.start_column = 1;
    lexer.pending_trivia.ptr = "";
    lexer.pending_trivia.len = 0;
    lexer.tokens = out_tokens;
    lexer.template_brace_stack = lexer.template_brace_local;
    lexer.template_brace_top = 0;
    lexer.template_brace_cap = sizeof(lexer.template_brace_local) / sizeof(lexer.template_brace_local[0]);
    lexer.error_status = BS_OK;

    while (!is_at_end(&lexer)) {
        scan_token(&lexer);
        if (lexer.error_status != BS_OK) {
            if (lexer.template_brace_stack != lexer.template_brace_local) {
                bs_free(lexer.template_brace_stack);
            }
            chirp_token_list_free(out_tokens);
            return lexer.error_status;
        }
    }

    if (lexer.template_brace_top > 0) {
        add_token(&lexer, CHIRP_TOK_ERROR);
    }

    // Append EOF token
    chirp_token eof_token;
    eof_token.type = CHIRP_TOK_EOF;
    eof_token.lexeme.ptr = lexer.source + lexer.current;
    eof_token.lexeme.len = 0;
    eof_token.line = lexer.line;
    eof_token.column = lexer.column;
    eof_token.leading_trivia = lexer.pending_trivia;

    bs_status st = chirp_token_list_append(out_tokens, eof_token);
    if (st != BS_OK) {
        if (lexer.template_brace_stack != lexer.template_brace_local) {
            bs_free(lexer.template_brace_stack);
        }
        chirp_token_list_free(out_tokens);
        return st;
    }

    if (lexer.template_brace_stack != lexer.template_brace_local) {
        bs_free(lexer.template_brace_stack);
    }
    return BS_OK;
}

bs_status chirp_tokenize_cstr(const char *source, chirp_token_list *out_tokens) {
    size_t len = source != NULL ? strlen(source) : 0;
    return chirp_tokenize(source, len, out_tokens);
}

bs_status chirp_format_text(const char *source, size_t length, char **out_formatted, size_t *out_length) {
    assert(out_formatted != NULL);
    chirp_token_list tokens;
    bs_status st = chirp_tokenize(source, length, &tokens);
    if (st != BS_OK) {
        return st;
    }

    size_t cap = length + 64;
    char *buf = (char *)bs_alloc(cap);
    if (buf == NULL) {
        chirp_token_list_free(&tokens);
        return BS_OUT_OF_MEMORY;
    }
    size_t len = 0;

    for (size_t i = 0; i < tokens.count; i++) {
        const chirp_token *t = &tokens.tokens[i];

        // Append leading trivia
        if (t->leading_trivia.len > 0) {
            while (len + t->leading_trivia.len + 16 > cap) {
                cap *= 2;
                char *new_buf = (char *)bs_realloc(buf, cap);
                if (new_buf == NULL) {
                    bs_free(buf);
                    chirp_token_list_free(&tokens);
                    return BS_OUT_OF_MEMORY;
                }
                buf = new_buf;
            }
            memcpy(buf + len, t->leading_trivia.ptr, t->leading_trivia.len);
            len += t->leading_trivia.len;
        }

        if (t->type == CHIRP_TOK_EOF) {
            break;
        }

        const char *replacement = NULL;
        size_t rep_len = 0;

        if (t->type == CHIRP_TOK_IN && chirp_str_view_equals_cstr(t->lexeme, "`in")) {
            replacement = "∈";
            rep_len = 3;
        } else if (t->type == CHIRP_TOK_NOT_IN && chirp_str_view_equals_cstr(t->lexeme, "`notin")) {
            replacement = "∉";
            rep_len = 3;
        } else if (t->type == CHIRP_TOK_UNION && chirp_str_view_equals_cstr(t->lexeme, "`or")) {
            replacement = "∪";
            rep_len = 3;
        } else if (t->type == CHIRP_TOK_INTERSECTION && chirp_str_view_equals_cstr(t->lexeme, "`and")) {
            replacement = "∩";
            rep_len = 3;
        } else {
            replacement = t->lexeme.ptr;
            rep_len = t->lexeme.len;
        }

        while (len + rep_len + 16 > cap) {
            cap *= 2;
            char *new_buf = (char *)bs_realloc(buf, cap);
            if (new_buf == NULL) {
                bs_free(buf);
                chirp_token_list_free(&tokens);
                return BS_OUT_OF_MEMORY;
            }
            buf = new_buf;
        }
        memcpy(buf + len, replacement, rep_len);
        len += rep_len;
    }

    buf[len] = '\0';
    chirp_token_list_free(&tokens);

    *out_formatted = buf;
    if (out_length != NULL) {
        *out_length = len;
    }
    return BS_OK;
}
