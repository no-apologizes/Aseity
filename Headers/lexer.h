#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Numbers
typedef enum {
    TOKEN_EOF,
    TOKEN_NUMBER_LIT,                   // TODO: Add the thing here
    TOKEN_UNKNOWN,
    TOKEN_TYPE,                     // Type keyword, e.g., i64, f32, etc.
    TOKEN_OPERATOR,

    //TOKEN_EXP or POW
    TOKEN_MUL,                      // *
    TOKEN_DIV,                      // /
    TOKEN_PLUS,                     // +
    TOKEN_MINUS,                    // -

    TOKEN_EQUALS,                   // =
    TOKEN_IDENTIFIER,
    TOKEN_TERM,                     // |
    TOKEN_DROP,
    TOKEN_LPAREN, TOKEN_RPAREN,     // ( and )
    TOKEN_LBRACKET, TOKEN_RBRACKET, // [ and ]
    TOKEN_LBRACE, TOKEN_RBRACE,     // { and }
    TOKEN_COLON,                    // :
    TOKEN_COMMA,                    // ,
    TOKEN_PERIOD,                   // .
    TOKEN_RETURN,                   // return keyword

    TOKEN_STRING_LIT,               // " Example "
    TOKEN_CHAR_LIT,                 // '\n'
    NUM_STR                         // i8*, string pointer
} TokenType;

// Numeric Literals
typedef enum {
    NUMERIC_UNKNOWN,
    NUMERIC_I64,  // 64-bit integer
    NUMERIC_F64,  // 64-bit float, ~15 decimal digits

    NUMERIC_I32,  // 32-bit int
    NUMERIC_F32,  // 32-bit float, ~7 decimal digits

    NUMERIC_BOOL, // i1, boolean
} NumericKind;

typedef struct {
    NumericKind kind;
    union {
        int64_t i64;
        _Float64 f64; // No explicit _Float64 like in C23 and double will sometimes be 32 bits but for here it's fine because I know it's 64 bits on my machine
                        // AHAHAHHAH I HAVE GCC THIS IS AMAZING

        int32_t i32;
        _Float32 f32;

        bool bivalent;
    };
} NumericLiteral;

typedef struct {
    TokenType type;
        // These two avoid either hardcoding a limit or a dynamically allocated a string using malloc where malloc is slow and hardcoding limits is boring
    const char *start; // First char in source string
    size_t length;     // Exact length of source token string
    int line;          // Errors
    int column;
} Token;

void lexer_init(const char *source);
Token lexer_next_token(void);