#ifndef KOF98_PATTERN_KEY_H
#define KOF98_PATTERN_KEY_H
#include "kof98/strip.h"
/* Canonical pixel identity, independent of palette, position and slot.
 * Native 16x16 forward/reverse strips share identical pixels: VDP1 flags
 * carry flips. For variants, the sampled rows and horizontal flip are
 * part of identity. All sixteen row nibbles are canonical, unused = 0. */
typedef struct {uint32_t tile,geometry,rows0,rows1;} kof_pattern_key;
static inline unsigned kof_pattern_pop(uint16_t mask){
    unsigned x=mask;
    x=x-((x>>1)&0x5555u);
    x=(x&0x3333u)+((x>>2)&0x3333u);
    x=(x+(x>>4))&0x0f0fu;
    return (x+(x>>8))&31u;
}
static inline int kof_pattern_key_make(const kof_strip *s,uint32_t bias,
    kof_pattern_key *key,uint16_t *width,uint16_t *flags,int *native){
    if(!s||!key||!width||!flags||!native||!s->height||s->height>16u||
       !s->width||s->width!=kof_pattern_pop(s->mask)||s->flip>1u||
       s->x+s->width>320u||s->y+s->height>224u||
       s->tile>=524288u||s->tile>UINT32_MAX-bias)return -1;
    uint32_t a=0,b=0;
    /* Validating all sampled rows and packing them is one pass; zero-fill
     * ignores any stale bytes beyond height. No variable shifts. */
    for(unsigned i=0;i<8u;++i){
        unsigned row=i<s->height?s->rows[i]:0u;
        if(row>=16u)return -1;
        a=(a<<4)|row;
    }
    for(unsigned i=8u;i<16u;++i){
        unsigned row=i<s->height?s->rows[i]:0u;
        if(row>=16u)return -1;
        b=(b<<4)|row;
    }
    unsigned forward=a==0x01234567u&&b==0x89abcdefu;
    unsigned reverse=a==0xfedcba98u&&b==0x76543210u;
    *native=s->mask==0xffffu&&s->height==16u&&(forward||reverse);
    *flags=0;
    unsigned flip=s->flip;
    if(*native){
        *flags=(uint16_t)((flip?0x20u:0u)|(reverse?0x40u:0u));
        a=0x01234567u;b=0x89abcdefu;flip=0;
    }
    key->tile=s->tile+bias;
    key->geometry=(uint32_t)s->mask|((uint32_t)s->height<<16)|((uint32_t)flip<<24);
    key->rows0=a;key->rows1=b;
    *width=(uint16_t)((s->width+7u)&~7u);
    return 0;
}
static inline uint32_t kof_pattern_key_hash(const kof_pattern_key *k){
    /* Hash selects a bucket only. Exact typed equality is mandatory. */
    uint32_t h=(k->tile*0x9e3779b1u)^k->geometry;
    h^=k->rows0*0x85ebca6bu;h^=k->rows1*0xc2b2ae35u;
    h^=h>>16;h^=h>>8;
    return h;
}
static inline int kof_pattern_key_equal(const kof_pattern_key *a,const kof_pattern_key *b){
    return a->tile==b->tile&&a->geometry==b->geometry&&a->rows0==b->rows0&&a->rows1==b->rows1;
}
#endif
