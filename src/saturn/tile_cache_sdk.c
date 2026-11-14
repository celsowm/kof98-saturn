#include "kof98/saturn_tile_cache.h"
static int upload(void *ctx,uint32_t off,const void *src,uint32_t bytes){
    kof_saturn_tile_cache *s=ctx;
    if(off>KOF_TILE_CACHE_BYTES || bytes>KOF_TILE_CACHE_BYTES-off)return -1;
    s->last_sdk_error=sat_vdp1_vram_write(s->base+off,src,bytes);
    return s->last_sdk_error!=SAT_OK;
}
sat_result_t kof_saturn_tile_cache_init(kof_saturn_tile_cache *s){
    if(!s)return SAT_ERR_INVALID_ARG;
    if(s->cache.write)return SAT_ERR_ALREADY_EXISTS;
    uint32_t raw;
    /* Reserve extra alignment to ensure every 16-word LUT starts on a
     * 32-byte boundary, even though SDK reservation guarantees 8 bytes. */
    SAT_TRY(sat_vdp1_vram_reserve(KOF_TILE_CACHE_BYTES+24u,&raw));
    s->base=(raw+31u)&~31u;s->last_sdk_error=SAT_OK;
    return kof_tile_cache_init(&s->cache,upload,s)==KOF_ASSET_OK?SAT_OK:SAT_ERR_INVALID_ARG;
}
kof_asset_result kof_saturn_tile_cache_get(kof_saturn_tile_cache *s,uint32_t id,const uint16_t colors[16],sat_vdp1_texture_t *out){
    if(!s||!out)return KOF_ASSET_INVALID;
    kof_tile_binding b;
    kof_asset_result r=kof_tile_cache_get(&s->cache,id,colors,&b);if(r!=KOF_ASSET_OK)return r;
    *out=(sat_vdp1_texture_t){.srca=(uint16_t)((s->base+b.pixels_offset)/8u),.width=16,.height=16,.palette=(uint16_t)((s->base+b.lut_offset)/8u),.valid=1,.format=SAT_VDP1_TEXTURE_LUT4};
    return KOF_ASSET_OK;
}
