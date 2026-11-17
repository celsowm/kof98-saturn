/* C++ global operators for the freestanding SH-2 audio build.
 * SH-2 size_t is unsigned int (32-bit), so use size_t directly. */
#include <stddef.h>
extern "C" void* malloc(size_t n);
extern "C" void free(void* p);
void* operator new(size_t n) { return malloc(n); }
void operator delete(void* p) { free(p); }
void operator delete(void* p, size_t) { free(p); }
void* operator new[](size_t n) { return malloc(n); }
void operator delete[](void* p) { free(p); }
void operator delete[](void* p, size_t) { free(p); }
