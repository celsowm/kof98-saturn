#include "kof98/tile_cache.h"
#include <string.h>
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int live(const kof_tile_view *v){return v && v->store && v->count && kof_asset_read(v->store,v->index,0,NULL,0)==KOF_ASSET_OK && kof_asset_read(v->store,v->pixels,0,NULL,0)==KOF_ASSET_OK;}
kof_asset_result kof_tile_view_open(kof_tile_view *out,kof_asset_store *s){
    if(!out||!s)return KOF_ASSET_INVALID;
    kof_tile_view v={0}; v.store=s;
    if(kof_asset_find(s,1,&v.index)!=KOF_ASSET_OK||kof_asset_find(s,2,&v.pixels)!=KOF_ASSET_OK)return KOF_ASSET_MISSING;
    uint32_t n=s->records[v.index.slot].desc.bytes, bytes=s->records[v.pixels.slot].desc.bytes;
    if(!n||n%4u||n/4u>KOF_TILE_ID_LIMIT||bytes!=n/4u*128u)return KOF_ASSET_INVALID;
    v.count=n/4u; uint32_t prev=0; uint8_t buf[128];
    for(uint32_t i=0;i<v.count;){
        uint32_t k=v.count-i;if(k>32u)k=32u;
        kof_asset_result r=kof_asset_read(s,v.index,i*4u,buf,k*4u);if(r!=KOF_ASSET_OK)return r;
        for(uint32_t j=0;j<k;++j){uint32_t id=be32(buf+j*4u);if(id>=KOF_TILE_ID_LIMIT||((i+j)&&id<=prev))return KOF_ASSET_INVALID;prev=id;}
        i+=k;
    }
    *out=v;return KOF_ASSET_OK;
}
kof_asset_result kof_tile_read(const kof_tile_view *v,uint32_t id,uint8_t out[128]){
    if(!out||id>=KOF_TILE_ID_LIMIT)return KOF_ASSET_INVALID;
    if(!live(v))return KOF_ASSET_MISSING;
    uint32_t lo=0,hi=v->count;
    while(lo<hi){uint32_t mid=lo+(hi-lo)/2u;uint8_t b[4];kof_asset_result r=kof_asset_read(v->store,v->index,mid*4u,b,4);if(r!=KOF_ASSET_OK)return r;uint32_t k=be32(b);if(k==id)return kof_asset_read(v->store,v->pixels,mid*128u,out,128);if(k<id)lo=mid+1u;else hi=mid;}
    return KOF_ASSET_MISSING;
}
kof_asset_result kof_tile_cache_init(kof_tile_cache *c,kof_vram_write_fn write,void *ctx){if(!c||!write)return KOF_ASSET_INVALID;memset(c,0,sizeof(*c));c->write=write;c->context=ctx;return KOF_ASSET_OK;}
kof_asset_result kof_tile_cache_begin(kof_tile_cache *c,const kof_tile_view *v){
    if(!c||!c->write)return KOF_ASSET_INVALID;
    if(c->active)return KOF_ASSET_BUSY;
    if(!live(v))return KOF_ASSET_MISSING;
    if(c->view.store!=v->store||c->view.index.generation!=v->index.generation){memset(c->tiles,0,sizeof c->tiles);memset(c->luts,0,sizeof c->luts);memset(c->tile_next,0,sizeof c->tile_next);memset(c->lut_next,0,sizeof c->lut_next);}
    c->view=*v;for(unsigned i=0;i<KOF_TILE_SLOTS;++i)c->tiles[i].pinned=0;for(unsigned i=0;i<KOF_LUT_SLOTS;++i)c->luts[i].pinned=0;c->active=1;return KOF_ASSET_OK;
}
kof_asset_result kof_tile_cache_end(kof_tile_cache *c){if(!c||!c->active)return KOF_ASSET_BUSY;c->active=0;return KOF_ASSET_OK;}
static uint32_t mix(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;return x;}
static uint32_t palette_hash(const uint16_t *p){uint32_t h=2166136261u;for(unsigned i=0;i<16;++i)h=(h^p[i])*16777619u;return mix(h);}
static int tile_slot(kof_tile_cache *c,uint32_t id,unsigned set){
    unsigned start=set*4u;
    for(unsigned i=0;i<4;++i)if(c->tiles[start+i].valid&&c->tiles[start+i].id==id)return (int)(start+i);
    for(unsigned i=0;i<4;++i)if(!c->tiles[start+i].valid&&!c->tiles[start+i].pinned)return (int)(start+i);
    for(unsigned i=0;i<4;++i){unsigned k=(c->tile_next[set]+i)&3u;if(!c->tiles[start+k].pinned){c->tile_next[set]=(uint8_t)((k+1u)&3u);return (int)(start+k);}}
    return -1;
}
static int lut_slot(kof_tile_cache *c,const uint16_t *p,unsigned set){
    unsigned start=set*4u;
    for(unsigned i=0;i<4;++i)if(c->luts[start+i].valid&&!memcmp(c->luts[start+i].colors,p,32))return (int)(start+i);
    for(unsigned i=0;i<4;++i)if(!c->luts[start+i].valid&&!c->luts[start+i].pinned)return (int)(start+i);
    for(unsigned i=0;i<4;++i){unsigned k=(c->lut_next[set]+i)&3u;if(!c->luts[start+k].pinned){c->lut_next[set]=(uint8_t)((k+1u)&3u);return (int)(start+k);}}
    return -1;
}
kof_asset_result kof_tile_cache_get(kof_tile_cache *c,uint32_t id,const uint16_t p[16],kof_tile_binding *out){
    if(!c||!out||!p||id>=KOF_TILE_ID_LIMIT||p[0])return KOF_ASSET_INVALID;
    for(unsigned i=1;i<16;++i)if(!(p[i]&0x8000u))return KOF_ASSET_INVALID;
    if(!c->active)return KOF_ASSET_BUSY;
    if(!live(&c->view))return KOF_ASSET_MISSING;
    int t=tile_slot(c,id,mix(id)&(KOF_TILE_SLOTS/4u-1u)),l=lut_slot(c,p,palette_hash(p)&(KOF_LUT_SLOTS/4u-1u));
    if(t<0||l<0){++c->blocked;return KOF_ASSET_FULL;}
    kof_tile_slot *ts=&c->tiles[t];kof_lut_slot *ls=&c->luts[l];
    uint32_t to=(uint32_t)t*128u,po=KOF_TILE_SLOTS*128u+(uint32_t)l*32u;
    if(!ts->valid||ts->id!=id){uint8_t pixels[128];++c->tile_misses;kof_asset_result r=kof_tile_read(&c->view,id,pixels);if(r!=KOF_ASSET_OK){if(r==KOF_ASSET_MISSING)++c->missing;return r;}ts->valid=0;if(c->write(c->context,to,pixels,128))return KOF_ASSET_IO;ts->id=id;ts->valid=1;}else ++c->tile_hits;
    if(!ls->valid||memcmp(ls->colors,p,32)){++c->lut_misses;ls->valid=0;if(c->write(c->context,po,p,32))return KOF_ASSET_IO;memcpy(ls->colors,p,32);ls->valid=1;}else ++c->lut_hits;
    ts->pinned=ls->pinned=1;out->pixels_offset=to;out->lut_offset=po;return KOF_ASSET_OK;
}
kof_asset_result kof_tile_cache_palette(kof_tile_cache *c,const uint16_t p[16],uint32_t *out){
    if(!c||!out||!p||p[0])return KOF_ASSET_INVALID;
    for(unsigned i=1;i<16;++i)if(!(p[i]&0x8000u))return KOF_ASSET_INVALID;
    if(!c->active)return KOF_ASSET_BUSY;
    if(!live(&c->view))return KOF_ASSET_MISSING;
    int l=lut_slot(c,p,palette_hash(p)&(KOF_LUT_SLOTS/4u-1u));
    if(l<0){++c->blocked;return KOF_ASSET_FULL;}
    kof_lut_slot *ls=&c->luts[l];uint32_t po=KOF_TILE_SLOTS*128u+(uint32_t)l*32u;
    if(!ls->valid||memcmp(ls->colors,p,32)){++c->lut_misses;ls->valid=0;if(c->write(c->context,po,p,32))return KOF_ASSET_IO;memcpy(ls->colors,p,32);ls->valid=1;}else ++c->lut_hits;
    ls->pinned=1;*out=po;return KOF_ASSET_OK;
}
