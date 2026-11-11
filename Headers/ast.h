#pragma once
#include "lexer.h"
#include "type.h"

// Names and literal text are string pool handles, so there's no MAX_VAR_LENGTH
// and everything lives in ast_arena

typedef enum {
    // Values
    NODE_LITERAL,       // token.type is it's type, literal.text_id is its text without quotes
    NODE_IDENTIFIER,
    NODE_BINOP,         // Binary operator (you know what it is)
    NODE_UNARY,         // Unary operator (you def know what it is)
    NODE_CALL,          // func(args) or receiver.func(args), a call on its own is a statement
    NODE_MEMBER_ACCESS, // member.access
    NODE_INDEX,         // base{i} and base{} <- empty which is only valid as a struct type
    NODE_VIEW,          //
    NODE_SLICE,         //
    NODE_ARRAY_LIT,     // {1, 2, 3}
    NODE_LABEL_ADDR,    // $NAME used as a value

    // Types
    NODE_TYPE,          // Its type.

    // Statements
    NODE_VAR_DECL,      // [shadow] [const] type name = value| also a parameter or struct field
    NODE_ASSIGN,        // target = value or target += value
    NODE_INCDEC,        // target ++ or target--
    NODE_BLOCK,         // [ .. ]
    NODE_IF,            // else_node is a NODE_BLOCK, another NODE_IF(else if) or null
    NODE_AS,            // as aka while loops will have either three or one param, like as (i64 x = 0, x 10 <, x++) [] or as(1), never two and separated by commas
    NODE_RETURN,        // expr is null for a bare return
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_USER_LABEL,   // $NAME : |
    NODE_INDIRECT_JMP, // value goto|

    // Top level
    NODE_FUNC_DECL,
    NODE_PARAM_LIST,  // Only while parsing, a function keeps its params as a list
    NODE_STRUCT_DECL,
    NODE_IMPORT,
} ASTNodeType;

typedef struct ASTNode ASTNode;

typedef struct {
    ASTNode **items;
    size_t count;
    size_t capacity;
} NodeList;

typedef enum { TYPE_SUFFIX_NONE, TYPE_SUFFIX_VIEW, TYPE_SUFFIX_ARRAY } TypeSuffix;

struct ASTNode {
    ASTNodeType type;
    bool grouped; // Was written inside parens, like if and as
    Token token;

    union {
        struct { uint32_t text_id; } literal;
        struct { uint32_t name_id; } ident; // NODE_IDENTIFIER, NODE_LABEL_ADDR, AND NODE_USER_LABEL, it's the name without the $
        struct { TokenType op_type; ASTNode *left, *right; } binop;
        struct { TokenType op_type; ASTNode *operand; } unary;
        struct { ASTNode *receiver; uint32_t func_name; NodeList args; } call; // receiver is null for f(args)
        struct { ASTNode *base; uint32_t field_name; } member;                 // Member of struct
        struct { ASTNode *base, *index; } sub;
        struct { ASTNode *base, *start, *end; } slice;
        NodeList list;
        struct {
            bool hollow;
            bool is_prim;
            TypeKind prim;
            uint32_t name_id;
            uint32_t pointer_depth;
            TypeSuffix suffix;
            ASTNode *size;
        } typeinfo;
        struct { bool is_shadow, is_const, is_param, is_alias; ASTNode *type; uint32_t name_id; ASTNode *value; } var_decl;
        struct { TokenType op_type; ASTNode *target, *value; } assign;
        struct { ASTNode *target; bool is_inc; } incdec;
        struct { ASTNode *expr; } ret_stmt;
        struct { ASTNode *ptr_expr; } indirect_jmp;
        struct { ASTNode *condition, *then_block, *else_node; } if_stmt;
        struct { ASTNode *init, *condition, *step, *body_block; } as_loop; // init and step are null for as (condition
        struct { bool is_inline; ASTNode *return_type; uint32_t func_name; NodeList params; ASTNode *body; } func_decl;
        struct { uint32_t struct_name; NodeList fields; } struct_decl;
        struct { uint32_t path_id; } import_stmt;
    };
};