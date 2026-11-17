/* ng_live_strips.c - decode the live guest LSPC raster into ordered sprite
 * strips and hash them. Shared by host and Saturn. See ng_live_strips.h. */
#include "kof98/ng_live_strips.h"
#include "kof98/strip.h"
#include "kof98/lspc_ir.h"
#include "kof98/strip_hash.h"

#ifndef LIVE_MAX_STRIPS
#define LIVE_MAX_STRIPS 2048u
#endif

#ifdef KOF_NG_SATURN
#define LIVE_RAM __attribute__((section(".wram_l")))
#else
#define LIVE_RAM
#endif
static kof_strip g_strips[LIVE_MAX_STRIPS] LIVE_RAM;
static kof_strip_builder g_builder LIVE_RAM;
static kof_line_ir g_ir;

uint32_t ng_live_sprite_strips(const uint16_t* vram, const uint8_t* zoom,
                               const uint16_t* pal, unsigned anim, int fix_bios,
                               uint32_t* out_hash) {
    if (kof_strip_begin(&g_builder, g_strips, LIVE_MAX_STRIPS)) return 0xFFFFFFFFu;
    for (unsigned line = 0; line < 224u; line++) {
        if (kof_lspc_line(vram, KOF_LSPC_WORDS, zoom, KOF_ZOOM_BYTES, anim, line,
                          fix_bios, &g_ir)) return 0xFFFFFFFFu;
        if (kof_strip_push(&g_builder, &g_ir, pal, 4096u)) return 0xFFFFFFFFu;
    }
    if (!kof_strip_complete(&g_builder)) return 0xFFFFFFFFu;
    uint32_t h = 2166136261u;
    for (unsigned slot = 0; slot < KOF_STRIP_SLOTS; slot++)
        for (uint16_t i = g_builder.head[slot]; i != KOF_STRIP_END; i = g_strips[i].next)
            h = kof_strip_hash_record(h, &g_strips[i], 0u);
    *out_hash = h;
    return g_builder.count;
}

kof_strip_builder* ng_live_builder(void) { return &g_builder; }

uint32_t ng_live_collect_sprite_tiles(uint32_t* out, uint32_t max) {
    uint32_t n = 0;
    for (unsigned slot = 0; slot < KOF_STRIP_SLOTS; slot++)
        for (uint16_t i = g_builder.head[slot]; i != KOF_STRIP_END; i = g_strips[i].next) {
            uint32_t t = g_strips[i].tile;
            if (t >= 524288u) continue;          /* FIX tiles are not in the sprite C ROM */
            int dup = 0;
            for (uint32_t k = 0; k < n; k++) if (out[k] == t) { dup = 1; break; }
            if (!dup && n < max) out[n++] = t;
        }
    return n;
}