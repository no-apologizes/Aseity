#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct StructDef StructDef;

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
    TYPE_PTR,

    TYPE_ARRAY,
    TYPE_FUNCTION,
    TYPE_STRUCT,
} TypeKind;

typedef struct Type Type;

typedef struct {
    uint32_t name_id;
    Type *type;
} StructField;

struct Type {
    TypeKind kind;
    size_t size_bytes;
    size_t align_bytes;
    bool is_signed;
    union {
        struct {
            Type *pointee;
            bool nullable;
        } pointer;
        struct { Type *element; uint64_t length; } array;
        struct {
            uint32_t name_id;
            StructField *fields;
            uint32_t field_count;
            bool is_complete;
        } structure;
        struct {
            Type *return_type;
            uint16_t param_count;
            Type **params;
        } function;
    };
};