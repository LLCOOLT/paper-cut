#pragma once
#include <stddef.h>
#include <stdarg.h>
typedef struct _PortProbeFILE FILE;
int printf(const char*, ...); int sprintf(char*, const char*, ...);
int snprintf(char*, size_t, const char*, ...); int vsprintf(char*, const char*, va_list);
int vsnprintf(char*, size_t, const char*, va_list);
extern FILE* stderr; extern FILE* stdout;
int fprintf(FILE*, const char*, ...); int vfprintf(FILE*, const char*, va_list);
int fputs(const char*, FILE*); int puts(const char*);
