#pragma once
#include "ast.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    IR_OP_LOAD_CONST,  // v_dest = 10
    IR_OP_LOAD_CONST128,
    IR_OP_LOAD_STR,    // v_dest = "string"
    IR_OP_LOAD_VAR,    // v_dest = x
    IR_OP_STORE_VAR,   // x = v_src1
    IR_OP_ADD,         // v_dest = v_src1 + v_src2
    IR_OP_SUB,
    IR_OP_MUL,
    IR_OP_DIV,
    IR_OP_CMP_LT,      // v_dest = v_src1 < v_src2
    IR_OP_LABEL,       // .L0:
    IR_OP_JMP_IF_FALSE,// if (!v_src1) goto dest_label
    IR_OP_JMP,         // goto dest_label
    IR_OP_ARG,         // ARG v_src1
    IR_OP_LOAD_PARAM,  // v_dest = PARAM src1
    IR_OP_CALL,        // v_dest = CALL func_name
    IR_OP_RET          // return v_src1
} IROpcode;

typedef enum {
    IR_OPERAND_NONE,
    IR_OPERAND_VREG,   // Virtual Register ID
    IR_OPERAND_CONST,  // Literal i64 / char value
    IR_OPERAND_CONST128,
    IR_OPERAND_STR,    // Pascal string payload
    IR_OPERAND_IDENT,  // Variable/Function string name
    IR_OPERAND_LABEL   // Label index
} IROperandType;

// 3AC operands: llvm ir uses 3 address code and requires it to be in SSA form, which means each virtual reg can only be assigned once, and requires strict typing
typedef struct {
    IROperandType type;
    union {
        uint32_t vreg_id;
        int64_t const_val;
        char str_val[MAX_VAR_LENGTH];
        char name[MAX_VAR_LENGTH];
        uint32_t label_id;
    };
} IROperand;

typedef struct {
    IROpcode op;
    IROperand dest;
    IROperand src1;
    IROperand src2;
} IRInstr; // llvm ir instructions

typedef struct {
    char func_name[MAX_VAR_LENGTH];
    IRInstr *instructions;
    size_t count;
    size_t capacity;
    uint32_t next_vreg;  // Virtual register counter
    uint32_t next_label; // Label counter
    size_t param_count;  // Tracks func signature
} IRFunction; // For functions

IRFunction* ir_function_create(const char *name);
void ir_emit(IRFunction *func, IROpcode op, IROperand dest, IROperand src1, IROperand src2);
void ir_print_function(const IRFunction *func);