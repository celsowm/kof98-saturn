/* ng_recomp_host.c - native-recomp (X2) feasibility harness for the real BIOS.
 *
 * Boots the actual Neo Geo BIOS from reset twice over an identical AES bus:
 *   (A) with the generated recompiled routines (native SH-2-style C) plus a
 *       Musashi fallback for addresses with no native entry, and
 *   (B) with the pure Musashi interpreter.
 * Both run EXACTLY the same number of 68000 instructions (the count (A)
 * executed), before the first vblank IRQ, so the architectural state must be
 * identical. Prints any divergence, plus the wall-clock speedup of (A) over (B).
 *
 * This proves the recompiler produces state-equivalent code and measures how
 * much faster native execution is - the gate for real-time speed on Saturn.
 * The recompiled code is generated from the user's own ROM (never committed).
 *
 * Build: see CMake target `ng_recomp_host`.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "m68k.h"
#include <neogeorecomp/cpu.h>
#include <neogeorecomp/bus.h>
#include <neogeorecomp/neogeo.h>

/* ---- AES machine (compact; mirrors src/ng/ng_boot.c's map) ---- */
static uint8_t  s_bios[0x20000];       /* 128 KB system ROM ($C00000) */
static uint8_t  s_vec[0x10000];        /* reset vector page ($000000) */
static uint8_t  s_wram[0x10000];       /* 64 KB work RAM ($100000) */
static uint16_t s_vram[0x10000];       /* 128 KB LSPC VRAM */
static uint16_t s_pal[2][0x1000];      /* 8 KB palette, two banks */
static int      s_pal_bank;
static uint16_t s_addr, s_mod;
static uint8_t  s_p1;
static uint8_t  s_bios_vectors = 1;
static uint8_t  s_pending;

/* Page tables (defined here, not in neogeorecomp's bus.c, to keep it standalone). */
uint8_t *ng_rmap[256];
uint8_t *ng_wmap[256];

static uint32_t vram_index(uint16_t a) { return (a & 0x8000) ? (0x8000u | (a & 0x07FFu)) : a; }

static uint16_t lspc_read(uint32_t addr) {
    switch (addr & 6) {
    case 0: case 2: return s_vram[vram_index(s_addr)];
    case 4: return s_mod;
    default: return 0x0000;
    }
}
static void lspc_write(uint32_t addr, uint16_t v) {
    switch (addr & 0xE) {
    case 0x0: s_addr = v; break;
    case 0x2:
        s_vram[vram_index(s_addr)] = v;
        s_addr = (uint16_t)((s_addr & 0x8000) | ((s_addr + s_mod) & 0x7FFF));
        break;
    case 0x4: s_mod = v; break;
    case 0xC:
        if (v & 1) s_pending &= (uint8_t)~4;
        if (v & 2) s_pending &= (uint8_t)~2;
        if (v & 4) s_pending &= (uint8_t)~1;
        break;
    default: break;
    }
}
static uint16_t pal_read(uint32_t a) { return s_pal[s_pal_bank][(a >> 1) & 0xFFF]; }
static void pal_write(uint32_t a, uint16_t v) { s_pal[s_pal_bank][(a >> 1) & 0xFFF] = v; }

static uint8_t io_read8(uint32_t a) {
    switch (a & 0xFE0000) {
    case 0x300000: if (!(a & 1)) return (uint8_t)~s_p1; if (a & 0x80) return 0x3F; return 0xFF;
    case 0x320000: return (a & 1) ? 0x3F : 0x00;
    case 0x340000: return (a & 1) ? 0xFF : 0xFF;
    case 0x380000: return (a & 1) ? 0xFF : 0x7F;
    default: return 0xFF;
    }
}
static void io_write8(uint32_t a, uint8_t v) {
    if ((a & 0xFE0000) == 0x3A0000 && (a & 1)) {
        int bit = (a >> 4) & 1;
        switch ((a >> 1) & 7) {
        case 1: s_bios_vectors = (uint8_t)!bit; break;   /* SWPBIOS */
        case 7: s_pal_bank = bit ? 0 : 1; break;         /* PALBANK */
        default: break;
        }
    }
}

static uint16_t dev_r16(uint32_t a) {
    if ((a & 0xFE0000) == 0x3C0000) return lspc_read(a);
    if (a >= 0x400000 && a < 0x800000) return pal_read(a);
    if (a >= 0x300000 && a < 0x400000) return (uint16_t)((io_read8(a) << 8) | io_read8(a | 1));
    return 0xFFFF;
}
static uint8_t dev_r8(uint32_t a) {
    if ((a & 0xFE0000) == 0x3C0000 || (a >= 0x400000 && a < 0x800000)) {
        uint16_t w = dev_r16(a & ~1u);
        return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    if (a >= 0x300000 && a < 0x400000) return io_read8(a);
    return 0xFF;
}
static void dev_w16(uint32_t a, uint16_t v) {
    if ((a & 0xFE0000) == 0x3C0000) { lspc_write(a, v); return; }
    if (a >= 0x400000 && a < 0x800000) { pal_write(a, v); return; }
    if (a >= 0x300000 && a < 0x400000) { io_write8(a, (uint8_t)(v >> 8)); io_write8(a | 1, (uint8_t)v); return; }
}
static void dev_w8(uint32_t a, uint8_t v) {
    if ((a & 0xFE0000) == 0x3C0000) { lspc_write(a & ~1u, (uint16_t)(v * 0x0101)); return; }
    if (a >= 0x400000 && a < 0x800000) {
        uint16_t w = pal_read(a & ~1u);
        w = (a & 1) ? (uint16_t)((w & 0xFF00) | v) : (uint16_t)((w & 0x00FF) | (v << 8));
        pal_write(a & ~1u, w);
        return;
    }
    if (a >= 0x300000 && a < 0x400000) { io_write8(a, v); return; }
}

/* ---- slow-path entry points (declared in bus.h) ---- */
uint8_t  ng_r8_slow(uint32_t a) { return dev_r8(a); }
uint16_t ng_r16_slow(uint32_t a) { return dev_r16(a); }
void ng_w8_slow(uint32_t a, uint8_t v) { dev_w8(a, v); }
void ng_w16_slow(uint32_t a, uint16_t v) { dev_w16(a, v); }

/* ---- Musashi hooks (config ng_musashi_conf_boot.h) ---- */
uint64_t g_mus_instrs;
void ng_insn_hook(unsigned int pc) { (void)pc; g_mus_instrs++; }
unsigned int ng_int_ack_handler(unsigned int level) { (void)level; return M68K_INT_ACK_AUTOVECTOR; }

/* Musashi memory callbacks -> this bus (page tables + slow path, shared with
 * the recompiled code so both see identical memory). */
unsigned int m68k_read_memory_8(unsigned int a) { return ng_r8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return ng_r32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { ng_w8(a, (uint8_t)v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { ng_w16(a, (uint16_t)v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { ng_w32(a, v); }
unsigned int m68k_read_immediate_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_immediate_32(unsigned int a) { return ng_r32(a); }
unsigned int m68k_read_pcrelative_8(unsigned int a) { return ng_r8(a); }
unsigned int m68k_read_pcrelative_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_pcrelative_32(unsigned int a) { return ng_r32(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return ng_r32(a); }

/* ---- recompiled routine table (generated; never committed) ---- */
extern const ng_func_entry_t bios_ng_recomp_table[];
extern const size_t bios_ng_recomp_count;
static ng_func_t *s_tab_bios;   /* $C00000-$CFFFFF, indexed by (addr&0x1FFFF)>>1 */

static ng_func_t lookup(uint32_t a) {
    a &= 0xFFFFFF;
    if (a >= 0xC00000 && a < 0xD00000) return s_tab_bios[(a & 0x1FFFF) >> 1];
    return NULL;
}
static void build_table(void) {
    s_tab_bios = (ng_func_t *)calloc(0x10000, sizeof(ng_func_t));
    for (size_t i = 0; i < bios_ng_recomp_count; i++) {
        uint32_t a = bios_ng_recomp_table[i].addr & 0xFFFFFF;
        if (a >= 0xC00000 && a < 0xD00000) s_tab_bios[(a & 0x1FFFF) >> 1] = bios_ng_recomp_table[i].fn;
    }
}

void ng_bad_dispatch(uint32_t pc) {
    fprintf(stderr, "fatal: native routine entered at $%06X which it does not contain\n", pc);
    abort();
}

/* ---- CPU<->Musashi hand-over (same model as neogeorecomp exec.c) ---- */
static void to_musashi(void) {
    m68k_set_reg(M68K_REG_SR, ng_get_sr());
    for (int i = 0; i < 8; i++) m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), CPU.d[i]);
    for (int i = 0; i < 7; i++) m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), CPU.a[i]);
    m68k_set_reg(M68K_REG_A7, CPU.a[7]);
    m68k_set_reg(CPU.s ? M68K_REG_USP : M68K_REG_ISP, CPU.osp);
    m68k_set_reg(M68K_REG_PC, CPU.pc);
    /* The BIOS init path never executes STOP before the first vblank, so the
     * stopped state stays clear; no Musashi glue is needed. */
    m68k_set_irq(0);
}
static void from_musashi(void) {
    uint16_t sr = (uint16_t)m68k_get_reg(NULL, M68K_REG_SR);
    CPU.s = (sr >> 13) & 1; CPU.t = (sr >> 15) & 1; CPU.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
    for (int i = 0; i < 8; i++) CPU.d[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
    for (int i = 0; i < 8; i++) CPU.a[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
    CPU.osp = m68k_get_reg(NULL, CPU.s ? M68K_REG_USP : M68K_REG_ISP);
    CPU.pc = m68k_get_reg(NULL, M68K_REG_PC);
    CPU.stopped = 0;
}

/* ---- one dispatch step ---- */
static long s_interp_instrs, s_native_blocks;
static void step(int interp_only) {
    if (!interp_only) {
        ng_func_t fn = lookup(CPU.pc);
        if (fn) { fn(); s_native_blocks++; return; }
    }
    to_musashi();
    ng_cycles += m68k_execute(1);
    from_musashi();
    s_interp_instrs++;
}

/* ---- machine reset ---- */
static void map_pages(void) {
    memset(ng_rmap, 0, sizeof(ng_rmap));
    memset(ng_wmap, 0, sizeof(ng_wmap));
    ng_rmap[0] = s_vec;                              /* $000000: vector page (read-only) */
    for (uint32_t p = 0x10; p < 0x20; p++) ng_rmap[p] = ng_wmap[p] = s_wram;
    for (uint32_t p = 0xC0; p < 0xD0; p++) ng_rmap[p] = s_bios + ((p & 1) << 16);
}
static void reset_all(void) {
    memset(s_wram, 0, sizeof(s_wram));
    memset(s_vram, 0, sizeof(s_vram));
    memset(s_pal, 0, sizeof(s_pal));
    memset(s_vec, 0xFF, sizeof(s_vec));
    memcpy(s_vec, s_bios, 0x80);
    s_addr = 0; s_mod = 1; s_pal_bank = 0; s_p1 = 0; s_pending = 0; s_bios_vectors = 1;
    map_pages();
    memset(&ng_cpu, 0, sizeof(ng_cpu));
    ng_cpu.s = 1; ng_cpu.ipl = 7;
    ng_cpu.a[7] = ng_r32(0);
    ng_cpu.pc = ng_r32(4);
    ng_cycles = 0; ng_next_event = 0; ng_icount = 0;
    g_mus_instrs = 0;
    s_interp_instrs = 0; s_native_blocks = 0;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
}

static uint32_t checksum(const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static double now_s(void) {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static void swap16(uint8_t *p, size_t n) {
    for (size_t i = 0; i + 1 < n; i += 2) { uint8_t t = p[i]; p[i] = p[i + 1]; p[i + 1] = t; }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <bios.bin> [cycles-before-first-vblank] [repeats]\n", argv[0]); return 2; }
    uint32_t target = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 150000u;
    int repeats = argc > 3 ? atoi(argv[3]) : 2000;

    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    size_t n = fread(s_bios, 1, sizeof(s_bios), f);
    fclose(f);
    if (n < sizeof(s_bios)) { fprintf(stderr, "%s is %zu bytes, need %u\n", argv[1], n, 0x20000u); return 1; }
    swap16(s_bios, sizeof(s_bios));

    build_table();
    printf("reset SSP=%08X PC=%08X  native entries=%zu  target_cycles=%u\n",
           (unsigned)(((uint32_t)s_bios[0] << 24) | ((uint32_t)s_bios[1] << 16) | ((uint32_t)s_bios[2] << 8) | s_bios[3]),
           (unsigned)(((uint32_t)s_bios[4] << 24) | ((uint32_t)s_bios[5] << 16) | ((uint32_t)s_bios[6] << 8) | s_bios[7]),
           bios_ng_recomp_count, target);

    /* (A) native + fallback */
    reset_all();
    double t0 = now_s();
    while (ng_cycles < target) step(0);
    while (!lookup(CPU.pc)) step(0);            /* stop at a routine boundary */
    double t1 = now_s();
    long total = (long)(ng_icount + (uint64_t)s_interp_instrs);
    ng_cpu_t ref = ng_cpu;
    int64_t ref_cycles = ng_cycles;
    uint32_t ref_wram = checksum(s_wram, sizeof(s_wram));
    uint32_t ref_vram = checksum(s_vram, sizeof(s_vram));
    long native_blocks = s_native_blocks, native_interp = s_interp_instrs;

    /* (B) pure interpreter, exactly the same instruction count */
    reset_all();
    double t2 = now_s();
    to_musashi();
    for (long i = 0; i < total; i++)
        ng_cycles += m68k_execute(1);
    from_musashi();
    double t3 = now_s();

    uint32_t cur_wram = checksum(s_wram, sizeof(s_wram));
    uint32_t cur_vram = checksum(s_vram, sizeof(s_vram));

    /* compare */
    int bad = 0;
    for (int i = 0; i < 8; i++) {
        if (ref.d[i] != CPU.d[i]) { printf("  D%d native %08X interp %08X\n", i, ref.d[i], CPU.d[i]); bad = 1; }
        if (ref.a[i] != CPU.a[i]) { printf("  A%d native %08X interp %08X\n", i, ref.a[i], CPU.a[i]); bad = 1; }
    }
    if (ref.pc != CPU.pc) { printf("  PC native %06X interp %06X\n", ref.pc, CPU.pc); bad = 1; }
    if (ref.osp != CPU.osp) { printf("  SP native %08X interp %08X\n", ref.osp, CPU.osp); bad = 1; }
    uint16_t sn = ng_get_sr(), sr = ng_get_sr();
    (void)sn; (void)sr;
    if (ref.s != CPU.s || ref.ipl != CPU.ipl || ref.x != CPU.x || ref.n != CPU.n ||
        ref.z != CPU.z || ref.v != CPU.v || ref.c != CPU.c) {
        printf("  SR/flags native S%u IPL%u X%uN%uZ%uV%uC%u interp S%u IPL%u X%uN%uZ%uV%uC%u\n",
               ref.s, ref.ipl, ref.x, ref.n, ref.z, ref.v, ref.c,
               CPU.s, CPU.ipl, CPU.x, CPU.n, CPU.z, CPU.v, CPU.c);
        bad = 1;
    }
    if (ref_wram != cur_wram) { printf("  WRAM checksum native %08X interp %08X\n", ref_wram, cur_wram); bad = 1; }
    if (ref_vram != cur_vram) { printf("  VRAM checksum native %08X interp %08X\n", ref_vram, cur_vram); bad = 1; }

    /* timing loops: average over `repeats` identical workloads */
    double tn = 0.0, ti = 0.0, tr = 0.0;
    if (repeats > 0) {
        double r0 = now_s();
        for (int r = 0; r < repeats; r++) reset_all();
        tr = (now_s() - r0) / repeats;              /* reset-only cost */

        double a = now_s();
        for (int r = 0; r < repeats; r++) {
            reset_all();
            while (ng_cycles < target) step(0);
            while (!lookup(CPU.pc)) step(0);
        }
        tn = (now_s() - a) / repeats;

        double b = now_s();
        for (int r = 0; r < repeats; r++) {
            reset_all();
            to_musashi();
            for (long i = 0; i < total; i++)
                ng_cycles += m68k_execute(1);
            from_musashi();
        }
        ti = (now_s() - b) / repeats;
    } else {
        tn = (t1 - t0);
        ti = (t3 - t2);
    }
    printf("native: %ld instructions (%ld blocks, %ld interpreted), cycles=%lld, %.6f s/run\n",
           total, native_blocks, native_interp, (long long)ref_cycles, tn);
    printf("interp: %ld instructions, cycles=%lld, %.6f s/run\n",
           total, (long long)ng_cycles, ti);
    printf("reset overhead: %.6f s/run\n", tr);
    printf("speedup (end-to-end): %.2fx\n", tn > 0 ? ti / tn : 0.0);
    {
        double nc = tn - tr, ic = ti - tr;
        printf("speedup (compute-only): %.2fx  (~%.0f M native instr/s)\n",
               nc > 0 ? ic / nc : 0.0, nc > 0 ? total / nc / 1e6 : 0.0);
    }
    printf("state: %s\n", bad ? "MISMATCH" : "MATCH (native == interpreter)");
    return bad ? 1 : 0;
}