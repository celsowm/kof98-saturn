#ifndef KOF_INTERNAL_BYTES_H
#define KOF_INTERNAL_BYTES_H
#include <stddef.h>
/* Bare-metal GCC ships stdint/stddef, but not necessarily libc headers.
 * LibSaturn's newlib_stubs.c supplies these two runtime symbols. */
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 0
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);
#else
#include <string.h>
#endif
/* No memcmp dependency on the bare-metal runtime. */
static inline int kof_memcmp(const void *lhs, const void *rhs, size_t n)
{
    const unsigned char *a = lhs, *b = rhs;
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    return 0;
}
#endif
