/* Minimal bus shim for differential equivalence: guest memory is a flat
 * native-endian array shared by the recomp C and the generated pure C. */
#ifndef PUREC_EQUIV_BUS_H
#define PUREC_EQUIV_BUS_H
#include <stdint.h>
#include <stddef.h>
#include <neogeorecomp/cpu.h>

extern uint8_t g_mem[0x1000000];

static inline uint8_t ng_r8(uint32_t a) { a &= 0xFFFFFF; return g_mem[a]; }
static inline uint16_t ng_r16(uint32_t a) { a &= 0xFFFFFE; return *(uint16_t *)(g_mem + a); }
static inline uint32_t ng_r32(uint32_t a) { a &= 0xFFFFFC; return *(uint32_t *)(g_mem + a); }
static inline void ng_w8(uint32_t a, uint8_t v) { a &= 0xFFFFFF; g_mem[a] = v; }
static inline void ng_w16(uint32_t a, uint16_t v) { a &= 0xFFFFFE; *(uint16_t *)(g_mem + a) = v; }
static inline void ng_w32(uint32_t a, uint32_t v) { a &= 0xFFFFFC; *(uint32_t *)(g_mem + a) = v; }

static inline void ng_push16(uint16_t v) { ng_cpu.a[7] -= 2; ng_w16(ng_cpu.a[7], v); }
static inline void ng_push32(uint32_t v) { ng_cpu.a[7] -= 4; ng_w32(ng_cpu.a[7], v); }
static inline uint16_t ng_pop16(void) { uint16_t v = ng_r16(ng_cpu.a[7]); ng_cpu.a[7] += 2; return v; }
static inline uint32_t ng_pop32(void) { uint32_t v = ng_r32(ng_cpu.a[7]); ng_cpu.a[7] += 4; return v; }

#endif
