#include "kof98/strip_hash.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint32_t rng=7;
static uint32_t next(void){rng=rng*1664525u+1013904223u;return rng;}
static void be16(uint8_t **p,unsigned v){*(*p)++=(uint8_t)(v>>8);*(*p)++=(uint8_t)v;}
static uint32_t reference(uint32_t h,const kof_strip *s,uint32_t bias){
    uint8_t bytes[64],*p=bytes;
    uint32_t tile=s->tile+bias;
    be16(&p,tile>>16);be16(&p,tile);
    be16(&p,s->x);be16(&p,s->y);be16(&p,s->mask);
    be16(&p,s->height);be16(&p,s->width);be16(&p,s->flip);
    for(unsigned i=0;i<16u;++i)be16(&p,s->colors[i]);
    for(unsigned i=0;i<16u;++i)*p++=s->rows[i];
    for(unsigned i=0;i<sizeof bytes;++i)h=(h^bytes[i])*16777619u;
    return h;
}
int main(void){
    uint32_t a=2166136261u,b=a;
    for(unsigned j=0;j<100000u;++j){
        kof_strip s={0};
        s.tile=next()&0x7ffffu;s.x=(uint16_t)next();s.y=(uint16_t)next();
        s.mask=(uint16_t)next();s.height=(uint8_t)next();
        s.width=(uint8_t)next();s.flip=(uint8_t)next();s.next=(uint16_t)next();
        for(unsigned i=0;i<16u;++i){s.colors[i]=(uint16_t)next();s.rows[i]=(uint8_t)(next()>>24);}
        unsigned bias=(j&1u)?524288u:0u;
        a=reference(a,&s,bias);b=kof_strip_hash_record(b,&s,bias);
        CHECK(a==b);
        uint32_t seed=next();CHECK(reference(seed,&s,bias)==kof_strip_hash_record(seed,&s,bias));
    }
    puts("strip hash: 100000 records, chained and arbitrary seed PASS");return 0;
}
