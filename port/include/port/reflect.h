#ifndef PORT_REFLECT_H
#define PORT_REFLECT_H

/* The game data mods can patch by name (docs/MODDING.md, "patches/"): the
 * variables port/mods/reflect.spec lists, their types field by field, and the
 * enum constants. Generated: port/src/aurora/reflect_gen.c
 * (tools/port/gen_reflect.py); read by port/mods/mods.cpp. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PortReflKind {
    PORT_REFL_UNSIGNED, /* integers, by size */
    PORT_REFL_SIGNED,
    PORT_REFL_FLOAT,    /* float or double, by size */
    PORT_REFL_STRING,   /* char*: a patch can point it at new text */
    PORT_REFL_POINTER,  /* any other pointer: not patchable */
    PORT_REFL_STRUCT,
    PORT_REFL_OPAQUE,   /* a union, function pointer or the like: not patchable */
} PortReflKind;

typedef struct PortReflStruct PortReflStruct;

typedef struct PortReflField {
    const char* name;
    int kind;                    /* PortReflKind */
    size_t offset;
    size_t size;                 /* of one element */
    int count;                   /* 1, or the array's length (all dimensions) */
    const PortReflStruct* type;  /* PORT_REFL_STRUCT */
} PortReflField;

struct PortReflStruct {
    const char* name;
    size_t size;
    int fieldCount;
    const PortReflField* fields;
};

typedef struct PortReflSymbol {
    const char* name;
    void* address;
    int kind;                    /* of an element */
    size_t size;                 /* of an element */
    int count;                   /* elements */
    const PortReflStruct* type;  /* PORT_REFL_STRUCT */
} PortReflSymbol;

typedef struct PortReflEnum {
    const char* name;
    long long value;
} PortReflEnum;

extern const PortReflSymbol portReflSymbols[];
extern const int portReflSymbolCount;
extern const PortReflEnum portReflEnums[];
extern const int portReflEnumCount;

#ifdef __cplusplus
}
#endif

#endif
