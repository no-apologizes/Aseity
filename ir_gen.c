#include "Headers/ir_gen.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define likely(a) __builtin_expect(!!(a), 1)
#define unlikely(a) __builtin_expect(!!(a), 0)

static inline uint32_t decode_utf8(const char* src, int* out_bytes) {
    const unsigned char* c = (const unsigned char*)src;
    const unsigned char lead = c[0];

    if (likely(lead < 0x80)) {
        *out_bytes = 1;
        return lead;
    }
    if ((lead & 0xE0) == 0xC0) {
        if (unlikely(c[1] == '\0')) goto malformed; // Bounds check
        *out_bytes = 2;
        return ((lead & 0x1F) << 6) | (c[1] & 0x3F);
    }
    if ((lead & 0xF0) == 0xE0) {
        if (unlikely(c[1] == '\0' || c[2] == '\0')) goto malformed; // Bounds check
        *out_bytes = 3;
        return ((lead & 0x0F) << 12) | ((c[1] & 0x3F) << 6) | (c[2] & 0x3F);
    }
    // Added proper F8 mask for 4-byte chars
    if ((lead & 0xF8) == 0xF0) {
        if (unlikely(c[1] == '\0' || c[2] == '\0' || c[3] == '\0')) goto malformed; // Bounds check
        *out_bytes = 4;
        return ((lead & 0x07) << 18) | ((c[1] & 0x3F) << 12) | ((c[2] & 0x3F) << 6) | (c[3] & 0x3F);
    }

    malformed:
    // If we hit EOF unexpectedly, consume 1 byte and return replacement char
    *out_bytes = 1;
    return 0xFFFD;
}

IROperand ir_make_vreg(IRFunction *func) {
    return (IROperand){ .type = IR_OPERAND_VREG, .vreg_id = func->next_vreg++ };
}

IROperand ir_make_const(int64_t val) {
    return (IROperand){ .type = IR_OPERAND_CONST, .const_val = val };
}

IROperand ir_make_str(const char *start, size_t len) {
    IROperand op;
    op.type = IR_OPERAND_STR;
    size_t copy_len = len < (MAX_VAR_LENGTH - 1) ? len : (MAX_VAR_LENGTH - 1);
    strncpy(op.str_val, start, copy_len);
    op.str_val[copy_len] = '\0';
    return op;
}

IROperand ir_make_ident(const char *name) {
    IROperand op;
    op.type = IR_OPERAND_IDENT;
    strncpy(op.name, name, MAX_VAR_LENGTH - 1);
    op.name[MAX_VAR_LENGTH - 1] = '\0';
    return op;
}

IROperand ir_make_label(IRFunction *func) {
    return (IROperand){ .type = IR_OPERAND_LABEL, .label_id = func->next_label++ };
}

IROperand ir_gen_expr(IRFunction *func, ASTNode *node) {
    if (!node) return (IROperand){ .type = IR_OPERAND_NONE };

    switch (node->type) {
        case NODE_LITERAL: {
            if (node->token.type == TOKEN_STRING_LIT) {
                // Strip leading and trailing quotes from literal token
                const char *str_start = node->token.start;
                size_t str_len = node->token.length;
                if (str_len >= 2 && str_start[0] == '"' && str_start[str_len - 1] == '"') {
                    str_start++;
                    str_len -= 2;
                }
                IROperand str_op = ir_make_str(str_start, str_len);
                IROperand dest = ir_make_vreg(func);
                ir_emit(func, IR_OP_LOAD_STR, dest, str_op, (IROperand){ .type = IR_OPERAND_NONE });
                return dest;
            } else if (node->token.type == TOKEN_CHAR_LIT) {
                // Decode full multi-byte utf8 codepoint
                const char *char_start = node->token.start;
                if (node->token.length >= 2 && char_start[0] == '\'') {
                    char_start++; // Skip opening quote
                }
                int bytes = 0;
                uint32_t codepoint = decode_utf8(char_start, &bytes); // Decodes 'ℝ' to 8477 (0x211D)

                IROperand const_op = ir_make_const((int64_t)codepoint);
                IROperand dest = ir_make_vreg(func);
                ir_emit(func, IR_OP_LOAD_CONST, dest, const_op, (IROperand){ .type = IR_OPERAND_NONE });
                return dest;
            } else {
                int64_t val = atoll(node->token.start);
                IROperand const_op = ir_make_const(val);
                IROperand dest = ir_make_vreg(func);
                ir_emit(func, IR_OP_LOAD_CONST, dest, const_op, (IROperand){ .type = IR_OPERAND_NONE });
                return dest;
            }
        }

        case NODE_IDENTIFIER: {
            IROperand dest = ir_make_vreg(func);
            IROperand src = ir_make_ident(node->var_decl.var_name);
            ir_emit(func, IR_OP_LOAD_VAR, dest, src, (IROperand){ .type = IR_OPERAND_NONE });
            return dest;
        }

        case NODE_BINOP: {
            IROperand left = ir_gen_expr(func, node->binop.left);
            IROperand right = ir_gen_expr(func, node->binop.right);
            IROperand dest = ir_make_vreg(func);

            IROpcode op = IR_OP_ADD;
            if (node->binop.op_type == TOKEN_PLUS)       op = IR_OP_ADD;
            else if (node->binop.op_type == TOKEN_MINUS) op = IR_OP_SUB;
            else if (node->binop.op_type == TOKEN_MUL)   op = IR_OP_MUL;
            else if (node->binop.op_type == TOKEN_DIV)   op = IR_OP_DIV;
            else if (node->binop.op_type == TOKEN_OPERATOR && node->token.start[0] == '<') op = IR_OP_CMP_LT;

            ir_emit(func, op, dest, left, right);
            return dest;
        }

        case NODE_CALL: {
            // Lower receiver if UFCS: 2.add(2)
            if (node->call_stmt.receiver) {
                IROperand recv_op = ir_gen_expr(func, node->call_stmt.receiver);
                ir_emit(func, IR_OP_ARG, (IROperand){ .type = IR_OPERAND_NONE }, recv_op, (IROperand){ .type = IR_OPERAND_NONE });
            }

            // Lower arguments
            ASTNode *args_node = node->call_stmt.args;
            if (args_node) {
                if (args_node->type == NODE_PARAM_LIST) {
                    for (size_t i = 0; i < args_node->param_list.count; i++) {
                        IROperand arg_op = ir_gen_expr(func, args_node->param_list.params[i]);
                        ir_emit(func, IR_OP_ARG, (IROperand){ .type = IR_OPERAND_NONE }, arg_op, (IROperand){ .type = IR_OPERAND_NONE });
                    }
                } else {
                    IROperand arg_op = ir_gen_expr(func, args_node);
                    ir_emit(func, IR_OP_ARG, (IROperand){ .type = IR_OPERAND_NONE }, arg_op, (IROperand){ .type = IR_OPERAND_NONE });
                }
            }

            // Emit CALL instruction
            IROperand dest = ir_make_vreg(func);
            IROperand func_op = ir_make_ident(node->call_stmt.func_name);
            ir_emit(func, IR_OP_CALL, dest, func_op, (IROperand){ .type = IR_OPERAND_NONE });
            return dest;
        }

        default:
            return (IROperand){ .type = IR_OPERAND_NONE };
    }
}

void ir_gen_stmt(IRFunction *func, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case NODE_VAR_DECL: {
            IROperand val_op = ir_gen_expr(func, node->var_decl.value);
            IROperand var_op = ir_make_ident(node->var_decl.var_name);
            ir_emit(func, IR_OP_STORE_VAR, var_op, val_op, (IROperand){ .type = IR_OPERAND_NONE });
            break;
        }

        case NODE_RETURN: {
            IROperand ret_op = ir_gen_expr(func, node->ret_stmt.expr);
            ir_emit(func, IR_OP_RET, (IROperand){ .type = IR_OPERAND_NONE }, ret_op, (IROperand){ .type = IR_OPERAND_NONE });
            break;
        }

        case NODE_AS_LOOP: {
            IROperand cond_label = ir_make_label(func);
            IROperand end_label  = ir_make_label(func);

            ir_emit(func, IR_OP_LABEL, cond_label, (IROperand){ .type = IR_OPERAND_NONE }, (IROperand){ .type = IR_OPERAND_NONE });

            IROperand cond_val = ir_gen_expr(func, node->as_loop.condition);
            ir_emit(func, IR_OP_JMP_IF_FALSE, end_label, cond_val, (IROperand){ .type = IR_OPERAND_NONE });

            ir_gen_stmt(func, node->as_loop.body_block);

            ir_emit(func, IR_OP_JMP, cond_label, (IROperand){ .type = IR_OPERAND_NONE }, (IROperand){ .type = IR_OPERAND_NONE });
            ir_emit(func, IR_OP_LABEL, end_label, (IROperand){ .type = IR_OPERAND_NONE }, (IROperand){ .type = IR_OPERAND_NONE });
            break;
        }

        case NODE_CALL: {
            ir_gen_expr(func, node); // Execute expression for side effects
            break;
        }

        case NODE_BLOCK: {
            for (size_t i = 0; i < node->block.count; i++) {
                ir_gen_stmt(func, node->block.statements[i]);
            }
            break;
        }

        default:
            break;
    }
}

IRFunction* ir_gen_function(ASTNode *func_node) {
    if (!func_node || func_node->type != NODE_FUNC_DECL) return NULL;

    IRFunction *func = ir_function_create(func_node->func_decl.func_name);

    if (func_node->func_decl.params && func_node->func_decl.params->type == NODE_PARAM_LIST) {
        ASTNode *params = func_node->func_decl.params;
        func->param_count = params->param_list.count; // Map the count

        for (size_t i = 0; i < params->param_list.count; i++) {
            ASTNode *p = params->param_list.params[i];
            if (p->type == NODE_VAR_DECL) {
                IROperand p_vreg = ir_make_vreg(func);
                IROperand p_idx = ir_make_const((int64_t)i);

                // Load the physical llvm parameter into a 3AC virtual register
                ir_emit(func, IR_OP_LOAD_PARAM, p_vreg, p_idx, (IROperand){ .type = IR_OPERAND_NONE });

                // Store it in the local variable for mem2reg to process
                IROperand p_ident = ir_make_ident(p->var_decl.var_name);
                ir_emit(func, IR_OP_STORE_VAR, p_ident, p_vreg, (IROperand){ .type = IR_OPERAND_NONE });
            }
        }
    }

    ir_gen_stmt(func, func_node->func_decl.body);
    return func;
}