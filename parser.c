#include "Headers/parser.h"
#include "Headers/ast.h"
#include "Headers/lexer.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define likely(a) __builtin_expect(!!(a), 1)
#define unlikely(a) __builtin_expect(!!(a), 0)

// How many thing can be on the stack at once
// Example of 35 things on the stack(limit is 1024 btw):
// 1024 pointers on a 64-bit system is 8kb which perfectly fits within L1 cache
// 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +
#define MAX_STACK_SIZE 1024

// Parser state
typedef struct {
    ASTNode *stack[MAX_STACK_SIZE];
    size_t stack_top;
} ParserState;

typedef enum {
    MARKER_NONE,
    MARKER_LPAREN,
    MARKER_LBRACKET,
    MARKER_IF,
    MARKER_ELSE,
    MARKER_AS,
    MARKER_PERIOD
} BlockMarker;

typedef struct {
    ASTNode base;
    BlockMarker marker;
} MarkerNode;

// Branch pradiction push/pop helpers
static inline void parser_push(ParserState *P, ASTNode *node) {
    if (unlikely(P->stack_top >= MAX_STACK_SIZE)) {
        fprintf(stderr, "Compiler Error(parser): Parser stack overflow on line %d\n", node->token.line);
        exit(EXIT_FAILURE); // exit code 1 also works but EXIT_FAILURE is faster to understand
    }
    P->stack[P->stack_top++] = node;
}

static inline ASTNode *parser_pop(ParserState *P) {
    if (unlikely(P->stack_top == 0)) {
        fprintf(stderr, "Compiler Error(parser): Stack underflow during postfix reduction\n");
        exit(EXIT_FAILURE);
    }
    return P->stack[--P->stack_top]; // That's crazy, I've never used a prefix op like this before
}

// Mem alloc helper
static inline ASTNode *allocate_node(ASTNodeType type, Token t) {
    //ASTNode *node = calloc(1, sizeof(ASTNode));
    ASTNode *node = malloc(sizeof(ASTNode));
    if (unlikely(!node)) {
        fprintf(stderr, "Compiler Error(parser): Out of memory allocating ASTNode\n");
        exit(EXIT_FAILURE);
    }
    memset(node, 0, sizeof(ASTNode));
    node->type = type;
    node->token = t;
    return node;
}

// Sentinel marker helper
static inline ASTNode *allocate_marker(BlockMarker marker_type, Token t) {
    MarkerNode *m_node = malloc(sizeof(MarkerNode));
        if (unlikely(!m_node)) {
        fprintf(stderr, "Compiler Error(parser): Out of memory allocating MarkerNode\n");
        exit(EXIT_FAILURE);
    }
    memset(m_node, 0, sizeof(MarkerNode));
    m_node->base.type = NODE_UNKNOWN;
    m_node->base.token = t;
    m_node->marker = marker_type;
    return (ASTNode*)m_node;
}

// Shift-Reduce Parser, 01:43
// Oh boy this will be fun
ASTNode *parse_next_statement(void) {
    // Jump table mapping to TokenType int values
    static const void *dispatch[] = {
        [TOKEN_EOF] = &&parse_eof,
        [TOKEN_NUMBER_LIT] = &&parse_literal,
        [TOKEN_UNKNOWN] = &&parse_ignore,
        [TOKEN_TYPE] = &&parse_type,
        [TOKEN_OPERATOR] = &&parse_binop,

        [TOKEN_MUL] = &&parse_binop,
        [TOKEN_DIV] = &&parse_binop,
        [TOKEN_PLUS] = &&parse_binop,
        [TOKEN_MINUS] = &&parse_binop,

        [TOKEN_EQUALS] = &&parse_equals,
        [TOKEN_IDENTIFIER] = &&parse_identifier,
        [TOKEN_TERM] = &&parse_term,
        [TOKEN_DROP] = &&parse_ignore,

        [TOKEN_LPAREN] = &&parse_lparen,
        [TOKEN_RPAREN] = &&parse_rparen,

        [TOKEN_LBRACKET] = &&parse_lbracket,
        [TOKEN_RBRACKET] = &&parse_rbracket,

        [TOKEN_LBRACE] = &&parse_ignore,
        [TOKEN_RBRACE] = &&parse_ignore,

        [TOKEN_COLON] = &&parse_ignore,
        [TOKEN_COMMA] = &&parse_ignore,
        [TOKEN_PERIOD] = &&parse_period,
        [TOKEN_RETURN] = &&parse_return,

        [TOKEN_STRING_LIT] = &&parse_literal,
        [TOKEN_CHAR_LIT] = &&parse_literal,
        [NUM_STR] = &&parse_ignore,
    };

    ParserState P = { .stack_top = 0 };
    Token t = lexer_next_token();

    if (t.type == TOKEN_EOF) return NULL;

    // Entry hop
    goto *dispatch[t.type];

parse_literal: {
    ASTNode *node = allocate_node(NODE_LITERAL, t);
    parser_push(&P, node);
    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_identifier: {
    if (t.length == 2 && strncmp(t.start, "if", 2) == 0) {
        parser_push(&P, allocate_marker(MARKER_IF, t));
    } else if (t.length == 4 && strncmp(t.start, "else", 4) == 0) {
        parser_push(&P, allocate_marker(MARKER_ELSE, t));
    } else if (t.length == 2 && strncmp(t.start, "as", 2) == 0) {
        parser_push(&P, allocate_marker(MARKER_AS, t));
    } else {
        ASTNode *node = allocate_node(NODE_IDENTIFIER, t);
        size_t len = t.length < (MAX_VAR_LENGTH - 1) ? t.length : (MAX_VAR_LENGTH - 1);
        strncpy(node->var_decl.var_name, t.start, len);
        node->var_decl.var_name[len] = '\0';
        parser_push(&P, node);
    }
    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_type: {
    ASTNode *node = allocate_node(NODE_VAR_DECL, t);
    size_t len = t.length < (MAX_VAR_LENGTH - 1) ? t.length : (MAX_VAR_LENGTH - 1);
    strncpy(node->var_decl.type_name, t.start, len);
    node->var_decl.type_name[len] = '\0';
    parser_push(&P, node);
    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_lparen: {
    parser_push(&P, allocate_marker(MARKER_LPAREN, t));
    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_rparen: {
    ASTNode *args = NULL;
    ASTNode *marker = NULL;

    // Check is parens are empty: add() <-
    if (P.stack_top > 0 && P.stack[P.stack_top - 1]->type == NODE_UNKNOWN &&
        ((MarkerNode*)P.stack[P.stack_top - 1])->marker == MARKER_LPAREN) {
            // Empty args
            marker = parser_pop(&P);
    } else {
            args = parser_pop(&P);
            marker = parser_pop(&P);
    }

    if (marker->type != NODE_UNKNOWN || ((MarkerNode*)marker)->marker != MARKER_LPAREN) {
        fprintf(stderr, "Syntax Error(parser): Open parenthesis on line %d\n", t.line);
        exit(EXIT_FAILURE);
    }
    free(marker);

    // Check for UFCS method call: receiver period method ( args )
    if (P.stack_top >= 2 &&
        P.stack[P.stack_top - 1]->type == NODE_IDENTIFIER &&
        P.stack[P.stack_top - 2]->type == NODE_UNKNOWN &&
        ((MarkerNode*)P.stack[P.stack_top - 2])->marker == MARKER_PERIOD) {

        ASTNode *method_id  = parser_pop(&P);
        ASTNode *dot_marker = parser_pop(&P);
        ASTNode *receiver   = parser_pop(&P);

        free(dot_marker);

        ASTNode *call_node = allocate_node(NODE_CALL, t);
        strncpy(call_node->call_stmt.func_name, method_id->var_decl.var_name, MAX_VAR_LENGTH - 1);
        call_node->call_stmt.receiver = receiver;
        call_node->call_stmt.args     = args;

        free(method_id);
        parser_push(&P, call_node);
    }

    // Check for standard function call: function( args )
    else if (P.stack_top >= 1 &&
             P.stack[P.stack_top - 1]->type == NODE_IDENTIFIER) {
        ASTNode *func_id = parser_pop(&P);

        ASTNode *call_node = allocate_node(NODE_CALL, t);
        strncpy(call_node->call_stmt.func_name, func_id->var_decl.var_name, MAX_VAR_LENGTH - 1);
        call_node->call_stmt.receiver = NULL; // No reciver for standard calls
        call_node->call_stmt.args     = args;

        free(func_id);
        parser_push(&P, call_node);
    }
    else {
        // Standard mathmatical grouping parens
        parser_push(&P, args);
    }

    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_lbracket: {
    parser_push(&P, allocate_marker(MARKER_LBRACKET, t));
    t = lexer_next_token();
    goto *dispatch[t.type]; // Love that copy-pasting exists
}

parse_rbracket: {
    size_t capacity = 8;
    ASTNode **stmts = malloc(capacity * sizeof(ASTNode*));
    size_t count = 0;
    bool found_lbracket = false;

    // Unwind stack untill MARKER_LBRACKET
    while (P.stack_top > 0) {
        ASTNode *top = P.stack[P.stack_top - 1]; // Peek top of stack
        if (top->type == NODE_UNKNOWN && ((MarkerNode*)top)->marker == MARKER_LBRACKET) {
            P.stack_top--;
            free(top);
            found_lbracket = true;
            break;
        }
        // Exponential memory realloc of O(something)
        if (count >= capacity ) {//&& stmts[count] != NULL) {
            capacity *= 2;
            stmts = realloc(stmts, capacity * sizeof(ASTNode*));
        }
        stmts[count++] = parser_pop(&P); // Collect statement
    }

    if (!found_lbracket) {
        fprintf(stderr, "Syntax Error(parser): Unmatched ']' on line %d.\n", t.line);
        exit(EXIT_FAILURE);
    }

    // Reverse array, popping yielded [S_n ... S_1], we need [S_1 ... S_n]
    for (size_t i = 0; i < count / 2; i++) {
        ASTNode *tmp = stmts[i];
        stmts[i] = stmts[count - 1 - i];
        stmts[count - 1 - i] = tmp;
    }

    // Package into NODE_BLOCK payload
    ASTNode *block_node = allocate_node(NODE_BLOCK, t);
    block_node->block.statements = stmts;
    block_node->block.count = count;
    block_node->block.capacity = capacity;
    parser_push(&P, block_node);

    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_equals: {
    parser_push(&P, allocate_marker(MARKER_NONE, t));
    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_binop: {
    ASTNode *node = allocate_node(NODE_BINOP, t);
    node->binop.op_type = t.type;
    // Pop operans in opposite order
    // Beacuse "a b +" pushes 'a' and then 'b', so the top of the stack is 'b' (right)
    node->binop.right = parser_pop(&P);
    node->binop.left = parser_pop(&P);
    // Push reduced subtree back onto stack
    parser_push(&P, node);
    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_period: {
    parser_push(&P, allocate_marker(MARKER_PERIOD, t));
    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_return: {
    ASTNode *node = allocate_node(NODE_RETURN, t);
    node->ret_stmt.expr = parser_pop(&P);
    parser_push(&P, node);
    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_term: {
    if (P.stack_top == 0) { // Continue if just a normal thing
        t = lexer_next_token();
        goto *dispatch[t.type];
    }

    // We want something like 'i64 a = 1|'
    // fix: requires NODE_VAR_DECL at top-4 and MARKER_NONE(=) at top-2
    if (P.stack_top >= 4 &&
        P.stack[P.stack_top - 4]->type == NODE_VAR_DECL &&
        P.stack[P.stack_top - 3]->type == NODE_IDENTIFIER && // Check for if someone writes i64 10 = 20|
        P.stack[P.stack_top - 2]->type == NODE_UNKNOWN && 
        ((MarkerNode*)P.stack[P.stack_top - 2])->marker == MARKER_NONE){
        // Find it backwards
        ASTNode *val_node  = parser_pop(&P); // numeric literal, 1
        ASTNode *eq_marker = parser_pop(&P); // 'equals' marker, = // fix: now actually verified as '='
        ASTNode *id_node   = parser_pop(&P); // id.              a
        ASTNode *type_node = parser_pop(&P); // type.            i64

        free(eq_marker); // Free '='
        // Copy variable name string into primary decl node
        strncpy(type_node->var_decl.var_name, id_node->var_decl.var_name, MAX_VAR_LENGTH - 1);
        type_node->var_decl.value = val_node; // Attach value subtree: '1 3 +'

        free(id_node); // Free id
        parser_push(&P, type_node); // Push complete NODE_VAR_DECL back onto stack
    }
    // Conditional Reduction(with if AND else block): `if (condition) [then] else [else]` <- 5 right here
    // fix: requires MARKER_IF at top-5 and MARKER_ELSE at top-2
    else if (P.stack_top >= 5 && 
             P.stack[P.stack_top - 5]->type == NODE_UNKNOWN && ((MarkerNode*)P.stack[P.stack_top - 5])->marker == MARKER_IF &&
             P.stack[P.stack_top - 2]->type == NODE_UNKNOWN && ((MarkerNode*)P.stack[P.stack_top - 2])->marker == MARKER_ELSE) {

        ASTNode *else_blk   = parser_pop(&P);
        ASTNode *else_mrk   = parser_pop(&P); // fix: now actually verifed as 'else'
        ASTNode *then_blk   = parser_pop(&P);
        ASTNode *condition  = parser_pop(&P);
        ASTNode *if_marker  = parser_pop(&P);

        free(else_mrk);
        free(if_marker);

        ASTNode *if_node = allocate_node(NODE_IF, t);
        if_node->if_stmt.condition  = condition;
        if_node->if_stmt.then_block = then_blk;
        if_node->if_stmt.else_block = else_blk;
        parser_push(&P, if_node);
    }
    // Conditional Reduction(ONLY with if block)
    else if (P.stack_top >= 3 && P.stack[P.stack_top - 3]->type == NODE_UNKNOWN && // Looks back 3 items to verify it's an as loop
             ((MarkerNode*)P.stack[P.stack_top - 3])->marker == MARKER_IF) {
        ASTNode *then_blk   = parser_pop(&P);
        ASTNode *condition  = parser_pop(&P);
        ASTNode *if_marker  = parser_pop(&P);

        free(if_marker);

        ASTNode *if_node = allocate_node(NODE_IF, t);
        if_node->if_stmt.condition  = condition;
        if_node->if_stmt.then_block = then_blk;
        if_node->if_stmt.else_block = NULL; // No else block
        parser_push(&P, if_node);
    }
    // Loop Reduction: `as (condition) [body]` <- 3 things
    else if (P.stack_top >= 3 && P.stack[P.stack_top - 3]->type == NODE_UNKNOWN && // Looks back 3 items to verify it's an as loop
             ((MarkerNode*)P.stack[P.stack_top - 3])->marker == MARKER_AS) {
        ASTNode *body_blk  = parser_pop(&P);
        ASTNode *condition = parser_pop(&P);
        ASTNode *as_marker = parser_pop(&P);

        free(as_marker);

        ASTNode *as_node = allocate_node(NODE_AS_LOOP, t);
        as_node->as_loop.condition  = condition;
        as_node->as_loop.body_block = body_blk;
        parser_push(&P, as_node);
    }
    // If we hit term and none of the valid reductions ran, but there's still a control flow marker near the top of the stack, the syntax is malformed
    else {
        for (size_t i = P.stack_top; i > 0 && (P.stack_top - i) < 6; i--) {
            ASTNode *check = P.stack[i - 1];
            if (check->type == NODE_UNKNOWN) {
                BlockMarker m = ((MarkerNode*)check)->marker;

                // Stop scanning if we hit a scope boundary
                if (m == MARKER_LBRACKET) { break; }

                if (m == MARKER_IF || m == MARKER_ELSE || m == MARKER_AS) {
                    fprintf(stderr, "Compiler Error(parser): Malformed control flow statement after line %d.\n", t.line);
                    exit(EXIT_FAILURE);
                }}}
    }

    // Check if we are inside an unclosed [ ... ] block
    bool inside_block = false;
    for (size_t i = 0; i < P.stack_top; i++) {
        if (P.stack[i]->type == NODE_UNKNOWN && ((MarkerNode*)P.stack[i])->marker == MARKER_LBRACKET) {
            inside_block = true; // Still inside open bracket scope
            break;
        }
    }

    // If top-level statement complete, return root AST node to caller
    if (!inside_block && P.stack_top > 0) {
        return parser_pop(&P);
    }

    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_ignore: {
    t = lexer_next_token();
    goto *dispatch[t.type];
}

parse_eof:
    if (P.stack_top == 1) {
        return parser_pop(&P);
    }
    else if (P.stack_top > 1) {
        fprintf(stderr, "Compiler Error(parser: Unexpected EOF, missing '|' terminator?\n");
        // Clean up stranded memory to prevent leaks
        while (P.stack_top > 0) {
            free_ast(parser_pop(&P));
        }
        exit(EXIT_FAILURE);
    }
    return NULL;
}

// GCC under -03 actually replaces this with gotos anyways, and it does with parse_next_statement() too but I'm too pro for that
void free_ast(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case NODE_BINOP:
            free_ast(node->binop.left);
            free_ast(node->binop.right);
            break;

        case NODE_VAR_DECL:
            free_ast(node->var_decl.value);
            break;

        case NODE_BLOCK:
            for (size_t i = 0; i < node->block.count; i++) {
                free_ast(node->block.statements[i]);
            }
            free(node->block.statements);
            break;

        case NODE_IF:
            free_ast(node->if_stmt.condition);
            free_ast(node->if_stmt.then_block);
            free_ast(node->if_stmt.else_block);
            break;

        case NODE_AS_LOOP:
            free_ast(node->as_loop.condition);
            free_ast(node->as_loop.body_block);
            break;

        case NODE_CALL:
            if (node->call_stmt.receiver) free_ast(node->call_stmt.receiver);
            if (node->call_stmt.args) free_ast(node->call_stmt.args);
            break;

        case NODE_RETURN:
            free_ast(node->ret_stmt.expr);
            break;

        default:
            break;
    }

    free(node);
}