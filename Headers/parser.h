#pragma once
#include "ast.h"

// Sequentially pulls tokens from lexer_next_token() and executes shift-reduce operations (A shift-reduce parser)
// Returns a pointer to a completed ASTNode statement or an NULL when EOF is hit
ASTNode *parse_next_statement(void);

// Use you're brain
void free_ast(ASTNode *node);