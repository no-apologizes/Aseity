#include "Headers/lexer.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define likely(a) __builtin_expect(!!(a), 1)
#define unlikely(a) __builtin_expect(!!(a), 0)
#define f_static_inline __attribute__((__always_inline__)) static inline

typedef struct {
    const char *cursor;
    const char *line_start;
    int64_t line;
    int64_t column;
} LexerState;

static LexerState S;
static const void *dispatch_table[256];
static bool table_initialized = false;

f_static_inline char peek(void) { return *S.cursor; }

f_static_inline char advance(void) {
    const char c = *S.cursor;
    if (likely(c != '\0')) { // It's likely that the current char isn't a null char(because the input is a char so the null char is the EOF)
        S.cursor++;
        if (unlikely(c == '\n')) { // This is unlikely because skip whitespace is called first
            S.line++;
            S.column = 1;
            S.line_start = S.cursor;
        } else {
            S.column++;
        }}
    return c;
}

f_static_inline void skip_whitespace(void) {
    while (1) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            S.cursor++;
            S.column++;
        } else if (c == '\n') {
            S.cursor++;
            S.line++;
            S.column = 1;
            S.line_start = S.cursor;
        } else {
            break;
        }}
}

f_static_inline uint32_t decode_utf8(const char* src, int32_t* out_bytes) {
    const unsigned char* c = (const unsigned char*)src;
    const unsigned char lead = c[0];

    if (likely(lead < 0x80)) {
        *out_bytes = 1;
        return lead;
    }
    if ((lead & 0xE0) == 0xC0) {
        if (unlikely(c[1] == '\0')) goto malformed; // Bounds check
        *out_bytes = 2;
        return (((uint32_t)lead & 0x1F) << 6) | ((uint32_t)c[1] & 0x3F);
    }
    if ((lead & 0xF0) == 0xE0) {
        if (unlikely(c[1] == '\0' || c[2] == '\0')) goto malformed; // Bounds check
        *out_bytes = 3;
        return (((uint32_t)lead & 0x0F) << 12) | (((uint32_t)c[1] & 0x3F) << 6) | ((uint32_t)c[2] & 0x3F);
    }
    // Added proper F8 mask for 4-byte chars
    if ((lead & 0xF8) == 0xF0) {
        if (unlikely(c[1] == '\0' || c[2] == '\0' || c[3] == '\0')) goto malformed; // Bounds check
        *out_bytes = 4;
        return (((uint32_t)lead & 0x07) << 18) | (((uint32_t)c[1] & 0x3F) << 12) | (((uint32_t)c[2] & 0x3F) << 6) | ((uint32_t)c[3] & 0x3F);
    }

    malformed:
    // If we hit EOF unexpectedly, consume 1 byte and return replacement char
    *out_bytes = 1;
    return 0xFFFD;
}

f_static_inline bool is_unicode_identifier(const uint32_t cp) {
    return (cp >= 0x2100 && cp <= 0x214F) || // Letterlike Symbols (ℝ) Sucks that they're called 'Double-Struck Capital'
           (cp >= 0x0370 && cp <= 0x03FF) || // Greek and Coptic script
           (cp >= 0x4E00 && cp <= 0x9FFF) || // CJK Unified Ideographs
           (cp >= 0x2200 && cp <= 0x22FF) || // Mathematical Operators (∀, ∃, ∈, ≠)
           (cp >= 0x2190 && cp <= 0x21FF) || // Arrows (⇒)
           (cp >= 0x2A00 && cp <= 0x2AFF);   // Supplemental Mathematical Operators
}

f_static_inline TokenType match_keyword(const char *start, const size_t length) {
    switch (length) {
        case 2: {
            if (strncmp(start, "i8", 2) == 0) return TOKEN_TYPE;
            if (strncmp(start, "u8", 2) == 0) return TOKEN_TYPE;

            if (strncmp(start, "if", 2) == 0) return TOKEN_IF;
            if (strncmp(start, "as", 2) == 0) return TOKEN_AS;
            break;
        }
        case 3: {
            if (strncmp(start, "i16", 3) == 0) return TOKEN_TYPE;
            if (strncmp(start, "u16", 3) == 0) return TOKEN_TYPE;
            if (strncmp(start, "i32", 3) == 0) return TOKEN_TYPE;
            if (strncmp(start, "u32", 3) == 0) return TOKEN_TYPE;
            if (strncmp(start, "f32", 3) == 0) return TOKEN_TYPE;
            if (strncmp(start, "i64", 3) == 0) return TOKEN_TYPE;
            if (strncmp(start, "u64", 3) == 0) return TOKEN_TYPE;
            if (strncmp(start, "f64", 3) == 0) return TOKEN_TYPE;
            if (strncmp(start, "str", 3) == 0) return TOKEN_TYPE;
            break;
        }
        case 4: {
            if (strncmp(start, "bool", 4) == 0) return TOKEN_TYPE;
            if (strncmp(start, "i128", 4) == 0) return TOKEN_TYPE;
            if (strncmp(start, "u128", 4) == 0) return TOKEN_TYPE;
            if (strncmp(start, "f128", 4) == 0) return TOKEN_TYPE;

            if (strncmp(start, "goto", 4) == 0) return TOKEN_GOTO;
            if (strncmp(start, "else", 4) == 0) return TOKEN_ELSE;
            break;
        }
        case 5: {
            if (strncmp(start, "const", 5) == 0) return TOKEN_CONST;
            if (strncmp(start, "break", 5) == 0) return TOKEN_BREAK;
            break;
        }
        case 6: {
            if (strncmp(start, "inline", 6) == 0) return TOKEN_INLINE;
            if (strncmp(start, "return", 6) == 0) return TOKEN_RETURN;
            if (strncmp(start, "struct", 6) == 0) return TOKEN_STRUCT;
            if (strncmp(start, "import", 6) == 0) return TOKEN_IMPORT;
            break;
        }
        case 8: {
            if (strncmp(start, "continue", 8) == 0) return TOKEN_CONTINUE;
            break;
        }
        default: break;
    }
    return TOKEN_IDENTIFIER;
}

// Consumes remainder of identifier after the first char has been verified
f_static_inline void consume_other_identifiers(void) {
    while (1) {
        const unsigned char c = (unsigned char)peek();
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') {
            advance();
            continue;
        }
        if (c >= 0x80) {
            int32_t bytes;
            const uint32_t cp = decode_utf8(S.cursor, &bytes);
            if (!is_unicode_identifier(cp)) break;
            S.cursor += bytes;
            S.column++;
            continue;
        }
        break;
    }
}

void lexer_init(const char *source) {
    S.cursor = source;
    S.line_start = S.cursor;
    S.line = 1;
    S.column = 1;

}

Token lexer_next_token(void) {
    if (unlikely(!table_initialized)) {
        for (int i = 0; i < 256; i++) {
            if (i >= '0' && i <= '9') {
                dispatch_table[i] = &&lex_digit;
            } else if ((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z') || i == '_') {
                dispatch_table[i] = &&lex_alpha;
            } else if (i >= 128) {
                // Because every UTF-8 char starts with a byte value of 128 or higher, we can assign slots 128 to 255 in the jump table to the utf-8 label
                dispatch_table[i] = &&lex_utf8;
            } else {
                dispatch_table[i] = &&lex_unknown;
            }}


        dispatch_table['^']  = &&lex_caret;
        dispatch_table['&']  = &&lex_ampersand;
        dispatch_table['*']  = &&lex_star;

        dispatch_table['$']  = &&lex_label_ref;
        dispatch_table['=']  = &&lex_equals;
        dispatch_table['+']  = &&lex_plus;
        dispatch_table['-']  = &&lex_minus;
        dispatch_table['~']  = &&lex_bnot;
        dispatch_table['<']  = &&lex_lt;
        dispatch_table['>']  = &&lex_gt;
        dispatch_table['!']  = &&lex_not;
        dispatch_table['%']  = &&lex_rem;
        dispatch_table['/']  = &&lex_div;
        dispatch_table['\\'] = &&lex_or;
        dispatch_table['|']  = &&lex_term;
        dispatch_table['(']  = &&lex_lparen;
        dispatch_table[')']  = &&lex_rparen;
        dispatch_table['[']  = &&lex_lbracket;
        dispatch_table[']']  = &&lex_rbracket;
        dispatch_table['{']  = &&lex_lbrace;
        dispatch_table['}']  = &&lex_rbrace;
        dispatch_table[':']  = &&lex_colon;
        dispatch_table[',']  = &&lex_comma;
        dispatch_table['.']  = &&lex_dot;
        dispatch_table['"']  = &&lex_string;
        dispatch_table['\''] = &&lex_char;

        dispatch_table['\0'] = &&lex_eof;

        table_initialized = true;
    }

    while (1) {
        skip_whitespace();
        if (peek() == '.' && S.cursor[1] == '/') {
            advance(); // '.'
            advance(); // '/'
            while (1) {
                if (peek() == '\0') {
                    Token t;
                    t.line = S.line;
                    t.column = S.column;
                    t.start = S.cursor;
                    t.type = TOKEN_UNKNOWN;
                    t.length = 0;
                    return t; // Unterminated comment
                }
                if (peek() == '\\' && S.cursor[1] == '.') {
                    advance(); // '\'
                    advance(); // '.'
                    break;
                }
                advance();
            }
            continue; // There might be more whitespace/comments after this one
        }
        break; // No more whitespace or comments, cursor is at a valid token
    }

    Token t;
    t.line = S.line;
    t.column = S.column;
    t.start = S.cursor;
    t.length = 0;

    char c = peek();

    goto *dispatch_table[(unsigned char)c];

lex_digit: {
    while (peek() >= '0' && peek() <= '9') { advance(); }
    // Fractional part requires digit after dot, so '5.' isn't consumed
    // Allows for access like 5.field
    if (peek() == '.' && S.cursor[1] >= '0' && S.cursor[1] <= '9') {
        advance();
        while (peek() >= '0' && peek() <= '9') { advance(); }
    }
    t.type = TOKEN_NUMBER_LIT;
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_alpha: {
    advance();
    consume_other_identifiers();
    t.length = (size_t)(S.cursor - t.start);
    t.type = match_keyword(t.start, t.length); // Falls back to TOKEN_IDENTIFIER
    return t;
}

lex_utf8: {
    int32_t bytes;
    uint32_t cp = decode_utf8(S.cursor, &bytes);
    if (!is_unicode_identifier(cp)) {
        // Not an identifier codepoint, consume as an unknown token
        S.cursor += bytes;
        S.column++;
        t.type = TOKEN_UNKNOWN;
        t.length = (size_t)(S.cursor - t.start);
        return t;
    }
    S.cursor += bytes;
    S.column++;
    consume_other_identifiers();
    t.type = TOKEN_IDENTIFIER; // Rest are ascii
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_caret: {
    advance();
    if (peek() == '=') { advance(); t.type = TOKEN_BXORE; } // ^=
    else { t.type = TOKEN_CARET; }                          // ^
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_ampersand: {
    advance();
    if (peek() == '=') { advance(); t.type = TOKEN_BANDE; } // &=
    else { t.type = TOKEN_AMPERSAND; }                      // address-of or bitwise AND
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_star: {
    advance();
    if (peek() == '=') { advance(); t.type = TOKEN_MULE; } // *=
    else { t.type = TOKEN_STAR; }                          // mul or dereference
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_label_ref: {
    advance();
    consume_other_identifiers();
    // Bare '$' isn't a valid label
    t.type = (S.cursor - t.start == 1) ? TOKEN_UNKNOWN : TOKEN_LABEL_REF;
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_equals: {
    advance();
    if (peek() == '=') { advance(); t.type = TOKEN_EQ; } // ==
    else { t.type = TOKEN_EQUALS; }                      // =
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_plus: {
    advance();
    if (peek() == '+') { advance(); t.type = TOKEN_INC; }        // ++
    else if (peek() == '=') { advance(); t.type = TOKEN_PLUSE; } // +=
    else { t.type = TOKEN_PLUS; }                                // +
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_minus: {
    advance();
    if (peek() == '-') { advance(); t.type = TOKEN_DEC; }       // --
    else if (peek() == '=') { advance(); t.type = TOKEN_MINE; } // -=
    else { t.type = TOKEN_MINUS; }                              // -
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_lt: {
    advance();
    if (peek() == '=') { advance(); t.type = TOKEN_LE; }       // <=
    else if (peek() == '<') {
        advance();
        if (peek() == '=') { advance(); t.type = TOKEN_BSTE; } // <<=
        else { t.type = TOKEN_BST; }                           // <<
    } else { t.type = TOKEN_LT; }                              // <
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_gt: {
    advance();
    if (peek() == '=') { advance(); t.type = TOKEN_GE; }            // >=
    else if (peek() == '>') {
        advance();
        if (peek() == '>') { advance(); t.type = TOKEN_LSR; }       // >>>
        else if (peek() == '=') { advance(); t.type = TOKEN_BSRE; } // >>=
        else { t.type = TOKEN_ST; }
    }
    else { t.type = TOKEN_GT; }                                     // >
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_not: {
    advance();
    if (peek() == '=') { advance(); t.type = TOKEN_NE; } // !=
    else { t.type = TOKEN_NOT; }                         // !
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_rem: {
    advance();
    if (peek() == '=') { advance(); t.type = TOKEN_REME; } // %=
    else { t.type = TOKEN_REM; }                           // %
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_div: {
    advance();
    if (peek() == '\\') { advance(); t.type = TOKEN_AND; }      // /\ '
    else if (peek() == '=') { advance(); t.type = TOKEN_DIVE; } // /=
    else { t.type = TOKEN_DIV; }                                // /
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_or: {
    advance();
    if (peek() == '/') { advance(); t.type = TOKEN_OR; }          // \/
    else if (peek() == '|') {
        advance();
        if (peek() == '=') { advance(); t.type = TOKEN_BORE; }    // \|=
        else t.type = TOKEN_BOR;                                  // \|
    } else { t.type = TOKEN_UNKNOWN; } // lone backslash isn't a valid token
    t.length = (size_t)(S.cursor - t.start);
    return t;
}

lex_term: {
    advance();
    t.type = TOKEN_TERM;
    t.length = 1;
    return t;
}

lex_lparen:   { advance(); t.type = TOKEN_LPAREN;   t.length = 1; return t; }
lex_rparen:   { advance(); t.type = TOKEN_RPAREN;   t.length = 1; return t; }
lex_lbracket: { advance(); t.type = TOKEN_LBRACKET; t.length = 1; return t; }
lex_rbracket: { advance(); t.type = TOKEN_RBRACKET; t.length = 1; return t; }
lex_lbrace:   { advance(); t.type = TOKEN_LBRACE;   t.length = 1; return t; }
lex_rbrace:   { advance(); t.type = TOKEN_RBRACE;   t.length = 1; return t; }
lex_bnot:     { advance(); t.type = TOKEN_BNOT;     t.length = 1; return t; }
lex_colon:    { advance(); t.type = TOKEN_COLON;    t.length = 1; return t; }
lex_comma:    { advance(); t.type = TOKEN_COMMA;    t.length = 1; return t; }
lex_dot:      { advance(); t.type = TOKEN_DOT;      t.length = 1; return t; }

lex_string: {
    advance();
    t.start = S.cursor; // Token text is the content only, not the quotes
    while (peek() != '"' && peek() != '\0') {
        if (peek() == '\\' && S.cursor[1] != '\0') { advance(); } // Skip escape char, let next advance() take the escaped char
        advance();
    }
    t.length = (size_t)(S.cursor - t.start);
    if (peek() == '"') { advance(); t.type = TOKEN_STRING_LIT; }
    else t.type = TOKEN_UNKNOWN; // Hit EOF with no closing quote
    return t;
}

lex_char: {
    advance();
    t.start = S.cursor;
    if (peek() == '\\' && S.cursor[1] != '\0') {
        advance(); advance(); // Backslash + escaped char
    } else if ((unsigned char)peek() >= 0x80) {
        int32_t bytes;
        decode_utf8(S.cursor, &bytes);
        S.cursor += bytes; S.column++; // Multibyte char literal 'ℵ'
    } else if (peek() != '\'' && peek() != '\0') { advance(); }
    t.length = (size_t)(S.cursor - t.start);
    if (peek() == '\'') { advance(); t.type = TOKEN_CHAR_LIT; }
    else t.type = TOKEN_UNKNOWN; // Unterminated or empty ''
    return t;
}

lex_eof: {
    t.type = TOKEN_EOF;
    t.length = 0;
    return t;
}

lex_unknown: {
    advance(); // Consume the byte so we can't get stuck retrying it
    t.type = TOKEN_UNKNOWN;
    t.length = (size_t)(S.cursor - t.start);
    return t;
}   }