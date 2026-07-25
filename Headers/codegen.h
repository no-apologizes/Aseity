#pragma once
#include "ir.h"
#include <llvm-c/Core.h>

void codegen_init(const char *module_name);
void codegen_emit_main_wrapper(void);
void codegen_lower_function(const IRFunction *ir_func); // Lower
void codegen_optimize_and_print(bool print); // With llvm mem2reg
void codegen_emit_object_and_link(const char *output_filename);
void codegen_shutdown(void);