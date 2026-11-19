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
#include "ng_boot_internal.h"
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

#ifndef NG_BOOT_RECOMP_ONLY
unsigned int ng_int_ack_handler(unsigned int level) {
    (void)level;
    ng_boot_irq_taken++;
    return M68K_INT_ACK_AUTOVECTOR;       /* autovector (Neo Geo) */
}
#endif /* !NG_BOOT_RECOMP_ONLY */

uint32_t s_initial_ssp, s_initial_pc;

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
int ng_irq_level(void) {
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

/* Per-frame guest-state trace (opt-in NG_GUEST_FRAME_TRACE) for diagnosing
 * recomp-vs-interpreter divergence. One record per vblank, dumped via Ymir
 * --dump-mem. Local diagnostic only; no effect unless the flag is set. */
#ifdef NG_GUEST_FRAME_TRACE
typedef struct {
    uint32_t frame, pc, instr, vram, blocks;
    uint16_t sr;
    uint8_t  stopped, irq;
} ng_frame_trace_rec_t;
ng_frame_trace_rec_t g_frame_trace[256];
volatile uint32_t g_frame_trace_n;
uint32_t g_pc_ring[256];            /* last 256 dispatched guest PCs */
volatile uint32_t g_pc_ring_w;
static uint16_t ng_trace_sr(void) {
#ifdef NG_BOOT_RECOMP
    return ng_get_sr();
#else
    return (uint16_t)m68k_get_reg(NULL, M68K_REG_SR);
#endif
}
static uint32_t ng_trace_pc(void) {
#ifdef NG_BOOT_RECOMP
    return ng_cpu.pc;
#else
    return (uint32_t)m68k_get_reg(NULL, M68K_REG_PC);
#endif
}
#endif

static void lspc_vblank(void) {
    ng_irq_pending |= 1;
    ng_boot_vblanks++;
#ifdef NG_GUEST_FRAME_TRACE
    if (g_frame_trace_n < 256) {
        ng_frame_trace_rec_t* r = &g_frame_trace[g_frame_trace_n++];
        r->frame = ng_boot_frames;
        r->pc = ng_trace_pc();
        r->sr = ng_trace_sr();
#ifdef NG_BOOT_RECOMP
        r->stopped = ng_cpu.stopped;
#else
        r->stopped = 0;
#endif
        r->irq = ng_irq_pending;
        r->instr = ng_boot_instructions;
        r->vram = ng_boot_vram_writes;
#ifdef NG_BOOT_RECOMP
        r->blocks = ng_boot_recomp_blocks;
#else
        r->blocks = 0;
#endif
    }
#endif
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
    case 1:
        s_bios_vectors = (uint8_t)!bit;              /* SWPBIOS: no P ROM present */
#ifdef NG_BOOT_LIVE_AUDIO
        audio_select_bios_rom(s_bios_vectors);       /* Z80 switches SM1/M1 driver */
#endif
        break;
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
    case 0x320000:
        if (a & 1) return status_a();
#ifdef NG_BOOT_LIVE_AUDIO
        return audio_reply();
#else
        return s_sound_reply;
#endif
    case 0x340000: return (a & 1) ? 0xFF : (uint8_t)~s_p2;
    case 0x380000: return (a & 1) ? 0xFF : status_b();
    default: return 0xFF;
    }
}
static void io_write8(uint32_t a, uint8_t v) {
    ng_boot_io_writes++;
    switch (a & 0xFE0000) {
    case 0x300000: break;
    case 0x320000:
        if (!(a & 1)) {
#ifdef NG_BOOT_LIVE_AUDIO
            audio_command(v);
#else
            s_sound_cmd = v; s_sound_reply = 0;
#endif
        }
        break;
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
    if (a < 0x200000) {
#ifdef NG_BOOT_PC_PROFILE
        if (a >= 0x100000u && a < 0x100200u && ng_profile_wram_writer_pc == 0u)
            ng_profile_wram_writer_pc = ng_boot_last_pc;
#endif
        uint8_t* p = s_wram + (a & 0xFFFF); p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; return;
    }
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


void ng_insn_hook(unsigned int pc) {
    ng_boot_instructions++;
    ng_boot_last_pc = pc;
#ifdef NG_BOOT_PC_PROFILE
    ng_profile_record(pc);
#endif
    if (pc >= 0xC00000 && pc < 0xD00000) ng_boot_bios_instr++;
    else if (pc >= 0x100000 && pc < 0x200000) ng_boot_wram_instr++;
    else if (pc < 0x100000 || (pc >= 0x200000 && pc < 0x300000)) ng_boot_cart_instr++;
}

#ifdef NG_BOOT_RECOMP
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

/* Wire the emulated hardware into the recomp backend's page tables.
 * Defined here (not in ng_boot_recomp.c) because it needs the hardware
 * arrays; ng_rmap/ng_wmap are owned by the recomp backend. */
void ng_boot_setup_recomp_maps(void) {
    for (int i = 0; i < 256; i++) { ng_rmap[i] = NULL; ng_wmap[i] = NULL; }
    for (uint32_t p = 0x10; p < 0x20; p++) ng_rmap[p] = ng_wmap[p] = s_wram;
    for (uint32_t p = 0xC0; p < 0xD0; p++) ng_rmap[p] = s_bios + ((p & 1) << 16);
    if (s_cart_bank[0])
        for (uint32_t p = 0x00; p < 0x10; p++) ng_rmap[p] = (uint8_t *)s_cart_bank[0] + (p << 16);
    else
        ng_rmap[0] = s_vec;
}
#endif


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
        /* Fire the slice hook before executing this slice, matching the pure
         * Musashi branch below. The hybrid core relies on this ordering for
         * bit-identical guest state when guest-RAM code runs on the fallback. */
        slice_hook_fire();
        recomp_step();
        if (ng_cpu.stopped) ng_cycles = target;  /* halted: skip to next event */
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
#ifdef NG_BOOT_LIVE_AUDIO
        audio_sync();   /* run the Z80/YM2610 up to the guest's cycle count */
#endif
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