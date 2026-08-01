#pragma once
#include "ir.h"
#include "symbol_table.h"

// Helper constructors for 3AC operands
IROperand ir_make_vreg(IRFunction *func);
IROperand ir_make_const(int64_t val);
IROperand ir_make_str(const char *start, size_t len);
IROperand ir_make_ident(const char *name);
IROperand ir_make_label(IRFunction *func);

// Lowering entry points
IROperand ir_gen_expr(SymbolTable *st, IRFunction *func, ASTNode *node);
void ir_gen_stmt(SymbolTable *st, IRFunction *func, ASTNode *node);
IRFunction* ir_gen_function(SymbolTable *st, ASTNode *func_node);