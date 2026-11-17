/* dense_routines.c - hand-optimized native handlers for the hottest guest
 * routines. Local build only; derived from the user's own ROM.
 *
 * Dense handler for guest 0x00A142 (114,920 hits = 43% of all native
 * dispatches): poll a byte at (A5+0x2785) until nonzero, yielding at each
 * cycle-budget boundary. The C recomp stores N/Z/V/C to ng_cpu on every
 * iteration; here flags are computed into locals and written once at exit.
 *
 * Dense handler for guest 0x0034EA (Ymir profile: 29.8% of all SH-2
 * instructions): the 68000 `clr.l (A0)+` bulk-clear loop
 *   0034EA clr.l (A0)+  x4 ; 0034F2 dbra D0,$34ea ; 0034F6 rts
 * The C recomp reloads/stores CPU.a[0], CPU.d[0], ng_cycles, ng_icount and
 * four CCR flags on every one of the 5 guest instructions (≈50 SH-2 insns
 * per clr.l in the disassembly). This handler keeps all of them in locals,
 * issues the same ng_w32 writes (so page-map and slow-path semantics are
 * identical), and writes guest state back once at yield or exit.
 */
#include <stdint.h>
#include <neogeorecomp/cpu.h>
#include <neogeorecomp/bus.h>
#include <neogeorecomp/neogeo.h>
#include "ng_boot.h"

extern uint8_t* ng_rmap[256];
extern uint8_t* ng_wmap[256];
extern int32_t ng_next_event;
extern int32_t ng_cycles;
extern uint32_t ng_icount;

/* Exported by ng_boot.c. Lets this ONE dense handler inline the LSPC VRAM
 * data-port poke (lspc_write case 2) instead of calling ng_w16_slow 16x per
 * iteration. The page-mapped and all other device cases still go through
 * ng_w16, so behaviour is identical even if A1 is not the VRAM port. */
extern uint16_t* const ng_dense_vram_base;
extern uint16_t* const ng_dense_lspc_addr;
extern uint16_t* const ng_dense_lspc_mod;
extern uint16_t* const ng_dense_pal_base;
extern int*      const ng_dense_pal_bank;
extern volatile uint32_t ng_boot_vram_writes;
extern volatile uint32_t ng_boot_io_writes;

/* Every `move.b Dn,$300001` sound-command strobe in the dense handlers: the
 * address is in page $30 (unmapped, so ng_w8 -> ng_w8_slow), whose io_write8
 * case 0x300000 is a no-op `break` — only ng_boot_io_writes increments. Doing
 * that directly is bit-exact and drops a lookup + call per strobe. */
static inline void dense_sound_strobe(void) { ng_boot_io_writes++; }
extern volatile uint32_t ng_boot_pal_writes;

static inline void dense_w16_fast(uint32_t a, uint16_t v) {
    a &= 0xFFFFFEu;
    uint8_t* p = ng_wmap[a >> 16];
    if (p) { p += a & 0xFFFFu; p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; return; }
    if ((a & 0xFE0000u) == 0x3C0000u && (a & 0xEu) == 0x2u) {
        uint16_t ad = *ng_dense_lspc_addr;
        uint32_t vi = (ad & 0x8000u) ? (0x8000u | (ad & 0x07FFu)) : ad;
        ng_dense_vram_base[vi] = v;
        ng_boot_vram_writes++;
        *ng_dense_lspc_addr = (uint16_t)((ad & 0x8000u) | ((ad + *ng_dense_lspc_mod) & 0x7FFFu));
        return;
    }
    ng_w16_slow(a, v);
}

/* Palette-only word write for the r_C11002 fill loops 2/3 (A0 = $402000),
 * where every write lands in palette RAM. Skips the ng_w16_slow call entirely;
 * falls back to ng_w16 for any unexpected address so behaviour is unchanged. */
static inline void dense_pal_w16(uint32_t a, uint16_t v) {
    a &= 0xFFFFFEu;
    if (!ng_wmap[a >> 16] && a >= 0x400000u && a < 0x800000u) {
        ng_dense_pal_base[*ng_dense_pal_bank * 0x1000u + ((a >> 1) & 0xFFFu)] = v;
        ng_boot_pal_writes++;
        return;
    }
    ng_w16(a, v);
}

/* Combined big-endian longword store with ONE page lookup instead of two
 * (ng_w32 -> ng_w16 x2). Bit-exact for 4-aligned addresses: a and a+2 are in
 * the same 64 KiB page, so the single p->addr translation is identical. For
 * unmapped (device) addresses it falls back to the exact two ng_w16 calls,
 * preserving each one's slow-path side effects. */
static inline void dense_w32_fast(uint32_t a, uint32_t v) {
    a &= 0xFFFFFEu;
    uint8_t* p = ng_wmap[a >> 16];
    if (p) {
        p += a & 0xFFFFu;
        p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
        return;
    }
    ng_w16(a, (uint16_t)(v >> 16));
    ng_w16(a + 2, (uint16_t)v);
}

/* movem.l D0-D3,-(A0) writing to palette RAM (bit-exact word order/flags). */
static inline void dense_movem4_pal(uint32_t* a, uint32_t d0, uint32_t d1,
                                    uint32_t d2, uint32_t d3) {
    uint32_t p = *a;
    p -= 4; dense_pal_w16(p + 2, (uint16_t)d3); dense_pal_w16(p, (uint16_t)(d3 >> 16));
    p -= 4; dense_pal_w16(p + 2, (uint16_t)d2); dense_pal_w16(p, (uint16_t)(d2 >> 16));
    p -= 4; dense_pal_w16(p + 2, (uint16_t)d1); dense_pal_w16(p, (uint16_t)(d1 >> 16));
    p -= 4; dense_pal_w16(p + 2, (uint16_t)d0); dense_pal_w16(p, (uint16_t)(d0 >> 16));
    *a = p;
}

/* 0x0034EA: clr.l (A0)+ x4 / dbra D0,$34ea / rts.
 * Bit-exact lowering of the C recomp at recomp_07.c:129260:
 *   clr.l: NG_INSN(20), N=0 Z=1 V=0 C=0, A0+=4, ng_w32(oldA0,0)
 *   dbra:  NG_INSN(12), D0=(D0&~0xffff)|(uint16)(D0-1);
 *          if low16 != 0xffff: cycles-=2, yield if cycles>=next, loop;
 *          else cycles+=2 (fall through)
 *   rts:   NG_INSN(16), PC=pop32()
 * All state is held in locals; ng_w32 is kept so page-map / device writes
 * keep their exact side effects. Flags are written once at exit/yield.
 */
void dense_r_0034EA(void) {
    uint32_t a0 = ng_cpu.a[0];
    uint32_t d0 = ng_cpu.d[0];
    int32_t  cycles = ng_cycles;
    uint32_t icount = ng_icount;
    for (;;) {
        uint32_t ad;
        ad = a0; a0 += 4; dense_w32_fast(ad, 0); cycles += 20; icount++;   /* clr.l x4 */
        ad = a0; a0 += 4; dense_w32_fast(ad, 0); cycles += 20; icount++;
        ad = a0; a0 += 4; dense_w32_fast(ad, 0); cycles += 20; icount++;
        ad = a0; a0 += 4; dense_w32_fast(ad, 0); cycles += 20; icount++;
        cycles += 12; icount++;                                   /* dbra */
        {
            uint16_t cnt = (uint16_t)(d0 - 1u);
            d0 = (d0 & 0xFFFF0000u) | cnt;
            if (cnt != 0xFFFFu) {
                cycles -= 2;
                if (cycles >= ng_next_event) {                     /* yielded */
                    ng_cpu.a[0] = a0; ng_cpu.d[0] = d0;
                    ng_cpu.pc = 0x0034EAu;
                    ng_cpu.n = 0; ng_cpu.z = 1; ng_cpu.v = 0; ng_cpu.c = 0;
                    ng_cycles = cycles; ng_icount = icount;
                    return;
                }
                continue;
            }
            cycles += 2;                                           /* dbra not taken */
        }
        break;
    }
    /* rts */
    cycles += 16; icount++;
    ng_cpu.a[0] = a0; ng_cpu.d[0] = d0;
    ng_cpu.n = 0; ng_cpu.z = 1; ng_cpu.v = 0; ng_cpu.c = 0;
    ng_cycles = cycles; ng_icount = icount;
    ng_cpu.pc = ng_pop32();
}

/* 0xC110E8: the BIOS VRAM-upload loop in r_C110FC (fresh Ymir profile:
 * r_C110FC = 14.9% of all SH-2 instructions). Bit-exact lowering of the C
 * recomp (recomp_12.c:332):
 *   C110E8 move.b D0,$300001
 *   C110EE..C1112A  16x ( move.w D1,(A1) ; swap D1 )
 *   C1112E dbra D7,$c110e8   (yield back to C110E8; on finish fall through)
 *   C11132 jsr $c16d28 (push return C11138)
 * Only PC C110E8 is routed here (C1103A/C11138 stay in generated code).
 * All guest state is kept in locals; ng_w8/ng_w16 retain exact device side
 * effects; flags/cycles/icount are written back once at yield or exit.
 */
void dense_r_C110E8(void) {
    uint32_t d0 = ng_cpu.d[0];
    uint32_t d1 = ng_cpu.d[1];
    uint32_t d7 = ng_cpu.d[7];
    uint32_t a1 = ng_cpu.a[1];
    int32_t  cycles = ng_cycles;
    uint32_t icount = ng_icount;
    uint8_t  n = 0, z = 1, v = 0, c = 0;
    /* Hoist the address classification out of the 16-write inner loop. A1 is
     * fixed here (the LSPC VRAM data port $3C0002); when it is unmapped and
     * matches the VRAM-port pattern, inline the poke directly (bit-exact with
     * dense_w16_fast's VRAM case). Otherwise fall back per word. */
    uint32_t wa = a1 & 0xFFFFFEu;
    int vram_mode = (!ng_wmap[wa >> 16] && (wa & 0xFE0000u) == 0x3C0000u && (wa & 0xEu) == 0x2u);
    for (;;) {
        uint16_t vb = (uint8_t)d0;                       /* C110E8 move.b D0,$300001 */
        cycles += 16; icount++;
        n = (uint8_t)((vb >> 7) & 1u); z = (uint8_t)(vb == 0u); v = 0; c = 0;
        dense_sound_strobe();
        for (int i = 0; i < 16; i++) {                    /* 16x move.w / swap */
            uint16_t vw = (uint16_t)d1;
            cycles += 8; icount++;
            n = (uint8_t)((vw >> 15) & 1u); z = (uint8_t)(vw == 0u); v = 0; c = 0;
            if (vram_mode) {
                uint16_t ad = *ng_dense_lspc_addr;
                uint32_t vi = (ad & 0x8000u) ? (0x8000u | (ad & 0x07FFu)) : ad;
                ng_dense_vram_base[vi] = vw;
                ng_boot_vram_writes++;
                *ng_dense_lspc_addr = (uint16_t)((ad & 0x8000u) | ((ad + *ng_dense_lspc_mod) & 0x7FFFu));
            } else dense_w16_fast(a1, vw);
            cycles += 4; icount++;
            d1 = (d1 >> 16) | (d1 << 16);                 /* swap: ng_logic32 */
            n = (uint8_t)((d1 >> 31) & 1u); z = (uint8_t)(d1 == 0u); v = 0; c = 0;
        }
        cycles += 12; icount++;                           /* C1112E dbra D7 */
        {
            uint16_t cnt = (uint16_t)(d7 - 1u);
            d7 = (d7 & 0xFFFF0000u) | cnt;
            if (cnt != 0xFFFFu) {
                cycles -= 2;
                if (cycles >= ng_next_event) {            /* yield -> C110E8 */
                    ng_cpu.d[0] = d0; ng_cpu.d[1] = d1; ng_cpu.d[7] = d7; ng_cpu.a[1] = a1;
                    ng_cpu.n = n; ng_cpu.z = z; ng_cpu.v = v; ng_cpu.c = c;
                    ng_cycles = cycles; ng_icount = icount;
                    ng_cpu.pc = 0x00C110E8u;
                    return;
                }
                continue;
            }
            cycles += 2;                                  /* dbra not taken */
        }
        break;
    }
    /* C11132: jsr $c16d28, push return C11138 */
    cycles += 20; icount++;
    ng_cpu.d[0] = d0; ng_cpu.d[1] = d1; ng_cpu.d[7] = d7; ng_cpu.a[1] = a1;
    ng_cpu.n = n; ng_cpu.z = z; ng_cpu.v = v; ng_cpu.c = c;
    ng_cycles = cycles; ng_icount = icount;
    ng_push32(0x00C11138u);
    ng_cpu.pc = 0x00C16D28u;
}

/* 0x000834: the word-copy loop in r_000828 (Ymir profile 4.4%). Bit-exact
 * lowering of recomp_14.c:5436 — entry PC 0x834 only:
 *   000834 move.b D0,$300001
 *   00083A move.w (A0)+,(A1)+   (x2088, D0 is the dbra counter)
 *   00083C dbra D0,$834         (yield back to 0x834; finish -> 0x840)
 *   000840 jsr $100000 (push return 0x846)
 * A0 (cart ROM) and A1 (WRAM) are both page-mapped, so reads/writes use the
 * fast inline path; ng_r16/ng_w16 keep that exact behaviour. All guest state
 * is held in locals and written back once at yield or exit.
 */
void dense_r_000834(void) {
    uint32_t d0 = ng_cpu.d[0];
    uint32_t a0 = ng_cpu.a[0];
    uint32_t a1 = ng_cpu.a[1];
    int32_t  cycles = ng_cycles;
    uint32_t icount = ng_icount;
    uint8_t  n = 0, z = 1, v = 0, c = 0;
    for (;;) {
        uint8_t vb = (uint8_t)d0;                        /* move.b D0,$300001 */
        cycles += 16; icount++;
        n = (uint8_t)((vb >> 7) & 1u); z = (uint8_t)(vb == 0u); v = 0; c = 0;
        dense_sound_strobe();
        {
            uint32_t as = a0; a0 += 2;                   /* move.w (A0)+,(A1)+ */
            uint16_t wv = ng_r16(as);
            cycles += 12; icount++;
            n = (uint8_t)((wv >> 15) & 1u); z = (uint8_t)(wv == 0u); v = 0; c = 0;
            ng_w16(a1, wv); a1 += 2;
        }
        cycles += 12; icount++;                          /* dbra D0,$834 */
        {
            uint16_t cnt = (uint16_t)(d0 - 1u);
            d0 = (d0 & 0xFFFF0000u) | cnt;
            if (cnt != 0xFFFFu) {
                cycles -= 2;
                if (cycles >= ng_next_event) {           /* yield -> 0x834 */
                    ng_cpu.d[0] = d0; ng_cpu.a[0] = a0; ng_cpu.a[1] = a1;
                    ng_cpu.n = n; ng_cpu.z = z; ng_cpu.v = v; ng_cpu.c = c;
                    ng_cycles = cycles; ng_icount = icount;
                    ng_cpu.pc = 0x000834u;
                    return;
                }
                continue;
            }
            cycles += 2;                                 /* dbra not taken */
        }
        break;
    }
    /* 0x840: jsr $100000, push return 0x846 */
    cycles += 20; icount++;
    ng_cpu.d[0] = d0; ng_cpu.a[0] = a0; ng_cpu.a[1] = a1;
    ng_cpu.n = n; ng_cpu.z = z; ng_cpu.v = v; ng_cpu.c = c;
    ng_cycles = cycles; ng_icount = icount;
    ng_push32(0x000846u);
    ng_cpu.pc = 0x00100000u;
}

/* 0xC16D20: compact VRAM-fill loop in r_C16D20 (Ymir profile 4.9%).
 * Bit-exact lowering of recomp_01.c:3790 (single dispatched entry):
 *   C16D20 move.w D0,(A0)   (A0 fixed; LSPC auto-increments the VRAM addr)
 *   C16D22 dbra D1,$c16d20  (yield back to C16D20; finish -> rts)
 *   C16D26 rts
 * Reuses dense_w16_fast so the VRAM data-port poke stays inline (bit-exact
 * for any address via the ng_w16_slow fallback). All state in locals.
 */
void dense_r_C16D20(void) {
    uint32_t d0 = ng_cpu.d[0];
    uint32_t d1 = ng_cpu.d[1];
    uint32_t a0 = ng_cpu.a[0];
    int32_t  cycles = ng_cycles;
    uint32_t icount = ng_icount;
    uint8_t  n, z, v = 0, c = 0;
    uint16_t wv = (uint16_t)d0;
    uint32_t wa = a0 & 0xFFFFFEu;                        /* hoist classification (fixed A0) */
    int vram_mode = (!ng_wmap[wa >> 16] && (wa & 0xFE0000u) == 0x3C0000u && (wa & 0xEu) == 0x2u);
    for (;;) {
        cycles += 8; icount++;                           /* move.w D0,(A0) */
        n = (uint8_t)((wv >> 15) & 1u); z = (uint8_t)(wv == 0u); v = 0; c = 0;
        if (vram_mode) {
            uint16_t ad = *ng_dense_lspc_addr;
            uint32_t vi = (ad & 0x8000u) ? (0x8000u | (ad & 0x07FFu)) : ad;
            ng_dense_vram_base[vi] = wv;
            ng_boot_vram_writes++;
            *ng_dense_lspc_addr = (uint16_t)((ad & 0x8000u) | ((ad + *ng_dense_lspc_mod) & 0x7FFFu));
        } else dense_w16_fast(a0, wv);
        cycles += 12; icount++;                          /* dbra D1 */
        {
            uint16_t cnt = (uint16_t)(d1 - 1u);
            d1 = (d1 & 0xFFFF0000u) | cnt;
            if (cnt != 0xFFFFu) {
                cycles -= 2;
                if (cycles >= ng_next_event) {           /* yield -> C16D20 */
                    ng_cpu.d[0] = d0; ng_cpu.d[1] = d1; ng_cpu.a[0] = a0;
                    ng_cpu.n = n; ng_cpu.z = z; ng_cpu.v = v; ng_cpu.c = c;
                    ng_cycles = cycles; ng_icount = icount;
                    ng_cpu.pc = 0x00C16D20u;
                    return;
                }
                continue;
            }
            cycles += 2;                                 /* dbra not taken */
        }
        break;
    }
    cycles += 16; icount++;                              /* rts */
    ng_cpu.d[0] = d0; ng_cpu.d[1] = d1; ng_cpu.a[0] = a0;
    ng_cpu.n = n; ng_cpu.z = z; ng_cpu.v = v; ng_cpu.c = c;
    ng_cycles = cycles; ng_icount = icount;
    ng_cpu.pc = ng_pop32();
}

/* One `movem.l D0-D3,-(A0)` from the r_C11002 fill loops. The generated code
 * (recomp_06.c:5918) charges NG_INSN(8)+32 cycles and 1 icount, writes D3,D2,
 * D1,D0 as two words each while predecrementing A0 by 4 per register, and
 * does NOT touch the CCR. ng_w16 keeps the exact page-table/slow-path writes. */
static inline void dense_movem4(uint32_t* a, uint32_t d0, uint32_t d1,
                                uint32_t d2, uint32_t d3) {
    uint32_t p = *a;
    p -= 4; dense_w32_fast(p, d3);
    p -= 4; dense_w32_fast(p, d2);
    p -= 4; dense_w32_fast(p, d1);
    p -= 4; dense_w32_fast(p, d0);
    *a = p;
}

/* 0xC11050: loop 1 of the BIOS memory-fill in r_C11002 (Ymir profile:
 * r_C11002 = 13.5%). Bit-exact lowering of recomp_06.c:5915:
 *   C11050 move.b D0,$300001 ; C11056..C11072 8x movem.l D0-D3,-(A0)
 *   C11076 dbra D7,$c11050   (yield -> C11050; finish -> fall through)
 * On natural completion, reproduce the C1107A..C11086 setup then hand off to
 * the dispatchable PC C1108A (a real entry in r_C11002's switch). */
void dense_r_C11050(void) {
    uint32_t d0 = ng_cpu.d[0], d1 = ng_cpu.d[1];
    uint32_t d2 = ng_cpu.d[2], d3 = ng_cpu.d[3];
    uint32_t a0 = ng_cpu.a[0], d7 = ng_cpu.d[7];
    int32_t  cycles = ng_cycles;
    uint32_t icount = ng_icount;
    uint8_t  n, z, v = 0, c = 0;
    for (;;) {
        uint8_t vb = (uint8_t)d0;                         /* C11050 move.b */
        cycles += 16; icount++;
        n = (uint8_t)((vb >> 7) & 1u); z = (uint8_t)(vb == 0u); v = 0; c = 0;
        dense_sound_strobe();
        for (int k = 0; k < 8; k++) {                     /* 8x movem.l */
            dense_movem4(&a0, d0, d1, d2, d3);
            cycles += 40; icount++;
        }
        cycles += 12; icount++;                          /* C11076 dbra D7 */
        {
            uint16_t cnt = (uint16_t)(d7 - 1u);
            d7 = (d7 & 0xFFFF0000u) | cnt;
            if (cnt != 0xFFFFu) {
                cycles -= 2;
                if (cycles >= ng_next_event) {           /* yield -> C11050 */
                    ng_cpu.d[0]=d0; ng_cpu.d[1]=d1; ng_cpu.d[2]=d2; ng_cpu.d[3]=d3;
                    ng_cpu.a[0]=a0; ng_cpu.d[7]=d7;
                    ng_cpu.n=n; ng_cpu.z=z; ng_cpu.v=v; ng_cpu.c=c;
                    ng_cycles=cycles; ng_icount=icount;
                    ng_cpu.pc = 0x00C11050u;
                    return;
                }
                continue;
            }
            cycles += 2;                                 /* dbra not taken */
        }
        break;
    }
    /* C1107A..C11086: move.b D0,$3a000f ; lea $402000,A0 ; move.w #$7f,D7 */
    {
        uint8_t vb = (uint8_t)d0;
        cycles += 16; icount++;
        n = (uint8_t)((vb >> 7) & 1u); z = (uint8_t)(vb == 0u); v = 0; c = 0;
        ng_w8(0x003A000Fu, vb);
        cycles += 12; icount++; a0 = 0x00402000u;        /* lea */
        cycles += 8; icount++; d7 = (d7 & 0xFFFF0000u) | 0x007Fu;  /* move.w #7f */
        n = 0; z = 0; v = 0; c = 0;                      /* ng_logic16(0x7f) */
    }
    ng_cpu.d[0]=d0; ng_cpu.d[1]=d1; ng_cpu.d[2]=d2; ng_cpu.d[3]=d3;
    ng_cpu.a[0]=a0; ng_cpu.d[7]=d7;
    ng_cpu.n=n; ng_cpu.z=z; ng_cpu.v=v; ng_cpu.c=c;
    ng_cycles=cycles; ng_icount=icount;
    ng_cpu.pc = 0x00C1108Au;                             /* hand off to loop 2 */
}

/* 0xC1108A: fill loop 2 in r_C11002 (recomp_06.c:5941). 4x movem.l, D7 from
 * 0x7f. On natural completion reproduce C1109E..C110B0 setup, then hand off
 * to dispatchable C110B4. */
void dense_r_C1108A(void) {
    uint32_t d0 = ng_cpu.d[0], d1 = ng_cpu.d[1];
    uint32_t d2 = ng_cpu.d[2], d3 = ng_cpu.d[3];
    uint32_t a0 = ng_cpu.a[0], d7 = ng_cpu.d[7];
    int32_t  cycles = ng_cycles;
    uint32_t icount = ng_icount;
    uint8_t  n, z, v = 0, c = 0;
    n = (ng_cpu.n); z = (ng_cpu.z); v = (ng_cpu.v); c = (ng_cpu.c);
    for (;;) {
        for (int k = 0; k < 4; k++) { dense_movem4_pal(&a0, d0, d1, d2, d3); cycles += 40; icount++; }
        cycles += 12; icount++;                          /* C1109A dbra D7 */
        {
            uint16_t cnt = (uint16_t)(d7 - 1u);
            d7 = (d7 & 0xFFFF0000u) | cnt;
            if (cnt != 0xFFFFu) {
                cycles -= 2;
                if (cycles >= ng_next_event) {
                    ng_cpu.d[0]=d0; ng_cpu.d[1]=d1; ng_cpu.d[2]=d2; ng_cpu.d[3]=d3;
                    ng_cpu.a[0]=a0; ng_cpu.d[7]=d7;
                    ng_cpu.n=n; ng_cpu.z=z; ng_cpu.v=v; ng_cpu.c=c;
                    ng_cycles=cycles; ng_icount=icount;
                    ng_cpu.pc = 0x00C1108Au;
                    return;
                }
                continue;
            }
            cycles += 2;
        }
        break;
    }
    /* C1109E move.b D0,$300001 ; C110A4 move.b D0,$3a001f */
    { uint8_t vb = (uint8_t)d0; cycles += 16; icount++;
      n=(uint8_t)((vb>>7)&1u); z=(uint8_t)(vb==0u); v=0; c=0; dense_sound_strobe(); }
    { uint8_t vb = (uint8_t)d0; cycles += 16; icount++;
      n=(uint8_t)((vb>>7)&1u); z=(uint8_t)(vb==0u); v=0; c=0; ng_w8(0x003A001Fu, vb); }
    cycles += 12; icount++; a0 = 0x00402000u;            /* C110AA lea */
    cycles += 8; icount++;                                /* C110B0 move.w #7f,D7 */
    d7 = (d7 & 0xFFFF0000u) | 0x007Fu; n=0; z=0; v=0; c=0;
    ng_cpu.d[0]=d0; ng_cpu.d[1]=d1; ng_cpu.d[2]=d2; ng_cpu.d[3]=d3;
    ng_cpu.a[0]=a0; ng_cpu.d[7]=d7;
    ng_cpu.n=n; ng_cpu.z=z; ng_cpu.v=v; ng_cpu.c=c;
    ng_cycles=cycles; ng_icount=icount;
    ng_cpu.pc = 0x00C110B4u;
}

/* 0xC110B4: fill loop 3 in r_C11002 (recomp_06.c:5959). 4x movem.l, D7 from
 * 0x7f. On natural completion reproduce C110C8 then the C110CE jsr. */
void dense_r_C110B4(void) {
    uint32_t d0 = ng_cpu.d[0], d1 = ng_cpu.d[1];
    uint32_t d2 = ng_cpu.d[2], d3 = ng_cpu.d[3];
    uint32_t a0 = ng_cpu.a[0], d7 = ng_cpu.d[7];
    int32_t  cycles = ng_cycles;
    uint32_t icount = ng_icount;
    uint8_t  n = ng_cpu.n, z = ng_cpu.z, v = ng_cpu.v, c = ng_cpu.c;
    for (;;) {
        for (int k = 0; k < 4; k++) { dense_movem4_pal(&a0, d0, d1, d2, d3); cycles += 40; icount++; }
        cycles += 12; icount++;                          /* C110C4 dbra D7 */
        {
            uint16_t cnt = (uint16_t)(d7 - 1u);
            d7 = (d7 & 0xFFFF0000u) | cnt;
            if (cnt != 0xFFFFu) {
                cycles -= 2;
                if (cycles >= ng_next_event) {
                    ng_cpu.d[0]=d0; ng_cpu.d[1]=d1; ng_cpu.d[2]=d2; ng_cpu.d[3]=d3;
                    ng_cpu.a[0]=a0; ng_cpu.d[7]=d7;
                    ng_cpu.n=n; ng_cpu.z=z; ng_cpu.v=v; ng_cpu.c=c;
                    ng_cycles=cycles; ng_icount=icount;
                    ng_cpu.pc = 0x00C110B4u;
                    return;
                }
                continue;
            }
            cycles += 2;
        }
        break;
    }
    /* C110C8 move.b D0,$300001 */
    { uint8_t vb = (uint8_t)d0; cycles += 16; icount++;
      n=(uint8_t)((vb>>7)&1u); z=(uint8_t)(vb==0u); v=0; c=0; dense_sound_strobe(); }
    /* C110CE jsr $c16cec: push return C110D2 */
    cycles += 18; icount++;
    ng_cpu.d[0]=d0; ng_cpu.d[1]=d1; ng_cpu.d[2]=d2; ng_cpu.d[3]=d3;
    ng_cpu.a[0]=a0; ng_cpu.d[7]=d7;
    ng_cpu.n=n; ng_cpu.z=z; ng_cpu.v=v; ng_cpu.c=c;
    ng_cycles=cycles; ng_icount=icount;
    ng_push32(0x00C110D2u);
    ng_cpu.pc = 0x00C16CECu;
}

/* 0xC11F9E: VRAM-fill loop in r_C11F78 (Ymir profile 2.8%). Bit-exact
 * lowering of recomp_04.c:500 — dispatched loop-head only (C11F78 setup stays
 * in generated code, which falls through internally; re-entry after a yield
 * dispatches to C11F9E):
 *   C11F9E move.b D0,$300001 ; C11FA4 move.w D0,(A1) ; C11FA6 dbra D7
 *   natural finish -> C11FAA subq.b #1,($ac2,A5) ; C11FAE rts
 * Reproduces ng_sub8 flags (X/C/V/N/Z) exactly for the subq.b tail.
 */
void dense_r_C11F9E(void) {
    uint32_t d0 = ng_cpu.d[0], d7 = ng_cpu.d[7];
    uint32_t a1 = ng_cpu.a[1], a5 = ng_cpu.a[5];
    int32_t  cycles = ng_cycles;
    uint32_t icount = ng_icount;
    uint8_t  n = 0, z = 1, v = 0, c = 0, x = ng_cpu.x;
    uint32_t wa = a1 & 0xFFFFFEu;                             /* hoist (fixed A1 VRAM port) */
    int vram_mode = (!ng_wmap[wa >> 16] && (wa & 0xFE0000u) == 0x3C0000u && (wa & 0xEu) == 0x2u);
    for (;;) {
        { uint8_t vb = (uint8_t)d0; cycles += 16; icount++;   /* move.b D0,$300001 */
          n=(uint8_t)((vb>>7)&1u); z=(uint8_t)(vb==0u); v=0; c=0; dense_sound_strobe(); }
        { uint16_t vw = (uint16_t)d0; cycles += 8; icount++;  /* move.w D0,(A1) */
          n=(uint8_t)((vw>>15)&1u); z=(uint8_t)(vw==0u); v=0; c=0;
          if (vram_mode) {
              uint16_t ad = *ng_dense_lspc_addr;
              uint32_t vi = (ad & 0x8000u) ? (0x8000u | (ad & 0x07FFu)) : ad;
              ng_dense_vram_base[vi] = vw;
              ng_boot_vram_writes++;
              *ng_dense_lspc_addr = (uint16_t)((ad & 0x8000u) | ((ad + *ng_dense_lspc_mod) & 0x7FFFu));
          } else dense_w16_fast(a1, vw); }
        cycles += 12; icount++;                               /* C11FA6 dbra D7 */
        {
            uint16_t cnt = (uint16_t)(d7 - 1u);
            d7 = (d7 & 0xFFFF0000u) | cnt;
            if (cnt != 0xFFFFu) {
                cycles -= 2;
                if (cycles >= ng_next_event) {                /* yield -> C11F9E */
                    ng_cpu.d[0]=d0; ng_cpu.d[7]=d7; ng_cpu.a[1]=a1; ng_cpu.a[5]=a5;
                    ng_cpu.n=n; ng_cpu.z=z; ng_cpu.v=v; ng_cpu.c=c;  /* X unchanged in loop */
                    ng_cycles=cycles; ng_icount=icount;
                    ng_cpu.pc = 0x00C11F9Eu;
                    return;
                }
                continue;
            }
            cycles += 2;
        }
        break;
    }
    /* C11FAA subq.b #1,($ac2,A5): ng_sub8(1,d) sets X/C/V/N/Z */
    {
        uint32_t ad = a5 + 2754u;
        uint8_t d = ng_r8(ad);
        uint8_t r = (uint8_t)(d - 1u);
        x = c = (uint8_t)(1u > d);
        v = (uint8_t)((((1u ^ d) & ((uint32_t)r ^ d)) >> 7) & 1u);
        n = (uint8_t)((r >> 7) & 1u); z = (uint8_t)(r == 0u);
        cycles += 16; icount++;
        ng_w8(ad, r);
    }
    cycles += 16; icount++;                                   /* C11FAE rts */
    ng_cpu.d[0]=d0; ng_cpu.d[7]=d7; ng_cpu.a[1]=a1; ng_cpu.a[5]=a5;
    ng_cpu.x=x; ng_cpu.n=n; ng_cpu.z=z; ng_cpu.v=v; ng_cpu.c=c;
    ng_cycles=cycles; ng_icount=icount;
    ng_cpu.pc = ng_pop32();
}

void dense_r_00A142(void) {
    uint32_t a5 = ng_cpu.a[5];
    uint32_t ad = (a5 + 0x2785u) & 0xFFFFFFu;
    uint8_t* p = ng_rmap[ad >> 16];
    if (!p) { ng_bad_dispatch(ng_cpu.pc); return; }
    uint8_t* q = p + (ad & 0xFFFFu);
    int32_t cycles = ng_cycles;
    uint8_t d;
    for (;;) {
        d = *q;
        cycles += 12;            /* tst.b */
        if (d != 0u) { cycles += 8; break; }   /* beq not taken (-2) */
        cycles += 10;            /* beq taken */
        if (cycles >= ng_next_event) {
            ng_cpu.pc = 0x00A142u;
            ng_cycles = cycles;
            /* flags are irrelevant on a yield (dispatcher re-enters) */
            return;
        }
    }
    /* one-time flag write (tst.b result) */
    ng_cpu.n = (uint8_t)((d >> 7) & 1u);
    ng_cpu.z = (uint8_t)(d == 0u);
    ng_cpu.v = 0; ng_cpu.c = 0;
    ng_cpu.pc = 0x00A148u;
    ng_cycles = cycles;
}
