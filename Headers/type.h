#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TYPE_UNIT,
    TYPE_NEVER,
    TYPE_VILE,

    TYPE_U8, TYPE_I8,
    TYPE_U16, TYPE_I16,
    TYPE_U32, TYPE_I32, TYPE_F32,
    TYPE_U64, TYPE_I64, TYPE_F64,
    TYPE_U128, TYPE_I128, TYPE_F128,

    TYPE_BOOL,
    TYPE_STR,
    TYPE_CHAR,

    // Complex types
    TYPE_PTR,
    TYPE_ARRAY,
    TYPE_FUNCTION,
    TYPE_STRUCT,
} TypeKind;

typedef struct Type Type;

void type_init(void);
Type *type_get_prim(TypeKind kind);
Type *type_intern_ptr(Type *pointee, bool nullable);
Type *type_intern_array(Type *element, size_t length);
Type *type_intern_function(Type *return_type, uint16_t param_count, Type **params);
Type *type_struct_declare(uint32_t name_id, bool *out_already_declared);
void type_struct_complete(Type *struct_type, uint32_t field_count, StructField *fields);
bool type_is_equal(const Type *a, const Type *b);