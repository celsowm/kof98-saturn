/* Shared state for the generated pure-C runtime. Local build artifact. */
#ifndef PURE_C_STATE_H
#define PURE_C_STATE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Flat 24-bit guest address space. Native-endian typed views are used by the
 * generated bindings, so byte/word/long accesses are self-consistent. */
extern uint8_t g_mem[0x1000000];

typedef struct {
    uint32_t d[8], a[8], osp, pc;
    uint8_t x, n, z, v, c, s, t, ipl, stopped;
} ng_cpu_t;
extern ng_cpu_t ng_cpu;

/* Helper functions emitted by clean_ghidra_c.py (defined in pure_c_helpers.c). */
uint32_t bcdAdjust(int);
void pushInvalidateCaches(int);
void halt_unimplemented(void);
void halt_baddata(void);
void __m68k_trap(int);
int32_t SBORROW1(int32_t, int32_t);
int32_t SBORROW2(int32_t, int32_t);
int32_t SCARRY1(int32_t, int32_t);
int32_t SCARRY4(int32_t, int32_t);
int32_t CARRY1(int32_t, int32_t);
int32_t CARRY2(int32_t, int32_t);
int32_t SUB42(void *, int32_t);

#endif
