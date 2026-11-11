#pragma once
#include "ast.h"
#include "string_pool.h"

// Filename is only used in error messages, and the lexre owns it
void parser_init(StringPool *pool);

// Pulls from lexer_next_token() and returns the next top-level statement
// on the first error it prints "file:line:col: error: messages" and exits
ASTNode *parse_next_statement(void);