#ifndef KOF98_STRIP_H
#define KOF98_STRIP_H
#include "kof98/lspc_ir.h"
#include "kof98/zoom_memo.h"
#define KOF_STRIP_END UINT16_MAX
#define KOF_STRIP_SLOTS 381u
#define KOF_STRIP_ROWS 16u
typedef struct {
    uint32_t tile;
    uint16_t x,y,mask,next;
    uint16_t colors[16]; /* exact guest palette words */
    uint8_t rows[16],height,width,flip;
    /* Live emitter cache metadata. Never serialized, hashed or compared by
     * the strip oracle. Zero means "unresolved"; the core builders clear the
     * pattern reference whenever sampled rows change and the palette
     * reference whenever a strip starts. */
    uint32_t pattern_epoch,palette_epoch;
    uint16_t pattern_slot,lut_slot;
} kof_strip;
typedef struct {
    kof_strip *strips;
    uint16_t capacity,count,line;
    uint16_t head[KOF_STRIP_SLOTS],tail[KOF_STRIP_SLOTS];
    uint32_t spans;
    uint32_t palette_token[KOF_STRIP_SLOTS]; /* valid only when tail != KOF_STRIP_END */
    uint8_t failed;
} kof_strip_builder;
/* Caller-owned bounded workspace. A failed frame must be discarded.
 * begin resets the logical chains; inactive tokens and struct padding
 * are unspecified, not part of the serialized strip representation. */
int kof_strip_begin(kof_strip_builder*,kof_strip*,uint16_t capacity);
/* Sprite-only IR, lines sequential from 0; current palette bank (4096 words). */
int kof_strip_push(kof_strip_builder*,const kof_line_ir*,const uint16_t*,uint32_t palette_words);
/* Optional versioned palette path. versions[256] must start at zero (or
 * another consistent epoch) and advance on EVERY change to the corresponding
 * 16-word palette during this builder's lifetime, including bank switches.
 * Do not wrap/reuse a version with different contents; restart the builder
 * before resetting versions. A different palette ID/version still merges
 * if all 16 raw words agree, exactly as the reference path does.
 * Do not mix ordinary and versioned pushes within a frame. */
int kof_strip_push_versioned(kof_strip_builder*,const kof_line_ir*,const uint16_t*,uint32_t palette_words,const uint16_t versions[256]);
/* Process a constant-state raster interval [builder.line,end). The schedule
 * must correspond to SCB2/3/4 in vram. Both builders are advanced together;
 * order is by slot (allocation order may differ from line-oriented pushes).
 * Caller splits at every VRAM/palette/animation/FIX-source change. */
int kof_strip_push_epoch(kof_strip_builder *sprites,kof_strip_builder *fix,
    const kof_lspc_schedule*,const uint16_t *vram,const uint8_t *zoom,
    const uint16_t *pal,const uint16_t versions[256],unsigned anim,int bios,unsigned end);
/* Independent epochs. FIX may advance ahead of sprites only if its map,
 * palette and source remain constant throughout that interval. */
int kof_strip_push_sprite_epoch(kof_strip_builder*,const kof_lspc_schedule*,
    const uint16_t *vram,const uint8_t *zoom,const uint16_t *pal,
    const uint16_t versions[256],unsigned anim,unsigned end);
/* Same semantics as sprite epoch; memo belongs to this immutable L0 ROM. */
int kof_strip_push_sprite_epoch_cached(kof_strip_builder*,const kof_lspc_schedule*,
    const uint16_t *vram,const uint8_t *zoom,const uint16_t *pal,
    const uint16_t versions[256],unsigned anim,unsigned end,kof_zoom_memo*);
/* Precompute source runs in the immutable L0 memo. All visibility holes,
 * palette splits and strip-height boundaries retain the reference semantics. */
int kof_strip_push_sprite_epoch_runs(kof_strip_builder*,const kof_lspc_schedule*,
    const uint16_t *vram,const uint8_t *zoom,const uint16_t *pal,
    const uint16_t versions[256],unsigned anim,unsigned end,kof_zoom_run_memo*);
int kof_strip_push_fix_epoch(kof_strip_builder*,const uint16_t *vram,
    const uint16_t *pal,const uint16_t versions[256],int bios,unsigned end);
int kof_strip_complete(const kof_strip_builder*);
/* Copy live-emitter cache metadata from a previous frame's strip chains to
 * the current ones. A reference is carried only while the corresponding
 * strips agree on every input of the pattern key and palette LUT (tile,
 * mask, width, height, flip, sampled rows and raw palette words); x/y may
 * differ because they are not part of either key. Carried slots are still
 * generation-checked before use, so an evicted entry simply falls back to
 * the slow resolver. Returns the number of strips that received metadata.
 * When clean is non-NULL, clean[slot] is set for chains that are identical
 * in every hashed field as well (x/y included), so a sequential hash may
 * resume from a boundary after such slots. The core strip representation
 * remains untouched; these fields are never hashed or serialized. */
unsigned kof_strip_carry_cache(kof_strip_builder *cur,const kof_strip_builder *prev,
                               uint8_t *clean);
/* Iterate head[0..380], then next: preserves slot painter order. */
/* 1: original 16x16 pattern reusable, flips returned separately. */
int kof_strip_valid(const kof_strip*);
int kof_strip_native(const kof_strip*,uint8_t *flip_h,uint8_t *flip_v);
/* Pack exact sampled pixels; width padded to 8/16 with transparent pen 0. */
int kof_strip_pixels(const kof_strip*,const uint8_t tile[128],uint8_t out[128],uint16_t *width);
#endif
