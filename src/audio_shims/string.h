#ifndef SH2_SHIM_STRING_H
#define SH2_SHIM_STRING_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* memset(void* s, int c, size_t n);
void* memcpy(void* d, const void* s, size_t n);
void* memmove(void* d, const void* s, size_t n);
int   memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
#ifdef __cplusplus
}
#endif
#endif
