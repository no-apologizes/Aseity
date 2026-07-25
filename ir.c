#include "Headers/ir.h"
#include "Headers/arena.h"
#include <stdio.h>
#include <string.h>

IRFunction* ir_function_create(const char *name) {
    IRFunction *func = arena_alloc_transient(&ast_arena, sizeof(IRFunction));
    strncpy(func->func_name, name, MAX_VAR_LENGTH - 1);
    func->func_name[MAX_VAR_LENGTH - 1] = '\0';

    func->capacity = 16;
    func->count = 0;
    func->next_vreg = 0;
    func->next_label = 0;
    func->param_count = 0;

    func->instructions = arena_alloc_transient(&ast_arena, func->capacity * sizeof(IRInstr));
    return func;
}

void ir_emit(IRFunction *func, IROpcode op, IROperand dest, IROperand src1, IROperand src2) {
    if (func->count >= func->capacity) {
        size_t old_cap = func->capacity;
        func->capacity *= 2;
        IRInstr *new_instrs = arena_alloc_transient(&ast_arena, func->capacity * sizeof(IRInstr));
        memcpy(new_instrs, func->instructions, old_cap * sizeof(IRInstr));
        func->instructions = new_instrs;
    }

    IRInstr *instr = &func->instructions[func->count++];
    instr->op = op;
    instr->dest = dest;
    instr->src1 = src1;
    instr->src2 = src2;
}

static void print_operand(IROperand op) {
    switch (op.type) {
        case IR_OPERAND_NONE:  break;
        case IR_OPERAND_VREG:  printf("v%u", op.vreg_id); break;
        case IR_OPERAND_CONST: printf("%ld", op.const_val); break;
        case IR_OPERAND_STR:   printf("\"%s\"", op.str_val); break;
        case IR_OPERAND_IDENT: printf("%s", op.name); break;
        case IR_OPERAND_LABEL: printf(".L%u", op.label_id); break;
    }
}

void ir_print_function(const IRFunction *func) {
    printf("\n=== IR: %s ===\n", func->func_name);
    for (size_t i = 0; i < func->count; i++) {
        IRInstr instr = func->instructions[i];

        if (instr.op == IR_OP_LABEL) {
            print_operand(instr.dest); printf(":\n");
            continue;
        }

        printf("%03zu:  ", i);

        switch (instr.op) {
            case IR_OP_LOAD_CONST:
                print_operand(instr.dest); printf(" = LOAD_CONST "); print_operand(instr.src1); break;
            case IR_OP_LOAD_STR:
                print_operand(instr.dest); printf(" = LOAD_STR "); print_operand(instr.src1); break;
            case IR_OP_LOAD_VAR:
                print_operand(instr.dest); printf(" = LOAD_VAR "); print_operand(instr.src1); break;
            case IR_OP_STORE_VAR:
                printf("STORE_VAR "); print_operand(instr.dest); printf(", "); print_operand(instr.src1); break;
            case IR_OP_ADD:
                print_operand(instr.dest); printf(" = ADD "); print_operand(instr.src1); printf(", "); print_operand(instr.src2); break;
            case IR_OP_SUB:
                print_operand(instr.dest); printf(" = SUB "); print_operand(instr.src1); printf(", "); print_operand(instr.src2); break;
            case IR_OP_MUL:
                print_operand(instr.dest); printf(" = MUL "); print_operand(instr.src1); printf(", "); print_operand(instr.src2); break;
            case IR_OP_DIV:
                print_operand(instr.dest); printf(" = DIV "); print_operand(instr.src1); printf(", "); print_operand(instr.src2); break;
            case IR_OP_CMP_LT:
                print_operand(instr.dest); printf(" = CMP_LT "); print_operand(instr.src1); printf(", "); print_operand(instr.src2); break;
            case IR_OP_JMP:
                printf("JMP "); print_operand(instr.dest); break;
            case IR_OP_JMP_IF_FALSE:
                printf("JMP_IF_FALSE "); print_operand(instr.src1); printf(" -> "); print_operand(instr.dest); break;
            case IR_OP_ARG:
                printf("ARG "); print_operand(instr.src1); break;
            case IR_OP_LOAD_PARAM:
                print_operand(instr.dest); printf(" = LOAD_PARAM "); print_operand(instr.src1); break;
            case IR_OP_CALL:
                print_operand(instr.dest); printf(" = CALL "); print_operand(instr.src1); break;
            case IR_OP_RET:
                printf("RET "); print_operand(instr.src1); break;
            default: break;
        }
        printf("\n");
    }
}