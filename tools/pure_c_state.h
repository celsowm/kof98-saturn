/* Shared state for generated pure-C runtime and equivalence harness.
 * Local build artifact. */
#ifndef PURE_C_STATE_H
#define PURE_C_STATE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Flat 24-bit guest address space, native-endian typed views. */
extern uint8_t g_mem[0x1000000];

/* Upstream 68000 register file (declares ng_cpu, ng_cycles, flags, helpers). */
#include <neogeorecomp/cpu.h>

/* Helpers emitted by clean_ghidra_c.py (defined in pure_c_helpers.c). */
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
void ng_bad_dispatch(uint32_t pc);
void ng_exception(int vector, uint32_t ret_pc);
void ng_set_sr(uint16_t sr);

#endif
