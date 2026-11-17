#ifndef SH2_SHIM_STDLIB_H
#define SH2_SHIM_STDLIB_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* malloc(size_t n);
void  free(void* p);
void  abort(void);
#ifdef __cplusplus
}
#endif
#endif
