#include "kof98/strip.h"
#include "bytes.h"
#ifndef KOF_LIVE_COMPACT_BUILD
#define KOF_LIVE_COMPACT_BUILD 0
#endif
static unsigned pop(uint16_t m){unsigned n=0;while(m){m=(uint16_t)(m&(m-1u));++n;}return n;}
int kof_strip_begin(kof_strip_builder *b,kof_strip *s,uint16_t cap){
    if(!b||!s||!cap)return -1;
#if KOF_LIVE_COMPACT_BUILD
    /* Tokens are meaningful only when a slot has a tail. Reset every
     * chain first; the first append publishes its token before any read.
     * Avoid clearing 381 dead tokens and clearing heads twice. */
    b->strips=s;b->capacity=cap;b->count=0;b->line=0;b->spans=0;b->failed=0;
#else
    memset(b,0,sizeof *b);b->strips=s;b->capacity=cap;
#endif
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
/* Append a contiguous run in one slot. All fields except row and y remain
 * constant; runs may span a strip boundary. Exact palette checks are still
 * required when tokens differ. Only this internal caller supplies runs. */
static int append_run(kof_strip_builder *b,unsigned slot,unsigned y,uint32_t tile,
    unsigned x,uint16_t mask,unsigned width,unsigned flip,unsigned palette,
    const uint16_t *pal,const uint16_t *versions,const uint8_t *rows,unsigned n){
    /* Internal runs are nonempty and at most one strip long. Fail before
     * modifying the builder if a caller violates that contract. */
    if(!n||n>KOF_STRIP_ROWS)return -1;
    const uint16_t *colors=pal+16u*palette;
    const uint32_t token=1u+((uint32_t)versions[palette]<<8)+palette;
    uint16_t tail=b->tail[slot];
    kof_strip *s=tail==KOF_STRIP_END?NULL:&b->strips[tail];
    if(s&&s->height<16u&&s->y+s->height==y&&s->tile==tile&&s->x==x&&
       s->mask==mask&&s->flip==flip&&
       (b->palette_token[slot]==token||palette_equal(s->colors,colors))){
        unsigned take=16u-s->height;if(take>n)take=n;
        for(unsigned i=0;i<take;++i)s->rows[s->height+i]=rows[i];
        s->height=(uint8_t)(s->height+take);b->spans+=take;
        y+=take;rows+=take;n-=take;
    }
    if(n){
        if(b->count==b->capacity)return -1;
        uint16_t idx=b->count++;
        if(s)s->next=idx;else b->head[slot]=idx;
        b->tail[slot]=idx;s=&b->strips[idx];
#if KOF_LIVE_COMPACT_BUILD
        /* Initialize each semantic field once. Never hash struct padding.
         * The unused row suffix MUST be zero for the KFR2 oracle. */
        s->tile=tile;s->x=(uint16_t)x;s->y=(uint16_t)y;s->mask=mask;
        s->next=KOF_STRIP_END;s->width=(uint8_t)width;s->flip=(uint8_t)flip;s->height=(uint8_t)n;
        s->colors[0]=colors[0];s->colors[1]=colors[1];
        s->colors[2]=colors[2];s->colors[3]=colors[3];
        s->colors[4]=colors[4];s->colors[5]=colors[5];
        s->colors[6]=colors[6];s->colors[7]=colors[7];
        s->colors[8]=colors[8];s->colors[9]=colors[9];
        s->colors[10]=colors[10];s->colors[11]=colors[11];
        s->colors[12]=colors[12];s->colors[13]=colors[13];
        s->colors[14]=colors[14];s->colors[15]=colors[15];
        /* Explicit destination bound also holds after GCC's loop
         * optimization. Read only the supplied n source bytes. */
        for(unsigned i=0;i<KOF_STRIP_ROWS;++i)
            s->rows[i]=i<n?rows[i]:0u;
#else
        *s=(kof_strip){.tile=tile,.x=(uint16_t)x,.y=(uint16_t)y,.mask=mask,
            .next=KOF_STRIP_END,.width=(uint8_t)width,.flip=(uint8_t)flip,.height=(uint8_t)n};
        memcpy(s->colors,colors,32u);
        for(unsigned i=0;i<n;++i)s->rows[i]=rows[i];
#endif
        b->spans+=n;
    }
    b->palette_token[slot]=token;
    return 0;
}
static unsigned source_row(const kof_lspc_column *c,const uint8_t *z,unsigned y){
    unsigned sl=(y+16u-c->y)&511u,zl=sl&255u,invert=sl>>8;
    if(invert)zl^=255u;
    if(c->rows>32u){
        unsigned period=((unsigned)c->zy+1u)*2u;
        if(zl>=period)zl%=period;
        if(zl>c->zy){zl=period-1u-zl;invert^=1u;}
    }
    unsigned a=z[((unsigned)c->zy<<8)|zl];
    return invert?(a^511u):a;
}
static int selected(const kof_lspc_column *c,unsigned y){
    return (c->selected[y>>5]&kof_lspc_bit[y&31u])!=0;
}
int kof_strip_push_sprite_epoch(kof_strip_builder *b,
    const kof_lspc_schedule *schedule,const uint16_t *v,const uint8_t *z,
    const uint16_t *pal,const uint16_t versions[256],unsigned anim,unsigned end){
    if(!b||!b->strips||b->failed)return -1;
    if(!schedule||!v||!z||!pal||!versions||end<=b->line||end>224u)goto fail_epoch;
    const unsigned first=b->line;
    for(unsigned slot=0;slot<381u;++slot){
        const kof_lspc_column *c=&schedule->columns[slot];
        unsigned y=c->first>first?c->first:first,stop=c->last<end?c->last:end;
        while(y<stop){
            if(!selected(c,y)){++y;continue;}
            unsigned a=source_row(c,z,y),tm=slot*64u+(a>>4)*2u,attr=v[tm+1u];
            uint32_t code=v[tm]|((attr&0xf0u)<<12);
            if(attr&8u)code=(code&~7u)|(anim&7u);else if(attr&4u)code=(code&~3u)|(anim&3u);
            unsigned flip=(attr&2u)?15u:0u;
            uint8_t rows[16];unsigned n=0,start=y;
            do{
                rows[n++]=(uint8_t)((a&15u)^flip);++y;
                if(y==stop||n==16u||!selected(c,y))break;
                a=source_row(c,z,y);
            }while(slot*64u+(a>>4)*2u==tm);
            if(append_run(b,slot,start,code&0x7ffffu,c->x,c->mask,c->width,attr&1u,
                          attr>>8,pal,versions,rows,n))goto fail_epoch;
        }
    }
    b->line=(uint16_t)end;return 0;
fail_epoch:
    b->failed=1;return -1;
}
int kof_strip_push_sprite_epoch_cached(kof_strip_builder *b,
    const kof_lspc_schedule *schedule,const uint16_t *v,const uint8_t *z,
    const uint16_t *pal,const uint16_t versions[256],unsigned anim,unsigned end,kof_zoom_memo *memo){
    if(!b||!b->strips||b->failed)return -1;
    if(!schedule||!v||!z||!pal||!versions||!memo||end<=b->line||end>224u)goto fail;
    const unsigned first=b->line;
    for(unsigned slot=0;slot<381u;++slot){
        const kof_lspc_column *c=&schedule->columns[slot];
        unsigned y=c->first>first?c->first:first,stop=c->last<end?c->last:end;
        if(y>=stop)continue;
        const unsigned contiguous=c->selected_count==(unsigned)c->last-c->first;
        const uint16_t *map=kof_zoom_memo_get(memo,z,c->zy,c->rows>32u);
        const unsigned offset=(16u-c->y)&511u;
        const uint16_t *scb=v+slot*64u;
        while(y<stop){
            if(!contiguous&&!selected(c,y)){++y;continue;}
            unsigned a=map[(y+offset)&511u],tile=a>>4,attr=scb[tile*2u+1u];
            uint32_t code=scb[tile*2u]|((attr&0xf0u)<<12);
            if(attr&8u)code=(code&~7u)|(anim&7u);else if(attr&4u)code=(code&~3u)|(anim&3u);
            unsigned flip=(attr&2u)?15u:0u;
            uint8_t rows[16];unsigned n=0,start=y;
            do{
                rows[n++]=(uint8_t)((a&15u)^flip);++y;
                if(y==stop||n==16u||(!contiguous&&!selected(c,y)))break;
                a=map[(y+offset)&511u];
            }while((a>>4)==tile);
            if(append_run(b,slot,start,code&0x7ffffu,c->x,c->mask,c->width,attr&1u,
                          attr>>8,pal,versions,rows,n))goto fail;
        }
    }
    b->line=(uint16_t)end;return 0;
fail:
    b->failed=1;return -1;
}
int kof_strip_push_sprite_epoch_runs(kof_strip_builder *b,
    const kof_lspc_schedule *schedule,const uint16_t *v,const uint8_t *z,
    const uint16_t *pal,const uint16_t versions[256],unsigned anim,unsigned end,kof_zoom_run_memo *memo){
    if(!b||!b->strips||b->failed)return -1;
    if(!schedule||!v||!z||!pal||!versions||!memo||end<=b->line||end>224u)goto fail;
    const unsigned first=b->line;
#if KOF_LIVE_COMPACT_BUILD
    if(schedule->active_count>381u)goto fail;
    for(unsigned j=0;j<schedule->active_count;++j){
        unsigned slot=schedule->active_slots[j];
        if(slot>=381u)goto fail;
#else
    for(unsigned slot=0;slot<381u;++slot){
#endif
        const kof_lspc_column *c=&schedule->columns[slot];
        unsigned y=c->first>first?c->first:first,stop=c->last<end?c->last:end;
        if(y>=stop)continue;
        const unsigned contiguous=c->selected_count==(unsigned)c->last-c->first;
        const uint16_t *map;
        const kof_zoom_runs *runs=kof_zoom_runs_get(memo,z,c->zy,c->rows>32u,&map);
        const unsigned offset=(16u-c->y)&511u;
        const uint16_t *scb=v+slot*64u;
        while(y<stop){
            if(!contiguous&&!selected(c,y)){++y;continue;}
            unsigned phase=(y+offset)&511u;
            unsigned tile=map[phase]>>4,attr=scb[tile*2u+1u];
            uint32_t code=scb[tile*2u]|((attr&0xf0u)<<12);
            if(attr&8u)code=(code&~7u)|(anim&7u);else if(attr&4u)code=(code&~3u)|(anim&3u);
            unsigned n=runs->length[phase];
            if(n>stop-y)n=stop-y;
            if(!contiguous){
                unsigned i=1;while(i<n&&selected(c,y+i))++i;n=i;
            }
            const uint8_t *rows=(attr&2u?runs->flipped:runs->rows)+phase;
            if(append_run(b,slot,y,code&0x7ffffu,c->x,c->mask,c->width,attr&1u,
                          attr>>8,pal,versions,rows,n))goto fail;
            y+=n;
        }
    }
    b->line=(uint16_t)end;return 0;
fail:
    b->failed=1;return -1;
}
int kof_strip_push_fix_epoch(kof_strip_builder *f,const uint16_t *v,
    const uint16_t *pal,const uint16_t versions[256],int bios,unsigned end){
    if(!f||!f->strips||f->failed)return -1;
    if(!v||!pal||!versions||(bios!=0&&bios!=1)||end<=f->line||end>224u)goto fail_fix;
    const unsigned first=f->line;
    for(unsigned col=0;col<40u;++col){
        unsigned y=first;
        while(y<end){
            const unsigned scan=y+16u,entry=v[0x7000u+col*32u+scan/8u],row=scan&7u;
            unsigned n=8u-row;if(n>end-y)n=end-y;
            uint8_t rows[16];for(unsigned j=0;j<n;++j)rows[j]=(uint8_t)(row+j);
            if(append_run(f,col,y,(entry&4095u)+(bios?4096u:0u),col*8u,255u,8u,0u,
                          entry>>12,pal,versions,rows,n))goto fail_fix;
            y+=n;
        }
    }
    f->line=(uint16_t)end;return 0;
fail_fix:
    f->failed=1;return -1;
}
int kof_strip_push_epoch(kof_strip_builder *b,kof_strip_builder *f,
    const kof_lspc_schedule *schedule,const uint16_t *v,const uint8_t *z,
    const uint16_t *pal,const uint16_t versions[256],unsigned anim,int bios,unsigned end){
    if(!b||!f||b==f||!b->strips||!f->strips||b->failed||f->failed)return -1;
    if(b->line!=f->line||(bios!=0&&bios!=1)||
       kof_strip_push_sprite_epoch(b,schedule,v,z,pal,versions,anim,end)||
       kof_strip_push_fix_epoch(f,v,pal,versions,bios,end)){
        b->failed=f->failed=1;return -1;
    }
    return 0;
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
