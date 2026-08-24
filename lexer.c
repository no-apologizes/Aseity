#include "Headers/lexer.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define likely(a) __builtin_expect(!!(a), 1)
#define unlikely(a) __builtin_expect(!!(a), 0)
#define MAX_INCLUDE_DEPTH 16


// Basically the goal is because we're targeting a single thread, our bottleneck is slow memory allocation via malloc or otherwise
// This is a Type 3 grammar

// Internal tracking state for position
typedef struct {
    const char *cursor;
    const char *line_start;
    int line; // Why int? and why not use size_t or an unsigned number where, we assume uint32 or 64 is lim of uint ⊇ lim ℕ and lim ℕ ⊆ lim uint
                // Because we'll use -1 later and using size_t or another type will allow type promotion and covert signed into unsigned and break a lot of stuff, and if someone writes 2.1 billion lines of code in my lang then they can change it
                    // And WE, yes, you and me don't use uint64 and like the max, 0xFFFFFFFFFFFFFFFF because two ints 4 bytes each fit perfectly into an 8 byte boundary and mixing signed and unsigned ints is naver a good thing
    int column;
} LexerState;

static LexerState S; // 'S' for 'scanner' as it's much easier to type than capital L
static const void *dispatch_table[256]; // Array of 256 for every possible ASCII byte
static bool table_initialized = false;

static LexerState state_stack[MAX_INCLUDE_DEPTH];
static int state_depth = 0;

void lexer_push_source(const char *source) {
    if (state_depth >= MAX_INCLUDE_DEPTH) {
        fprintf(stderr, "Compiler Error: Maximum import depth of %d exceeded.\n", MAX_INCLUDE_DEPTH);
        exit(EXIT_FAILURE);
    }
    state_stack[state_depth++] = S;
    S.cursor = source;
    S.line = 1;
    S.column = 1;
}

bool lexer_pop_source(void) {
    if (state_depth == 0) return false;
    S = state_stack[--state_depth];
    return true;
}

static inline char advance(void) {
    char c = *S.cursor;
    if (likely((c != '\0'))) { // This isn't true 99% of the time
        S.cursor++;
        if (unlikely(c == '\n')) { // VERY likely
            S.line++;
            S.column = 1;
            S.line_start = S.cursor;
        } else {
            S.column++;
        }}
    return c;
}

static inline char peek(void) {
    return *S.cursor;
}

// Last time we called advance() within the whitespace loop but advance() checks for \0 every time so unrolling the pointer math for specifically known whitespace chars: '\n', we skip the null char check
static inline void skip_whitespace(void) {
    while (1) {
        char c = peek();
        if (c == ' '|| c == '\t' || c == '\r') {
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

static inline uint32_t decode_utf8(const char* src, int* out_bytes) {
    const unsigned char* c = (const unsigned char*)src;
    const unsigned char lead = c[0];

    if (likely(lead < 0x80)) {
        *out_bytes = 1;
        return lead;
    }
    if ((lead & 0xE0) == 0xC0) {
        if (unlikely(c[1] == '\0')) goto malformed; // Bounds check
        *out_bytes = 2;
        return ((lead & 0x1F) << 6) | (c[1] & 0x3F);
    }
    if ((lead & 0xF0) == 0xE0) {
        if (unlikely(c[1] == '\0' || c[2] == '\0')) goto malformed; // Bounds check
        *out_bytes = 3;
        return ((lead & 0x0F) << 12) | ((c[1] & 0x3F) << 6) | (c[2] & 0x3F);
    }
    // Added proper F8 mask for 4-byte chars
    if ((lead & 0xF8) == 0xF0) {
        if (unlikely(c[1] == '\0' || c[2] == '\0' || c[3] == '\0')) goto malformed; // Bounds check
        *out_bytes = 4;
        return ((lead & 0x07) << 18) | ((c[1] & 0x3F) << 12) | ((c[2] & 0x3F) << 6) | (c[3] & 0x3F);
    }

    malformed:
    // If we hit EOF unexpectedly, consume 1 byte and return replacement char
    *out_bytes = 1;
    return 0xFFFD;
}

static inline bool is_unicode_operator(uint32_t cp) {
    return (cp >= 0x2200 && cp <= 0x22FF) || // Mathematical Operators (∀, ∃, ∈, ≠)
           (cp >= 0x2190 && cp <= 0x21FF) || // Arrows (⇒)
           (cp >= 0x2A00 && cp <= 0x2AFF);   // Supplemental Mathematical Operators
}


static inline bool is_unicode_identifier(uint32_t cp) {
    return (cp >= 0x2100 && cp <= 0x214F) || // Letterlike Symbols (ℝ) Sucks that they're called 'Double-Struck Capital'
           (cp >= 0x0370 && cp <= 0x03FF) || // Greek and Coptic script
           (cp >= 0x4E00 && cp <= 0x9FFF);   // CJK Unified Ideographs
}

static inline TokenType match_keyword(const char *start, size_t length) {
    // Check lengths before string eval as a normal switch over strings is impossible in C
    switch (length) {
        case 2:
            if (strncmp(start, "i8", 2) == 0) return TOKEN_TYPE;
            if (strncmp(start, "u8", 2) == 0) return TOKEN_TYPE;
            break;
        case 3:
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
        case 4:
            if (strncmp(start, "goto", 4) == 0) return TOKEN_GOTO;
            if (strncmp(start, "bool", 4) == 0) return TOKEN_TYPE;
            if (strncmp(start, "i128", 4) == 0) return TOKEN_TYPE;
            if (strncmp(start, "u128", 4) == 0) return TOKEN_TYPE;
            if (strncmp(start, "f128", 4) == 0) return TOKEN_TYPE;
            break;
        case 6:
            if (strncmp(start, "return", 6) == 0) return TOKEN_RETURN;
            if (strncmp(start, "import", 6) == 0) return TOKEN_IMPORT;
            if (strncmp(start, "struct", 6) == 0) return TOKEN_STRUCT;
            break;
        default: break;
    }
    return TOKEN_IDENTIFIER;
}

void lexer_init(const char *source) {
    S.cursor = source;
    S.line = 1;
    S.column = 1;
}

Token lexer_next_token(void) {
    // Bake code labels into a single-hop memory lookup block
    if (unlikely(!table_initialized)) {
        for (int i = 0; i < 256; i++) {
            if (i >= '0' && i <= '9') { // Digits
                dispatch_table[i] = &&lex_digit;
            } else if ((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z') || i == '_') {
                dispatch_table[i] = &&lex_alpha; // Letters
            } else if (i >= 128) {
                // Because every UTF-8 char starts with a byte value of 128 or higher, we can assign slots 128 to 255 in the jump table to the utf-8 label
                dispatch_table[i] = &&lex_utf8;
            } else {
                dispatch_table[i] = &&lex_unknown;
            }
        }

        dispatch_table['\0'] = &&lex_eof;
        dispatch_table['$']  = &&lex_label_ref;
        dispatch_table['=']  = &&lex_equals;
        dispatch_table['+']  = &&lex_plus;
        dispatch_table['-']  = &&lex_minus;
        dispatch_table['*']  = &&lex_mul;
        dispatch_table['%']  = &&lex_mod;
        dispatch_table['<']  = &&lex_operator;
        dispatch_table['>']  = &&lex_operator;
        dispatch_table['!']  = &&lex_operator;
        //dispatch_table['%']  = &&lex_operator;
        dispatch_table['/']  = &&lex_div;
        dispatch_table['|']  = &&lex_term;
        dispatch_table['(']  = &&lex_lparen;
        dispatch_table[')']  = &&lex_rparen;
        dispatch_table['[']  = &&lex_lbracket;
        dispatch_table[']']  = &&lex_rbracket;
        dispatch_table['{']  = &&lex_lbrace;
        dispatch_table['}']  = &&lex_rbrace;
        dispatch_table[':']  = &&lex_colon;
        dispatch_table[',']  = &&lex_comma;
        dispatch_table['.']  = &&lex_period;
        dispatch_table['"']  = &&lex_string;
        dispatch_table['\''] = &&lex_char;

        table_initialized = true;
    }

    skip_whitespace();

    Token token;
    token.line = S.line;
    token.column = S.column;
    token.start = S.cursor;
    token.length = 0;

    char c = peek();

    goto *dispatch_table[(unsigned char)c];

lex_plus:     advance(); token.length = 1; token.type = TOKEN_PLUS;     return token;
lex_minus:    advance(); token.length = 1; token.type = TOKEN_MINUS;    return token;
lex_mul:      advance(); token.length = 1; token.type = TOKEN_MUL;      return token;
lex_mod:      advance(); token.length = 1; token.type = TOKEN_MOD;  return token;
lex_div:      advance(); token.length = 1; token.type = TOKEN_DIV;      return token;
lex_term:     advance(); token.length = 1; token.type = TOKEN_TERM;     return token;
lex_lparen:   advance(); token.length = 1; token.type = TOKEN_LPAREN;   return token;
lex_rparen:   advance(); token.length = 1; token.type = TOKEN_RPAREN;   return token;
lex_lbracket: advance(); token.length = 1; token.type = TOKEN_LBRACKET; return token;
lex_rbracket: advance(); token.length = 1; token.type = TOKEN_RBRACKET; return token;
lex_lbrace:   advance(); token.length = 1; token.type = TOKEN_LBRACE;   return token;
lex_rbrace:   advance(); token.length = 1; token.type = TOKEN_RBRACE;   return token;
lex_colon:    advance(); token.length = 1; token.type = TOKEN_COLON;    return token;
lex_comma:    advance(); token.length = 1; token.type = TOKEN_COMMA;    return token;

lex_equals:
    advance();
    if (peek() == '=') {
        advance();
        token.length = 2;
        token.type = TOKEN_OPERATOR;
    } else if (peek() == '>') { // Support =>
        advance();
        token.length = 2;
        token.type = TOKEN_OPERATOR;
    } else {
        token.length = 1;
        token.type = TOKEN_EQUALS;
    }
    return token;

lex_operator: {
    char op_char = advance();
    if ((op_char == '<' || op_char == '>' || op_char == '!') && peek() == '=') {
        advance();
        token.length = 2;
    } else {
        token.length = 1;
    }
    token.type = TOKEN_OPERATOR;
    return token;
}

lex_period: {
    if (S.cursor[1] == '/') { // Check for a '/'
        advance(); // Consume '.'
        advance(); // Consume '/'

        while (1) {
            char current = peek();
            if (unlikely(current == '\0')) {
                token.type = TOKEN_EOF;
                return token;
            }
            // Check for comment block terminator '\.'
            if (current == '\\' && S.cursor[1] == '.') {
                advance(); // Consume '\\'
                advance(); // Consume '.'
                break;
            }
            // Step forward char by char to preserve line coords
            advance();
        }

        // Tail call instead of returning a useless comment token
        return lexer_next_token();
    }
    advance();
    token.length = 1;
    token.type = TOKEN_PERIOD;
    return token;
}

lex_string: {
    advance(); // Consume opening '"'
    while (1) {
        char current = peek();
        if (unlikely(current == '\0')) {
            token.type = TOKEN_UNKNOWN; // Catch unclosed file term errors
            token.length = (size_t)(S.cursor - token.start);
            return token;
        }
        if (current == '"') {
            advance(); // Consume closing '"'
            break;
        }
        if (unlikely(current == '\\')) {
            advance(); // Skip backslash character escape
            advance(); // Skip nested escape sequence
        }
        if ((unsigned char)current >= 128) {
            int bytes;
            decode_utf8(S.cursor, &bytes);
            S.cursor += bytes; // Move cursor past the multi-byte character
            S.column += 1;     // Only increment the visual column by 1
        } else {
            advance(); // Process a standard uniform string
        }}
    token.length = (size_t)(S.cursor - token.start);
    token.type = TOKEN_STRING_LIT;
    return token;
}

lex_char: {
    advance(); // Consume opening '\'
    char current = peek();
    if (unlikely(current == '\0' || current == '\'')) {
        token.type = TOKEN_UNKNOWN; // Flag malformed or empty numeric literals
        token.length = (size_t)(S.cursor - token.start);
        if (current == '\'') advance();
        return token;
    }
    else if (unlikely(current == '\\')) {
        advance(); // Skip backslash character escape
        advance(); // Skip nested escape sequence
    } else if ((unsigned char)current >= 128) {
        int bytes;
        decode_utf8(S.cursor, &bytes);
        S.cursor += bytes;
        S.column += 1; // Because it looks like one character
    } else {
        advance(); // Extract std 1-byte ASCII
    }
    if (peek() == '\'') {
        advance(); // Consume closing '\'
    }
    token.length = (size_t)(S.cursor - token.start);
    token.type = TOKEN_CHAR_LIT;
    return token;
}

lex_digit: {
    advance();
    while (1) {
        char next = peek();
        if (next >= '0' && next <= '9') {
            S.cursor++;
            S.column++;
        } else {
            break;
        }
    }
    if (peek() == '.') {
        char next_next = S.cursor[1];
        if (next_next >= '0' && next_next <= '9') {
            advance(); // consume '.'
            while (1) {
                char f_next = peek();
                if (f_next >= '0' && f_next <= '9') {
                    S.cursor++;
                    S.column++;
                } else {
                    break;
                }}}
    }
    token.length = (size_t)(S.cursor - token.start);
    token.type = TOKEN_NUMBER_LIT;
    return token;
}

lex_alpha: {
    advance();
    goto consume_identifier;
}

lex_utf8: {
    int bytes;
    uint32_t cp = decode_utf8(S.cursor, &bytes);

    if (is_unicode_operator(cp)) {
        S.cursor += bytes;
        S.column += 1;
        token.length = (size_t)(S.cursor - token.start);
        token.type = TOKEN_OPERATOR;
        return token;
    }
    if (is_unicode_identifier(cp)) {
        S.cursor += bytes;
        S.column += 1;
        goto consume_identifier;
    }
    S.cursor += bytes; S.column += 1;
    token.length = (size_t)(S.cursor - token.start);
    token.type = TOKEN_UNKNOWN;
    return token;
}

consume_identifier: {
    while (1) {
        unsigned char next = (unsigned char)peek();
        // Consume standard valid alphanumeric ASCII characters
        if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') ||
            (next >= '0' && next <= '9') || next == '_') {
            S.cursor++;
            S.column++;
        }
        // Consume multibyte Unicode characters that qualify as words (like ℤ or ℝ)
        else if (next >= 128) {
            int bytes;
            uint32_t cp = decode_utf8(S.cursor, &bytes);
            if (is_unicode_identifier(cp)) {
                S.cursor += bytes;
                S.column += 1;
            } else {
                break; // Break if the Unicode character is an operator (like ∀ or ∈)
            }}
        // Stop if hit an ASCII operator, space, or a paran
        else { break; }
    }
    token.length = (size_t)(S.cursor - token.start);
    token.type = match_keyword(token.start, token.length);
    return token;
}

lex_unknown:
    advance();
    token.length = 1;
    token.type = TOKEN_UNKNOWN;
    return token;

lex_label_ref: {
    advance(); // Consume '$'
    while(1) {
        unsigned char next = (unsigned char)peek();
        if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || // Add utf-8 support later
            (next >= '0' && next <= '9') || next == '_') {
            S.cursor++;
            S.column++;
        } else { break; }
    }
    token.length = (size_t)(S.cursor - token.start);
    token.type = TOKEN_LABEL_REF;
    return token;
}

lex_eof:
    token.length = 0;
    token.type = TOKEN_EOF;
    return token;
}