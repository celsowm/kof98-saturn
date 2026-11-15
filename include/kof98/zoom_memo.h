#ifndef KOF98_ZOOM_MEMO_H
#define KOF98_ZOOM_MEMO_H
#include <stdint.h>
#define KOF_ZOOM_MEMO_SLOTS 16u
/* Caller-owned, initially zero. Reset the cache whenever the 64 KiB L0
 * ROM contents change. Returned pointer stays valid until the next get
 * using the same set. Map values are 9-bit tile/row addresses. */
typedef struct {
    uint16_t tags[KOF_ZOOM_MEMO_SLOTS];
    uint16_t maps[KOF_ZOOM_MEMO_SLOTS][512];
    uint32_t hits,misses;
} kof_zoom_memo;
static inline const uint16_t *kof_zoom_memo_get(kof_zoom_memo *m,const uint8_t *z,unsigned zy,unsigned tall){
    unsigned set=(zy^(zy>>4)^(tall?7u:0u))&(KOF_ZOOM_MEMO_SLOTS-1u);
    unsigned tag=1u+zy+(tall?256u:0u);
    uint16_t *out=m->maps[set];
    if(m->tags[set]==tag){++m->hits;return out;}
    ++m->misses;
    const uint8_t *base=z+zy*256u;
    unsigned phase=0,period=(zy+1u)*2u;
    for(unsigned i=0;i<256u;++i){
        unsigned value;
        if(!tall)value=base[i];
        else{
            value=phase<=zy?base[phase]:(base[period-1u-phase]^511u);
            if(++phase==period)phase=0;
        }
        out[i]=(uint16_t)value;out[511u-i]=(uint16_t)(value^511u);
    }
    m->tags[set]=(uint16_t)tag;
    return out;
}
/* Run metadata belongs to exactly the same immutable L0 as the base map.
 * Store up to 15 extra rows so even a wrapped run has contiguous storage.
 * Lengths are capped at the strip height limit, not zoom tile height.
 * Publish metadata only after both row arrays and every length are valid. */
typedef struct {
    uint8_t length[512],rows[527],flipped[527];
} kof_zoom_runs;
typedef struct {
    kof_zoom_memo base;
    uint16_t tags[KOF_ZOOM_MEMO_SLOTS];
    kof_zoom_runs runs[KOF_ZOOM_MEMO_SLOTS];
} kof_zoom_run_memo;
static inline const kof_zoom_runs *kof_zoom_runs_get(kof_zoom_run_memo *m,
    const uint8_t *z,unsigned zy,unsigned tall,const uint16_t **map){
    unsigned set=(zy^(zy>>4)^(tall?7u:0u))&(KOF_ZOOM_MEMO_SLOTS-1u);
    unsigned tag=1u+zy+(tall?256u:0u);
    *map=kof_zoom_memo_get(&m->base,z,zy,tall);
    kof_zoom_runs *r=&m->runs[set];
    if(m->tags[set]==tag)return r;
    unsigned prev=32u,n=0;
    for(unsigned k=527u;k!=0u;){
        unsigned i=--k,a=(*map)[i&511u],tile=a>>4;
        n=tile==prev?(n<16u?n+1u:16u):1u;
        prev=tile;
        r->rows[i]=(uint8_t)(a&15u);r->flipped[i]=(uint8_t)((a&15u)^15u);
        if(i<512u)r->length[i]=(uint8_t)n;
    }
    m->tags[set]=(uint16_t)tag;
    return r;
}
#endif
