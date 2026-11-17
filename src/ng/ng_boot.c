/* ng_boot.c - compact freestanding Neo Geo 68000 system (see ng_boot.h).
 *
 * Reuses the upstream Musashi core (deps/.../ext/musashi) as the 68000 and
 * reimplements the AES 68000 memory map, LSPC, palette and $300000 I/O with the
 * same semantics as neogeorecomp (bus.c/lspc.c/io.c/video.c), but with no libc,
 * no allocation and no video renderer. It boots the real system BIOS.
 */
/* Recomp-only target: no Musashi linked (the BIOS recomp slice covers 100 % of
 * executed instructions), so the whole 589 KB interpreter table set is dropped
 * and the recompiled BIOS fits in WRAM-H. A dispatch miss is a hard error.
 * Must be decided before ng_boot.h so its recomp counter externs are visible. */
#if defined(NG_BOOT_RECOMP_ONLY) && !defined(NG_BOOT_RECOMP)
#define NG_BOOT_RECOMP
#endif

#include "ng_boot.h"
#ifndef NG_BOOT_RECOMP_ONLY
#include "m68k.h"       /* interpreter only; the recomp-only build never includes it */
#endif
#include <stddef.h>

#ifdef NG_BOOT_RECOMP
/* Native recomp dispatch (X2): reuse the recomp runtime's CPU file, page tables
 * and inline helpers, and the generated routine table (from the user's ROM). */
#include <neogeorecomp/bus.h>   /* ng_rmap/ng_wmap, ng_r8.., ng_push/pop */
#include <neogeorecomp/cpu.h>   /* ng_cpu, ng_cycles, ng_set_sr, inline ALU */
#include <neogeorecomp/neogeo.h>
uint8_t  ng_boot_read8(uint32_t a);
uint16_t ng_boot_read16(uint32_t a);
uint32_t ng_boot_read32(uint32_t a);
void     ng_boot_write8(uint32_t a, uint8_t v);
void     ng_boot_write16(uint32_t a, uint16_t v);
void     ng_boot_write32(uint32_t a, uint32_t v);
#endif

/* On Saturn, place the large Neo Geo RAM/ROM images in Work RAM Low (they do not
 * fit alongside Musashi's tables in Work RAM High). On the host, plain .bss. */
#ifdef KOF_NG_SATURN
#define NG_RAM __attribute__((section(".wram_l")))
#else
#define NG_RAM
#endif

/* ---- memory (all big-endian, as the 68000 sees it) ---- */
static uint8_t  s_bios[0x20000] NG_RAM;    /* 128 KB system ROM ($C00000) */
static uint8_t  s_vec[0x10000] NG_RAM;     /* reset vector page ($000000) */
static uint8_t  s_wram[0x10000] NG_RAM;    /* 64 KB work RAM ($100000) */
static uint8_t  s_backup[0x10000] NG_RAM;  /* 64 KB backup RAM ($D00000, MVS) */
static uint16_t s_vram[0x10000] NG_RAM;    /* 128 KB LSPC VRAM */
static uint16_t s_pal[2][0x1000] NG_RAM;   /* 8 KB palette, two banks */
static int      s_pal_bank;

/* ---- LSPC ---- */
static uint16_t s_addr, s_mod;

/* Exported pointer aliases so a single dense handler can inline the LSPC
 * VRAM data-port poke (lspc_write case 2) without exposing device state in
 * bus.h — doing it there duplicated code at every ng_w16 call site and blew
 * the WRAM-H budget. Unique names avoid colliding with the static s_vram /
 * s_addr in the host-recomp and lspc translation units. */
uint16_t* const ng_dense_vram_base = s_vram;
uint16_t* const ng_dense_lspc_addr = &s_addr;
uint16_t* const ng_dense_lspc_mod  = &s_mod;
uint16_t* const ng_dense_pal_base  = &s_pal[0][0];
int*      const ng_dense_pal_bank  = &s_pal_bank;

static uint8_t  s_anim_speed, s_anim_frames, s_anim_disabled, s_anim_counter;
static uint8_t  s_timer_ctrl;
static uint32_t s_timer_reload;
static int64_t  s_timer_deadline;
enum { TMR_IRQ_ENABLE = 0x10, TMR_LOAD_ON_LOW = 0x20, TMR_LOAD_VBLANK = 0x40, TMR_LOAD_REPEAT = 0x80 };

/* ---- I/O ---- */
static uint8_t s_p1, s_p2, s_start1, s_start2, s_select1, s_select2;
static uint8_t s_coin1, s_coin2, s_service, s_test;
static uint8_t s_bios_vectors = 1, s_backup_locked = 1;
static int     s_mvs;
static uint8_t s_sound_cmd, s_sound_reply;
static uint8_t s_fix_bios;

/* Optional cartridge (P ROM) ----
 * When a cart is attached, the 68000 fixed bank ($000000) and bank window
 * ($200000) read guest P ROM from the CPU-mapped 4 MiB expansion cart (two 2 MiB
 * banks) instead of internal RAM. Layout: P1 (1 MiB) at cart 0, then P2 banks at
 * 0x100000 + bank*0x100000. Without a cart, $000000 is the BIOS vector page. */
static const uint8_t* s_cart_bank[2];
static uint32_t s_cart_bank_bytes[2];
static uint8_t  s_p_bank;                 /* selected P2 bank (0..7) */

void ng_boot_set_cart(const uint8_t* bank0, uint32_t bytes0, const uint8_t* bank1, uint32_t bytes1) {
    s_cart_bank[0] = bank0; s_cart_bank_bytes[0] = bytes0;
    s_cart_bank[1] = bank1; s_cart_bank_bytes[1] = bytes1;
}

/* Feed controller state (active high). Start is P1 start; the reference script
 * format also drives select/coin/service/test. */
void ng_boot_set_start(int on) { s_start1 = (uint8_t)(on ? 1 : 0); }
void ng_boot_set_start2(int on) { s_start2 = (uint8_t)(on ? 1 : 0); }
void ng_boot_set_select1(int on) { s_select1 = (uint8_t)(on ? 1 : 0); }
void ng_boot_set_select2(int on) { s_select2 = (uint8_t)(on ? 1 : 0); }
void ng_boot_set_coin1(int on) { s_coin1 = (uint8_t)(on ? 1 : 0); }
void ng_boot_set_coin2(int on) { s_coin2 = (uint8_t)(on ? 1 : 0); }
void ng_boot_set_service(int on) { s_service = (uint8_t)(on ? 1 : 0); }
void ng_boot_set_test(int on) { s_test = (uint8_t)(on ? 1 : 0); }
/* Active-high pad bytes, same bit order as the reference ng_input_t:
 * bit 0 up, 1 down, 2 left, 3 right, 4 A, 5 B, 6 C, 7 D. */
void ng_boot_set_p1(uint8_t v) { s_p1 = v; }
void ng_boot_set_p2(uint8_t v) { s_p2 = v; }

static uint8_t cart_read8(uint32_t off) {
    if (s_cart_bank[0] && off < s_cart_bank_bytes[0]) return s_cart_bank[0][off];
    if (s_cart_bank[1]) {
        uint32_t o2 = off - s_cart_bank_bytes[0];
        if (o2 < s_cart_bank_bytes[1]) return s_cart_bank[1][o2];
    }
    return 0xFF;                          /* open bus */
}

/* Bank-handshake debug log (instrumentation). Declared early; defined below. */

/* ---- timing / IRQ ---- */
#ifdef KOF_32BIT_CYCLES
int32_t ng_cycles;
#else
int64_t ng_cycles;
#endif
int64_t ng_frame_start;
uint8_t ng_irq_pending;

/* ---- stats ---- */
volatile uint32_t ng_boot_instructions;
volatile uint32_t ng_boot_frames;
volatile uint32_t ng_boot_vblanks;
volatile uint32_t ng_boot_timer_irqs;
volatile uint32_t ng_boot_vram_writes;
volatile uint32_t ng_boot_pal_writes;
volatile uint32_t ng_boot_bios_instr;
volatile uint32_t ng_boot_io_writes;
volatile uint32_t ng_boot_last_pc;
volatile uint32_t ng_boot_cart_instr;      /* PC in P1/P2 cart ($000000 / $200000) */
volatile uint32_t ng_boot_wram_instr;      /* PC in work RAM ($100000) */
volatile uint32_t ng_boot_irq_taken;
volatile uint32_t ng_boot_status_reads;
volatile uint32_t ng_boot_status_start_reads;       /* interrupts taken by the 68000 */

#ifndef NG_BOOT_RECOMP_ONLY
unsigned int ng_int_ack_handler(unsigned int level) {
    (void)level;
    ng_boot_irq_taken++;
    return M68K_INT_ACK_AUTOVECTOR;       /* autovector (Neo Geo) */
}
#endif /* !NG_BOOT_RECOMP_ONLY */

static uint32_t s_initial_ssp, s_initial_pc;

/* Wrap-safe elapsed guest cycles since the frame start. With KOF_32BIT_CYCLES
 * ng_cycles is int32 and wraps at 2^31 (~10591 frames); ng_frame_start is the
 * int64 epoch, so truncate the subtraction to int32 before dividing. Without
 * this, ng_vpos goes hugely negative after the wrap and the guest's raster
 * status read breaks. */
static int64_t ng_vpos(void) {
#ifdef KOF_32BIT_CYCLES
    return (int64_t)(int32_t)(ng_cycles - (int32_t)ng_frame_start) / 768;
#else
    return (ng_cycles - ng_frame_start) / 768;
#endif
}
static int ng_irq_level(void) {
    return (ng_irq_pending & 4) ? 3 : (ng_irq_pending & 2) ? 2 : (ng_irq_pending & 1) ? 1 : 0;
}

/* ---- LSPC ---- */
static uint32_t vram_index(uint16_t a) { return (a & 0x8000) ? (0x8000u | (a & 0x07FFu)) : a; }

static void timer_load(int64_t from) {
    if (s_timer_reload == 0xFFFFFFFFu) { s_timer_deadline = INT64_MAX; return; }
    s_timer_deadline = from + ((int64_t)s_timer_reload + 1) * 2;
}
static void timer_fire(void) {
    int64_t at = s_timer_deadline;
    s_timer_deadline = INT64_MAX;
    if (s_timer_ctrl & TMR_IRQ_ENABLE) { ng_irq_pending |= 2; ng_boot_timer_irqs++; }
    if (s_timer_ctrl & TMR_LOAD_REPEAT) timer_load(at);
}

/* Per-field service hook (see ng_boot.h): fires from the VBlank entry below. */
static ng_boot_slice_fn s_field_fn;

static void lspc_vblank(void) {
    ng_irq_pending |= 1;
    ng_boot_vblanks++;
    if (!s_anim_disabled) {
        if (s_anim_frames == 0) { s_anim_frames = s_anim_speed; s_anim_counter++; }
        else s_anim_frames--;
    }
    if (s_timer_ctrl & TMR_LOAD_VBLANK) timer_load(ng_cycles);
    if (s_field_fn) s_field_fn();
}
static uint16_t lspc_read(uint32_t addr) {
    switch (addr & 0x6) {
    case 0x0:
    case 0x2: return s_vram[vram_index(s_addr)];
    case 0x4: return s_mod;
    default: {
        int v = (int)ng_vpos() + 0x100;
        if (v >= 0x200) v -= 264;
        return (uint16_t)((v << 7) | (s_anim_counter & 7));
    }
    }
}
static void lspc_write(uint32_t addr, uint16_t v) {
    switch (addr & 0xE) {
    case 0x0: s_addr = v; break;
    case 0x2:
        s_vram[vram_index(s_addr)] = v; ng_boot_vram_writes++;
        s_addr = (uint16_t)((s_addr & 0x8000) | ((s_addr + s_mod) & 0x7FFF));
        break;
    case 0x4: s_mod = v; break;
    case 0x6:
        s_anim_speed = (uint8_t)(v >> 8);
        s_anim_disabled = (v & 0x08) != 0;
        s_timer_ctrl = (uint8_t)(v & 0xF0);
        break;
    case 0x8: s_timer_reload = (s_timer_reload & 0x0000FFFFu) | ((uint32_t)v << 16); break;
    case 0xA:
        s_timer_reload = (s_timer_reload & 0xFFFF0000u) | v;
        if (s_timer_ctrl & TMR_LOAD_ON_LOW) timer_load(ng_cycles);
        break;
    case 0xC:
        if (v & 1) ng_irq_pending &= (uint8_t)~4;
        if (v & 2) ng_irq_pending &= (uint8_t)~2;
        if (v & 4) ng_irq_pending &= (uint8_t)~1;
#ifndef NG_BOOT_RECOMP
        m68k_set_irq((unsigned)ng_irq_level());   /* recomp dispatch owns IRQ */
#endif
        break;
    default: break;
    }
}

/* ---- palette ---- */
static uint16_t pal_read(uint32_t addr) { return s_pal[s_pal_bank][(addr >> 1) & 0xFFF]; }
static void pal_write(uint32_t addr, uint16_t v) {
    s_pal[s_pal_bank][(addr >> 1) & 0xFFF] = v;
    ng_boot_pal_writes++;
}

/* ---- I/O ---- */
static void system_latch(uint32_t a) {
    int bit = (a >> 4) & 1;
    switch ((a >> 1) & 7) {
    case 0: break;
    case 1: s_bios_vectors = (uint8_t)!bit; break;   /* SWPBIOS: no P ROM present */
    case 5: s_fix_bios = (uint8_t)!bit; break;        /* BRDFIX/CRTFIX */
    case 6: s_backup_locked = (uint8_t)!bit; break;   /* SRAM lock */
    case 7: s_pal_bank = bit ? 0 : 1; break;          /* PALBANK */
    default: break;
    }
}
static uint8_t status_a(void) {
    uint8_t v = 0x3F;
    if (s_coin1) v &= (uint8_t)~0x01;
    if (s_coin2) v &= (uint8_t)~0x02;
    if (s_service) v &= (uint8_t)~0x04;
    return v;
}
static uint8_t status_b(void) {
    uint8_t v = 0x7F;
    ng_boot_status_reads++;
    if (s_start1) { v &= (uint8_t)~0x01; ng_boot_status_start_reads++; }
    if (s_select1) v &= (uint8_t)~0x02;
    if (s_start2) v &= (uint8_t)~0x04;
    if (s_select2) v &= (uint8_t)~0x08;
    return (uint8_t)(v | (s_mvs ? 0x80 : 0));
}

static uint8_t io_read8(uint32_t a) {
    switch (a & 0xFE0000) {
    case 0x300000:
        if (!(a & 1)) return (uint8_t)~s_p1;
        if (a & 0x80) return (uint8_t)(0x3F | (s_test ? 0 : 0x80));
        return 0xFF;
    case 0x320000: return (a & 1) ? status_a() : s_sound_reply;
    case 0x340000: return (a & 1) ? 0xFF : (uint8_t)~s_p2;
    case 0x380000: return (a & 1) ? 0xFF : status_b();
    default: return 0xFF;
    }
}
static void io_write8(uint32_t a, uint8_t v) {
    ng_boot_io_writes++;
    switch (a & 0xFE0000) {
    case 0x300000: break;
    case 0x320000: if (!(a & 1)) { s_sound_cmd = v; s_sound_reply = 0; } break;
    case 0x380000: break;
    case 0x3A0000: if (a & 1) system_latch(a); break;
    default: break;
    }
}

/* ---- bus ---- */
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

uint8_t ng_boot_read8(uint32_t a) {
    a &= 0xFFFFFF;
    if (a < 0x100000) {                       /* fixed bank / reset vectors */
        if (s_cart_bank[0]) {
            if (s_bios_vectors && a < 0x80) return s_bios[a];
            return cart_read8(a);
        }
        return (a < 0x10000) ? s_vec[a] : 0xFF;
    }
    if (a < 0x200000) return s_wram[a & 0xFFFF];
    if (a < 0x300000) {                        /* bank window (P2) */
        if (s_cart_bank[0]) return cart_read8(0x100000u + (uint32_t)s_p_bank * 0x100000u + (a - 0x200000));
        return 0xFF;
    }
    if (a < 0x400000) return dev_r8(a);
    if (a < 0x800000) return dev_r8(a);
    if (a >= 0xC00000 && a < 0xD00000) return s_bios[a & 0x1FFFF];
    if (s_mvs && a >= 0xD00000 && a < 0xE00000) return s_backup[a & 0xFFFF];
    return 0xFF;
}
uint16_t ng_boot_read16(uint32_t a) {
    a &= 0xFFFFFE;
    if (a < 0x100000) {
        if (s_cart_bank[0]) {
            if (s_bios_vectors && a < 0x80) return (uint16_t)((s_bios[a] << 8) | s_bios[a + 1]);
            return (uint16_t)((cart_read8(a) << 8) | cart_read8(a + 1));
        }
        if (a < 0x10000) return (uint16_t)((s_vec[a] << 8) | s_vec[a + 1]);
        return 0xFFFF;
    }
    if (a < 0x200000) { const uint8_t* p = s_wram + (a & 0xFFFF); return (uint16_t)((p[0] << 8) | p[1]); }
    if (a < 0x300000) {
        if (s_cart_bank[0]) {
            uint32_t off = 0x100000u + (uint32_t)s_p_bank * 0x100000u + (a - 0x200000);
            uint16_t rv = (uint16_t)((cart_read8(off) << 8) | cart_read8(off + 1));
            return rv;
        }
        return 0xFFFF;
    }
    if (a < 0x800000) return dev_r16(a);
    if (a >= 0xC00000 && a < 0xD00000) { const uint8_t* p = s_bios + (a & 0x1FFFF); return (uint16_t)((p[0] << 8) | p[1]); }
    if (s_mvs && a >= 0xD00000 && a < 0xE00000) { const uint8_t* p = s_backup + (a & 0xFFFF); return (uint16_t)((p[0] << 8) | p[1]); }
    return 0xFFFF;
}
uint32_t ng_boot_read32(uint32_t a) { return ((uint32_t)ng_boot_read16(a) << 16) | ng_boot_read16(a + 2); }


void ng_boot_write8(uint32_t a, uint8_t v) {
    a &= 0xFFFFFF;
    if (a >= 0x2FFFF0 && a < 0x300000) { s_p_bank = (uint8_t)(v & 7); return; }  /* bank switch */
    if (a < 0x100000) return;                      /* ROM */
    if (a < 0x200000) { s_wram[a & 0xFFFF] = v; return; }
    if (a < 0x300000) return;                      /* bank ROM */
    if (a < 0x800000) { dev_w8(a, v); return; }
    if (s_mvs && a >= 0xD00000 && a < 0xE00000 && !s_backup_locked) s_backup[a & 0xFFFF] = v;
}
void ng_boot_write16(uint32_t a, uint16_t v) {
    a &= 0xFFFFFE;
    if (a >= 0x2FFFF0 && a < 0x300000) { s_p_bank = (uint8_t)(v & 7); return; }  /* bank switch */
    if (a < 0x100000) return;
    if (a < 0x200000) { uint8_t* p = s_wram + (a & 0xFFFF); p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; return; }
    if (a < 0x300000) return;
    if (a < 0x800000) { dev_w16(a, v); return; }
    if (s_mvs && a >= 0xD00000 && a < 0xE00000 && !s_backup_locked) {
        uint8_t* p = s_backup + (a & 0xFFFF); p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
    }
}
void ng_boot_write32(uint32_t a, uint32_t v) { ng_boot_write16(a, (uint16_t)(v >> 16)); ng_boot_write16(a + 2, (uint16_t)v); }

/* ---- Musashi callbacks ---- */
unsigned int m68k_read_memory_8(unsigned int a) { return ng_boot_read8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return ng_boot_read16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return ng_boot_read32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { ng_boot_write8(a, (uint8_t)v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { ng_boot_write16(a, (uint16_t)v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { ng_boot_write32(a, v); }
unsigned int m68k_read_immediate_16(unsigned int a) { return ng_boot_read16(a); }
unsigned int m68k_read_immediate_32(unsigned int a) { return ng_boot_read32(a); }
unsigned int m68k_read_pcrelative_8(unsigned int a) { return ng_boot_read8(a); }
unsigned int m68k_read_pcrelative_16(unsigned int a) { return ng_boot_read16(a); }
unsigned int m68k_read_pcrelative_32(unsigned int a) { return ng_boot_read32(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return ng_boot_read16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return ng_boot_read32(a); }

/* Called for every instruction by m68ki_instr_hook (compile-time hook). */
#ifdef NG_BOOT_PC_PROFILE
/* Host-only guest PC profiler (X1 work package). Covers the cart/WRAM window
 * (0x000000-0x2FFFFF) and the BIOS (0xC00000-0xC1FFFF) at word granularity,
 * plus 4 KB page hit counts for hot-set sizing. Never built for Saturn. */
#include <stdio.h>
#define NG_PROF_CART_BYTES 0x300000u
#define NG_PROF_CART_PAGES (NG_PROF_CART_BYTES >> 12)
#define NG_PROF_BIOS_BYTES 0x20000u
#define NG_PROF_BIOS_PAGES (NG_PROF_BIOS_BYTES >> 12)
#define NG_PROF_BITS ((NG_PROF_CART_BYTES + NG_PROF_BIOS_BYTES) >> 1)
#define NG_PROF_PAGES (NG_PROF_CART_PAGES + NG_PROF_BIOS_PAGES)
#define NG_PROF_LINES ((NG_PROF_CART_BYTES + NG_PROF_BIOS_BYTES) >> 6)
static uint8_t s_prof_words[NG_PROF_BITS / 8];
static uint32_t s_prof_page_hits[NG_PROF_PAGES];
static uint32_t s_prof_page_words[NG_PROF_PAGES];
static uint32_t s_prof_line_hits[NG_PROF_LINES];
static uint64_t s_prof_calls;

static void prof_record(uint32_t pc) {
    uint32_t bit, page;
    pc &= 0xFFFFFFu;
    if (pc < NG_PROF_CART_BYTES) {
        bit = pc >> 1;
        page = pc >> 12;
    } else if (pc >= 0xC00000u && pc < 0xC00000u + NG_PROF_BIOS_BYTES) {
        bit = (NG_PROF_CART_BYTES >> 1) + ((pc - 0xC00000u) >> 1);
        page = NG_PROF_CART_PAGES + ((pc - 0xC00000u) >> 12);
    } else {
        return;
    }
    uint8_t mask = (uint8_t)(1u << (bit & 7u));
    if (!(s_prof_words[bit >> 3] & mask)) {
        s_prof_words[bit >> 3] |= mask;
        s_prof_page_words[page]++;       /* distinct instruction words in this page */
    }
    s_prof_page_hits[page]++;
    s_prof_line_hits[bit >> 5]++;        /* 64-byte line, finer hot-loop view */
    s_prof_calls++;
}
void ng_boot_profile_reset(void) {
    for (unsigned i = 0; i < sizeof(s_prof_words); i++) s_prof_words[i] = 0;
    for (unsigned i = 0; i < NG_PROF_PAGES; i++) s_prof_page_hits[i] = 0;
    for (unsigned i = 0; i < NG_PROF_PAGES; i++) s_prof_page_words[i] = 0;
    for (unsigned i = 0; i < NG_PROF_LINES; i++) s_prof_line_hits[i] = 0;
    s_prof_calls = 0;
}
void ng_boot_profile_dump(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t distinct = 0, pages = 0;
    for (unsigned i = 0; i < sizeof(s_prof_words); i++) {
        uint8_t v = s_prof_words[i];
        while (v) { distinct += v & 1u; v >>= 1; }
    }
    for (unsigned i = 0; i < NG_PROF_PAGES; i++) pages += s_prof_page_hits[i] != 0;
    fprintf(f, "# kof-guest-profile-v1\n");
    fprintf(f, "instructions=%llu\ndistinct_words=%u\ndistinct_pages=%u\n",
            (unsigned long long)s_prof_calls, distinct, pages);
    for (unsigned i = 0; i < NG_PROF_PAGES; i++) {
        if (!s_prof_page_hits[i]) continue;
        uint32_t base = i < NG_PROF_CART_PAGES ? (i << 12)
                                               : (0xC00000u + ((i - NG_PROF_CART_PAGES) << 12));
        fprintf(f, "page %06X hits %u words %u\n", base, s_prof_page_hits[i],
                s_prof_page_words[i]);
    }
    for (unsigned i = 0; i < NG_PROF_LINES; i++) {
        if (!s_prof_line_hits[i]) continue;
        uint32_t base = i < (NG_PROF_CART_BYTES >> 6) ? (i << 6)
                                                       : (0xC00000u + ((i - (NG_PROF_CART_BYTES >> 6)) << 6));
        fprintf(f, "line %06X hits %u\n", base, s_prof_line_hits[i]);
    }
    fclose(f);
}
/* Word-address of a profiler bit; the two windows are contiguous in the bitmap. */
static uint32_t prof_bit_addr(uint32_t bit) {
    if (bit < (NG_PROF_CART_BYTES >> 1)) return bit << 1;
    return 0xC00000u + ((bit - (NG_PROF_CART_BYTES >> 1)) << 1);
}
void ng_boot_profile_dump_words(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "# kof-guest-words-v1\n");
    uint32_t run_start = 0;
    int in_run = 0;
    for (uint32_t bit = 0; bit <= NG_PROF_BITS; bit++) {
        int set = 0;
        if (bit < NG_PROF_BITS)
            set = (s_prof_words[bit >> 3] >> (bit & 7u)) & 1u;
        if (set && !in_run) { run_start = bit; in_run = 1; }
        else if (!set && in_run) {
            fprintf(f, "%06X-%06X\n", prof_bit_addr(run_start), prof_bit_addr(bit - 1));
            in_run = 0;
        }
    }
    fclose(f);
}
#endif /* NG_BOOT_PC_PROFILE */

void ng_insn_hook(unsigned int pc) {
    ng_boot_instructions++;
    ng_boot_last_pc = pc;
#ifdef NG_BOOT_PC_PROFILE
    prof_record(pc);
#endif
    if (pc >= 0xC00000 && pc < 0xD00000) ng_boot_bios_instr++;
    else if (pc >= 0x100000 && pc < 0x200000) ng_boot_wram_instr++;
    else if (pc < 0x100000 || (pc >= 0x200000 && pc < 0x300000)) ng_boot_cart_instr++;
}

#ifdef NG_BOOT_RECOMP
/* ---- native recomp dispatch (X2) ----
 * Executes recompiled 68000 routines (generated from the user's own ROM) as
 * native C, falling back to Musashi for addresses with no native entry. IRQ is
 * taken at routine boundaries (like exec.c), not per instruction, so the guest
 * sees real 68000 exceptions. CPU state lives in ng_cpu; Musashi is synced in and
 * out only around fallback instructions. The entry table is sorted, so lookup is
 * a binary search (no 512 KB pointer table). */
extern const ng_func_entry_t bios_ng_recomp_table[];
extern const size_t bios_ng_recomp_count;

ng_cpu_t ng_cpu;
#ifdef KOF_32BIT_CYCLES
int32_t  ng_next_event;
#else
int64_t  ng_next_event;
#endif
#ifdef KOF_32BIT_CYCLES
uint32_t ng_icount;
#else
uint64_t ng_icount;
#endif
uint8_t *ng_rmap[256];
uint8_t *ng_wmap[256];
static int s_recomp_disabled;
volatile uint32_t ng_boot_recomp_bad;
volatile uint32_t ng_boot_recomp_blocks;   /* native routines entered */
volatile uint32_t ng_boot_recomp_interp;   /* instructions run in Musashi */

uint8_t  ng_r8_slow(uint32_t a) { return ng_boot_read8(a); }
uint16_t ng_r16_slow(uint32_t a) { return ng_boot_read16(a); }
/* Ymir profile: ng_w8_slow 4.2% + io_write8 1.6%. The dominant 8-bit writes
 * are the sound-command strobes `move.b D0,$300001` in every BIOS VRAM
 * uploader. In io_write8, every address in $300000-$30FFFF hits
 * `case 0x300000: break` — only ng_boot_io_writes increments. Hoist that
 * exact behaviour ahead of the ng_boot_write8->dev_w8->io_write8 chain;
 * all other addresses fall through unchanged. */
void ng_w8_slow(uint32_t a, uint8_t v) {
    a &= 0xFFFFFFu;
    if ((a & 0xFE0000u) == 0x300000u) { ng_boot_io_writes++; return; }
    ng_boot_write8(a, v);
}

/* Ymir SH-2 profile: ng_w16_slow is 20.5% of all instructions (the compiler
 * inlines the whole ng_boot_write16 -> dev_w16 range chain into it). Guest
 * counters show 14.2M VRAM + 5.8M palette writes dominate that chain. Hoist
 * bit-exact fast paths for those two regions ahead of the generic chain:
 * both are exactly what dev_w16->lspc_write(case 2) / dev_w16->pal_write do,
 * including the same stat-counter increments. All other addresses fall
 * through to the original ng_boot_write16, so behaviour is unchanged. */
void ng_w16_slow(uint32_t a, uint16_t v) {
    a &= 0xFFFFFE;
    if (a >= 0x400000u && a < 0x800000u) {           /* palette (dev_w16->pal_write) */
        s_pal[s_pal_bank][(a >> 1) & 0xFFFu] = v;
        ng_boot_pal_writes++;
        return;
    }
    if ((a & 0xFE0000u) == 0x3C0000u && (a & 0xEu) == 0x2u) {  /* LSPC VRAM data (lspc_write case 2) */
        s_vram[vram_index(s_addr)] = v;
        ng_boot_vram_writes++;
        s_addr = (uint16_t)((s_addr & 0x8000u) | ((s_addr + s_mod) & 0x7FFFu));
        return;
    }
    ng_boot_write16(a, v);
}

void ng_set_sr(uint16_t sr) {
    uint8_t s = (sr >> 13) & 1;
    if (s != ng_cpu.s) { uint32_t t = ng_cpu.a[7]; ng_cpu.a[7] = ng_cpu.osp; ng_cpu.osp = t; ng_cpu.s = s; }
    ng_cpu.t = (sr >> 15) & 1;
    ng_cpu.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
}
void ng_exception(int vector, uint32_t ret_pc) {
    uint16_t old = ng_get_sr();
    ng_set_sr((uint16_t)((old | 0x2000) & ~0x8000));
    /* The recompiler passes the address of the faulting instruction.  A 68000
     * exception frame holds the PC after it; ILLEGAL/LINEA/LINEF are all
     * 2-byte opcodes.  The host oracle hides this via Musashi fallback, but a
     * native-only build would re-dispatch the same illegal opcode forever. */
    if (vector == 4 || vector == 10 || vector == 11) ret_pc = (ret_pc + 2u) & 0xFFFFFFu;
    ng_push32(ret_pc);
    ng_push16(old);
    ng_cpu.pc = ng_r32((uint32_t)vector * 4);
    ng_cycles += 34;
}
void ng_bad_dispatch(uint32_t pc) {
    /* Table and generated code disagree: disable native dispatch (fall back to
     * Musashi everywhere) rather than spin or corrupt state. */
    (void)pc;
    ng_boot_recomp_bad++;
    s_recomp_disabled = 1;
}

#ifdef NG_BOOT_OVERLAY
static ng_func_t overlay_lookup(uint32_t a);   /* defined with the overlay runtime */
#endif

/* ---- game overlay (X2) ----------------------------------------------------------------------
 * The game P-ROM recomp is split into NG_RECOMP_PAGES load units on the disc;
 * a dispatch into a non-resident page faults it in through the platform
 * loader (ng_overlay_loader), evicting the LRU page when the resident budget
 * is full. The game page map (game_recomp_pages.h, local-only) carries the
 * addr -> {fn, page} entries; the flat table is not linked. */
#ifdef NG_BOOT_GAME_OVERLAY
#include "game_recomp_pages.h"   /* ng_gamemap_page_entry_t, NG_RECOMP_PAGES */
extern int ng_overlay_loader(unsigned page);
extern uint8_t* ng_overlay_page_base(unsigned page);
__attribute__((weak)) const ng_gamemap_page_entry_t* ng_game_pages(void) { return NULL; }
__attribute__((weak)) size_t ng_game_pages_count(void) { return 0; }
static uint8_t s_game_page_resident[NG_RECOMP_PAGES];
static uint16_t s_game_page_clock[NG_RECOMP_PAGES];
static uint16_t s_game_page_stamp;
static unsigned s_game_budget;        /* 0 = every page resident */
volatile uint32_t ng_boot_game_loads;
volatile uint32_t ng_boot_game_faults;
volatile uint32_t ng_boot_game_evictions;
volatile uint32_t ng_boot_game_errors;
volatile uint32_t ng_boot_game_resident;

void ng_game_overlay_set_budget(unsigned pages) {
    s_game_budget = pages > NG_RECOMP_PAGES ? NG_RECOMP_PAGES : pages;
}

static int game_resident_count(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) n += s_game_page_resident[i] != 0;
    return (int)n;
}
static int game_victim(void) {
    int best = -1;
    uint16_t best_age = 0;
    for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) {
        if (!s_game_page_resident[i]) continue;
        if (best < 0 || s_game_page_clock[i] < best_age) { best = (int)i; best_age = s_game_page_clock[i]; }
    }
    return best;
}
static int game_fault(unsigned page) {
    if (!s_game_page_resident[page]) {
        ng_boot_game_faults++;
        /* The Saturn arena is direct-mapped: any resident page whose slot
         * equals this page's slot will be overwritten by the load.  Drop all
         * of those from the residency set first. */
        uint8_t* slot = ng_overlay_page_base(page);
        for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) {
            if (s_game_page_resident[i] && ng_overlay_page_base(i) == slot) {
                s_game_page_resident[i] = 0;
                ng_boot_game_resident--;
                ng_boot_game_evictions++;
            }
        }
        if (ng_overlay_loader(page) != 0) { ng_boot_game_errors++; return 0; }
        s_game_page_resident[page] = 1;
        ng_boot_game_resident++;
        ng_boot_game_loads++;
    }
    s_game_page_clock[page] = ++s_game_page_stamp;
    return 1;
}

static ng_func_t game_overlay_lookup(uint32_t a) {
    const ng_gamemap_page_entry_t* pages = ng_game_pages();
    const size_t count = ng_game_pages_count();
    if (pages == NULL || count == 0) return NULL;
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t m = lo + ((hi - lo) >> 1);
        uint32_t e = pages[m].addr;
        if (e < a) lo = m + 1; else hi = m;
    }
    if (lo >= count || pages[lo].addr != a) return NULL;
    if (!game_fault(pages[lo].page)) { ng_boot_recomp_bad++; return NULL; }
    /* rawcode: the map carries an offset into the resident page blob, not a
     * linked function pointer. Resolve it against the page's arena base. */
    return (ng_func_t)(void*)(ng_overlay_page_base(pages[lo].page) + pages[lo].off);
}
#endif /* NG_BOOT_GAME_OVERLAY */

/* bus_peek16: plain-memory read used by kof_local_recomp-recompiled code. */
uint16_t bus_peek16(uint32_t a) {
    return ng_boot_read16(a);
}

/* bus_peek8 for symmetry */
uint8_t bus_peek8(uint32_t a) { return ng_boot_read8(a); }

/* Resident-native lookup: weak default; a target may link a strong version. */
__attribute__((weak)) ng_func_t ng_resident_lookup(uint32_t pc) { (void)pc; return NULL; }

static ng_func_t recomp_lookup(uint32_t a) {
    a &= 0xFFFFFF;
    if (s_recomp_disabled) return NULL;
    /* Resident-native hot routines (linked directly by a target build). */
    {
        extern ng_func_t ng_resident_lookup(uint32_t pc);
        ng_func_t rf = ng_resident_lookup(a);
        if (rf) return rf;
    }
    /* BIOS: overlay or full table. */
    if (a >= 0xC00000 && a < 0xD00000) {
#ifdef NG_BOOT_OVERLAY
        return overlay_lookup(a);
#else
        size_t lo = 0, hi = bios_ng_recomp_count;
        while (lo < hi) {
            size_t m = lo + ((hi - lo) >> 1);
            uint32_t e = bios_ng_recomp_table[m].addr;
            if (e < a) lo = m + 1; else hi = m;
        }
        if (lo < bios_ng_recomp_count && bios_ng_recomp_table[lo].addr == a)
            return bios_ng_recomp_table[lo].fn;
        return NULL;
#endif
    }
    /* Game P-ROM, guest low RAM and the WRAM code window: route all of
     * $000000-$0FFFFF (vector/P1), $100000-$1FFFFF (WRAM, the BIOS/game copy
     * executable code there), and $200000-$4FFFFF (P2 banks) to the overlay. */
    if (a < 0x500000u) {
#ifdef NG_BOOT_GAME_OVERLAY
        return game_overlay_lookup(a);
#else
        return NULL;   /* no game recomp linked */
#endif
    }
    return NULL;
}

#ifdef NG_BOOT_OVERLAY
/* ---- recomp routine overlay / streaming ----
 *
 * Recompiled routines never call each other directly: every intra-ROM transfer
 * (JSR/JMP/branch out of the routine) stores the target in CPU.pc and returns to
 * the dispatcher. A routine is therefore an independent unit that only the
 * dispatcher enters, which is exactly what paging needs.
 *
 * `m68krecomp --files N` emits each load unit (page) as its own translation
 * unit, and tools/mk_recomp_pages.py turns those into a PC -> page map
 * (ng_recomp_pages[], sorted by address). A page that is not resident is brought
 * in through the platform hook ng_overlay_loader(); when the resident budget is
 * full the least-recently-used non-pinned page is evicted. The hook is the only
 * platform-specific part: on Saturn it copies the page's linked bytes from the
 * disc into the page's fixed arena address.
 */
extern int ng_overlay_loader(unsigned page);   /* platform: make `page` resident */

static uint8_t  s_page_resident[NG_RECOMP_PAGES];
static uint16_t s_page_clock[NG_RECOMP_PAGES];
static uint16_t s_page_stamp;
static unsigned s_page_budget;        /* 0 = every page resident (no paging) */
static int      s_page_pinned = -1;   /* page holding the reset PC: never evicted */

volatile uint32_t ng_boot_overlay_loads;      /* pages actually brought in */
volatile uint32_t ng_boot_overlay_faults;     /* dispatch into a non-resident page */
volatile uint32_t ng_boot_overlay_evictions;  /* pages thrown out to make room */
volatile uint32_t ng_boot_overlay_errors;     /* load failures / unpinnable budget */
volatile uint32_t ng_boot_overlay_resident;   /* pages resident right now */

void ng_overlay_set_budget(unsigned pages) {
    s_page_budget = pages > NG_RECOMP_PAGES ? NG_RECOMP_PAGES : pages;
}
unsigned ng_overlay_budget(void) { return s_page_budget; }

static int overlay_resident_count(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) n += s_page_resident[i] != 0;
    return (int)n;
}
static int overlay_victim(void) {
    int best = -1;
    uint16_t best_age = 0;
    for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) {
        if (!s_page_resident[i] || (int)i == s_page_pinned) continue;
        if (best < 0 || s_page_clock[i] < best_age) { best = (int)i; best_age = s_page_clock[i]; }
    }
    return best;
}
/* Make `page` resident, evicting LRU if the budget is full. Returns 1 on success. */
static int overlay_fault(unsigned page) {
    if (!s_page_resident[page]) {
        ng_boot_overlay_faults++;                 /* true fault: page was not in */
        if (s_page_budget && overlay_resident_count() >= (int)s_page_budget) {
            int v = overlay_victim();
            if (v < 0) { ng_boot_overlay_errors++; return 0; }   /* budget is all pinned */
            s_page_resident[v] = 0;
            ng_boot_overlay_resident--;
            ng_boot_overlay_evictions++;
        }
        if (ng_overlay_loader(page) != 0) { ng_boot_overlay_errors++; return 0; }
        s_page_resident[page] = 1;
        ng_boot_overlay_resident++;
        ng_boot_overlay_loads++;
    }
    s_page_clock[page] = ++s_page_stamp;
    return 1;
}

static ng_func_t overlay_lookup(uint32_t a) {
    size_t lo = 0, hi = ng_recomp_pages_count;
    while (lo < hi) {
        size_t m = lo + ((hi - lo) >> 1);
        uint32_t e = ng_recomp_pages[m].addr;
        if (e < a) lo = m + 1; else hi = m;
    }
    if (lo >= ng_recomp_pages_count || ng_recomp_pages[lo].addr != a) return NULL;
    if (!overlay_fault(ng_recomp_pages[lo].page)) { ng_boot_recomp_bad++; return NULL; }
    return ng_recomp_pages[lo].fn;
}
void ng_overlay_pin_reset_page(uint32_t pc) {
    size_t lo = 0, hi = ng_recomp_pages_count;
    while (lo < hi) {
        size_t m = lo + ((hi - lo) >> 1);
        if (ng_recomp_pages[m].addr < pc) lo = m + 1; else hi = m;
    }
    s_page_pinned = (lo < ng_recomp_pages_count) ? (int)ng_recomp_pages[lo].page : -1;
}
#endif /* NG_BOOT_OVERLAY */

static void recomp_map_pages(void) {
    for (int i = 0; i < 256; i++) { ng_rmap[i] = NULL; ng_wmap[i] = NULL; }
    for (uint32_t p = 0x10; p < 0x20; p++) ng_rmap[p] = ng_wmap[p] = s_wram;
    for (uint32_t p = 0xC0; p < 0xD0; p++) ng_rmap[p] = s_bios + ((p & 1) << 16);
    if (s_cart_bank[0])
        for (uint32_t p = 0x00; p < 0x10; p++) ng_rmap[p] = (uint8_t *)s_cart_bank[0] + (p << 16);
    else
        ng_rmap[0] = s_vec;
}

#ifndef NG_BOOT_RECOMP_ONLY
static void recomp_to_musashi(void) {
    m68k_set_reg(M68K_REG_SR, ng_get_sr());
    for (int i = 0; i < 8; i++) m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), ng_cpu.d[i]);
    for (int i = 0; i < 7; i++) m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), ng_cpu.a[i]);
    m68k_set_reg(M68K_REG_A7, ng_cpu.a[7]);
    m68k_set_reg(ng_cpu.s ? M68K_REG_USP : M68K_REG_ISP, ng_cpu.osp);
    m68k_set_reg(M68K_REG_PC, ng_cpu.pc);
    m68k_set_irq(0);   /* the dispatcher owns IRQ at routine boundaries */
}
static void recomp_from_musashi(void) {
    uint16_t sr = (uint16_t)m68k_get_reg(NULL, M68K_REG_SR);
    ng_cpu.s = (sr >> 13) & 1; ng_cpu.t = (sr >> 15) & 1; ng_cpu.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
    for (int i = 0; i < 8; i++) ng_cpu.d[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
    for (int i = 0; i < 8; i++) ng_cpu.a[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
    ng_cpu.osp = m68k_get_reg(NULL, ng_cpu.s ? M68K_REG_USP : M68K_REG_ISP);
    ng_cpu.pc = m68k_get_reg(NULL, M68K_REG_PC);
    ng_cpu.stopped = 0;
}
#endif /* !NG_BOOT_RECOMP_ONLY */

static void recomp_take_irq(int level) {
    uint16_t old = ng_get_sr();
    ng_cpu.stopped = 0;
    ng_set_sr((uint16_t)(((old | 0x2000) & ~0x8700) | (level << 8)));
    ng_push32(ng_cpu.pc);
    ng_push16(old);
    ng_cpu.pc = ng_r32(0x60 + (uint32_t)level * 4);
    ng_cycles += 44;
    ng_boot_irq_taken++;
}
/* Dispatch PC trace (instrumentation): circular log of guest PCs + D0. */

static void recomp_step(void) {
    int level = ng_irq_level();
    if (level > ng_cpu.ipl || level == 7) recomp_take_irq(level);
    if (ng_cpu.stopped) return;
    ng_func_t fn = recomp_lookup(ng_cpu.pc);
    if (fn) {
        uint64_t a = ng_icount;
        fn();
        ng_boot_instructions += (uint32_t)(ng_icount - a);
        ng_boot_last_pc = ng_cpu.pc;
        ng_boot_recomp_blocks++;
        return;
    }
#ifdef NG_BOOT_RECOMP_ONLY
    /* No interpreter in this build: a miss is a real fault, never silently
     * stepped. Report it, halt the guest and count it (visible to the probe). */
    ng_boot_recomp_bad++;
    ng_boot_recomp_interp++;        /* mis-dispatch count, reported as such */
    ng_cpu.stopped = 1;
    ng_boot_last_pc = ng_cpu.pc;
#else
    recomp_to_musashi();
    {
        int used = m68k_execute(1);
        ng_cycles += used;
        recomp_from_musashi();
        if (used == 0) ng_cpu.stopped = 1;   /* STOP with no IRQ: wait for one */
    }
    ng_boot_recomp_interp++;
    ng_boot_last_pc = ng_cpu.pc;
#endif
}
static void recomp_init(void) {
    recomp_map_pages();
#ifdef NG_BOOT_OVERLAY
    /* Page 0 of the reset PC is pinned; the rest are loaded on demand. A budget
     * of 0 means "all resident", which is the no-paging reference. */
    ng_overlay_pin_reset_page(s_initial_pc);
    {
        unsigned resident = 0;
        for (unsigned i = 0; i < NG_RECOMP_PAGES; i++) {
            s_page_resident[i] = (uint8_t)(s_page_budget == 0);
            resident += s_page_resident[i] != 0;
        }
        ng_boot_overlay_resident = resident;
        if (s_page_budget == 0) ng_boot_overlay_loads = NG_RECOMP_PAGES;
    }
#endif
#ifdef NG_BOOT_RECOMP_ONLY
    /* Reset state comes straight from the reset vector page; no Musashi to sync. */
    ng_cpu.d[0] = ng_cpu.d[1] = ng_cpu.d[2] = ng_cpu.d[3] = 0;
    ng_cpu.d[4] = ng_cpu.d[5] = ng_cpu.d[6] = ng_cpu.d[7] = 0;
    for (int i = 0; i < 8; i++) ng_cpu.a[i] = 0;
    ng_cpu.s = 1; ng_cpu.t = 0; ng_cpu.ipl = 7;
    ng_cpu.x = ng_cpu.n = ng_cpu.z = ng_cpu.v = ng_cpu.c = 0;
    ng_cpu.a[7] = s_initial_ssp;
    ng_cpu.osp = 0;
    ng_cpu.pc = s_initial_pc;
    ng_cpu.stopped = 0;
#else
    recomp_from_musashi();   /* capture the reset state ng_boot_init set in m68k */
#endif
}
#endif /* NG_BOOT_RECOMP */


/* ---- reset ---- */
void ng_boot_init(const uint8_t* bios_68k, uint32_t bios_size) {
    (void)bios_size;
    for (uint32_t i = 0; i < sizeof(s_bios); i++) s_bios[i] = bios_68k[i];
    for (uint32_t i = 0; i < sizeof(s_vec); i++) s_vec[i] = 0xFF;
    for (uint32_t i = 0; i < 0x80; i++) s_vec[i] = s_bios[i];
    for (uint32_t i = 0; i < sizeof(s_wram); i++) s_wram[i] = 0;
    for (uint32_t i = 0; i < sizeof(s_backup); i++) s_backup[i] = 0;
    for (uint32_t i = 0; i < sizeof(s_vram) / 2; i++) s_vram[i] = 0;
    for (uint32_t i = 0; i < sizeof(s_pal) / 2; i++) ((uint16_t*)s_pal)[i] = 0;
    s_addr = 0; s_mod = 1;
    s_anim_speed = s_anim_frames = s_anim_disabled = s_anim_counter = 0;
    s_timer_ctrl = 0; s_timer_reload = 0; s_timer_deadline = INT64_MAX;
    s_p1 = s_p2 = 0; s_start1 = s_start2 = s_select1 = s_select2 = 0;
    s_coin1 = s_coin2 = s_service = s_test = 0;
    s_bios_vectors = 1; s_backup_locked = 1; s_mvs = 0;
    s_sound_cmd = s_sound_reply = 0; s_fix_bios = 0;
    s_pal_bank = 0;
    ng_cycles = 0; ng_frame_start = 0; ng_irq_pending = 0;

    s_initial_ssp = ((uint32_t)s_vec[0] << 24) | ((uint32_t)s_vec[1] << 16) | ((uint32_t)s_vec[2] << 8) | s_vec[3];
    s_initial_pc  = ((uint32_t)s_vec[4] << 24) | ((uint32_t)s_vec[5] << 16) | ((uint32_t)s_vec[6] << 8) | s_vec[7];

#ifndef NG_BOOT_RECOMP_ONLY
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_reg(M68K_REG_SR, 0x2700);            /* supervisor, IPL7 */
    m68k_set_reg(M68K_REG_A7, s_initial_ssp);
    m68k_set_reg(M68K_REG_ISP, s_initial_ssp);
    m68k_set_reg(M68K_REG_PC, s_initial_pc);
    m68k_set_irq(0);
#endif
#ifdef NG_BOOT_RECOMP
    recomp_init();
#endif
}

/* ---- run ---- */
/* Service hook (see ng_boot.h): fires from the guest execution loops so a
 * slow backend can keep real-time services fed. `s_slice_cycles == 0` means
 * every dispatch chunk; otherwise at most once per that many guest cycles. */
static ng_boot_slice_fn s_slice_fn;
static uint32_t s_slice_cycles;
static uint64_t s_slice_next;

void ng_boot_set_slice_hook(ng_boot_slice_fn fn, uint32_t slice_cycles) {
    s_slice_fn = fn;
    s_slice_cycles = slice_cycles;
    s_slice_next = 0;
}

static void slice_hook_fire(void) {
    if (!s_slice_fn) return;
    if (s_slice_cycles != 0u) {
        if ((uint64_t)ng_cycles < s_slice_next) return;
        s_slice_next = (uint64_t)ng_cycles + (uint64_t)s_slice_cycles;
    }
    s_slice_fn();
}

/* Per-field hook: fires from lspc_vblank(), i.e. once per emulated display
 * field inside the guest frame (the slice hook fires per guest-cycle budget
 * and cannot see the field boundary). A backend that services per-field
 * devices (the SCSP streams) needs this cadence: the field counter the
 * services read (sat_field_count) only advances on the VBlank edge, so a
 * slice-only hook leaves them serviced once per guest frame. */
void ng_boot_set_field_hook(ng_boot_slice_fn fn) {
    s_field_fn = fn;
}

static void run_until(int64_t t) {
#ifdef NG_BOOT_RECOMP
    while (ng_cycles < t) {
        int64_t target = t;
        if (s_timer_deadline < target) target = s_timer_deadline;
        ng_next_event = target;              /* routines yield here */
        recomp_step();
        if (ng_cpu.stopped) ng_cycles = target;  /* halted: skip to next event */
        slice_hook_fire();
        if (ng_cycles >= s_timer_deadline) timer_fire();
    }
#else
    while (ng_cycles < t) {
        int64_t target = t;
        if (s_timer_deadline < target) target = s_timer_deadline;
        if (target > ng_cycles) {
            int64_t budget = target - ng_cycles;
            if (budget > 4000) budget = 4000;
            slice_hook_fire();
            m68k_set_irq((unsigned)ng_irq_level());
            ng_cycles += m68k_execute((int)budget);
        } else {
            /* only a pending timer event remained */
        }
        if (ng_cycles >= s_timer_deadline) timer_fire();
    }
#endif
}

void ng_boot_run_frames(uint32_t frames) {
    for (uint32_t f = 0; f < frames; f++) {
#ifdef KOF_32BIT_CYCLES
        /* ng_cycles is int32 (smaller generated code) but ng_frame_start and
         * the run_until targets are int64. Without rebasing, ng_cycles wraps
         * negative at 2^31 (~10591 frames) while the targets keep growing, so
         * `while (ng_cycles < t)` never terminates: no vblank, no IRQ, guest
         * frozen. Rebase both epochs by a large signed-safe amount while
         * ng_cycles is still well below INT32_MAX. Generated code only ever
         * compares ng_cycles to ng_next_event (set one scanline ahead), so a
         * synchronized rebase preserves all guest-visible timing exactly. */
        if (ng_frame_start > (int64_t)(INT32_MAX / 2)) {
            const int64_t rebase = INT32_MAX / 2;
            ng_cycles -= (int32_t)rebase;
            ng_frame_start -= rebase;
            if (s_timer_deadline != INT64_MAX) s_timer_deadline -= rebase;
        }
#endif
        int64_t fs = ng_frame_start;
        for (int line = 0; line < 264; line++) {
            int64_t ls = fs + (int64_t)line * 768;
            run_until(ls);
            if (line == 240) { run_until(ls + 0x11F * 2); lspc_vblank(); }
        }
        ng_frame_start = fs + (int64_t)264 * 768;
        run_until(ng_frame_start);
        ng_boot_frames++;
    }
}

uint32_t ng_boot_pc(void) {
#ifdef NG_BOOT_RECOMP
    return ng_cpu.pc;
#else
    return (uint32_t)m68k_get_reg(NULL, M68K_REG_PC);
#endif
}
uint32_t ng_boot_initial_ssp(void) { return s_initial_ssp; }
uint32_t ng_boot_initial_pc(void) { return s_initial_pc; }
uint32_t ng_boot_cycles_lo(void) { return (uint32_t)ng_cycles; }

/* Snapshot the live guest LSPC raster state for the strip pipeline:
 * VRAM (KOF_LSPC_WORDS words), the active palette bank (4096 words), the
 * animation counter (0..7) and the BIOS/cart FIX selector. */
void ng_boot_lspc_snapshot(uint16_t* vram, uint16_t* pal, unsigned* anim, int* fix_bios) {
    for (uint32_t i = 0; i < 0x8580u; i++) vram[i] = s_vram[i];
    for (uint32_t i = 0; i < 4096u; i++) pal[i] = s_pal[s_pal_bank][i];
    *anim = s_anim_counter;
    *fix_bios = s_fix_bios;
}
/* Zero-copy accessors for the Saturn renderer (s_vram / active palette bank). */
const uint16_t* ng_boot_vram(void) { return s_vram; }
const uint16_t* ng_boot_pal(void) { return s_pal[s_pal_bank]; }
unsigned ng_boot_anim(void) { return s_anim_counter; }
int ng_boot_fix_bios(void) { return s_fix_bios; }