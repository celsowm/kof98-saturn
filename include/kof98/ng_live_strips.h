#ifndef KOF98_NG_LIVE_STRIPS_H
#define KOF98_NG_LIVE_STRIPS_H
#include <stdint.h>
#include "kof98/strip.h"

/* Build the sprite strip chains from the live guest LSPC state (VRAM, L0 zoom,
 * palette, animation, fix-bios) using the existing LSPC/strip pipeline, and
 * hash the ordered strips. Returns the sprite-strip count, or 0xFFFFFFFF on
 * overflow/error. The hash is FNV-1a over the ordered KFR2 records, matching
 * kof_strip_hash_record (bias 0). No C ROM / tile pixels are needed: strips are
 * decoded from the guest's own LSPC raster state. */
uint32_t ng_live_sprite_strips(const uint16_t* vram, const uint8_t* zoom,
                               const uint16_t* pal, unsigned anim, int fix_bios,
                               uint32_t* out_hash);

/* The builder holding the most recent ng_live_sprite_strips() chains, for the
 * VDP1 strip renderer to draw. NULL before the first build. */
kof_strip_builder* ng_live_builder(void);

/* Collect the unique sprite C ROM tile IDs (< 524288) referenced by the current
 * builder into out[0..max). Returns the count (may exceed max; only max stored).
 * Used to build a tile-residency pack from a live frame. */
uint32_t ng_live_collect_sprite_tiles(uint32_t* out, uint32_t max);

#endif