#include "kof98/pattern_key.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint32_t rng=319u;
static uint32_t next(void){rng=rng*1664525u+1013904223u;return rng;}
typedef struct {uint32_t tile;uint16_t mask;uint8_t h,flip,rows[16];} old_key;
static int old(const kof_strip *s,uint32_t bias,old_key *k,uint16_t *width,uint16_t *flags,int *native){
    if(!kof_strip_valid(s)||s->tile>=524288u)return -1;
    uint8_t h=0,v=0;*native=kof_strip_native(s,&h,&v);
    memset(k,0,sizeof *k);k->tile=s->tile+bias;k->mask=s->mask;k->h=s->height;
    k->flip=*native?0:s->flip;
    for(unsigned j=0;j<s->height;++j)k->rows[j]=*native?(uint8_t)j:s->rows[j];
    *width=(uint16_t)((s->width+7u)&~7u);
    *flags=*native?(uint16_t)((h?0x20u:0u)|(v?0x40u:0u)):0;
    return 0;
}
static int test_one(const kof_strip *s,uint32_t bias){
    old_key a;kof_pattern_key b;
    uint16_t aw=0,af=0,bw=0,bf=0;int an=0,bn=0;
    int r=old(s,bias,&a,&aw,&af,&an),t=kof_pattern_key_make(s,bias,&b,&bw,&bf,&bn);
    CHECK(r==t);if(r)return 0;
    CHECK(aw==bw&&af==bf&&an==bn&&a.tile==b.tile);
    CHECK(b.geometry==((uint32_t)a.mask|((uint32_t)a.h<<16)|((uint32_t)a.flip<<24)));
    uint32_t p=0,q=0;
    for(unsigned i=0;i<8u;++i){p=(p<<4)|a.rows[i];q=(q<<4)|a.rows[i+8u];}
    CHECK(p==b.rows0&&q==b.rows1);
    return 0;
}
int main(void){
    kof_strip s={.tile=17,.mask=0xffffu,.height=16,.width=16};
    kof_pattern_key first,key;uint16_t w,f;int native;
    for(unsigned i=0;i<16u;++i)s.rows[i]=(uint8_t)i;
    CHECK(!test_one(&s,0));CHECK(!kof_pattern_key_make(&s,0,&first,&w,&f,&native)&&native&&!f);
    for(unsigned i=0;i<16u;++i)s.rows[i]=(uint8_t)(15u-i);
    s.flip=1;CHECK(!test_one(&s,0));CHECK(!kof_pattern_key_make(&s,0,&key,&w,&f,&native)&&f==0x60u);
    CHECK(kof_pattern_key_equal(&first,&key));CHECK(kof_pattern_key_hash(&first)==kof_pattern_key_hash(&key));
    CHECK(!kof_pattern_key_make(&s,524288u,&key,&w,&f,&native));CHECK(!kof_pattern_key_equal(&first,&key));
    for(unsigned m=1;m<=65535u;++m){
        memset(&s,0,sizeof s);s.mask=(uint16_t)m;
        for(unsigned i=0;i<16u;++i)s.width+=(m>>i)&1u;
        s.height=(uint8_t)(1u+(next()>>28));s.tile=next()&0x7ffffu;s.flip=(uint8_t)(next()>>31);
        s.x=(uint16_t)(320u-s.width);s.y=(uint16_t)(224u-s.height);
        for(unsigned i=0;i<16u;++i)s.rows[i]=(uint8_t)(i<s.height?(next()>>28):255u);
        CHECK(!test_one(&s,(m&1u)?524288u:0u));
        ++s.x;CHECK(!test_one(&s,0));--s.x;
        s.rows[s.height-1u]=16;CHECK(!test_one(&s,0));
    }
    puts("pattern key: exact canonicalization, flips, clipping and 65535 masks PASS");
    return 0;
}
