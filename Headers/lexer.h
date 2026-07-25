#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Numbers
typedef enum {
    TOKEN_EOF,
    TOKEN_NUMBER_LIT,               // TODO: Add the thing here : I don't know what I'm supposed to do
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

    NUMERIC_I8,
    NUMERIC_U8,

    NUMERIC_I16,
    NUMERIC_U16,

    NUMERIC_I32,  // 32-bit int
    NUMERIC_U32,
    NUMERIC_F32,  // 32-bit float, ~7 decimal digits

    NUMERIC_I64,  // 64-bit integer
    NUMERIC_U64,
    NUMERIC_F64,  // 64-bit float, ~15 decimal digits

    NUMERIC_I128,
    NUMERIC_U128,
    NUMERIC_F128,

    NUMERIC_BOOL, // i1, boolean
} NumericKind;

typedef struct {
    NumericKind kind;
    union {

        int8_t   i8;
        uint8_t  u8;

        int16_t  i16;
        uint16_t u16;

        int32_t  i32;
        uint32_t u32;
        _Float32 f32;

        int64_t  i64;
        uint64_t u64;
        _Float64 f64; // No explicit _Float64 like in C23 and double will sometimes be 32 bits but for here it's fine because I know it's 64 bits on my machine
                        // AHAHAHHAH I HAVE GCC THIS IS AMAZING
                        // This will use the C17 standard and atleast gcc (GCC) 16.1.1 20260625, not clang 

        __int128          i128;
        unsigned __int128 u128;
        __float128        f128;

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