/* ng_boot_internal.h - internals shared by the ng_boot modules.
 *
 * ng_boot.c owns the emulated hardware (memory, LSPC, I/O), the cycle clock and
 * the frame loop. The backend modules (ng_boot_recomp.c, ng_boot_stats.c,
 * ng_boot_profile.c) are separate responsibilities that consume a small,
 * explicit slice of that state through this header. Nothing here is public API:
 * applications use ng_boot.h only.
 */
#ifndef KOF98_NG_BOOT_INTERNAL_H
#define KOF98_NG_BOOT_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include "ng_boot.h"

/* ---- clock / scheduler state owned by ng_boot.c ---- */
#ifdef KOF_32BIT_CYCLES
extern int32_t  ng_cycles;
extern int32_t  ng_next_event;
extern uint32_t ng_icount;
#else
extern int64_t  ng_cycles;
extern int64_t  ng_next_event;
extern uint64_t ng_icount;
#endif
extern int64_t  ng_frame_start;
extern uint8_t  ng_irq_pending;
extern uint32_t s_initial_ssp, s_initial_pc;   /* reset vector, set by ng_boot_init */

int ng_irq_level(void);

/* Bus entry points: the recomp backend's slow-path helpers forward here. */
uint8_t  ng_boot_read8(uint32_t a);
uint16_t ng_boot_read16(uint32_t a);
uint32_t ng_boot_read32(uint32_t a);
void     ng_boot_write8(uint32_t a, uint8_t v);
void     ng_boot_write16(uint32_t a, uint16_t v);
void     ng_boot_write32(uint32_t a, uint32_t v);

/* Recomp page tables are owned by ng_boot_recomp.c; ng_boot.c fills them at
 * reset because only it can see the hardware memory arrays. */
extern uint8_t* ng_rmap[256];
extern uint8_t* ng_wmap[256];
void ng_boot_setup_recomp_maps(void);

/* Device-side slow paths (recomp ABI, implemented in ng_boot.c): these poke
 * LSPC/palette/I/O state directly, so they stay with the hardware module. */
uint8_t  ng_r8_slow(uint32_t a);
uint16_t ng_r16_slow(uint32_t a);
void     ng_w8_slow(uint32_t a, uint8_t v);
void     ng_w16_slow(uint32_t a, uint16_t v);

/* Native recomp dispatch backend (ng_boot_recomp.c). */
#ifdef NG_BOOT_RECOMP
void recomp_init(void);
void recomp_step(void);
#endif

/* Host-only PC profiler (ng_boot_profile.c). */
#ifdef NG_BOOT_PC_PROFILE
void ng_profile_record(uint32_t pc);
#endif

/* Live Z80/YM2610 sound (upstream audio.c, BSD-3). */
#ifdef NG_BOOT_LIVE_AUDIO
void   audio_command(uint8_t cmd);
uint8_t audio_reply(void);
void   audio_select_bios_rom(int bios);
void   audio_sync(void);
size_t audio_take_samples(int16_t* out, size_t max_frames);
#endif

#endif /* KOF98_NG_BOOT_INTERNAL_H */
