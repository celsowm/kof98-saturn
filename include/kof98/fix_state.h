#ifndef KOF98_FIX_STATE_H
#define KOF98_FIX_STATE_H
#include <stddef.h>
#include "kof98/lspc_ir.h"
/* FIX consumes only 40*32 map words, the first 16 palettes and the
 * BIOS/cart source flag. Deliberately include nonvisible map rows and
 * pen zero, so exact equality also preserves the reference strip hash.
 * Tile ROMs remain immutable for the lifetime of the snapshot. */
#define KOF_FIX_MAP_START 0x7000u
#define KOF_FIX_MAP_WORDS (40u*32u)
#define KOF_FIX_PAL_WORDS (16u*16u)
typedef struct {
    uint16_t map[KOF_FIX_MAP_WORDS],pal[KOF_FIX_PAL_WORDS];
    uint8_t bios,valid;
    uint32_t hash; /* exact map/palette/bios digest; never serialized */
} kof_fix_snapshot;
static inline int kof_fix_state_word(unsigned idx){
    return (idx>=KOF_FIX_MAP_START&&idx<KOF_FIX_MAP_START+KOF_FIX_MAP_WORDS)||
           (idx>=KOF_LSPC_WORDS&&idx<KOF_LSPC_WORDS+KOF_FIX_PAL_WORDS);
}
static inline int kof_fix_snapshot_equal(const kof_fix_snapshot *s,
    const uint16_t *v,const uint16_t *pal,unsigned bios){
    if(!s->valid||s->bios!=bios)return 0;
    for(unsigned i=0;i<KOF_FIX_MAP_WORDS;++i)
        if(s->map[i]!=v[KOF_FIX_MAP_START+i])return 0;
    for(unsigned i=0;i<KOF_FIX_PAL_WORDS;++i)
        if(s->pal[i]!=pal[i])return 0;
    return 1;
}
static inline void kof_fix_snapshot_save(kof_fix_snapshot *s,
    const uint16_t *v,const uint16_t *pal,unsigned bios){
    uint32_t h=2166136261u;
    for(unsigned i=0;i<KOF_FIX_MAP_WORDS;++i){s->map[i]=v[KOF_FIX_MAP_START+i];h=(h^s->map[i])*16777619u;}
    for(unsigned i=0;i<KOF_FIX_PAL_WORDS;++i){s->pal[i]=pal[i];h=(h^s->pal[i])*16777619u;}
    s->bios=(uint8_t)bios;s->valid=1;
    h=(h^(uint32_t)bios)*16777619u;
    s->hash=h;
}
/* Look ahead only to find the next FIX-state boundary, never its values.
 * Malformed/truncated headers terminate the safe interval; the ordinary
 * replay reader still validates all packet contents before submission. */
static inline unsigned kof_fix_trace_until(const uint8_t *p,const uint8_t *end,
    unsigned line,unsigned bios){
    while(line<224u){
        if((size_t)(end-p)<6u)return line;
        unsigned next_bios=((unsigned)p[2]<<8)|p[3];
        unsigned n=((unsigned)p[4]<<8)|p[5];p+=6;
        if(next_bios!=bios||(size_t)(end-p)<n*4u)return line;
        for(unsigned j=0;j<n;++j,p+=4){
            unsigned idx=((unsigned)p[0]<<8)|p[1];
            if(kof_fix_state_word(idx))return line;
        }
        ++line;
    }
    return line;
}
#endif
