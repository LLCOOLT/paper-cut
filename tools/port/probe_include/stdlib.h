#pragma once
#include <stddef.h>
void* malloc(size_t); void* calloc(size_t, size_t); void* realloc(void*, size_t); void free(void*);
void abort(void); void exit(int); int abs(int); long labs(long); int atoi(const char*);
char* getenv(const char*);
void qsort(void*, size_t, size_t, int (*)(const void*, const void*));
