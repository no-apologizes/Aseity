#include "Headers/ir_gen.h"
#include "Headers/ir.h"
#include "Headers/lexer.h"
#include "symbol_table.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"

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

static const char* resolve_print_target(SymbolTable *st, ASTNode *arg_node) {
    if (!arg_node) return "aseity_print_i64";

    // Literal node type inspection
    if (arg_node->type == NODE_LITERAL) {
        if (arg_node->token.type == TOKEN_STRING_LIT) return "aseity_print_str";
        if (arg_node->token.type == TOKEN_CHAR_LIT)   return "aseity_print_utf8";
        return "aseity_print_i64";
    }

    // Identifier node symbol table lookup
    if (arg_node->type == NODE_IDENTIFIER) {
        Symbol *sym = symbol_table_lookup(st, arg_node->var_decl.var_name);
        if (sym) {
            if (strcmp(sym->type_name, "i128") == 0) return "aseity_print_i128";
            if (strcmp(sym->type_name, "u128") == 0) return "aseity_print_u128";
            if (strcmp(sym->type_name, "bool") == 0) return "aseity_print_bool";
            if (strcmp(sym->type_name, "str") == 0)  return "aseity_print_str";
            if (strcmp(sym->type_name, "char") == 0) return "aseity_print_utf8";
            if (strcmp(sym->type_name, "f64") == 0)  return "aseity_print_f64";
            if (strcmp(sym->type_name, "u8") == 0  || strcmp(sym->type_name, "i8") == 0  ||
                strcmp(sym->type_name, "u16") == 0 || strcmp(sym->type_name, "i16") == 0 ||
                strcmp(sym->type_name, "u32") == 0 || strcmp(sym->type_name, "i32") == 0 ||
                strcmp(sym->type_name, "u64") == 0 || strcmp(sym->type_name, "i64") == 0) {
                return "aseity_print_i64";
                }
        }
    }

    // Default fallback integer print
    return "aseity_print_i64";
}

IROperand ir_gen_expr(SymbolTable *st, IRFunction *func, ASTNode *node) {
    if (!node) return (IROperand){ .type = IR_OPERAND_NONE };

    switch (node->type) {
        case NODE_LITERAL: {
            if (node->token.type == TOKEN_STRING_LIT) {
                const char *start = node->token.start;
                size_t len = node->token.length;

                // Slice outer quotes if present
                if (len >= 2 && start[0] == '"' && start[len - 1] == '"') {
                    start++;
                    len -= 2;
                }

                IROperand str_op = ir_make_str(start, len);
                IROperand dest = ir_make_vreg(func);
                ir_emit(func, IR_OP_LOAD_STR, dest, str_op, (IROperand){ .type = IR_OPERAND_NONE });
                return dest;
            }
            else {
                const char *num_str = node->token.start;
                size_t num_len = node->token.length;

                char num_buf[128];
                size_t copy_len = num_len < 127 ? num_len : 127;

                // Use memcpy to safely bound token slice
                memcpy(num_buf, num_str, copy_len);
                num_buf[copy_len] = '\0';

                char *endptr;
                errno = 0;
                long long val = strtoll(num_buf, &endptr, 10);

                if (errno == ERANGE || num_len > 19) {
                    IROperand str_op = ir_make_str(num_str, num_len);
                    IROperand dest = ir_make_vreg(func);
                    ir_emit(func, IR_OP_LOAD_CONST128, dest, str_op, (IROperand){ .type = IR_OPERAND_NONE });
                    return dest;
                } else {
                    IROperand const_op = ir_make_const((int64_t)val);
                    IROperand dest = ir_make_vreg(func);
                    ir_emit(func, IR_OP_LOAD_CONST, dest, const_op, (IROperand){ .type = IR_OPERAND_NONE });
                    return dest;
                }
            }
            break; // Prevents implicit fallthrough into NODE_IDENTIFIER
        }

        case NODE_IDENTIFIER: {
            IROperand dest = ir_make_vreg(func);
            IROperand src = ir_make_ident(node->var_decl.var_name);
            ir_emit(func, IR_OP_LOAD_VAR, dest, src, (IROperand){ .type = IR_OPERAND_NONE });
            return dest;
        }

        case NODE_BINOP: {
            IROperand left = ir_gen_expr(st, func, node->binop.left);
            IROperand right = ir_gen_expr(st, func, node->binop.right);
            IROperand dest = ir_make_vreg(func);

            IROpcode op = IR_OP_ADD;
            if (node->binop.op_type == TOKEN_PLUS)       op = IR_OP_ADD;
            else if (node->binop.op_type == TOKEN_MINUS) op = IR_OP_SUB;
            else if (node->binop.op_type == TOKEN_MUL)   op = IR_OP_MUL;
            else if (node->binop.op_type == TOKEN_MOD)   op = IR_OP_MOD;
            else if (node->binop.op_type == TOKEN_DIV)   op = IR_OP_DIV;
            else if (node->binop.op_type == TOKEN_OPERATOR) {
                if (strncmp(node->token.start, "==", 2) == 0)      op = IR_OP_CMP_EQ;
                else if (strncmp(node->token.start, "!=", 2) == 0) op = IR_OP_CMP_NE;
                else if (strncmp(node->token.start, "<=", 2) == 0) op = IR_OP_CMP_LE;
                else if (strncmp(node->token.start, ">=", 2) == 0 || strncmp(node->token.start, "=>", 2) == 0) op = IR_OP_CMP_GE;
                else if (node->token.start[0] == '<')               op = IR_OP_CMP_LT;
                else if (node->token.start[0] == '>')               op = IR_OP_CMP_GT;
            }

            ir_emit(func, op, dest, left, right);
            return dest;
        }

        case NODE_CALL: {
            const char *target_func_name = node->call_stmt.func_name;

            // Safe argument extraction for intrinsics
            ASTNode *arg0 = NULL;
            ASTNode *arg1 = NULL;
            if (node->call_stmt.args) {
                if (node->call_stmt.args->type == NODE_PARAM_LIST) {
                    if (node->call_stmt.args->param_list.count > 0) arg0 = node->call_stmt.args->param_list.params[0];
                    if (node->call_stmt.args->param_list.count > 1) arg1 = node->call_stmt.args->param_list.params[1];
                } else {
                    arg0 = node->call_stmt.args;
                }
            }

            // Monomorphize generic 'print'
            if (strcmp(target_func_name, "print") == 0) {
                target_func_name = resolve_print_target(st, arg0);
            }

            // Intercept intrinsics using unwrapped arguments
            if (strcmp(target_func_name, "malloc") == 0) {
                IROperand size_op = ir_gen_expr(st, func, arg0);
                ir_emit(func, IR_OP_ARG, (IROperand){ .type = IR_OPERAND_NONE }, size_op, (IROperand){ .type = IR_OPERAND_NONE });

                IROperand dest = ir_make_vreg(func);
                IROperand func_op = ir_make_ident("aseity_mem_alloc");
                ir_emit(func, IR_OP_CALL, dest, func_op, (IROperand){ .type = IR_OPERAND_NONE });
                return dest;
            }

            if (strcmp(target_func_name, "ptr_read_u8") == 0) {
                IROperand addr = ir_gen_expr(st, func, arg0);
                IROperand dest = ir_make_vreg(func);
                ir_emit(func, IR_OP_PTR_READ8, dest, addr, (IROperand){.type = IR_OPERAND_NONE});
                return dest;
            }
            if (strcmp(target_func_name, "ptr_write_u8") == 0) {
                IROperand addr = ir_gen_expr(st, func, arg0);
                IROperand val  = ir_gen_expr(st, func, arg1);
                ir_emit(func, IR_OP_PTR_WRITE8, (IROperand){.type = IR_OPERAND_NONE}, addr, val);
                return (IROperand){.type = IR_OPERAND_NONE};
            }
            if (strcmp(target_func_name, "ptr_read64") == 0) {
                IROperand addr = ir_gen_expr(st, func, arg0);
                IROperand dest = ir_make_vreg(func);
                ir_emit(func, IR_OP_PTR_READ64, dest, addr, (IROperand){.type = IR_OPERAND_NONE});
                return dest;
            }
            if (strcmp(target_func_name, "ptr_write64") == 0) {
                IROperand addr = ir_gen_expr(st, func, arg0);
                IROperand val  = ir_gen_expr(st, func, arg1);
                ir_emit(func, IR_OP_PTR_WRITE64, (IROperand){.type = IR_OPERAND_NONE}, addr, val);
                return (IROperand){.type = IR_OPERAND_NONE};
            }

            // Lower Standard Arguments
            ASTNode *args_node = node->call_stmt.args;
            if (args_node) {
                if (args_node->type == NODE_PARAM_LIST) {
                    for (size_t i = 0; i < args_node->param_list.count; i++) {
                        IROperand arg_op = ir_gen_expr(st, func, args_node->param_list.params[i]);
                        ir_emit(func, IR_OP_ARG, (IROperand){ .type = IR_OPERAND_NONE }, arg_op, (IROperand){ .type = IR_OPERAND_NONE });
                    }
                } else {
                    IROperand arg_op = ir_gen_expr(st, func, args_node);
                    ir_emit(func, IR_OP_ARG, (IROperand){ .type = IR_OPERAND_NONE }, arg_op, (IROperand){ .type = IR_OPERAND_NONE });
                }
            }

            // Emit standard CALL instruction
            IROperand dest = ir_make_vreg(func);
            IROperand func_op = ir_make_ident(target_func_name);
            ir_emit(func, IR_OP_CALL, dest, func_op, (IROperand){ .type = IR_OPERAND_NONE });
            return dest;
        }

        case NODE_LABEL_ADDR: {
            IROperand dest = ir_make_vreg(func);
            IROperand lbl_op = ir_make_ident(node->user_label.label_name);
            ir_emit(func, IR_OP_LOAD_LABEL_ADDR, dest, lbl_op, (IROperand){ .type = IR_OPERAND_NONE });
            return dest;
        }

        case NODE_MEMBER_ACCESS: {
            // Get the struct type of the instance variable
            Symbol *sym = symbol_table_lookup(st, node->member_access.instance_name);
            if (!sym) {
                fprintf(stderr, "Semantic Error: Undeclared instance '%s'\n", node->member_access.instance_name);
                exit(EXIT_FAILURE);
            }

            // Find the struct definition
            StructDef *sdef = st->struct_registry;
            while (sdef && strcmp(sdef->name, sym->type_name) != 0) sdef = sdef->next;

            // Find the field offset
            size_t offset = 0;
            FieldDef *fdef = sdef ? sdef->fields : NULL;
            while (fdef) {
                if (strcmp(fdef->name, node->member_access.field_name) == 0) {
                    offset = fdef->offset;
                    break;
                }
                fdef = fdef->next;
            }

            // Lower instance pointer to vreg
            IROperand instance_ptr = ir_make_vreg(func);
            IROperand ident_op = ir_make_ident(node->member_access.instance_name);
            ir_emit(func, IR_OP_LOAD_VAR, instance_ptr, ident_op, (IROperand){.type = IR_OPERAND_NONE});

            // Load the structural byte offset constant into a vreg
            IROperand offset_const = ir_make_const(offset);
            IROperand offset_vreg = ir_make_vreg(func);
            ir_emit(func, IR_OP_LOAD_CONST, offset_vreg, offset_const, (IROperand){.type = IR_OPERAND_NONE});

            // Lower to pointer arithmetic: dest = instance_ptr + offset_vreg
            IROperand dest = ir_make_vreg(func);
            ir_emit(func, IR_OP_ADD, dest, instance_ptr, offset_vreg);

            // Return the calculated memory address, letting the user invoke ptr_read64/ptr_write64
            return dest;
        }

        default:
            return (IROperand){ .type = IR_OPERAND_NONE };
    }
}

void ir_gen_stmt(SymbolTable *st, IRFunction *func, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case NODE_IF: {
            IROperand cond_val = ir_gen_expr(st, func, node->if_stmt.condition);
            IROperand else_label = ir_make_label(func);
            IROperand end_label  = ir_make_label(func);

            if (node->if_stmt.else_block) {
                ir_emit(func, IR_OP_JMP_IF_FALSE, else_label, cond_val, (IROperand){ .type = IR_OPERAND_NONE });
                ir_gen_stmt(st, func, node->if_stmt.then_block);
                ir_emit(func, IR_OP_JMP, end_label, (IROperand){ .type = IR_OPERAND_NONE }, (IROperand){ .type = IR_OPERAND_NONE });
                ir_emit(func, IR_OP_LABEL, else_label, (IROperand){ .type = IR_OPERAND_NONE }, (IROperand){ .type = IR_OPERAND_NONE });
                ir_gen_stmt(st, func, node->if_stmt.else_block);
                ir_emit(func, IR_OP_LABEL, end_label, (IROperand){ .type = IR_OPERAND_NONE }, (IROperand){ .type = IR_OPERAND_NONE });
            } else {
                ir_emit(func, IR_OP_JMP_IF_FALSE, end_label, cond_val, (IROperand){ .type = IR_OPERAND_NONE });
                ir_gen_stmt(st, func, node->if_stmt.then_block);
                ir_emit(func, IR_OP_LABEL, end_label, (IROperand){ .type = IR_OPERAND_NONE }, (IROperand){ .type = IR_OPERAND_NONE });
            }
            break;
        }
        case NODE_VAR_DECL: {
            // Register variable in current symbol table scope for type resolution
            symbol_table_insert(st, node->var_decl.var_name, node->var_decl.type_name, node->token.line);

            // If it's an uninitialized struct, allocate it on the stack
            if (!node->var_decl.value) {
                StructDef *sdef = st->struct_registry;
                while (sdef) {
                    if (strcmp(sdef->name, node->var_decl.type_name) == 0) {
                        IROperand size_op = ir_make_const(sdef->total_size);
                        IROperand var_op = ir_make_ident(node->var_decl.var_name);
                        IROperand dest_vreg = ir_make_vreg(func);

                        // v_reg = alloca(size)
                        ir_emit(func, IR_OP_ALLOCA, dest_vreg, size_op, (IROperand){.type = IR_OPERAND_NONE});
                        // var = v_reg
                        ir_emit(func, IR_OP_STORE_VAR, var_op, dest_vreg, (IROperand){.type = IR_OPERAND_NONE});
                        return; // Fix: Prevents fall-through into secondary STORE_VAR
                    }
                    sdef = sdef->next;
                }
            }

            // Directly intercept i128 / u128 numeric literal initializers
            else if ((strcmp(node->var_decl.type_name, "i128") == 0 || strcmp(node->var_decl.type_name, "u128") == 0) &&
                node->var_decl.value && node->var_decl.value->type == NODE_LITERAL &&
                node->var_decl.value->token.type == TOKEN_NUMBER_LIT) {

                IROperand str_op = ir_make_str(node->var_decl.value->token.start, node->var_decl.value->token.length);
                IROperand val_op = ir_make_vreg(func);
                ir_emit(func, IR_OP_LOAD_CONST128, val_op, str_op, (IROperand){ .type = IR_OPERAND_NONE });

                IROperand var_op = ir_make_ident(node->var_decl.var_name);
                ir_emit(func, IR_OP_STORE_VAR, var_op, val_op, (IROperand){ .type = IR_OPERAND_NONE });
                break;
                }

            IROperand val_op = ir_gen_expr(st, func, node->var_decl.value);
            IROperand var_op = ir_make_ident(node->var_decl.var_name);
            ir_emit(func, IR_OP_STORE_VAR, var_op, val_op, (IROperand){ .type = IR_OPERAND_NONE });
            break;
        }

        case NODE_RETURN: {
            IROperand ret_op = ir_gen_expr(st, func, node->ret_stmt.expr);
            ir_emit(func, IR_OP_RET, (IROperand){ .type = IR_OPERAND_NONE }, ret_op, (IROperand){ .type = IR_OPERAND_NONE });
            break;
        }

        case NODE_AS_LOOP: {
            IROperand cond_label = ir_make_label(func);
            IROperand end_label  = ir_make_label(func);

            ir_emit(func, IR_OP_LABEL, cond_label, (IROperand){ .type = IR_OPERAND_NONE }, (IROperand){ .type = IR_OPERAND_NONE });

            IROperand cond_val = ir_gen_expr(st, func, node->as_loop.condition);
            ir_emit(func, IR_OP_JMP_IF_FALSE, end_label, cond_val, (IROperand){ .type = IR_OPERAND_NONE });

            ir_gen_stmt(st, func, node->as_loop.body_block);

            ir_emit(func, IR_OP_JMP, cond_label, (IROperand){ .type = IR_OPERAND_NONE }, (IROperand){ .type = IR_OPERAND_NONE });
            ir_emit(func, IR_OP_LABEL, end_label, (IROperand){ .type = IR_OPERAND_NONE }, (IROperand){ .type = IR_OPERAND_NONE });
            break;
        }

        case NODE_CALL: {
            ir_gen_expr(st, func, node);
            break;
        }

        case NODE_USER_LABEL: {
            IROperand lbl_op = ir_make_ident(node->user_label.label_name);
            ir_emit(func, IR_OP_USER_LABEL, lbl_op, (IROperand){.type = IR_OPERAND_NONE}, (IROperand){.type = IR_OPERAND_NONE});
            break;
        }

        case NODE_INDIRECT_JMP: {
            IROperand ptr_val = ir_gen_expr(st, func, node->indirect_jmp.ptr_expr);
            ir_emit(func, IR_OP_INDIRECT_JMP, (IROperand){.type = IR_OPERAND_NONE}, ptr_val, (IROperand){.type = IR_OPERAND_NONE});
            break;
        }

        case NODE_BLOCK: {
            symbol_table_push_scope(st); // Push block scope
            for (size_t i = 0; i < node->block.count; i++) {
                ir_gen_stmt(st, func, node->block.statements[i]);
            }
            symbol_table_pop_scope(st); // Pop block scope
            break;
        }

        default:
            break;
    }
}

IRFunction* ir_gen_function(SymbolTable *st, ASTNode *func_node) {
    if (!func_node || func_node->type != NODE_FUNC_DECL) return NULL;

    IRFunction *func = ir_function_create(func_node->func_decl.func_name);

    symbol_table_push_scope(st); // Push function scope for IR generation

    if (func_node->func_decl.params && func_node->func_decl.params->type == NODE_PARAM_LIST) {
        ASTNode *params = func_node->func_decl.params;
        func->param_count = params->param_list.count;

        for (size_t i = 0; i < params->param_list.count; i++) {
            ASTNode *p = params->param_list.params[i];
            if (p->type == NODE_VAR_DECL) {
                symbol_table_insert(st, p->var_decl.var_name, p->var_decl.type_name, p->token.line);

                IROperand p_vreg = ir_make_vreg(func);
                IROperand p_idx = ir_make_const((int64_t)i);
                ir_emit(func, IR_OP_LOAD_PARAM, p_vreg, p_idx, (IROperand){ .type = IR_OPERAND_NONE });

                IROperand p_ident = ir_make_ident(p->var_decl.var_name);
                ir_emit(func, IR_OP_STORE_VAR, p_ident, p_vreg, (IROperand){ .type = IR_OPERAND_NONE });
            }
        }
    }

    ir_gen_stmt(st, func, func_node->func_decl.body);

    symbol_table_pop_scope(st); // Clean up function scope
    return func;
}