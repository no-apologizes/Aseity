#include "Headers/lexer.h"

#include <ctype.h>
#include <string.h>

// Basically the goal is because we're targeting a single thread, our bottleneck is slow memory allocation via malloc or otherwise
// This is a Type 3 grammar

// Internal tracking state for position
typedef struct {
    const char *cursor;
    int line; // Why int? and why not use size_t or an unsigned number where, we assume uint32 or 64 is lim of uint ⊇ lim ℕ and lim ℕ ⊆ lim uint
                // Because we'll use -1 later and using size_t or another type will allow type promotion and covert signed into unsigned and break a lot of stuff, and if someone writes 2.1 billion lines of code in my lang then they can change it
                    // And WE, yes, you and me don't use uint64 and like the max, 0xFFFFFFFFFFFFFFFF because two ints 4 bytes each fit perfectly into an 8 byte boundary and mixing signed and unsigned ints is naver a good thing
    int column;
} LexerState;

static LexerState L;

void lexer_init(const char *source) {
    L.cursor = source;
    L.line = 1;
    L.column = 1;
}

static char peek(void) {
    return *L.cursor;
}

static char advance(void) {
    char c = *L.cursor;
    if (c != '\0') {
        L.cursor++;
        if (c == '\n') {
            L.line++;
            L.column = 1;
        } else {
            L.column++;
        }
    }
    return c;
}

static void skip_whitespace(void) {
    // \t is tab, \r is carriage return and \n is newline ♥
    while (peek() == ' '|| peek() == '\t' || peek() == '\n' || peek() == '\r') { advance(); }
}

static TokenType match_identifier_or_keyword(const char *start, size_t length) {
    // Check prim-locked types
    if (length == 3 && strncmp(start, "i64", 3) == 0) return TOKEN_TYPE;
    if (length == 3 && strncmp(start, "f64", 3) == 0) return TOKEN_TYPE;
    if (length == 3 && strncmp(start, "i32", 3) == 0) return TOKEN_TYPE;
    if (length == 3 && strncmp(start, "f32", 3) == 0) return TOKEN_TYPE;
    if (length == 4 && strncmp(start, "bool", 4) == 0) return TOKEN_TYPE;
    if (length == 3 && strncmp(start, "str", 3) == 0) return TOKEN_TYPE;

    if (length == 6 && strncmp(start, "return", 6) == 0) { return TOKEN_RETURN; }
    if (length == 4 && strncmp(start, "drop", 4) == 0) return TOKEN_DROP;

    return TOKEN_IDENTIFIER;
}

Token lexer_next_token(void) {
    skip_whitespace();

    Token token;
    token.line = L.line;
    token.column = L.column;
    token.start = L.cursor;
    token.length = 0;

    char c = peek();
    if (c == '\0') {
        token.type = TOKEN_EOF;
        return token;
    }

    // Single-character punctuators and operators
    advance();
    token.length = 1;

    switch (c) {
        case '=': token.type = TOKEN_EQUALS;     return token;
        case '+': token.type = TOKEN_PLUS;       return token;
        case '-': token.type = TOKEN_MINUS;      return token;
        case '*': token.type = TOKEN_MUL;        return token;
        case '/': token.type = TOKEN_DIV;        return token;
        case '|': token.type = TOKEN_TERM;       return token;
        case '(': token.type = TOKEN_LPAREN;     return token;
        case ')': token.type = TOKEN_RPAREN;     return token;
        case '[': token.type = TOKEN_LBRACKET;   return token;
        case ']': token.type = TOKEN_RBRACKET;   return token;
        case '{': token.type = TOKEN_LBRACE;     return token;
        case '}': token.type = TOKEN_RBRACE;     return token;
        case ':': token.type = TOKEN_COLON;      return token;
        case ',': token.type = TOKEN_COMMA;      return token;
        default: ;
    }

    // Numbers, ℤ/2^nℤ for ints and ℤ[1/2] where n = m/2^k where m ∈ ℤ and k ∈ ℕ
    if (isdigit(c)) {
        while (isdigit(peek())) {
            advance();
        }
        // If we hit a trailing dot followed by a number, it's a float literal
        if (peek() == '.' && isdigit(L.cursor[1])) {
            advance(); // consume '.'
            while (isdigit(peek())) {
                advance();
            }
        }
        token.length = L.cursor - token.start;
        token.type = TOKEN_NUMBER;
        return token;
    }

    // Identifiers and Keywords
    if (isalpha(c) || c == '_') {
        while (isalnum(peek()) || peek() == '_') {
            advance();
        }
        token.length = L.cursor - token.start;
        token.type = match_identifier_or_keyword(token.start, token.length);
        return token;
    }

    // Fallback if an unexpected character enters the stream
    token.type = TOKEN_UNKNOWN;
    return token;
}