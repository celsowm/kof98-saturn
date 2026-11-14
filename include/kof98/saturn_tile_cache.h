#ifndef KOF98_SATURN_TILE_CACHE_H
#define KOF98_SATURN_TILE_CACHE_H
#include "kof98/tile_cache.h"
#include <saturn/vdp1.h>
/* Zero-initialize once at a stable address, after sat_init. Arena survives
 * phases but not SDK reinitialization. Master only, synchronous writes. */
typedef struct {
    kof_tile_cache cache;
    uint32_t base;
    sat_result_t last_sdk_error;
} kof_saturn_tile_cache;
sat_result_t kof_saturn_tile_cache_init(kof_saturn_tile_cache*);
kof_asset_result kof_saturn_tile_cache_get(kof_saturn_tile_cache*,uint32_t,const uint16_t[16],sat_vdp1_texture_t*);
#endif
