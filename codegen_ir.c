#include "Headers/codegen.h"
#include "Headers/ir.h"
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define unlikely(a) __builtin_expect(!!(a), 0)

// Shared LLVM State for the whole module
static LLVMContextRef ctx;     // The core thread-safe context managing global llvm types and constrants; the top-level container for all LLVM global data
static LLVMModuleRef module;   // The global container for functions, basic blocks, and global variables; llvm modules represent the top-level structure in an llvm program, an llvm module is effectively  a translation unit or a collection of translation units merged together
// All basic blocks must also end with a term instruction like br(branch) or ret(return)
static LLVMBuilderRef builder; // The instruction emission cursor (IR Builder) used to append instructions to basic blocks https://llvm.org/doxygen/classllvm_1_1BasicBlock.html

typedef struct {
    char name[MAX_VAR_LENGTH];
    LLVMValueRef alloca_ptr;
} VarAllocMap; // Associative mapping binding an Aseity source variable string identifier(name) to its stack alloca reference(alloca_ptr)

typedef struct {
    uint32_t label_id;
    LLVMBasicBlockRef block_ref;
} LabelBlockMap; // Associative mapping binding a 3AC numeric label ID(label_id) to its corresponding LLVM basic block handle(block_ref)

void codegen_init(const char *module_name) { // Instantiates the global llvm context, creates and named translation unit called module, and initializes the IR builder
    ctx = LLVMContextCreate();
    module = LLVMModuleCreateWithNameInContext(module_name, ctx); // Translation units are units of compilation that represents a single, self-contained source code file after it has processed all macro expansions and header inclusions
                                                                  // https://stackoverflow.com/questions/7146425/llvm-translation-unit#:~:text=So%2C%20translation%20unit%20is%20the%20single%20source%20file%20%28file%2Ec%29%20after%20preprocessing%20%28all%20%23included%20%2A%2Eh%20files%20instantiated%2C%20all%20macro%20are%20expanded%2C%20all%20comments%20are%20skipped%2C%20and%20file%20is%20ready%20for%20tokenizing%29%2E
    builder = LLVMCreateBuilderInContext(ctx);
}

void codegen_emit_main_wrapper(void) {
    // Check if 'origin' exists in the module
    LLVMValueRef origin_func = LLVMGetNamedFunction(module, "origin");
    if (!origin_func) {
        fprintf(stderr, "Aseity Compiler Error: Required entry point 'origin' function not found in source file.\n");
        exit(EXIT_FAILURE);
    }

    // Define native C main: i32 main(i32 argc, i8** argv)
    LLVMTypeRef main_param_types[] = {
        LLVMInt32TypeInContext(ctx),
        LLVMPointerType(LLVMPointerType(LLVMInt8TypeInContext(ctx), 0), 0)
    };
    LLVMTypeRef main_type = LLVMFunctionType(LLVMInt32TypeInContext(ctx), main_param_types, 2, 0);
    LLVMValueRef main_func = LLVMAddFunction(module, "main", main_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, main_func, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    // Get C main arguments
    LLVMValueRef argc_32 = LLVMGetParam(main_func, 0);
    LLVMValueRef argv_ptr = LLVMGetParam(main_func, 1);

    // Convert argc from i32 to i64 for Aseity's origin(argc: i64, ...)
    LLVMValueRef argc_64 = LLVMBuildZExt(builder, argc_32, LLVMInt64TypeInContext(ctx), "argc_i64");

    // Call Aseity's origin(argc_64, argv_ptr)
    LLVMValueRef args[] = { argc_64, argv_ptr };
    LLVMValueRef ret_val = LLVMBuildCall2(builder, LLVMGlobalGetValueType(origin_func), origin_func, args, 2, "origin_ret");

    // Truncate i64 return value down to i32 for C process exit code
    LLVMValueRef ret_32 = LLVMBuildTrunc(builder, ret_val, LLVMInt32TypeInContext(ctx), "ret_i32");
    LLVMBuildRet(builder, ret_32);
}

void codegen_lower_function(const IRFunction *ir_func) {
    LLVMTypeRef ret_type = LLVMInt64TypeInContext(ctx);

    LLVMValueRef llvm_func = LLVMGetNamedFunction(module, ir_func->func_name);
    if (!llvm_func) {
        // Dynamically allocate parameter types based on 3AC
        LLVMTypeRef *param_types = malloc(ir_func->param_count * sizeof(LLVMTypeRef));
        for(size_t i = 0; i < ir_func->param_count; i++) {
            param_types[i] = LLVMInt64TypeInContext(ctx);
        }

         LLVMTypeRef func_type = LLVMFunctionType(ret_type, param_types, ir_func->param_count, 0);
        llvm_func = LLVMAddFunction(module, ir_func->func_name, func_type);
        free(param_types);
    }

    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(ctx, llvm_func, "entry");
    LLVMPositionBuilderAtEnd(builder, entry_bb);

    LabelBlockMap label_map[64];
    size_t label_count = 0;
    VarAllocMap var_map[64];
    size_t var_count = 0;
    LLVMValueRef vregs[1024] = {0};
    LLVMValueRef call_args[16];
    size_t arg_count = 0;

    // Pre-scan labels
    for (size_t i = 0; i < ir_func->count; i++) {
        IRInstr instr = ir_func->instructions[i];
        if (instr.op == IR_OP_LABEL) {
            char block_name[16];
            snprintf(block_name, sizeof(block_name), "L%u", instr.dest.label_id);
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(ctx, llvm_func, block_name);
            label_map[label_count++] = (LabelBlockMap){ instr.dest.label_id, bb };
        }
    }

    #define GET_LABEL_BB(lbl_id) ({ \
        LLVMBasicBlockRef _lbl_target_bb = NULL; \
        for (size_t k = 0; k < label_count; k++) { \
            if (label_map[k].label_id == (lbl_id)) { _lbl_target_bb = label_map[k].block_ref; break; } \
        } _lbl_target_bb; \
    })

    #define GET_VAR_ALLOCA(var_name) ({ \
        LLVMValueRef alloca_ref = NULL; \
        for (size_t k = 0; k < var_count; k++) { \
            if (strcmp(var_map[k].name, (var_name)) == 0) { alloca_ref = var_map[k].alloca_ptr; break; } \
        } \
        if (!alloca_ref) { \
            LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(builder); \
            LLVMPositionBuilder(builder, entry_bb, LLVMGetFirstInstruction(entry_bb)); \
            alloca_ref = LLVMBuildAlloca(builder, LLVMInt64TypeInContext(ctx), (var_name)); \
            LLVMPositionBuilderAtEnd(builder, current_bb); \
            strncpy(var_map[var_count].name, (var_name), MAX_VAR_LENGTH - 1); \
            var_map[var_count++].alloca_ptr = alloca_ref; \
        } alloca_ref; \
    })

    // Lower instructions
    for (size_t i = 0; i < ir_func->count; i++) {
        IRInstr instr = ir_func->instructions[i];

        switch (instr.op) {
            case IR_OP_LOAD_CONST:
                vregs[instr.dest.vreg_id] = LLVMConstInt(LLVMInt64TypeInContext(ctx), instr.src1.const_val, 1);
                break;

            case IR_OP_LOAD_STR: {
                // String literal lowering
                LLVMValueRef str_const = LLVMConstStringInContext(ctx, instr.src1.str_val, strlen(instr.src1.str_val), 1);
                LLVMValueRef global_str = LLVMAddGlobal(module, LLVMTypeOf(str_const), ".str");
                LLVMSetInitializer(global_str, str_const);
                LLVMSetGlobalConstant(global_str, 1);
                // Convert to raw pointer for LLVM compatibility
                LLVMValueRef zero = LLVMConstInt(LLVMInt64TypeInContext(ctx), 0, 0);
                LLVMValueRef indices[] = { zero, zero };
                vregs[instr.dest.vreg_id] = LLVMBuildGEP2(builder, LLVMTypeOf(str_const), global_str, indices, 2, "str_ptr");
                break;
            }

            case IR_OP_LOAD_VAR:
                vregs[instr.dest.vreg_id] = LLVMBuildLoad2(builder, LLVMInt64TypeInContext(ctx), GET_VAR_ALLOCA(instr.src1.name), "load_tmp");
                break;

            case IR_OP_STORE_VAR:
                LLVMBuildStore(builder, vregs[instr.src1.vreg_id], GET_VAR_ALLOCA(instr.dest.name));
                break;

            case IR_OP_ADD:
                vregs[instr.dest.vreg_id] = LLVMBuildAdd(builder, vregs[instr.src1.vreg_id], vregs[instr.src2.vreg_id], "add_tmp");
                break;

            case IR_OP_CMP_LT: {
                // LLVM CMP returns i1
                // We must extend it to i64 to match Aseity's vregs
                LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntSLT, vregs[instr.src1.vreg_id], vregs[instr.src2.vreg_id], "cmp_tmp");
                vregs[instr.dest.vreg_id] = LLVMBuildZExt(builder, cmp, LLVMInt64TypeInContext(ctx), "cmp_ext");
                break;
            }

            case IR_OP_LABEL: {
                LLVMBasicBlockRef target_bb = GET_LABEL_BB(instr.dest.label_id);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder))) {
                    LLVMBuildBr(builder, target_bb);
                }
                LLVMPositionBuilderAtEnd(builder, target_bb);
                break;
            }

            case IR_OP_JMP:
                LLVMBuildBr(builder, GET_LABEL_BB(instr.dest.label_id));
                break;

            case IR_OP_JMP_IF_FALSE: {
                LLVMBasicBlockRef false_bb = GET_LABEL_BB(instr.dest.label_id);
                LLVMBasicBlockRef true_bb = LLVMAppendBasicBlockInContext(ctx, llvm_func, "fallthrough");
                // Truncate i64 back to i1 for the LLVM branch instruction
                LLVMValueRef cond_i1 = LLVMBuildTrunc(builder, vregs[instr.src1.vreg_id], LLVMInt1TypeInContext(ctx), "cond_i1");
                LLVMBuildCondBr(builder, cond_i1, true_bb, false_bb);
                LLVMPositionBuilderAtEnd(builder, true_bb);
                break;
            }

            case IR_OP_ARG:
                if (arg_count < 16) {
                    call_args[arg_count++] = vregs[instr.src1.vreg_id];
                } else {
                    fprintf(stderr, "Codegen Error: Exceeded argument buffer capacity (16)\n");
                    exit(EXIT_FAILURE);
                }
                break;

            case IR_OP_LOAD_PARAM:
                // Pulls the native parameter from the LLVM function
                vregs[instr.dest.vreg_id] = LLVMGetParam(llvm_func, instr.src1.const_val);
                break;

            case IR_OP_CALL: {
                // Intrinsic override: map 'print' to C's 'printf'
                // This will prob be the only hardcoded function unlike where C needs the std lib
                // I will prob write my own print specifcally for this too
                // This can only print i64
                if (strcmp(instr.src1.name, "print") == 0) {
                    LLVMValueRef printf_func = LLVMGetNamedFunction(module, "printf");
                    if (!printf_func) {
                        // Declare: i64 printf(i8*, ...)
                        LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8TypeInContext(ctx), 0) };
                        LLVMTypeRef printf_type = LLVMFunctionType(LLVMInt64TypeInContext(ctx), printf_args, 1, 1);
                        printf_func = LLVMAddFunction(module, "printf", printf_type);
                    }

                    // Create the string format: signed 64-bit int
                    LLVMValueRef fmt_str = LLVMBuildGlobalString(builder, "%lld\n", "fmt_i64"); // <- this controls the entire thing

                    // Wire arguments into printf
                    LLVMValueRef printf_args_vals[] = { fmt_str, call_args[0] };
                    vregs[instr.dest.vreg_id] = LLVMBuildCall2(builder, LLVMGlobalGetValueType(printf_func), printf_func, printf_args_vals, 2, "printf_tmp");
                    arg_count = 0;
                    break;
                }

                LLVMValueRef target_func = LLVMGetNamedFunction(module, instr.src1.name);
                if (!target_func) {
                    // Forward declaration stub (variadic to allow anything)
                    LLVMTypeRef func_type = LLVMFunctionType(LLVMInt64TypeInContext(ctx), NULL, 0, 1);
                    target_func = LLVMAddFunction(module, instr.src1.name, func_type);
                }
                LLVMTypeRef target_type = LLVMGlobalGetValueType(target_func);

                // Strictly match args to the signature to prevent LLVM crashing
                size_t expected = LLVMCountParams(target_func);
                LLVMValueRef safe_args[16];
                size_t safe_count = 0;

                for (size_t k = 0; k < expected; k++) {
                    // Pad missing arguments with 0 so LLVM is satisfied
                    safe_args[safe_count++] = (k < arg_count) ? call_args[k] : LLVMConstInt(LLVMInt64TypeInContext(ctx), 0, 0);
                }

                // If variadic, append the rest
                if (LLVMIsFunctionVarArg(target_type)) {
                    for (size_t k = expected; k < arg_count; k++) {
                        safe_args[safe_count++] = call_args[k];
                    }
                }

                vregs[instr.dest.vreg_id] = LLVMBuildCall2(builder, target_type, target_func, safe_args, safe_count, "call_tmp");
                arg_count = 0;
                break;
            }

            case IR_OP_RET:
                LLVMBuildRet(builder, vregs[instr.src1.vreg_id]);
                break;

            default: break;
        }
    }

    LLVMBasicBlockRef first_real = LLVMGetNextBasicBlock(entry_bb);
    if (first_real && !LLVMGetBasicBlockTerminator(entry_bb)) {
        LLVMPositionBuilderAtEnd(builder, entry_bb);
        LLVMBuildBr(builder, first_real);
    }

    // LLVM will infinitely loop if ANY block lacks a terminator!
    // Since 'main' is a script, it naturally ends without a return statement
    // We must manually cap all empty/unterminated blocks with a 0 return
    LLVMBasicBlockRef bb_iter = LLVMGetFirstBasicBlock(llvm_func);
    while (bb_iter) {
        if (!LLVMGetBasicBlockTerminator(bb_iter)) {
            LLVMPositionBuilderAtEnd(builder, bb_iter);
            LLVMBuildRet(builder, LLVMConstInt(LLVMInt64TypeInContext(ctx), 0, 0));
        }
        bb_iter = LLVMGetNextBasicBlock(bb_iter);
    }
}

void codegen_optimize_and_print(bool print) {
    LLVMPassBuilderOptionsRef options = LLVMCreatePassBuilderOptions();

    // Run mem2reg to generate phi nodes automatically
    LLVMErrorRef err = LLVMRunPasses(module, "mem2reg", NULL, options);
    if (err) {
        char *msg = LLVMGetErrorMessage(err);
        fprintf(stderr, "LLVM Pass Error: %s\n", msg);
        LLVMDisposeErrorMessage(msg);
    }

    if (unlikely(print == true)) {
        printf("\n=== LLVM IR ===\n");
        LLVMDumpModule(module);
        LLVMDisposePassBuilderOptions(options);
    }
}

void codegen_emit_object_and_link(const char *output_filename) {
    // Initialize native target for the host architecture
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    char *error = NULL;
    char *target_triple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;

    if (LLVMGetTargetFromTriple(target_triple, &target, &error)) {
        fprintf(stderr, "Target Error: %s\n", error);
        LLVMDisposeMessage(error);
        return;
    }

    // Create the target machine configured for zen3
    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, target_triple, "znver3", "",
        LLVMCodeGenLevelAggressive, LLVMRelocPIC, LLVMCodeModelDefault
    );

    // Emit the raw object file: *.o
    char obj_filename[256];
    snprintf(obj_filename, sizeof(obj_filename), "%s.o", output_filename);

    if (LLVMTargetMachineEmitToFile(machine, module, obj_filename, LLVMObjectFile, &error)) {
        fprintf(stderr, "Object Emission Error: %s\n", error);
        LLVMDisposeMessage(error);
    } else {
        //printf("\n[AOT] Successfully emitted ELF object: %s\n", obj_filename);

        // Invoke the system linker to create the final executable
        char link_cmd[512];
        snprintf(link_cmd, sizeof(link_cmd), "cc %s -o %s -lm", obj_filename, output_filename);

        int link_res = system(link_cmd);
        if (link_res == 0) {
            //printf("[AOT] Successfully linked executable: ./%s\n", output_filename);
        } else {
            fprintf(stderr, "[AOT] System linker failed with code %d\n", link_res);
        }
        fflush(stdout); // Force clion to print output
        fflush(stderr); // Force clion to print errors
    }

    // Cleanup target resources
    LLVMDisposeTargetMachine(machine);
    LLVMDisposeMessage(target_triple);
}

void codegen_shutdown(void) {
    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(module);
    LLVMContextDispose(ctx);
}