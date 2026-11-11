#include "parser.h"
#include "arena.h"
#include "lexer.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// This is still a shift-reduce parser, but instead of sentinels, we use "items" where a stack entry is an `Item` record with a `kind`, and real nodes are wrapped as `IT_NODE` <- there's prob an extension to support markdown in C comments
// Bracket's meaning is also decided on the opening one instead of the closing one
// Each [ gets its own stack and its own statement list, and finished statements leave the stack,
// so the stack only holds the current statement’s pieces, and ] builds the block from the list

typedef enum {
    IT_NODE,              // A finished node: a value, a type, a block, a finished statement
    IT_ASSIGN,            // = or op= (tok says which)
    IT_INCDEC,            // ++ or --
    IT_PREFIX,            // -x *p &x !x ~x, waiting for its value
    IT_COMMA,
    IT_COLON,
    IT_DOTDOT,
    IT_AT,
    IT_DOT,               // The . of a member access, waiting for the name
    IT_MOD,               // shadow const hollow inline alias
    IT_KW,                // if else as struct return
    IT_OPEN_GROUP,        // '('
    IT_OPEN_HEADER,       // as ( : the params of a loop: one condition, or init, condition, and step
    IT_OPEN_CALL,         // name( : id is the callee, node is the receiver or null
    IT_OPEN_PARAMS,       // type name(
    IT_OPEN_BRACE_ARRAY,  // {1, 2} : an array literal
    IT_OPEN_BRACE_SUFFIX, // base{i} : node is the base, taken off the stack
    IT_OPEN_BRACE_TYPE,   // i64{4} : node is the type, which stays on the stack
    IT_OPEN_BLOCK,        // '['
} ItemKind;

typedef struct {
    ItemKind kind;
    TokenType tok;
    Token token;          // Where it was written, nodes only have line and column filled in
    ASTNode *node;
    uint32_t id;
} Item;

typedef struct Frame {
    struct Frame *parent;
    Item *items;
    size_t count, capacity;
    NodeList stmts;       // The statements finished so far in this block
    bool in_params;
} Frame;

static StringPool *pool;
static Token prev, cur, peeked;
static bool have_peeked;
static Frame *F;          // The block being parsed
static bool in_type;      // The thing on top is a type that may still be followed by * or {...}

// Errors, the first one exits
__attribute__((noreturn, format(printf, 2, 3)))
static void fail(const Token at, const char *fmt, ...) {
    fprintf(stderr, "%s:%lld:%lld: error: ", lexer_file_name(), (long long)at.line, (long long)at.column);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

#define TEXT(t) (int)(t).length, (t).start

static void advance(void) {
    prev = cur;
    if (have_peeked) { cur = peeked; have_peeked = false; }
    else { cur = lexer_next_token(); }
    if (cur.type == TOKEN_UNCLOSED_COMMENT) { fail(cur, "this comment is never closed (`./` ... `\\.`)"); }
    if (cur.type == TOKEN_UNKNOWN) { fail(cur, "unexpected `%.*s`: not valid in this language", TEXT(cur)); }
}

static Token peek_token(void) {
    if (!have_peeked) { peeked = lexer_next_token(); have_peeked = true; }
    return peeked;
}

void parser_init(StringPool *string_pool) {
    pool = string_pool;
    have_peeked = false;
    memset(&prev, 0, sizeof(prev));
    memset(&cur, 0, sizeof(cur));
}

// Nodes lists and the stack
static ASTNode *new_node(const ASTNodeType type, const Token at) {
    ASTNode *n = arena_alloc_bump_zeroed(&ast_arena, sizeof(ASTNode));
    n->type = type;
    n->token = at;
    return n;
}

static void list_push(NodeList *list, ASTNode *node) {
    if (list->count == list->capacity) {
        const size_t capacity = list->capacity ? list->capacity * 2 : 4;
        ASTNode **items = arena_alloc_bump(&ast_arena, capacity * sizeof(ASTNode *));
        if (list->count) { memcpy(items, list->items, list->count * sizeof(ASTNode *)); }
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = node;
}

static Item make_item(const ItemKind kind, const Token token) {
    Item it = {0};
    it.kind = kind;
    it.tok = token.type;
    it.token = token;
    return it;
}

static Item node_item(ASTNode *node) {
    Item it = {0};
    it.kind = IT_NODE;
    it.node = node;
    it.token = node->token;
    return it;
}

static Frame *new_frame(Frame *parent) {
    Frame *f = arena_alloc_bump_zeroed(&ast_arena, sizeof(Frame));
    f->parent = parent;
    return f;
}

static void push(const Item item) {
    if (F->count == F->capacity) {
        const size_t capacity = F->capacity ? F->capacity * 2 : 16;
        Item *items = arena_alloc_bump(&ast_arena, capacity * sizeof(Item));
        if (F->count) { memcpy(items, F->items, F->count * sizeof(Item)); }
        F->items = items;
        F->capacity = capacity;
    }
    F->items[F->count++] = item;
}

static Item pop(void) { return F->items[--F->count]; }

// from_top 0 is the top of the stack, null when there is nothing there
static Item *item_at(const size_t from_top) {
    return from_top < F->count ? &F->items[F->count - 1 - from_top] : NULL;
}

static uint32_t intern(const char *s, const size_t n) { return string_intern(pool, s, n); }

static bool is_value_node(const ASTNode *n) {
    switch (n->type) {
        case NODE_LITERAL: case NODE_IDENTIFIER: case NODE_BINOP: case NODE_UNARY: case NODE_CALL:
        case NODE_MEMBER_ACCESS: case NODE_VIEW: case NODE_SLICE: case NODE_ARRAY_LIT: case NODE_LABEL_ADDR:
            return true;
        case NODE_INDEX: // base{} is not a value, it is half of a struct type like Point{} v
            return n->sub.index != NULL;
        default:
            return false;
    }
}

static bool is_value(const Item *it) { return it->kind == IT_NODE && is_value_node(it->node); }
static bool is_type_item(const Item *it) { return it->kind == IT_NODE && it->node->type == NODE_TYPE; }
static bool is_node_of(const Item *it, const ASTNodeType type) { return it->kind == IT_NODE && it->node->type == type; }

// The innermost ( or { still open in this block
static bool find_open(size_t *index) {
    for (size_t i = F->count; i > 0; i--) {
        const ItemKind k = F->items[i - 1].kind;
        if (k == IT_OPEN_GROUP || k == IT_OPEN_HEADER || k == IT_OPEN_CALL || k == IT_OPEN_PARAMS ||
            k == IT_OPEN_BRACE_ARRAY || k == IT_OPEN_BRACE_SUFFIX || k == IT_OPEN_BRACE_TYPE) {
            *index = i - 1;
            return true;
        }
    }
    return false;
}

// Whitespace matters, "ab+" and "a b +", and so are "a -b +" and "a b -"
static bool is_quoted(const TokenType t) { return t == TOKEN_STRING_LIT || t == TOKEN_CHAR_LIT; }
static const char *token_begin(const Token t) { return is_quoted(t.type) ? t.start - 1 : t.start; }
static const char *token_end(const Token t) { return t.start + t.length + (is_quoted(t.type) ? 1 : 0); }

// Is this operator attached to the front of the next value? One next to a '|' or ')' isn't
static bool tight_after(void) {
    const Token next = peek_token();
    const TokenType t = next.type;
    const bool starts_value = t == TOKEN_IDENTIFIER || t == TOKEN_NUMBER_LIT || t == TOKEN_STRING_LIT ||
                              t == TOKEN_CHAR_LIT || t == TOKEN_LABEL_REF || t == TOKEN_LPAREN || t == TOKEN_MINUS ||
                              t == TOKEN_STAR || t == TOKEN_AMPERSAND || t == TOKEN_NOT || t == TOKEN_BNOT;
    return starts_value && token_end(cur) == token_begin(next);
}

// Is it attached to the end of the value before it?
static bool tight_before(void) {
    const TokenType t = prev.type;
    const bool ends_value = t == TOKEN_IDENTIFIER || t == TOKEN_NUMBER_LIT || t == TOKEN_STRING_LIT ||
                            t == TOKEN_CHAR_LIT || t == TOKEN_LABEL_REF || t == TOKEN_RPAREN || t == TOKEN_RBRACE;
    return prev.start != NULL && ends_value && token_end(prev) == token_begin(cur);
}

// A prefix operator is waiting under a complete value, the suffixes '.', '{', and calls bind tighter than the prefix so this is unused when they appear
static void settle_prefix(void) {
    while (F->count >= 2 && F->items[F->count - 1].kind == IT_NODE && F->items[F->count - 2].kind == IT_PREFIX) {
        const Item operand = pop();
        const Item op = pop();
        if (!is_value_node(operand.node)) { fail(op.token, "`%.*s` needs a value to work on", TEXT(op.token)); }
        ASTNode *u = new_node(NODE_UNARY, op.token);
        u->unary.op_type = op.tok;
        u->unary.operand = operand.node;
        push(node_item(u));
    }
}

static ASTNode *new_struct_type(const Token at, const uint32_t name_id) {
    ASTNode *t = new_node(NODE_TYPE, at);
    t->typeinfo.is_prim = false;
    t->typeinfo.name_id = name_id;
    return t;
}

// The values between two brackets, separated by commas: {1, 2, 3} and f(a, b)
static void collect_list(const Item *c, const size_t cn, NodeList *out, const char *what) {
    if (cn == 0) { return; }
    for (size_t k = 0; k < cn; k++) {
        if (k % 2 == 0) {
            if (c[k].kind == IT_AT) { fail(c[k].token, "`@` only works on an existing array, like `name{@}`"); }
            if (!is_value(&c[k])) { fail(c[k].token, "expected a value in %s", what); }
            list_push(out, c[k].node);
        } else if (c[k].kind != IT_COMMA) {
            fail(c[k].token, "expected `,` between the values in %s (a postfix expression is one value, like `a b +`)", what);
        }
    }
    if (cn % 2 == 0) { fail(cur, "expected a value after the last `,` in %s", what); } // Two found but no value after the third
}

static ASTNode *make_slice(ASTNode *base, ASTNode *start, ASTNode *end) {
    ASTNode *n = new_node(NODE_SLICE, base->token);
    n->slice.base = base;
    n->slice.start = start;
    n->slice.end = end;
    return n;
}

typedef struct {
    bool shadow, is_const, hollow, is_inline, alias;
    Token shadow_t, const_t, inline_t, alias_t, first;
} Mods;

static bool is_lvalue(const ASTNode *e) {
    return e->type == NODE_IDENTIFIER || e->type == NODE_MEMBER_ACCESS || e->type == NODE_INDEX ||
           (e->type == NODE_UNARY && e->unary.op_type == TOKEN_STAR);
}

// After "type name": a function (parameters and a block) or a variable (and maybe "= value")
static ASTNode *finish_typed(const Item *it, const size_t n, size_t *i, const Mods *m, ASTNode *type, const uint32_t name_id) {
    if (m->hollow) { type->typeinfo.hollow = true; }
    if (*i < n && is_node_of(&it[*i], NODE_PARAM_LIST)) {
        if (*i + 1 >= n || !is_node_of(&it[*i + 1], NODE_BLOCK)) { fail(it[*i].token, "a function needs a [ ... ] body after its parameters"); }
        ASTNode *fn = new_node(NODE_FUNC_DECL, m->first);
        fn->func_decl.is_inline = m->is_inline;
        fn->func_decl.return_type = type;
        fn->func_decl.func_name = name_id;
        fn->func_decl.params = it[*i].node->list;
        fn->func_decl.body = it[*i + 1].node;
        *i += 2;
        return fn;
    }
    ASTNode *decl = new_node(NODE_VAR_DECL, m->first);
    decl->var_decl.is_shadow = m->shadow;
    decl->var_decl.is_const = m->is_const;
    decl->var_decl.type = type;
    decl->var_decl.name_id = name_id;
    if (*i < n) {
        if (it[*i].kind != IT_ASSIGN || it[*i].tok != TOKEN_EQUALS) { fail(it[*i].token, "expected `=` or the end of the declaration here"); }
        if (*i + 1 >= n || !is_value(&it[*i + 1])) { fail(it[*i].token, "expected a value after `=`"); }
        decl->var_decl.value = it[*i + 1].node;
        *i += 2;
    }
    return decl;
}

static ASTNode *reduce_if(const Item *it, const size_t n, size_t *i) {
    const Item *kw = &it[*i];
    (*i)++;
    if (*i >= n || !is_value(&it[*i]) || !it[*i].node->grouped) { fail(kw->token, "`if` needs its condition in parentheses, like `if (a b <)`"); }
    ASTNode *node = new_node(NODE_IF, kw->token);
    node->if_stmt.condition = it[*i].node;
    (*i)++;
    if (*i >= n || !is_node_of(&it[*i], NODE_BLOCK)) { fail(kw->token, "`if` needs a [ ... ] block after its condition"); }
    node->if_stmt.then_block = it[*i].node;
    (*i)++;
    if (*i < n && it[*i].kind == IT_KW && it[*i].tok == TOKEN_ELSE) {
        const Item *el = &it[*i];
        (*i)++;
        if (*i < n && is_node_of(&it[*i], NODE_BLOCK)) {
            node->if_stmt.else_node = it[*i].node;
            (*i)++;
        } else if (*i < n && it[*i].kind == IT_KW && it[*i].tok == TOKEN_IF) {
            node->if_stmt.else_node = reduce_if(it, n, i);
        } else {
            fail(el->token, "`else` needs a [ ... ] block or another `if`");
        }
    }
    return node;
}

// Works on any list of items, so the parts of an `as` header use it too
static ASTNode *reduce_items(const Item *it, const size_t n, const bool top_level) {
    Mods m = {0};
    m.first = it[0].token;
    size_t i = 0;
    while (i < n && it[i].kind == IT_MOD) {
        switch (it[i].tok) {
            case TOKEN_SHADOW: m.shadow = true; m.shadow_t = it[i].token; break;
            case TOKEN_CONST: m.is_const = true; m.const_t = it[i].token; break;
            case TOKEN_HOLLOW: m.hollow = true; break;
            case TOKEN_INLINE: m.is_inline = true; m.inline_t = it[i].token; break;
            default: m.alias = true; m.alias_t = it[i].token; break;
        }
        i++;
    }
    if (i == n) { fail(it[0].token, "a modifier needs a declaration after it"); }

    ASTNode *result = NULL;
    const Item *cur_item = &it[i];
    if (cur_item->kind == IT_KW) {
        switch (cur_item->tok) {
            case TOKEN_IF:
                result = reduce_if(it, n, &i);
                break;
            case TOKEN_AS:
                if (i + 1 < n && is_node_of(&it[i + 1], NODE_AS)) { // as (init, condition, step) [ ... ]
                    if (i + 2 >= n || !is_node_of(&it[i + 2], NODE_BLOCK)) { fail(cur_item->token, "`as` needs a [ ... ] body after its header"); }
                    result = it[i + 1].node;
                    result->token = cur_item->token;
                    result->as_loop.body_block = it[i + 2].node;
                    i += 3;
                    break;
                }
                if (i + 1 >= n || !is_value(&it[i + 1]) || !it[i + 1].node->grouped) { fail(cur_item->token, "`as` needs its condition in parentheses, like `as (i max <)`"); }
                if (i + 2 >= n || !is_node_of(&it[i + 2], NODE_BLOCK)) { fail(cur_item->token, "`as` needs a [ ... ] body after its condition"); }
                result = new_node(NODE_AS, cur_item->token);
                result->as_loop.condition = it[i + 1].node;
                result->as_loop.body_block = it[i + 2].node;
                i += 3;
                break;
            case TOKEN_STRUCT:
                if (i + 1 >= n || !is_node_of(&it[i + 1], NODE_IDENTIFIER)) { fail(cur_item->token, "`struct` needs a name"); }
                if (i + 2 >= n || !is_node_of(&it[i + 2], NODE_BLOCK)) { fail(cur_item->token, "`struct` needs a [ ... ] body with its fields"); }
                result = new_node(NODE_STRUCT_DECL, cur_item->token);
                result->struct_decl.struct_name = it[i + 1].node->ident.name_id;
                result->struct_decl.fields = it[i + 2].node->list;
                for (size_t k = 0; k < result->struct_decl.fields.count; k++) {
                    if (result->struct_decl.fields.items[k]->type != NODE_VAR_DECL) {
                        fail(result->struct_decl.fields.items[k]->token, "a struct body can only declare fields, like `i64 x|`");
                    }
                }
                i += 3;
                break;
            default: // return value|   (value return| was already turned into a node when the token arrived)
                result = new_node(NODE_RETURN, cur_item->token);
                i++;
                if (i < n) {
                    if (!is_value(&it[i])) { fail(it[i].token, "`return` can only return a value"); }
                    result->ret_stmt.expr = it[i].node;
                    i++;
                }
                break;
        }
    } else if (cur_item->kind == IT_NODE) {
        ASTNode *node = cur_item->node;
        switch (node->type) {
            case NODE_TYPE: { // i64 x, i64 f(...) [ ... ]
                if (i + 1 >= n || !is_node_of(&it[i + 1], NODE_IDENTIFIER)) { fail(it[i].token, "expected a name after the type"); }
                const uint32_t name_id = it[i + 1].node->ident.name_id;
                i += 2;
                result = finish_typed(it, n, &i, &m, node, name_id);
                break;
            }
            case NODE_RETURN: case NODE_BREAK: case NODE_CONTINUE: case NODE_INDIRECT_JMP: case NODE_IMPORT: case NODE_BLOCK:
                result = node;
                i++;
                break;
            default: { // Starts with a value: a struct-typed declaration, an assignment, a label or a call
                const Token first_t = it[i].token;

                // Point p, Point p = x, Point{4} arr, Point *p look like values until the `|`
                if (i + 1 < n && it[i + 1].kind == IT_NODE &&
                    (node->type == NODE_IDENTIFIER || (node->type == NODE_INDEX && node->sub.base->type == NODE_IDENTIFIER))) {
                    const ASTNode *declared = it[i + 1].node; // The name being declared, with any * in front of it
                    uint32_t pointer_depth = 0;
                    while (declared->type == NODE_UNARY && declared->unary.op_type == TOKEN_STAR) { pointer_depth++; declared = declared->unary.operand; }
                    if (declared->type == NODE_IDENTIFIER) {
                        const bool has_braces = node->type == NODE_INDEX;
                        ASTNode *type_name = has_braces ? node->sub.base : node;
                        ASTNode *type = new_struct_type(type_name->token, type_name->ident.name_id);
                        type->typeinfo.pointer_depth = pointer_depth;
                        if (has_braces && pointer_depth > 0) { fail(node->token, "a pointer to a struct array isn't supported: use a view, or a primitive"); }
                        if (has_braces && node->sub.index) { type->typeinfo.suffix = TYPE_SUFFIX_ARRAY; type->typeinfo.size = node->sub.index; }
                        else if (has_braces) { type->typeinfo.suffix = TYPE_SUFFIX_VIEW; }
                        const uint32_t name_id = declared->ident.name_id;
                        i += 2;
                        if (pointer_depth > 0 && i < n && is_node_of(&it[i], NODE_PARAM_LIST)) { fail(first_t, "functions that return a pointer to a struct aren't supported yet"); }
                        result = finish_typed(it, n, &i, &m, type, name_id);
                        break;
                    }
                }
                if (i + 1 < n && it[i + 1].kind == IT_ASSIGN) { // target = value, target += value
                    if (!is_lvalue(node)) { fail(first_t, "can't assign to this: only a name, `a.b`, `a{i}` or `*p` can be assigned to"); }
                    const Item *op = &it[i + 1];
                    if (i + 2 >= n || !is_value(&it[i + 2])) { fail(op->token, "expected a value after `%.*s`", TEXT(op->token)); }
                    result = new_node(NODE_ASSIGN, first_t);
                    result->assign.op_type = op->tok;
                    result->assign.target = node;
                    result->assign.value = it[i + 2].node;
                    i += 3;
                    break;
                }
                if (i + 1 < n && it[i + 1].kind == IT_INCDEC) { // target ++
                    if (!is_lvalue(node)) { fail(first_t, "can't change this: only a name, `a.b`, `a{i}` or `*p` can be incremented"); }
                    result = new_node(NODE_INCDEC, first_t);
                    result->incdec.target = node;
                    result->incdec.is_inc = it[i + 1].tok == TOKEN_INC;
                    i += 2;
                    break;
                }
                if (i + 1 < n && it[i + 1].kind == IT_COLON && node->type == NODE_LABEL_ADDR) { // $L :
                    result = new_node(NODE_USER_LABEL, first_t);
                    result->ident.name_id = node->ident.name_id;
                    i += 2;
                    break;
                }
                if (node->type == NODE_CALL) { // A call is a statement as it is
                    result = node;
                    i += 1;
                    break;
                }
                if (is_value_node(node)) { fail(first_t, "this does nothing as a statement: only a call, an assignment or a declaration can be one"); }
                fail(first_t, "not a statement");
            }
        }
    } else {
        fail(cur_item->token, "this does not start a statement");
    }

    if (i != n) { fail(it[i].token, "unexpected token at the end of the statement: write `|` after the last one"); }

    // Which modifiers fit, and where each kind of statement may go
    if (m.alias) { fail(m.alias_t, "`alias` only applies to function parameters"); }
    if (result->type == NODE_FUNC_DECL) {
        if (m.shadow) { fail(m.shadow_t, "`shadow` only applies to variable declarations"); }
        if (m.is_const) { fail(m.const_t, "`const` only applies to variable declarations and parameters"); }
    } else if (result->type == NODE_VAR_DECL) {
        if (m.is_inline) { fail(m.inline_t, "`inline` only applies to functions"); }
    } else if (m.shadow || m.is_const || m.hollow || m.is_inline) {
        fail(m.first, "modifiers can only go on a declaration or a function");
    }
    const bool top_kind = result->type == NODE_VAR_DECL || result->type == NODE_FUNC_DECL || result->type == NODE_STRUCT_DECL || result->type == NODE_IMPORT;
    if (top_level && !top_kind) {
        fail(result->token, "this is only allowed inside a function body: at the top level there can only be functions, structs, imports and variable declarations");
    }
    if (!top_level && (result->type == NODE_FUNC_DECL || result->type == NODE_STRUCT_DECL || result->type == NODE_IMPORT)) {
        fail(result->token, "functions, structs and imports can only be declared at the top level");
    }
    return result;
}

// Called at a `|`, and for the last statement before a `]` or the end of the file, which may leave its `|` out
static ASTNode *finish_statement(void) {
    settle_prefix();
    size_t oi;
    if (find_open(&oi)) {
        const ItemKind k = F->items[oi].kind;
        fail(F->items[oi].token, "this `%s` is never closed",
             k == IT_OPEN_GROUP || k == IT_OPEN_HEADER || k == IT_OPEN_CALL || k == IT_OPEN_PARAMS ? "(" : "{");
    }
    if (F->items[F->count - 1].kind == IT_PREFIX) {
        fail(F->items[F->count - 1].token, "`%.*s` has no value to work on", TEXT(F->items[F->count - 1].token));
    }
    ASTNode *stmt = reduce_items(F->items, F->count, F->parent == NULL);
    F->count = 0;
    return stmt;
}

// Return one statement per call
ASTNode *parse_next_statement(void) {
    static const void *dispatch[TOKEN_EOF + 1];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i <= TOKEN_EOF; i++) { dispatch[i] = &&parse_unexpected; }

        dispatch[TOKEN_NUMBER_LIT] = &&parse_literal;
        dispatch[TOKEN_STRING_LIT] = &&parse_literal;
        dispatch[TOKEN_CHAR_LIT] = &&parse_literal;
        dispatch[TOKEN_LABEL_REF] = &&parse_label_ref;
        dispatch[TOKEN_IDENTIFIER] = &&parse_identifier;
        dispatch[TOKEN_TYPE] = &&parse_type;

        dispatch[TOKEN_SHADOW] = &&parse_modifier;
        dispatch[TOKEN_CONST] = &&parse_modifier;
        dispatch[TOKEN_HOLLOW] = &&parse_modifier;
        dispatch[TOKEN_INLINE] = &&parse_modifier;
        dispatch[TOKEN_ALIAS] = &&parse_modifier;

        dispatch[TOKEN_IF] = &&parse_statement_keyword;
        dispatch[TOKEN_AS] = &&parse_statement_keyword;
        dispatch[TOKEN_STRUCT] = &&parse_statement_keyword;
        dispatch[TOKEN_ELSE] = &&parse_else;
        dispatch[TOKEN_RETURN] = &&parse_return;
        dispatch[TOKEN_BREAK] = &&parse_break;
        dispatch[TOKEN_CONTINUE] = &&parse_break;
        dispatch[TOKEN_GOTO] = &&parse_goto;
        dispatch[TOKEN_IMPORT] = &&parse_import;

        dispatch[TOKEN_STAR] = &&parse_star;
        dispatch[TOKEN_PLUS] = &&parse_operator;
        dispatch[TOKEN_MINUS] = &&parse_operator;
        dispatch[TOKEN_DIV] = &&parse_operator;
        dispatch[TOKEN_REM] = &&parse_operator;
        dispatch[TOKEN_AND] = &&parse_operator;
        dispatch[TOKEN_OR] = &&parse_operator;
        dispatch[TOKEN_BOR] = &&parse_operator;
        dispatch[TOKEN_AMPERSAND] = &&parse_operator;
        dispatch[TOKEN_CARET] = &&parse_operator;
        dispatch[TOKEN_BST] = &&parse_operator;
        dispatch[TOKEN_ST] = &&parse_operator;
        dispatch[TOKEN_LSR] = &&parse_operator;
        dispatch[TOKEN_EQ] = &&parse_operator;
        dispatch[TOKEN_NE] = &&parse_operator;
        dispatch[TOKEN_LT] = &&parse_operator;
        dispatch[TOKEN_LE] = &&parse_operator;
        dispatch[TOKEN_GT] = &&parse_operator;
        dispatch[TOKEN_GE] = &&parse_operator;
        dispatch[TOKEN_NOT] = &&parse_operator;
        dispatch[TOKEN_BNOT] = &&parse_operator;

        dispatch[TOKEN_EQUALS] = &&parse_assign;
        dispatch[TOKEN_PLUSE] = &&parse_assign;
        dispatch[TOKEN_MINE] = &&parse_assign;
        dispatch[TOKEN_MULE] = &&parse_assign;
        dispatch[TOKEN_DIVE] = &&parse_assign;
        dispatch[TOKEN_REME] = &&parse_assign;
        dispatch[TOKEN_BANDE] = &&parse_assign;
        dispatch[TOKEN_BORE] = &&parse_assign;
        dispatch[TOKEN_BXORE] = &&parse_assign;
        dispatch[TOKEN_BSTE] = &&parse_assign;
        dispatch[TOKEN_BSRE] = &&parse_assign;
        dispatch[TOKEN_INC] = &&parse_incdec;
        dispatch[TOKEN_DEC] = &&parse_incdec;

        dispatch[TOKEN_DOT] = &&parse_dot;
        dispatch[TOKEN_DOTDOT] = &&parse_dotdot;
        dispatch[TOKEN_AT] = &&parse_at;
        dispatch[TOKEN_COMMA] = &&parse_comma;
        dispatch[TOKEN_COLON] = &&parse_colon;
        dispatch[TOKEN_LPAREN] = &&parse_lparen;
        dispatch[TOKEN_RPAREN] = &&parse_rparen;
        dispatch[TOKEN_LBRACE] = &&parse_lbrace;
        dispatch[TOKEN_RBRACE] = &&parse_rbrace;
        dispatch[TOKEN_LBRACKET] = &&parse_lbracket;
        dispatch[TOKEN_RBRACKET] = &&parse_rbracket;
        dispatch[TOKEN_TERM] = &&parse_term;
        dispatch[TOKEN_EOF] = &&parse_eof;
        ready = true;
    }

    // Each token ends with the same hop: step forward and jump to the next label
    // type_open is true for the token right after a TYPE token (or after a * that followed one)
    bool type_open = false;
#define NEXT() do { type_open = in_type; in_type = false; advance(); goto *dispatch[cur.type]; } while (0)

    F = new_frame(NULL);
    in_type = false;
    advance();
    goto *dispatch[cur.type];

parse_literal: {
    settle_prefix();
    ASTNode *n = new_node(NODE_LITERAL, cur);
    n->literal.text_id = intern(cur.start, cur.length);
    push(node_item(n));
    NEXT();
}

parse_label_ref: {
    settle_prefix();
    ASTNode *n = new_node(NODE_LABEL_ADDR, cur);
    n->ident.name_id = intern(cur.start + 1, cur.length - 1); // Without the $
    push(node_item(n));
    NEXT();
}

parse_identifier: {
    const Item *tp = item_at(0);
    if (tp && tp->kind == IT_DOT) { // base.name
        pop();
        const Item base = pop();
        ASTNode *member = new_node(NODE_MEMBER_ACCESS, base.node->token);
        member->member.base = base.node;
        member->member.field_name = intern(cur.start, cur.length);
        push(node_item(member));
        NEXT();
    }
    settle_prefix();
    tp = item_at(0);
    if (F->in_params && tp && (tp->kind == IT_COLON || tp->kind == IT_MOD)) { // name: Type
        push(node_item(new_struct_type(cur, intern(cur.start, cur.length))));
        in_type = true;
        NEXT();
    }
    ASTNode *n = new_node(NODE_IDENTIFIER, cur);
    n->ident.name_id = intern(cur.start, cur.length);
    push(node_item(n));
    NEXT();
}

parse_type: {
    settle_prefix();
    static const struct { const char *text; TypeKind kind; } prims[] = {
        {.text = "i8", .kind = TYPE_I8}, {.text = "u8", .kind = TYPE_U8}, {.text = "i16", .kind = TYPE_I16}, {.text = "u16", .kind = TYPE_U16}, {.text = "i32", .kind = TYPE_I32}, {.text = "u32", .kind = TYPE_U32},
        {.text = "f32", .kind = TYPE_F32}, {.text = "i64", .kind = TYPE_I64}, {.text = "u64", .kind = TYPE_U64}, {.text = "f64", .kind = TYPE_F64}, {.text = "i128", .kind = TYPE_I128},
        {.text = "u128", .kind = TYPE_U128}, {.text = "f128", .kind = TYPE_F128}, {.text = "bool", .kind = TYPE_BOOL}, {.text = "str", .kind = TYPE_STR}, {.text = "char", .kind = TYPE_CHAR}, {.text = "unit", .kind = TYPE_UNIT},
        {.text = "never", .kind = TYPE_NEVER},
    };
    TypeKind kind = TYPE_UNIT;
    bool found = false;
    for (size_t i = 0; i < sizeof(prims) / sizeof(prims[0]) && !found; i++) {
        if (strlen(prims[i].text) == cur.length && memcmp(prims[i].text, cur.start, cur.length) == 0) { kind = prims[i].kind; found = true; }
    }
    if (!found) { fail(cur, "`%.*s` is not a type", TEXT(cur)); }
    ASTNode *n = new_node(NODE_TYPE, cur);
    n->typeinfo.is_prim = true;
    n->typeinfo.prim = kind;
    push(node_item(n));
    in_type = true;
    NEXT();
}

parse_modifier: { // shadow const hollow inline alias
    bool ok = true;
    if (F->in_params) {
        const Item *tp = item_at(0);
        ok = tp && (tp->kind == IT_COLON || tp->kind == IT_MOD);
    } else {
        size_t oi;
        size_t first = 0;
        if (find_open(&oi) && F->items[oi].kind == IT_OPEN_HEADER) { first = oi + 1; } // The first part of an as header
        for (size_t i = first; i < F->count; i++) { if (F->items[i].kind != IT_MOD) { ok = false; } }
    }
    if (!ok) { fail(cur, "`%.*s` goes before the type, at the start of the declaration", TEXT(cur)); }
    push(make_item(IT_MOD, cur));
    NEXT();
}

parse_statement_keyword: { // if as struct
    const Item *tp = item_at(0);
    const bool after_else = cur.type == TOKEN_IF && tp && tp->kind == IT_KW && tp->tok == TOKEN_ELSE;
    if (F->count != 0 && !after_else) { fail(cur, "`%.*s` has to start a statement", TEXT(cur)); }
    push(make_item(IT_KW, cur));
    NEXT();
}

parse_else: {
    const Item *tp = item_at(0);
    if (!tp || !is_node_of(tp, NODE_BLOCK)) { fail(cur, "`else` has to follow the [ ... ] block of an `if`"); }
    push(make_item(IT_KW, cur));
    NEXT();
}

parse_return: {
    if (F->count == 0) { push(make_item(IT_KW, cur)); NEXT(); } // return value|
    settle_prefix();                                                     // value return|
    const Item *tp = item_at(0);
    if (!tp || !is_value(tp)) { fail(cur, "`return` needs a value before it (value return|) or after it (return value|)"); }
    ASTNode *ret = new_node(NODE_RETURN, cur);
    ret->ret_stmt.expr = pop().node;
    push(node_item(ret));
    NEXT();
}

parse_break: { // break and continue
    if (F->count != 0) { fail(cur, "`%.*s` has to be a statement by itself", TEXT(cur)); }
    push(node_item(new_node(cur.type == TOKEN_BREAK ? NODE_BREAK : NODE_CONTINUE, cur)));
    NEXT();
}

parse_goto: {
    settle_prefix();
    const Item *tp = item_at(0);
    if (!tp || !is_value(tp)) { fail(cur, "`goto` needs a label address before it, like `$LABEL goto|`"); }
    ASTNode *jump = new_node(NODE_INDIRECT_JMP, cur);
    jump->indirect_jmp.ptr_expr = pop().node;
    push(node_item(jump));
    NEXT();
}

parse_import: {
    if (F->count != 0) { fail(cur, "`import` has to start a statement"); }
    const Token path = peek_token();
    if (path.type != TOKEN_STRING_LIT) { fail(cur, "`import` needs a string with the path, like `import \"file.ase\"|`"); }
    ASTNode *n = new_node(NODE_IMPORT, cur);
    n->import_stmt.path_id = intern(path.start, path.length);
    push(node_item(n));
    advance(); // Step over the string, it belongs to the import
    NEXT();
}

parse_star: { // A pointer in a type (i64 *p), or an operator
    Item *tp = item_at(0);
    if (type_open && tp && is_type_item(tp)) { // Spacing doesn't matter inside a type
        tp->node->typeinfo.pointer_depth++;
        in_type = true;
        NEXT();
    }
    goto parse_operator;
}

parse_operator: { // + - * / % /\ \/ \| & ^ << >> >>> == != < <= > >= ! ~
    settle_prefix();
    if (cur.type == TOKEN_NOT || cur.type == TOKEN_BNOT) { // Always prefix
        if (!tight_after() || tight_before()) {
            fail(cur, "`%.*s` must be written directly in front of its value, with a space before it, like `%.*sx`", TEXT(cur), TEXT(cur));
        }
        push(make_item(IT_PREFIX, cur));
        NEXT();
    }
    if (cur.type == TOKEN_MINUS || cur.type == TOKEN_STAR || cur.type == TOKEN_AMPERSAND) {
        if (tight_after() && tight_before()) {
            fail(cur, "`%.*s` touches the value on both sides. Put a space before it to use it as an operator (a b %.*s), "
                      "or a space after it to use it on the next value (%.*sb)", TEXT(cur), TEXT(cur), TEXT(cur));
        }
        if (tight_after()) { push(make_item(IT_PREFIX, cur)); NEXT(); }
    }
    // A postfix operator: "a b +" pops b, then a, and pushes the sum
    if (F->count < 2 || !is_value(&F->items[F->count - 1]) || !is_value(&F->items[F->count - 2])) {
        fail(cur, "`%.*s` needs two values before it: write them first, then the operator (a b %.*s)", TEXT(cur), TEXT(cur));
    }
    const Item right = pop();
    const Item left = pop();
    ASTNode *binop = new_node(NODE_BINOP, left.node->token);
    binop->binop.op_type = cur.type;
    binop->binop.left = left.node;
    binop->binop.right = right.node;
    push(node_item(binop));
    NEXT();
}

parse_assign: { // = += -= *= /= %= &= \|= ^= <<= >>=
    settle_prefix();
    size_t oi;
    if (find_open(&oi) && F->items[oi].kind != IT_OPEN_HEADER) { fail(cur, "`%.*s` can't be used inside parentheses or braces", TEXT(cur)); }
    const Item *tp = item_at(0);
    if (!tp || tp->kind == IT_OPEN_HEADER || tp->kind == IT_COMMA) { fail(cur, "`%.*s` needs a target on its left", TEXT(cur)); }
    push(make_item(IT_ASSIGN, cur));
    NEXT();
}

parse_incdec: {
    settle_prefix();
    const Item *tp = item_at(0);
    if (!tp || !is_value(tp)) { fail(cur, "`%.*s` goes after the thing it changes, like `i %.*s|`", TEXT(cur), TEXT(cur)); }
    push(make_item(IT_INCDEC, cur));
    NEXT();
}

parse_dot: {
    const Item *tp = item_at(0);
    if (!tp || !is_value(tp)) { fail(cur, "`.` needs a value on its left"); }
    if (peek_token().type != TOKEN_IDENTIFIER) { fail(cur, "`.` needs a name after it"); }
    push(make_item(IT_DOT, cur));
    NEXT();
}

parse_dotdot: {
    settle_prefix();
    size_t oi;
    if (!find_open(&oi) || F->items[oi].kind != IT_OPEN_BRACE_SUFFIX) { fail(cur, "`..` is only for ranges inside `{}` after a value, like `a{1..3}`"); }
    push(make_item(IT_DOTDOT, cur));
    NEXT();
}

parse_at: {
    settle_prefix();
    size_t oi;
    const bool open = find_open(&oi);
    if (open && F->items[oi].kind == IT_OPEN_BRACE_TYPE) {
        fail(cur, "`@` takes a view of an existing array (`name{@}` in a value), it is not part of a type: `i64{}` is the view type");
    }
    if (!open || F->items[oi].kind != IT_OPEN_BRACE_SUFFIX) { fail(cur, "`@` only works inside `{}` right after an existing array, like `name{@}`"); }
    push(make_item(IT_AT, cur));
    NEXT();
}

parse_comma: {
    settle_prefix();
    size_t oi;
    if (!find_open(&oi)) { fail(cur, "unexpected `,`"); }
    push(make_item(IT_COMMA, cur));
    NEXT();
}

parse_colon: {
    settle_prefix();
    size_t oi;
    const Item *tp = item_at(0);
    if ((find_open(&oi) && F->items[oi].kind == IT_OPEN_PARAMS) || (tp && is_node_of(tp, NODE_LABEL_ADDR))) { // name: type, $LABEL :|
        push(make_item(IT_COLON, cur));
    } else {
        fail(cur, "unexpected `:` (it goes after a parameter name, or after a `$LABEL`)");
    }
    NEXT();
}

parse_lparen: { // A call f(a), a group (a b <), the parameters of a function head, or the header of an `as`
    // A function head is "type name" with nothing but modifiers before it
    size_t k = 0;
    while (k < F->count && F->items[k].kind == IT_MOD) { k++; }
    if (F->count == k + 2 && is_node_of(&F->items[k + 1], NODE_IDENTIFIER) &&
        (is_node_of(&F->items[k], NODE_TYPE) || is_node_of(&F->items[k], NODE_IDENTIFIER))) {
        push(make_item(IT_OPEN_PARAMS, cur));
        F->in_params = true;
        NEXT();
    }
    const Item *tp = item_at(0);
    if (tp && tp->kind == IT_KW && tp->tok == TOKEN_AS) {
        push(make_item(IT_OPEN_HEADER, cur));
        NEXT();
    }
    if (tp && (is_node_of(tp, NODE_IDENTIFIER) || is_node_of(tp, NODE_MEMBER_ACCESS))) { // A call binds tighter than a prefix
        const Item callee = pop();
        Item open = make_item(IT_OPEN_CALL, cur);
        open.token = callee.node->token;
        if (callee.node->type == NODE_IDENTIFIER) {
            open.id = callee.node->ident.name_id;
        } else {
            open.id = callee.node->member.field_name;
            open.node = callee.node->member.base;
        }
        push(open);
        NEXT();
    }
    settle_prefix();
    push(make_item(IT_OPEN_GROUP, cur));
    NEXT();
}

parse_rparen: { // Close whatever the ( opened: a group, a call, a function's parameters, or an `as` header
    settle_prefix();
    size_t oi;
    if (!find_open(&oi)) { fail(cur, "`)` has no matching `(`"); }
    const Item open = F->items[oi];
    if (open.kind != IT_OPEN_GROUP && open.kind != IT_OPEN_HEADER && open.kind != IT_OPEN_CALL && open.kind != IT_OPEN_PARAMS) {
        fail(open.token, "this `{` is never closed before the `)`");
    }
    const Item *c = &F->items[oi + 1];
    const size_t cn = F->count - oi - 1;
    ASTNode *result;

    if (open.kind == IT_OPEN_GROUP) {
        if (cn != 1 || !is_value(&c[0])) { fail(open.token, "parentheses hold exactly one value (a postfix expression like `a b +` is one value)"); }
        result = c[0].node;
        result->grouped = true;
    } else if (open.kind == IT_OPEN_CALL) {
        result = new_node(NODE_CALL, open.token);
        result->call.func_name = open.id;
        result->call.receiver = open.node;
        collect_list(c, cn, &result->call.args, "the arguments of a call");
    } else if (open.kind == IT_OPEN_PARAMS) { // name: [alias] [const] [hollow] type, separated by commas
        result = new_node(NODE_PARAM_LIST, open.token);
        size_t k = 0;
        while (k < cn) {
            if (!is_node_of(&c[k], NODE_IDENTIFIER)) { fail(c[k].token, "expected a parameter name"); }
            ASTNode *param = new_node(NODE_VAR_DECL, c[k].node->token);
            param->var_decl.is_param = true;
            param->var_decl.name_id = c[k].node->ident.name_id;
            k++;
            if (k >= cn || c[k].kind != IT_COLON) { fail(c[k - 1].token, "expected `:` and a type after the parameter name"); }
            k++;
            bool hollow = false;
            while (k < cn && c[k].kind == IT_MOD) {
                if (c[k].tok == TOKEN_ALIAS) { param->var_decl.is_alias = true; }
                else if (c[k].tok == TOKEN_CONST) { param->var_decl.is_const = true; }
                else if (c[k].tok == TOKEN_HOLLOW) { hollow = true; }
                else { fail(c[k].token, "`%.*s` can't be used on a parameter", TEXT(c[k].token)); }
                k++;
            }
            if (k >= cn || !is_type_item(&c[k])) { fail(c[k - 1].token, "expected a type for the parameter"); }
            param->var_decl.type = c[k].node;
            if (hollow) { param->var_decl.type->typeinfo.hollow = true; }
            k++;
            list_push(&result->list, param);
            if (k < cn) {
                if (c[k].kind != IT_COMMA) { fail(c[k].token, "expected `,` or `)` after a parameter"); }
                k++;
                if (k >= cn) { fail(open.token, "expected a parameter after the last `,`"); }
            }
        }
        F->in_params = false;
    } else { // as (condition)  or  as (init, condition, step): one part or three, never two and never empty
        if (cn == 0) { fail(open.token, "`as` needs a condition: `as (cond)`, or `as (init, cond, step)`"); }
        size_t starts[3], lengths[3], parts = 0, start = 0;
        for (size_t k = 0; k <= cn; k++) {
            if (k < cn && c[k].kind != IT_COMMA) { continue; }
            if (parts == 3) { fail(open.token, "an `as` header has one condition or three parts (init, condition, step), never more"); }
            starts[parts] = start;
            lengths[parts] = k - start;
            parts++;
            start = k + 1;
        }
        if (parts == 2) { fail(open.token, "an `as` header has one condition or three parts (init, condition, step), never two"); }
        for (size_t p = 0; p < parts; p++) {
            if (lengths[p] == 0) { fail(open.token, "an `as` header can't have an empty part"); }
        }
        if (parts == 1) { // The condition of a while loop
            if (lengths[0] != 1 || !is_value(&c[0])) { fail(open.token, "the condition of `as` is one value, like `as (i max <)`"); }
            c[0].node->grouped = true;
            result = c[0].node;
        } else { // ( init, condition, step )
                 // The init declares or assigns, the step changes something (x++, x += 2, a call)
            if (lengths[0] == 1 && is_value(&c[starts[0]])) { fail(c[starts[0]].token, "the first part of an `as` header declares or assigns, like `i64 x = 0`"); }
            ASTNode *init = reduce_items(&c[starts[0]], lengths[0], false);
            if (init->type != NODE_VAR_DECL && init->type != NODE_ASSIGN) { fail(init->token, "the first part of an `as` header declares or assigns, like `i64 x = 0`"); }
            const Item *condition = &c[starts[1]];
            if (lengths[1] != 1 || !is_value(condition)) { fail(condition->token, "the second part of an `as` header is the condition, one value like `x 10 <`"); }
            ASTNode *step = reduce_items(&c[starts[2]], lengths[2], false);
            if (step->type != NODE_ASSIGN && step->type != NODE_INCDEC && step->type != NODE_CALL) {
                fail(step->token, "the last part of an `as` header changes something: `x++`, `x += 2` or a call");
            }
            result = new_node(NODE_AS, open.token);
            result->as_loop.init = init;
            result->as_loop.condition = condition->node;
            result->as_loop.step = step;
            result->grouped = true;
        }
    }
    F->count = oi;
    push(node_item(result));
    NEXT();
}

parse_lbrace: { // An array literal {1, 2}, an index a{i}, a view a{@}, a slice a{1..3}, or the size part of a type i64{4}
    Item *tp = item_at(0);
    if (type_open && tp && is_type_item(tp)) { // i64{4} and i64{}
        Item open = make_item(IT_OPEN_BRACE_TYPE, cur);
        open.node = tp->node;
        push(open);
        NEXT();
    }
    if (tp && is_value(tp)) { // base{...} binds tighter than a prefix operator, so no settle
        Item open = make_item(IT_OPEN_BRACE_SUFFIX, cur);
        open.node = pop().node;
        push(open);
        NEXT();
    }
    settle_prefix();
    push(make_item(IT_OPEN_BRACE_ARRAY, cur));
    NEXT();
}

parse_rbrace: {
    settle_prefix();
    size_t oi;
    if (!find_open(&oi)) { fail(cur, "`}` has no matching `{`"); }
    const Item open = F->items[oi];
    if (open.kind != IT_OPEN_BRACE_ARRAY && open.kind != IT_OPEN_BRACE_SUFFIX && open.kind != IT_OPEN_BRACE_TYPE) {
        fail(open.token, "this `(` is never closed before the `}`");
    }
    const Item *c = &F->items[oi + 1];
    const size_t cn = F->count - oi - 1;
    ASTNode *result = NULL;

    if (open.kind == IT_OPEN_BRACE_ARRAY) {
        if (cn == 0) { fail(open.token, "an empty `{}` is not a value: list the elements, like {0, 1}"); }
        result = new_node(NODE_ARRAY_LIT, open.token);
        collect_list(c, cn, &result->list, "an array literal");
    } else if (open.kind == IT_OPEN_BRACE_TYPE) {
        ASTNode *type = open.node;
        if (cn == 0) {
            type->typeinfo.suffix = TYPE_SUFFIX_VIEW;
        } else if (cn == 1 && is_value(&c[0])) {
            type->typeinfo.suffix = TYPE_SUFFIX_ARRAY;
            type->typeinfo.size = c[0].node;
        } else {
            fail(open.token, "the size of an array is one constant expression, like `i64{4}` or `i64{n 2 *}`");
        }
    } else { // base{...}
        ASTNode *base = open.node;
        if (cn == 0) {
            result = new_node(NODE_INDEX, base->token);
            result->sub.base = base; // index stays NULL
        } else if (cn == 1 && c[0].kind == IT_AT) {
            result = new_node(NODE_VIEW, base->token);
            result->sub.base = base;
        } else if (cn == 1 && c[0].kind == IT_DOTDOT) {
            result = make_slice(base, NULL, NULL);
        } else if (cn == 1 && is_value(&c[0])) {
            result = new_node(NODE_INDEX, base->token);
            result->sub.base = base;
            result->sub.index = c[0].node;
        } else if (cn == 2 && is_value(&c[0]) && c[1].kind == IT_DOTDOT) {
            result = make_slice(base, c[0].node, NULL);
        } else if (cn == 2 && c[0].kind == IT_DOTDOT && is_value(&c[1])) {
            result = make_slice(base, NULL, c[1].node);
        } else if (cn == 3 && is_value(&c[0]) && is_value(&c[2]) && (c[1].kind == IT_DOTDOT || c[1].kind == IT_COMMA)) {
            result = make_slice(base, c[0].node, c[2].node);
        } else {
            fail(open.token, "inside `{}` after a value write an index `{i}`, `{@}`, or a range `{a..b}`, `{a..}`, `{..b}` or `{a, b}`");
        }
    }
    F->count = oi;
    if (result) { push(node_item(result)); }
    NEXT();
}

parse_lbracket: {
    settle_prefix();
    size_t oi;
    if (find_open(&oi)) { fail(cur, "`[` can't be inside parentheses or braces"); }
    push(make_item(IT_OPEN_BLOCK, cur));
    F = new_frame(F);
    NEXT();
}

parse_rbracket: {
    if (!F->parent) { fail(cur, "`]` has no matching `[`"); }
    if (F->count != 0) { list_push(&F->stmts, finish_statement()); } // The last statement may leave its | out
    Frame *inner = F;
    F = inner->parent;
    const Item open = pop(); // The IT_OPEN_BLOCK
    ASTNode *block = new_node(NODE_BLOCK, open.token);
    block->list = inner->stmts;
    push(node_item(block));
    NEXT();
}

parse_term: {
    settle_prefix();
    if (F->count == 0) { fail(cur, "empty statement: there is nothing before this `|`"); }
    ASTNode *stmt = finish_statement();
    if (!F->parent) { return stmt; } // A top-level statement is complete
    list_push(&F->stmts, stmt);
    NEXT();
}

parse_eof: {
    if (F->parent) {
        const Frame *outer = F->parent;
        fail(outer->items[outer->count - 1].token, "this `[` is never closed");
    }
    if (F->count == 0) { return NULL; }
    return finish_statement(); // The last statement of the file may leave its | out
}

parse_unexpected:
    fail(cur, "unexpected `%.*s` here", TEXT(cur));
#undef NEXT
}