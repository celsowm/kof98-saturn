/* Runtime stubs for Ghidra-emitted helpers. Local build artifact. */
#include "pure_c_state.h"

uint8_t g_mem[0x1000000];
ng_cpu_t ng_cpu;

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

/* Called-only guest routines outside the WRAM set: stubs let the runnable
 * subset link; a full build supplies the real translated bodies. */
void wram_fn_100868(void) {}
void wram_fn_c004c2(void) {}
void wram_fn_c004c8(void) {}
void wram_fn_c004ce(void) {}
