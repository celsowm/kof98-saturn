#ifndef KOF98_TILE_CACHE_H
#define KOF98_TILE_CACHE_H
#include "kof98/asset_store.h"
#define KOF_TILE_SLOTS 512u
#define KOF_LUT_SLOTS 64u
#define KOF_TILE_CACHE_BYTES (KOF_TILE_SLOTS*128u+KOF_LUT_SLOTS*32u)
#define KOF_TILE_ID_LIMIT 524288u
/* Backend must fence the last submitted list before writing any VRAM bytes.
 * Calls are synchronous; source storage is borrowed only for the call. */
typedef int (*kof_vram_write_fn)(void*,uint32_t,const void*,uint32_t);
typedef struct { kof_asset_store *store; kof_asset_handle index,pixels; uint32_t count; } kof_tile_view;
typedef struct { uint32_t id; uint8_t valid,pinned; } kof_tile_slot;
typedef struct { uint16_t colors[16]; uint8_t valid,pinned; } kof_lut_slot;
typedef struct { uint32_t pixels_offset,lut_offset; } kof_tile_binding;
typedef struct {
    kof_tile_view view;
    kof_vram_write_fn write; void *context;
    uint8_t active;
    kof_tile_slot tiles[KOF_TILE_SLOTS];
    kof_lut_slot luts[KOF_LUT_SLOTS];
    uint8_t tile_next[KOF_TILE_SLOTS/4u],lut_next[KOF_LUT_SLOTS/4u];
    uint32_t tile_hits,tile_misses,lut_hits,lut_misses,blocked,missing;
} kof_tile_cache;
kof_asset_result kof_tile_view_open(kof_tile_view*,kof_asset_store*);
kof_asset_result kof_tile_read(const kof_tile_view*,uint32_t id,uint8_t out[128]);
kof_asset_result kof_tile_cache_init(kof_tile_cache*,kof_vram_write_fn,void*);
kof_asset_result kof_tile_cache_begin(kof_tile_cache*,const kof_tile_view*);
/* Must be called only after successful submission or actual discard of the
 * prepared list. The backend still fences submitted lists before overwrite. */
kof_asset_result kof_tile_cache_end(kof_tile_cache*);
/* LUT words are native-endian RGB555/MSB; pen zero must be zero. On any
 * failure the caller must discard the prepared list, not omit a sprite. */
kof_asset_result kof_tile_cache_palette(kof_tile_cache*,const uint16_t[16],uint32_t*);
kof_asset_result kof_tile_cache_get(kof_tile_cache*,uint32_t id,const uint16_t colors[16],kof_tile_binding*);
#endif
