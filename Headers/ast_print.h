#pragma once
#include "ast.h"
#include "string_pool.h"
#include <stdbool.h>
#include <stddef.h>

// Prints a node and everything under it as an indented tree, names and literals read back out of the string
// pool. Returns how many nodes it printed. color adds ANSI colors, use it when stdout is a terminal.
size_t ast_print(const StringPool *pool, const ASTNode *root, bool color);