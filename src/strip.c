#include "kof98/strip.h"
#include "bytes.h"
static unsigned pop(uint16_t m){unsigned n=0;while(m){m=(uint16_t)(m&(m-1u));++n;}return n;}
int kof_strip_begin(kof_strip_builder *b,kof_strip *s,uint16_t cap){
    if(!b||!s||!cap)return -1;
    memset(b,0,sizeof *b);b->strips=s;b->capacity=cap;
    for(unsigned i=0;i<KOF_STRIP_SLOTS;++i)b->head[i]=b->tail[i]=KOF_STRIP_END;
    return 0;
}
int kof_strip_push(kof_strip_builder *b,const kof_line_ir *ir,const uint16_t *pal,uint32_t words){
    if(!b||!b->strips||b->failed)return -1;
    if(!ir||!pal||words!=4096u||b->line>=224u||ir->count>96u)goto fail;
    unsigned previous=0;
    for(unsigned i=0;i<ir->count;++i){
        const kof_sprite_span *p=&ir->sprites[i];
        if(p->slot>=381u||(i&&p->slot<=previous)||p->tile>=524288u||p->row>=16u||p->flip>1u||!p->width||pop(p->mask)!=p->width||p->x+p->width>320u)goto fail;
        previous=p->slot;
        const uint16_t *colors=pal+16u*p->palette;
        uint16_t tail=b->tail[p->slot];kof_strip *s=tail==KOF_STRIP_END?NULL:&b->strips[tail];
        if(!s||s->height==16u||s->y+s->height!=b->line||s->tile!=p->tile||s->x!=p->x||s->mask!=p->mask||s->flip!=p->flip||kof_memcmp(s->colors,colors,32)){
            if(b->count==b->capacity)goto fail;
            uint16_t idx=b->count++;
            if(s)s->next=idx;else b->head[p->slot]=idx;
            b->tail[p->slot]=idx;s=&b->strips[idx];
            *s=(kof_strip){.tile=p->tile,.x=p->x,.y=b->line,.mask=p->mask,.next=KOF_STRIP_END,.width=p->width,.flip=p->flip};
            memcpy(s->colors,colors,32);
        }
        s->rows[s->height++]=p->row;++b->spans;
    }
    ++b->line;return 0;
fail:b->failed=1;return -1;
}
/* SWAR population count, no loop and no compiler runtime call. */
static unsigned pop_fast(uint16_t m){
    unsigned v=m;
    v=v-((v>>1)&0x5555u);
    v=(v&0x3333u)+((v>>2)&0x3333u);
    v=(v+(v>>4))&0x0f0fu;
    return (v+(v>>8))&31u;
}
static int palette_equal(const uint16_t *a,const uint16_t *b){
    /* Typed halfword reads: no alignment or aliasing assumptions. */
    return ((a[0]^b[0])|(a[1]^b[1])|(a[2]^b[2])|(a[3]^b[3])|
            (a[4]^b[4])|(a[5]^b[5])|(a[6]^b[6])|(a[7]^b[7])|
            (a[8]^b[8])|(a[9]^b[9])|(a[10]^b[10])|(a[11]^b[11])|
            (a[12]^b[12])|(a[13]^b[13])|(a[14]^b[14])|(a[15]^b[15]))==0;
}
int kof_strip_push_versioned(kof_strip_builder *b,const kof_line_ir *ir,const uint16_t *pal,uint32_t words,const uint16_t versions[256]){
    if(!b||!b->strips||b->failed)return -1;
    if(!ir||!pal||!versions||words!=4096u||b->line>=224u||ir->count>96u)goto fail;
    unsigned previous=0;
    for(unsigned i=0;i<ir->count;++i){
        const kof_sprite_span *p=&ir->sprites[i];
        if(p->slot>=381u||(i&&p->slot<=previous)||p->tile>=524288u||p->row>=16u||p->flip>1u||
           !p->width||pop_fast(p->mask)!=p->width||p->x+p->width>320u)goto fail;
        previous=p->slot;
        const uint16_t *colors=pal+16u*p->palette;
        const uint32_t token=1u+((uint32_t)versions[p->palette]<<8)+p->palette;
        const uint16_t tail=b->tail[p->slot];
        kof_strip *s=tail==KOF_STRIP_END?NULL:&b->strips[tail];
        if(!s||s->height==16u||s->y+s->height!=b->line||s->tile!=p->tile||
           s->x!=p->x||s->mask!=p->mask||s->flip!=p->flip||
           (b->palette_token[p->slot]!=token&&!palette_equal(s->colors,colors))){
            if(b->count==b->capacity)goto fail;
            uint16_t idx=b->count++;
            if(s)s->next=idx;else b->head[p->slot]=idx;
            b->tail[p->slot]=idx;s=&b->strips[idx];
            /* Same serialized record as reference; keep unused rows zero. */
            *s=(kof_strip){.tile=p->tile,.x=p->x,.y=b->line,.mask=p->mask,
                .next=KOF_STRIP_END,.width=p->width,.flip=p->flip};
            memcpy(s->colors,colors,32u);
        }
        b->palette_token[p->slot]=token;
        s->rows[s->height++]=p->row;++b->spans;
    }
    ++b->line;return 0;
fail:b->failed=1;return -1;
}
int kof_strip_complete(const kof_strip_builder *b){return b&&b->strips&&!b->failed&&b->line==224u;}
int kof_strip_valid(const kof_strip *s){
    if(!s||!s->height||s->height>16u||!s->width||s->width!=pop(s->mask)||s->flip>1u||s->x+s->width>320u||s->y+s->height>224u)return 0;
    for(unsigned i=0;i<s->height;++i)if(s->rows[i]>=16u)return 0;
    return 1;
}
int kof_strip_native(const kof_strip *s,uint8_t *h,uint8_t *v){
    if(!h||!v||!kof_strip_valid(s)||s->mask!=0xffffu||s->height!=16u)return 0;
    int forward=1,backward=1;
    for(unsigned i=0;i<16u;++i){forward&=s->rows[i]==i;backward&=s->rows[i]==15u-i;}
    if(!forward&&!backward)return 0;
    *h=s->flip;*v=(uint8_t)backward;return 1;
}
int kof_strip_pixels(const kof_strip *s,const uint8_t tile[128],uint8_t out[128],uint16_t *width){
    if(!kof_strip_valid(s)||!tile||!out||!width)return -1;
    unsigned w=(s->width+7u)&~7u;memset(out,0,128);
    for(unsigned y=0;y<s->height;++y){unsigned x=0;for(unsigned i=0;i<16u;++i)if(s->mask&(1u<<i)){
        unsigned col=s->flip?15u-i:i;unsigned pen=(tile[s->rows[y]*8u+col/2u]>>(col%2u?0:4))&15u;
        out[y*(w/2u)+x/2u]|=(uint8_t)(pen<<(x%2u?0:4));++x;
    }}
    *width=(uint16_t)w;return 0;
}
