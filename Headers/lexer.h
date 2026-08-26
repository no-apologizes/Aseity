#pragma once
#include <stddef.h>

typedef enum {
    // Identifiers & Literals
    TOKEN_IDENTIFIER,               // _name naeem nam∈ // supports utf8
    TOKEN_NUMBER_LIT,               // 100 or 5
    TOKEN_STRING_LIT,               // " Example "
    TOKEN_CHAR_LIT,                 // '\n'

    // Keywords
    TOKEN_TYPE,                     // Type, e.g., i64, f32, etc.
    TOKEN_RETURN,                   // return
    TOKEN_GOTO,                     // goto
    TOKEN_LABEL_REF,                // $LABEL_NAME
    TOKEN_STRUCT,                   // struct
    TOKEN_IMPORT,                   // import or include
    TOKEN_IF,                       // if
    TOKEN_ELSE,                     // else
    TOKEN_AS,                       // as
    TOKEN_BREAK,                    // break
    TOKEN_CONTINUE,                 // continue

    // Multiple meanings
    TOKEN_ST,                       // >> Shift Right, could be arithmetic or bitwise
    TOKEN_CARET,                    // math ^ or XOR
    TOKEN_AMPERSAND,                // & for address-of and bitwise AND
    TOKEN_STAR,                     // * for mul and dereferencing


    // Arithmetic Operators
    TOKEN_OPERATOR,                 // Fallback for now, or user defined ones like ∈ or something
    TOKEN_EQUALS,                   // =
    // TOKEN_EXP,                      // ** or ^
    // TOKEN_MUL,                      // *
    TOKEN_DIV,                      // /
    TOKEN_REM,                      // %
    TOKEN_PLUS,                     // +
    TOKEN_MINUS,                    // -
    // TOKEN_AST,                      // Arithmetic Shift Right: >>
    TOKEN_PLUSE,                    // +=
    TOKEN_MINE,                     // -=
    TOKEN_MULE,                     // *=
    TOKEN_DIVE,                     // /=
    TOKEN_REME,                     // %=

    // Unary Operators
    // Not the lexer's job to decide
    // TOKEN_DEREF,                    // Dereference: *
    // TOKEN_ADDR,                     // Address-of: &
    TOKEN_INC,                      // Increment, ++
    TOKEN_DEC,                      // Decrement, --

    // Comparison Operators
    TOKEN_EQ,                       // ==
    TOKEN_NE,                       // !=
    TOKEN_LT,                       // <
    TOKEN_LE,                       // <=
    TOKEN_GT,                       // >
    TOKEN_GE,                       // >=

    // Logical Operators
    TOKEN_AND,                      // /\    '
    TOKEN_OR,                       // \/
    TOKEN_NOT,                      // !
    TOKEN_LSR,                      // Logical Shift Right: >>>

    // Bitwise Operators
    // TOKEN_BAND,                     // &
    TOKEN_BOR,                      // \|
    // TOKEN_BXOR,                     // ^
    TOKEN_BNOT,                     // ~
    TOKEN_BST,                      // <<
    // TOKEN_BSR,                      // >>
    TOKEN_BANDE,                    // &=
    TOKEN_BORE,                     // \|=
    TOKEN_BXORE,                    // ^=
    TOKEN_BSTE,                     // <<=
    TOKEN_BSRE,                     // >>=


    // Delimiters
    TOKEN_TERM,                     // |
    TOKEN_LPAREN, TOKEN_RPAREN,     // ( and )
    TOKEN_LBRACKET, TOKEN_RBRACKET, // [ and ]
    TOKEN_LBRACE, TOKEN_RBRACE,     // { and }
    TOKEN_COLON,                    // :
    TOKEN_COMMA,                    // ,
    TOKEN_DOT,                      // .

    TOKEN_UNKNOWN,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;    // Token type
    const char *start; // First char in source string
    size_t length;     // Exact length of source token string
    int line;          // Errors
    int column;        // Errors
} Token;

void lexer_init(const char *source);
Token lexer_next_token(void);

// For interface files
// void lexer_push_source(const char *source);
// bool lexer_pop_source(void);