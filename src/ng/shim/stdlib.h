#ifndef NG_SHIM_STDLIB_H
#define NG_SHIM_STDLIB_H

#include <stddef.h>

void exit(int code);
void abort(void);
void* malloc(size_t n);
void* calloc(size_t n, size_t sz);
void* realloc(void* p, size_t n);
void free(void* p);
int atoi(const char* s);
long strtol(const char* s, char** end, int base);
int abs(int v);
void qsort(void* base, size_t n, size_t sz, int (*cmp)(const void*, const void*));

#endif