#include "kof98/fix_state.h"
#include "kof98/strip.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint16_t v[KOF_LSPC_WORDS],pal[4096],versions[256];
static kof_fix_snapshot snapshot;
static kof_strip a[2048],b[2048];
static int chains(const kof_strip_builder *x,const kof_strip_builder *y){
    if(x->count!=y->count||x->spans!=y->spans)return 0;
    for(unsigned c=0;c<381u;++c){
        uint16_t i=x->head[c],j=y->head[c];
        while(i!=KOF_STRIP_END&&j!=KOF_STRIP_END){
            const kof_strip *s=&x->strips[i],*t=&y->strips[j];
            if(s->tile!=t->tile||s->x!=t->x||s->y!=t->y||s->mask!=t->mask||
               s->height!=t->height||s->width!=t->width||s->flip!=t->flip||
               memcmp(s->colors,t->colors,32)||memcmp(s->rows,t->rows,16))return 0;
            i=s->next;j=t->next;
        }
        if(i!=KOF_STRIP_END||j!=KOF_STRIP_END)return 0;
    }
    return 1;
}
static int compare(unsigned dynamic){
    kof_strip_builder slow,fast;kof_line_ir ir={0};
    memset(v,0,sizeof v);memset(versions,0,sizeof versions);
    for(unsigned i=0;i<4096u;++i)pal[i]=(uint16_t)i;
    for(unsigned i=0;i<KOF_FIX_MAP_WORDS;++i)v[KOF_FIX_MAP_START+i]=(uint16_t)(((i/32u)%16u)<<12|(i%3u));
    CHECK(!kof_strip_begin(&slow,a,2048));CHECK(!kof_strip_begin(&fast,b,2048));
    for(unsigned y=0;y<224u;++y){
        if(dynamic&&y==73u){v[KOF_FIX_MAP_START+11u]^=1u;pal[0]^=0x8000u;++versions[0];}
        unsigned bios=dynamic&&y>=129u;
        if(!y||(dynamic&&(y==73u||y==129u))){
            unsigned end=!dynamic?224u:y==0?73u:y==73u?129u:224u;
            CHECK(!kof_strip_push_fix_epoch(&fast,v,pal,versions,(int)bios,end));
        }
        /* Unrelated sprite writes must not affect FIX output. */
        v[0x8200u]=(uint16_t)y;
        ir.count=40;
        for(unsigned c=0;c<40u;++c){
            unsigned e=v[KOF_FIX_MAP_START+c*32u+(y+16u)/8u];
            ir.sprites[c]=(kof_sprite_span){.tile=(e&4095u)+(bios?4096u:0u),
                .x=(uint16_t)(c*8u),.slot=(uint16_t)c,.mask=255u,.width=8u,
                .row=(uint8_t)(y&7u),.palette=(uint8_t)(e>>12)};
        }
        CHECK(!kof_strip_push(&slow,&ir,pal,4096u));
    }
    CHECK(kof_strip_complete(&slow)&&kof_strip_complete(&fast));
    CHECK(chains(&slow,&fast));
    return 0;
}
int main(void){
    CHECK(!compare(0));CHECK(!compare(1));
    CHECK(!kof_fix_snapshot_equal(&snapshot,v,pal,0));
    kof_fix_snapshot_save(&snapshot,v,pal,0);
    CHECK(kof_fix_snapshot_equal(&snapshot,v,pal,0));
    v[0x8200u]^=1u;pal[256]^=1u;
    CHECK(kof_fix_snapshot_equal(&snapshot,v,pal,0));
    CHECK(!kof_fix_snapshot_equal(&snapshot,v,pal,1));
    v[KOF_FIX_MAP_START+KOF_FIX_MAP_WORDS-1u]^=1u;
    CHECK(!kof_fix_snapshot_equal(&snapshot,v,pal,0));
    v[KOF_FIX_MAP_START+KOF_FIX_MAP_WORDS-1u]^=1u;pal[0]^=1u;
    CHECK(!kof_fix_snapshot_equal(&snapshot,v,pal,0));
    /* Three valid line headers: non-FIX write, empty, FIX map write. */
    const uint8_t trace[]={0,0,0,0,0,1,0x82,0,0,1,
                           0,0,0,0,0,0,
                           0,0,0,0,0,1,0x70,0,0,1};
    CHECK(kof_fix_trace_until(trace,trace+sizeof trace,10,0)==12);
    CHECK(kof_fix_trace_until(trace,trace+sizeof trace,10,1)==10);
    CHECK(kof_fix_trace_until(trace,trace+7,10,0)==10);
    CHECK(kof_fix_state_word(KOF_LSPC_WORDS+255u));
    CHECK(!kof_fix_state_word(KOF_LSPC_WORDS+256u));
    puts("FIX independent epochs, exact snapshot and trace boundaries PASS");
    return 0;
}
