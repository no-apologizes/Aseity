#pragma once
#include "lexer.h" // I'm aware all headers are in the same dir but I'm sure including the absolute path won't hurt

// The longest a variable's name can be, 63 max not including the \0
#define MAX_VAR_LENGTH 64

typedef enum {
    NODE_LITERAL,      // Literals, numbers, chars
    NODE_IDENTIFIER,   // Var or function names
    NODE_BINOP,        // Binary Operators
    NODE_VAR_DECL,     // Declarations (i64 a = ...)
    NODE_BLOCK,        // Code enclosed in brackets [ ... ]
    NODE_IF,           // Conditionals
    NODE_AS_LOOP,      // Loops
    NODE_RETURN,       // Return
    NODE_UNKNOWN       // Sentinels/internal markers
} ASTNodeType;

typedef struct ASTNode ASTNode;

// Payload for code between brackets
typedef struct {
    ASTNode **statements;
    size_t count;
    size_t capacity;
} BlockPayload;

struct ASTNode {
    ASTNodeType type;
    Token token; // Hold line and col for error tracking

    // Tagged union
    union {
        // Postfix ops (binary operators)
        // Redundant for the THRID time but wtv
        struct {
            ASTNode *left;
            ASTNode *right;
            TokenType op_type;
        } binop;

        // Explicit type vars: i64 a = ...
        struct {
            char type_name[MAX_VAR_LENGTH];
            char var_name[MAX_VAR_LENGTH];
            ASTNode *value;
        } var_decl;

        // Code blocks
        BlockPayload block;

        // Conditionals
        struct {
            ASTNode *condition;
            ASTNode *then_block;
            ASTNode *else_block;
        } if_stmt; // If statement

        // Loops
        struct {
            ASTNode *condition;
            ASTNode *body_block;
        } as_loop;

        // Return expressions
        struct {
            ASTNode *expr; // Kinda looks like exp(onential)
        } ret_stmt;
    };
};