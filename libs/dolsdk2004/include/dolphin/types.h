#ifndef _DOLPHIN_TYPES_H_
#define _DOLPHIN_TYPES_H_

#ifdef TARGET_PC
/* Fixed-width on every host ABI (long is 64-bit on LP64 Linux/macOS). */
#include <stdint.h>
typedef int8_t   s8;
typedef uint8_t  u8;
typedef int16_t  s16;
typedef uint16_t u16;
typedef int32_t  s32;
typedef uint32_t u32;
typedef int64_t  s64;
typedef uint64_t u64;
typedef volatile s8  vs8;
typedef volatile u8  vu8;
typedef volatile s16 vs16;
typedef volatile u16 vu16;
typedef volatile s32 vs32;
typedef volatile u32 vu32;
typedef volatile s64 vs64;
typedef volatile u64 vu64;
#else
typedef signed   char          s8;
typedef unsigned char          u8;
typedef signed   short int     s16;
typedef unsigned short int     u16;
typedef signed   long          s32;
typedef unsigned long          u32;
typedef signed   long long int s64;
typedef unsigned long long int u64;

typedef volatile signed   char          vs8;
typedef volatile unsigned char          vu8;
typedef volatile signed   short int     vs16;
typedef volatile unsigned short int     vu16;
typedef volatile signed   long          vs32;
typedef volatile unsigned long          vu32;
typedef volatile signed   long long int vs64;
typedef volatile unsigned long long int vu64;
#endif

typedef float  f32;
typedef double f64;

typedef char *Ptr;

typedef int BOOL;

#define FALSE 0
#define TRUE 1

#if defined(TARGET_PC)
#define AT_ADDRESS(addr)
#elif defined(__MWERKS__)
#define AT_ADDRESS(addr) : (addr)
#elif defined(__GNUC__)
//#define AT_ADDRESS(addr) __attribute__((address((addr))))
#define AT_ADDRESS(addr)  // was removed in GCC. define in linker script instead.
#else
#error unknown compiler
#endif

#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))

#ifndef INT_MAX
#define INT_MAX 2147483647
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifdef TARGET_PC
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#else
#include "libc/stdio.h"
#include "libc/stdarg.h"
#include "libc/string.h"
#include "libc/ctype.h"
#endif

#include "cmath.h"

#endif
