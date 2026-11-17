/* shim_alloc.c - minimal allocator for the freestanding SH-2 audio build.
 * A bump allocator over a static arena; ymfm's channels are allocated once at
 * init and never freed, so no reuse is needed. */
#include <stddef.h>
#include <stdint.h>

#define ARENA_SIZE (48 * 1024)
static uint8_t s_arena[ARENA_SIZE] __attribute__((aligned(16)));
static size_t s_used;

void* malloc(size_t n) {
    n = (n + 15u) & ~(size_t)15u;
    if (s_used + n > ARENA_SIZE) return 0;
    void* p = &s_arena[s_used];
    s_used += n;
    return p;
}
void free(void* p) { (void)p; }
void abort(void) { for (;;) {} }

/* z80.c and audio.c reference these only in debug paths (never taken).
 * Provide freestanding stubs so the link succeeds. */
typedef struct { int unused; } FILE;
FILE* stderr = 0;
int fprintf(void* f, const char* fmt, ...) { (void)f; (void)fmt; return 0; }
int printf(const char* fmt, ...) { (void)fmt; return 0; }
