/* ng_boot.h - native 68000 execution of the real Neo Geo BIOS on Saturn.
 *
 * This is a compact, freestanding Neo Geo 68000 system: the Musashi interpreter
 * core over a minimal AES memory map (BIOS + 64 KB work RAM + 64 KB backup +
 * LSPC VRAM/registers + palette + $300000 I/O). It boots the actual Neo Geo
 * system BIOS (the real 68000 program) with the same vector table, autovectored
 * IRQ1 (vblank) and LSPC timer the hardware uses.
 *
 * With the 4 MiB data cart attached it hands off to the game's own P1/P2 code.
 * The interpreter is the correctness reference; the optional recomp dispatcher
 * (NG_BOOT_RECOMP, plus NG_BOOT_OVERLAY for routine paging) runs the same code
 * natively without changing bus or IRQ semantics.
 *
 * Shared by the host validator (src/host/ng_boot_host.c) and the Saturn target
 * (src/saturn/ng_boot_main.c). No libc, no allocation.
 */
#ifndef NG_BOOT_H
#define NG_BOOT_H

#include <stdint.h>

/* Load the 128 KB system BIOS, already in 68000 byte order (big-endian words).
 * Builds the reset vector page and resets the CPU, LSPC, I/O and palette. */
void ng_boot_init(const uint8_t* bios_68k, uint32_t bios_size);

/* Attach the CPU-mapped 4 MiB expansion cartridge holding P1 (1 MiB) then P2
 * banks at 0x100000 + bank*0x100000. With a cart, the 68000 fixed bank reads
 * P1 and the $200000 window reads P2; without one, $000000 is the BIOS vectors.
 * Call before ng_boot_init so the BIOS sees a valid cart. */
void ng_boot_set_cart(const uint8_t* bank0, uint32_t bytes0, const uint8_t* bank1, uint32_t bytes1);

/* Feed controller state (active high), same bit order as ng_input_t:
 * pad bit 0 up, 1 down, 2 left, 3 right, 4 A, 5 B, 6 C, 7 D. */
void ng_boot_set_p1(uint8_t v);
void ng_boot_set_p2(uint8_t v);
void ng_boot_set_start(int on);      /* P1 Start */
void ng_boot_set_start2(int on);
void ng_boot_set_select1(int on);
void ng_boot_set_select2(int on);
void ng_boot_set_coin1(int on);
void ng_boot_set_coin2(int on);
void ng_boot_set_service(int on);
void ng_boot_set_test(int on);

/* Snapshot the live guest LSPC raster state (VRAM 0x8580 words, active palette
 * bank 4096 words, anim 0..7, fix-bios) for the strip pipeline. */
void ng_boot_lspc_snapshot(uint16_t* vram, uint16_t* pal, unsigned* anim, int* fix_bios);
/* Zero-copy accessors (Saturn renderer): s_vram, active palette bank, anim, fix. */
const uint16_t* ng_boot_vram(void);
const uint16_t* ng_boot_pal(void);
unsigned ng_boot_anim(void);
int ng_boot_fix_bios(void);

/* Low 32 bits of the guest cycle counter (frame-boundary alignment check). */
uint32_t ng_boot_cycles_lo(void);

#ifdef NG_BOOT_RECOMP
extern volatile uint32_t ng_boot_recomp_blocks;
extern volatile uint32_t ng_boot_recomp_interp;
extern volatile uint32_t ng_boot_recomp_bad;
#endif

#ifdef NG_BOOT_OVERLAY
/* Recomp routine overlay/streaming (see ng_boot.c): routines are paged in from
 * a platform loader. Budget 0 = every page resident (no paging). */
void     ng_overlay_set_budget(unsigned pages);
unsigned ng_overlay_budget(void);
void     ng_overlay_pin_reset_page(uint32_t pc);
extern volatile uint32_t ng_boot_overlay_loads;
extern volatile uint32_t ng_boot_overlay_faults;
extern volatile uint32_t ng_boot_overlay_evictions;
extern volatile uint32_t ng_boot_overlay_errors;
extern volatile uint32_t ng_boot_overlay_resident;

/* Game-overlay (X2): the game P-ROM recomp is paged in from the disc. */
void     ng_game_overlay_set_budget(unsigned pages);
extern volatile uint32_t ng_boot_game_loads;
extern volatile uint32_t ng_boot_game_faults;
extern volatile uint32_t ng_boot_game_evictions;
extern volatile uint32_t ng_boot_game_errors;
extern volatile uint32_t ng_boot_game_resident;
#endif

#ifdef NG_BOOT_PC_PROFILE
/* Host-only guest PC profiler (X1): executed instruction words + 4 KB page
 * hits over the cart/WRAM and BIOS windows. Never built for Saturn. */
void ng_boot_profile_reset(void);
void ng_boot_profile_dump(const char* path);
/* Optional compact list of executed word address ranges (text, one per line). */
void ng_boot_profile_dump_words(const char* path);
#endif

/* Run `frames` video frames (264 lines each) with vblank IRQ1 and the LSPC
 * timer IRQ2, exactly as the hardware would. */
void ng_boot_run_frames(uint32_t frames);

/* Optional service hook, called from inside ng_boot_run_frames while guest
 * code executes: a slow backend (the interpreter takes many emulated fields
 * per guest frame) uses it to keep real-time services - the disc scheduler
 * and the SCSP - fed during a long guest frame. The core fires it at most
 * once per `slice_cycles` guest cycles (0 = on every dispatch chunk) and the
 * hook must rate-limit itself against emulated time if it needs a cadence.
 * The hook must not touch guest state; it never changes guest timing. */
typedef void (*ng_boot_slice_fn)(void);
void ng_boot_set_slice_hook(ng_boot_slice_fn fn, uint32_t slice_cycles);

/* Per-field hook: fires from the core's VBlank entry, once per emulated
 * display field (60 Hz) inside the guest frame - the slice hook's cadence is
 * guest cycles and cannot see the field boundary. Use this for per-field
 * devices whose service clock is the field counter (the SCSP streams). */
void ng_boot_set_field_hook(ng_boot_slice_fn fn);

/* Read the current 68000 PC (for a trace/diagnostic). */
uint32_t ng_boot_pc(void);

/* Read the initial SSP and PC the BIOS reset vector selected. */
uint32_t ng_boot_initial_ssp(void);
uint32_t ng_boot_initial_pc(void);

/* Counters (also exported as symbols for the Ymir probe to dump).
 *   instructions   68000 instructions executed by the backend (interp or recomp)
 *   frames         video frames run
 *   vblanks         vblank IRQ1s raised
 *   timer_irqs      LSPC timer IRQ2s raised
 *   vram_writes     words written to LSPC VRAM (BIOS video init)
 *   pal_writes      palette words written
 *   bios_instr      instructions fetched from $C00000-$CFFFFF (BIOS ROM)
 *   io_writes       writes into the $300000 I/O block
 */
extern volatile uint32_t ng_boot_instructions;
extern volatile uint32_t ng_boot_frames;
extern volatile uint32_t ng_boot_vblanks;
extern volatile uint32_t ng_boot_timer_irqs;
extern volatile uint32_t ng_boot_vram_writes;
extern volatile uint32_t ng_boot_pal_writes;
extern volatile uint32_t ng_boot_bios_instr;
extern volatile uint32_t ng_boot_io_writes;
extern volatile uint32_t ng_boot_last_pc;
extern volatile uint32_t ng_boot_cart_instr;
extern volatile uint32_t ng_boot_wram_instr;
extern volatile uint32_t ng_boot_irq_taken;
/* Input diagnostics: reads of $380000 and reads while P1 Start is held. */
extern volatile uint32_t ng_boot_status_reads;
extern volatile uint32_t ng_boot_status_start_reads;

#endif