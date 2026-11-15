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
#endif
