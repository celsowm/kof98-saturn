#include "kof98/strip_emit.h"
int kof_strip_preflight(const kof_strip_builder *b,unsigned budget,kof_emit_stats *out){
    if(!out||!kof_strip_complete(b)||b->count>b->capacity||b->count>KOF_EMIT_MAX||b->count>budget)return -1;
    uint8_t seen[KOF_EMIT_MAX/8u]={0};kof_emit_stats stats={0};unsigned count=0;
    for(unsigned slot=0;slot<KOF_STRIP_SLOTS;++slot){
        unsigned bottom=0;uint16_t last=KOF_STRIP_END;
        for(uint16_t i=b->head[slot];i!=KOF_STRIP_END;i=b->strips[i].next){
            if(i>=b->count||(seen[i/8u]&(1u<<(i%8u))))return -1;
            seen[i/8u]|=(uint8_t)(1u<<(i%8u));const kof_strip *s=&b->strips[i];
            if(!kof_strip_valid(s)||s->tile>=524288u||s->y<bottom)return -1;
            bottom=s->y+s->height;last=i;++count;uint8_t h,v;
            if(kof_strip_native(s,&h,&v))++stats.native;else if(++stats.variants>KOF_VARIANT_SLOTS)return -1;
        }
        if(last!=b->tail[slot])return -1;
    }
    if(count!=b->count)return -1;
    *out=stats;return 0;
}
