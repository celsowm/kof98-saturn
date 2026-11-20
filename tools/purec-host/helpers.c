/* Game-independent helper shims for the generated pure-C guest runtime.
 * The host engine provides ng_cpu/ng_cycles/ng_bad_dispatch; this file only
 * supplies the small Ghidra helper surface that neogeorecomp does not export.
 */
#include <stdint.h>

uint32_t bcdAdjust(int v) { (void)v; return 0; }
void pushInvalidateCaches(int v) { (void)v; }
void halt_unimplemented(void) {}
void halt_baddata(void) {}
void __m68k_trap(int v) { (void)v; }
int32_t SBORROW1(int32_t a, int32_t b) { return (int32_t)((uint32_t)a < (uint32_t)b); }
int32_t SBORROW2(int32_t a, int32_t b) { return (int32_t)((uint32_t)a < (uint32_t)b); }
int32_t SCARRY1(int32_t a, int32_t b) { return (int32_t)(((uint32_t)a + (uint32_t)b) > 0xFF); }
int32_t SCARRY4(int32_t a, int32_t b) { return (int32_t)(((uint32_t)a + (uint32_t)b) > 0xFFFFFFFFu); }
int32_t CARRY1(int32_t a, int32_t b) { return (int32_t)(((uint32_t)a + (uint32_t)b) > 0xFF); }
int32_t CARRY2(int32_t a, int32_t b) { return (int32_t)(((uint32_t)a + (uint32_t)b) > 0xFFFF); }
int32_t SUB42(void *a, int32_t b) { (void)a; return b; }
