#include "ast_print.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "string_pool.h"

// Prints the AST as a tree:
//   func sum(a: i64, b: i64) -> i64
//   └─ block (1 statement)
//      └─ return
//         └─ binop +
//            ├─ ident a
//            └─ ident b
// A node's children are handed to kid() one at a time. kid() holds the newest one back until the next arrives, so
// it knows whether the earlier one was the last child (└─) or not (├─) without counting first.

typedef struct {
    const StringPool *pool;
    bool color;
    size_t nodes;
} Ctx;

typedef struct {
    Ctx *ctx;
    const char *prefix;     // What goes in front of every child line of this node
    const char *held_label; // The child being held back
    const ASTNode *held;
} Kids;

#define RESET "\x1b[0m"
#define DIM "\x1b[2m"
#define KIND "\x1b[1;36m"  // Node kinds: bold cyan
#define NAME "\x1b[33m"    // Names: yellow
#define LIT "\x1b[32m"     // Literals: green
#define TYPE_COLOR "\x1b[35m" // Types: magenta

static const char *c(const Ctx *x, const char *code) { return x->color ? code : ""; }

static void print_name(const Ctx *x, const uint32_t id) {
    printf("%s%.*s%s", c(x, NAME), (int)string_get_length(x->pool, id), string_get_pool(x->pool, id), c(x, RESET));
}

static const char *op_text(const TokenType t) {
    switch (t) {
        case TOKEN_PLUS: return "+";   case TOKEN_MINUS: return "-";  case TOKEN_STAR: return "*";  case TOKEN_DIV: return "/";
        case TOKEN_REM: return "%";    case TOKEN_AND: return "/\\";  case TOKEN_OR: return "\\/";  case TOKEN_BOR: return "\\|";
        case TOKEN_AMPERSAND: return "&"; case TOKEN_CARET: return "^"; case TOKEN_BST: return "<<"; case TOKEN_ST: return ">>";
        case TOKEN_LSR: return ">>>";  case TOKEN_EQ: return "==";    case TOKEN_NE: return "!=";   case TOKEN_LT: return "<";
        case TOKEN_LE: return "<=";    case TOKEN_GT: return ">";     case TOKEN_GE: return ">=";
        case TOKEN_EQUALS: return "="; case TOKEN_PLUSE: return "+="; case TOKEN_MINE: return "-="; case TOKEN_MULE: return "*=";
        case TOKEN_DIVE: return "/=";  case TOKEN_REME: return "%=";  case TOKEN_BANDE: return "&="; case TOKEN_BORE: return "\\|=";
        case TOKEN_BXORE: return "^="; case TOKEN_BSTE: return "<<="; case TOKEN_BSRE: return ">>=";
        default: return "?";
    }
}

static const char *unary_text(const TokenType t) {
    switch (t) {
        case TOKEN_MINUS: return "negate"; case TOKEN_STAR: return "deref"; case TOKEN_AMPERSAND: return "address-of";
        case TOKEN_NOT: return "not";      case TOKEN_BNOT: return "bitwise-not"; default: return "?";
    }
}

static const char *prim_text(const TypeKind k) {
    switch (k) {
        case TYPE_I8: return "i8";     case TYPE_U8: return "u8";     case TYPE_I16: return "i16";   case TYPE_U16: return "u16";
        case TYPE_I32: return "i32";   case TYPE_U32: return "u32";   case TYPE_F32: return "f32";   case TYPE_I64: return "i64";
        case TYPE_U64: return "u64";   case TYPE_F64: return "f64";   case TYPE_I128: return "i128"; case TYPE_U128: return "u128";
        case TYPE_F128: return "f128"; case TYPE_BOOL: return "bool"; case TYPE_STR: return "str";   case TYPE_CHAR: return "char";
        case TYPE_UNIT: return "unit"; case TYPE_NEVER: return "never"; default: return "?";
    }
}

// A type as it was written: i64, hollow Point*, i64{3}, str{}
static void print_type(const Ctx *x, const ASTNode *t) {
    printf("%s", c(x, TYPE_COLOR));
    if (t->typeinfo.hollow) { printf("hollow "); }
    if (t->typeinfo.is_prim) { printf("%s", prim_text(t->typeinfo.prim)); }
    else { printf("%.*s", (int)string_get_length(x->pool, t->typeinfo.name_id), string_get_pool(x->pool, t->typeinfo.name_id)); }
    for (uint32_t i = 0; i < t->typeinfo.pointer_depth; i++) { printf("*"); }
    if (t->typeinfo.suffix == TYPE_SUFFIX_VIEW) { printf("{}"); }
    if (t->typeinfo.suffix == TYPE_SUFFIX_ARRAY) {
        const ASTNode *size = t->typeinfo.size;
        if (size->type == NODE_LITERAL) {
            printf("{%.*s}", (int)string_get_length(x->pool, size->literal.text_id), string_get_pool(x->pool, size->literal.text_id));
        } else {
            printf("{...}");
        }
    }
    printf("%s", c(x, RESET));
}

static void print_node(Ctx *x, const ASTNode *n, const char *prefix, bool is_root, bool last, const char *label);

static void emit_held(Kids *k, const bool last) {
    print_node(k->ctx, k->held, k->prefix, false, last, k->held_label);
    k->held = NULL;
}

static void kid(Kids *k, const char *label, const ASTNode *n) {
    if (!n) { return; }
    if (k->held) { emit_held(k, false); }
    k->held = n;
    k->held_label = label;
}

static void kids_end(Kids *k) { if (k->held) { emit_held(k, true); } }

static void kind(const Ctx *x, const char *word) { printf("%s%s%s", c(x, KIND), word, c(x, RESET)); }

static void print_node(Ctx *x, const ASTNode *n, const char *prefix, const bool is_root, const bool last, const char *label) {
    x->nodes++;
    if (!is_root) { printf("%s%s%s%s", c(x, DIM), prefix, last ? "└─ " : "├─ ", c(x, RESET)); }
    if (label) { printf("%s%s:%s ", c(x, DIM), label, c(x, RESET)); }

    // The line for this node
    switch (n->type) {
        case NODE_LITERAL: {
            const char *q = n->token.type == TOKEN_STRING_LIT ? "\"" : n->token.type == TOKEN_CHAR_LIT ? "'" : "";
            printf("%s%s%.*s%s%s", c(x, LIT), q, (int)string_get_length(x->pool, n->literal.text_id), string_get_pool(x->pool, n->literal.text_id), q, c(x, RESET));
            break;
        }
        case NODE_IDENTIFIER: kind(x, "ident "); print_name(x, n->ident.name_id); break;
        case NODE_LABEL_ADDR: kind(x, "label-address "); printf("$"); print_name(x, n->ident.name_id); break;
        case NODE_BINOP: kind(x, "binop "); printf("%s", op_text(n->binop.op_type)); break;
        case NODE_UNARY: kind(x, "unary "); printf("%s", unary_text(n->unary.op_type)); break;
        case NODE_CALL: kind(x, "call "); print_name(x, n->call.func_name); break;
        case NODE_MEMBER_ACCESS: kind(x, "member ."); print_name(x, n->member.field_name); break;
        case NODE_INDEX: kind(x, n->sub.index ? "index" : "index{} (half of a struct type)"); break;
        case NODE_VIEW: kind(x, "view"); break;
        case NODE_SLICE: kind(x, "slice"); break;
        case NODE_ARRAY_LIT: kind(x, "array literal"); printf(" (%zu values)", n->list.count); break;
        case NODE_TYPE: kind(x, "type "); print_type(x, n); break;
        case NODE_VAR_DECL:
            kind(x, n->var_decl.is_param ? "param " : "decl ");
            if (n->var_decl.is_shadow) { printf("shadow "); }
            if (n->var_decl.is_const) { printf("const "); }
            if (n->var_decl.is_alias) { printf("alias "); }
            print_name(x, n->var_decl.name_id);
            printf(": ");
            print_type(x, n->var_decl.type);
            break;
        case NODE_ASSIGN: kind(x, "assign "); printf("%s", op_text(n->assign.op_type)); break;
        case NODE_INCDEC: kind(x, n->incdec.is_inc ? "increment" : "decrement"); break;
        case NODE_BLOCK: kind(x, "block"); printf(" (%zu statement%s)", n->list.count, n->list.count == 1 ? "" : "s"); break;
        case NODE_IF: kind(x, "if"); break;
        case NODE_AS: kind(x, n->as_loop.init ? "as (init, condition, step)" : "as (condition)"); break;
        case NODE_RETURN: kind(x, "return"); break;
        case NODE_BREAK: kind(x, "break"); break;
        case NODE_CONTINUE: kind(x, "continue"); break;
        case NODE_USER_LABEL: kind(x, "label $"); print_name(x, n->ident.name_id); break;
        case NODE_INDIRECT_JMP: kind(x, "goto"); break;
        case NODE_FUNC_DECL:
            kind(x, n->func_decl.is_inline ? "inline func " : "func ");
            print_name(x, n->func_decl.func_name);
            printf("(");
            for (size_t i = 0; i < n->func_decl.params.count; i++) {
                const ASTNode *p = n->func_decl.params.items[i];
                if (i) { printf(", "); }
                print_name(x, p->var_decl.name_id);
                printf(": ");
                if (p->var_decl.is_alias) { printf("alias "); }
                if (p->var_decl.is_const) { printf("const "); }
                print_type(x, p->var_decl.type);
            }
            printf(") -> ");
            print_type(x, n->func_decl.return_type);
            break;
        case NODE_STRUCT_DECL: kind(x, "struct "); print_name(x, n->struct_decl.struct_name); break;
        case NODE_IMPORT: kind(x, "import "); printf("%s\"%.*s\"%s", c(x, LIT), (int)string_get_length(x->pool, n->import_stmt.path_id), string_get_pool(x->pool, n->import_stmt.path_id), c(x, RESET)); break;
        case NODE_PARAM_LIST: kind(x, "params"); break;
    }
    printf("\n");

    // Its children
    char *child_prefix = malloc(strlen(prefix) + 8);
    if (!child_prefix) { return; }
    strcpy(child_prefix, prefix);
    if (!is_root) { strcat(child_prefix, last ? "   " : "│  "); }
    Kids k = {.ctx = x, .prefix = child_prefix};

    switch (n->type) {
        case NODE_BINOP: kid(&k, "left", n->binop.left); kid(&k, "right", n->binop.right); break;
        case NODE_UNARY: kid(&k, NULL, n->unary.operand); break;
        case NODE_CALL:
            kid(&k, "receiver", n->call.receiver);
            for (size_t i = 0; i < n->call.args.count; i++) { kid(&k, "arg", n->call.args.items[i]); }
            break;
        case NODE_MEMBER_ACCESS: kid(&k, "of", n->member.base); break;
        case NODE_INDEX: kid(&k, "base", n->sub.base); kid(&k, "index", n->sub.index); break;
        case NODE_VIEW: kid(&k, "of", n->sub.base); break;
        case NODE_SLICE: kid(&k, "base", n->slice.base); kid(&k, "from", n->slice.start); kid(&k, "to", n->slice.end); break;
        case NODE_ARRAY_LIT: for (size_t i = 0; i < n->list.count; i++) { kid(&k, NULL, n->list.items[i]); } break;
        case NODE_VAR_DECL:
            if (n->var_decl.type->typeinfo.suffix == TYPE_SUFFIX_ARRAY && n->var_decl.type->typeinfo.size->type != NODE_LITERAL) {
                kid(&k, "size", n->var_decl.type->typeinfo.size);
            }
            kid(&k, "value", n->var_decl.value);
            break;
        case NODE_ASSIGN: kid(&k, "target", n->assign.target); kid(&k, "value", n->assign.value); break;
        case NODE_INCDEC: kid(&k, "target", n->incdec.target); break;
        case NODE_BLOCK: for (size_t i = 0; i < n->list.count; i++) { kid(&k, NULL, n->list.items[i]); } break;
        case NODE_IF: kid(&k, "condition", n->if_stmt.condition); kid(&k, "then", n->if_stmt.then_block); kid(&k, "else", n->if_stmt.else_node); break;
        case NODE_AS:
            kid(&k, "init", n->as_loop.init);
            kid(&k, "while", n->as_loop.condition);
            kid(&k, "step", n->as_loop.step);
            kid(&k, "body", n->as_loop.body_block);
            break;
        case NODE_RETURN: kid(&k, NULL, n->ret_stmt.expr); break;
        case NODE_INDIRECT_JMP: kid(&k, NULL, n->indirect_jmp.ptr_expr); break;
        case NODE_FUNC_DECL: kid(&k, NULL, n->func_decl.body); break;
        case NODE_STRUCT_DECL: for (size_t i = 0; i < n->struct_decl.fields.count; i++) { kid(&k, NULL, n->struct_decl.fields.items[i]); } break;
        default: break;
    }
    kids_end(&k);
    free(child_prefix);
}

size_t ast_print(const StringPool *pool, const ASTNode *root, const bool color) {
    Ctx x = {.pool = pool, .color = color, .nodes = 0};
    print_node(&x, root, "", true, true, NULL);
    return x.nodes;
}