#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Numbers
typedef enum {
    TOKEN_NUMBER,
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_MUL, TOKEN_DIV,  // + - * /
    TOKEN_EQUALS,                                   // =
    TOKEN_IDENTIFIER, TOKEN_TERM, TOKEN_DROP,       //
    TOKEN_TYPE,                                     // Type keyword, e.g., i64, f32, etc.
    TOKEN_LPAREN, TOKEN_RPAREN,                     // ( and )
    TOKEN_LBRACKET, TOKEN_RBRACKET,                 // [ and ]
    TOKEN_LBRACE, TOKEN_RBRACE,                     // { and }
    TOKEN_COLON,                                    // :
    TOKEN_COMMA,                                    // ,
    TOKEN_RETURN,                                   // return keyword
    TOKEN_EOF,

    TOKEN_UNKNOWN,

    NUM_STR,  // i8*, string pointer
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
        double f64;

        int32_t i32;
        float f32;

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