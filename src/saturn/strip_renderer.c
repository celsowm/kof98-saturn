#include "kof98/saturn_strip_renderer.h"
#include <saturn/video.h>
static sat_result_t asset_status(kof_saturn_strip_renderer *r,kof_asset_result a){
    r->asset_error=a;
    switch(a){case KOF_ASSET_OK:return SAT_OK;case KOF_ASSET_FULL:return SAT_ERR_CAPACITY;case KOF_ASSET_MISSING:return SAT_ERR_NOT_FOUND;case KOF_ASSET_BUSY:return SAT_ERR_BUSY;case KOF_ASSET_INVALID:return SAT_ERR_INVALID_ARG;default:return SAT_ERR_IO;}
}
sat_result_t kof_saturn_strip_renderer_init(kof_saturn_strip_renderer *r,kof_saturn_tile_cache *t){
    if(!r||!t||!t->cache.write)return SAT_ERR_INVALID_ARG;
    if(r->tiles)return SAT_ERR_ALREADY_EXISTS;
    SAT_TRY(sat_vdp1_vram_reserve(KOF_VARIANT_BYTES,&r->base));r->tiles=t;return SAT_OK;
}
sat_result_t kof_saturn_strip_draw(kof_saturn_strip_renderer *r,const kof_strip_builder *b,const kof_tile_view *view,kof_emit_stats *out){
    if(!r||!r->tiles||!out)return SAT_ERR_INVALID_ARG;
    if(r->tiles->cache.active)return SAT_ERR_BUSY;
    r->rollback_error=SAT_OK;r->asset_error=KOF_ASSET_OK;
    if(sat_video_width()!=320||sat_video_height()!=224)return SAT_ERR_UNSUPPORTED;
    sat_vdp1_command_checkpoint_t cp;SAT_TRY(sat_vdp1_command_checkpoint(&cp));
    sat_vdp1_command_stats_t cs;SAT_TRY(sat_vdp1_command_stats(&cs));
    unsigned reserved=cs.overlay_pass?0u:cs.overlay_reserved;
    if(cs.overlay_pass||(unsigned)cs.used+reserved+1u>cs.capacity)return SAT_ERR_CAPACITY;
    kof_emit_stats stats;if(kof_strip_preflight(b,cs.capacity-cs.used-reserved-1u,&stats))return SAT_ERR_CAPACITY;
    sat_result_t st=asset_status(r,kof_tile_cache_begin(&r->tiles->cache,view));if(st!=SAT_OK)return st;
    unsigned variant=0;
    for(unsigned slot=0;slot<KOF_STRIP_SLOTS;++slot)for(uint16_t i=b->head[slot];i!=KOF_STRIP_END;i=b->strips[i].next){
        const kof_strip *s=&b->strips[i];uint16_t lut[16]={0};for(unsigned p=1;p<16;++p)lut[p]=kof_lspc_rgb555(s->colors[p]);
        sat_vdp1_texture_t tex;uint8_t h=0,v=0;
        if(kof_strip_native(s,&h,&v)){
            st=asset_status(r,kof_saturn_tile_cache_get(r->tiles,s->tile,lut,&tex));if(st!=SAT_OK)goto fail;
        }else{
            uint8_t raw[128],pixels[128];uint16_t width;uint32_t po;
            st=asset_status(r,kof_tile_read(view,s->tile,raw));if(st!=SAT_OK)goto fail;
            if(kof_strip_pixels(s,raw,pixels,&width)){st=SAT_ERR_INVALID_ARG;goto fail;}
            st=asset_status(r,kof_tile_cache_palette(&r->tiles->cache,lut,&po));if(st!=SAT_OK)goto fail;
            uint32_t off=r->base+variant++*128u;
            st=sat_vdp1_vram_write(off,pixels,128);if(st!=SAT_OK)goto fail;
            tex=(sat_vdp1_texture_t){.srca=(uint16_t)(off/8u),.width=width,.height=s->height,.palette=(uint16_t)((r->tiles->base+po)/8u),.valid=1,.format=SAT_VDP1_TEXTURE_LUT4};
        }
        sat_sprite_cmd_t cmd={.x=((int32_t)s->x-160)*SAT_FX16_ONE,.y=((int32_t)s->y-112)*SAT_FX16_ONE,.width=tex.width,.height=tex.height,.texture=&tex,.flags=(uint16_t)((h?SAT_SPRITE_FLAG_FLIP_H:0u)|(v?SAT_SPRITE_FLAG_FLIP_V:0u))};
        st=sat_draw_sprite(&cmd);if(st!=SAT_OK)goto fail;
    }
    *out=stats;return SAT_OK;
fail:
    r->rollback_error=sat_vdp1_command_rollback(&cp);
    if(r->rollback_error==SAT_OK)(void)kof_tile_cache_end(&r->tiles->cache);
    return st;
}
