/* Runtime stubs for Ghidra/recomp helpers. Local build artifact. */
#include "pure_c_state.h"

uint8_t g_mem[0x1000000];
ng_cpu_t ng_cpu;
int64_t ng_cycles;
int64_t ng_next_event;
uint64_t ng_icount;

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

void ng_bad_dispatch(uint32_t pc) { fprintf(stderr, "bad dispatch %06X\n", pc); }
void ng_exception(int vector, uint32_t ret_pc) { (void)vector; (void)ret_pc; }
void ng_set_sr(uint16_t v) {
    ng_cpu.x = (v >> 4) & 1; ng_cpu.n = (v >> 3) & 1;
    ng_cpu.z = (v >> 2) & 1; ng_cpu.v = (v >> 1) & 1; ng_cpu.c = v & 1;
}
