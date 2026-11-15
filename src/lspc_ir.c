#include "kof98/lspc_ir.h"
static const uint16_t masks[16]={0x0100u,0x0110u,0x1110u,0x1114u,0x5114u,0x5154u,0x5554u,0x5555u,0x5755u,0x575du,0xd75du,0xd7ddu,0xf7ddu,0xf7dfu,0xffdfu,0xffffu};
static int visible(int line,int y,int rows){if(rows>32)rows=32;int end=(y+rows*16-1)&511;return end>=y?(line>=y&&line<=end):(line>=y||line<=end);}
uint16_t kof_lspc_rgb555(uint16_t w){unsigned r=((w>>7)&30u)|((w>>14)&1u),g=((w>>3)&30u)|((w>>13)&1u),b=((w<<1)&30u)|((w>>12)&1u);return (uint16_t)(0x8000u|r|(g<<5)|(b<<10));}
int kof_lspc_line(const uint16_t *v,uint32_t words,const uint8_t *z,uint32_t nz,unsigned anim,unsigned line,int bios,kof_line_ir *o){
 if(!v||words<KOF_LSPC_WORDS||!z||nz!=KOF_ZOOM_BYTES||line>=224||!o||(bios!=0&&bios!=1))return -1;
 o->count=0;o->evaluated=0;o->fix_bios=(uint8_t)bios;
 int scan=(int)line+16,x=0,y=0,rows=0,zy=0,zx=0;
 for(unsigned n=0;n<381;n++){
  unsigned ctl=v[0x8200+n],zoom=v[0x8000+n];
  if(ctl&64u){x=(x+zx+1)&511;zx=(zoom>>8)&15;}
  else{y=512-(int)(ctl>>7);x=v[0x8400+n]>>7;zy=zoom&255;zx=(zoom>>8)&15;rows=ctl&63;}
  if(!rows||!visible(scan,y,rows))continue;
  if(o->evaluated==KOF_LINE_SPRITES)break;
  ++o->evaluated;
  unsigned px=(unsigned)x, mask=0, first=0,width=0;
  for(unsigned i=0;i<16;i++)if(masks[zx]&(1u<<i)){if(px<320){if(!width)first=px;mask|=1u<<i;++width;}px=(px+1u)&511u;}
  if(!width)continue;
  int sl=(scan-y)&511, zl=sl&255, invert=(sl&256)!=0;
  if(invert)zl^=255;
  if(rows>32){int period=(zy+1)*2;zl%=period;if(zl>zy){zl=period-1-zl;invert=!invert;}}
  unsigned yt=z[(zy<<8)|zl],tile=yt>>4,row=yt&15;
  if(invert){row^=15;tile^=31;}
  unsigned tm=n*64u+tile*2u,attr=v[tm+1];uint32_t code=v[tm]|((attr&0xf0u)<<12);
  if(attr&8u)code=(code&~7u)|(anim&7u);else if(attr&4u)code=(code&~3u)|(anim&3u);
  if(attr&2u)row^=15;
  o->sprites[o->count++]=(kof_sprite_span){.tile=code&0x7ffffu,.mask=(uint16_t)mask,.x=(uint16_t)first,.slot=(uint16_t)n,.row=(uint8_t)row,.palette=(uint8_t)(attr>>8),.flip=(uint8_t)(attr&1u),.width=(uint8_t)width};
 }
 for(unsigned col=0;col<40;col++){uint16_t e=v[0x7000+col*32u+(unsigned)scan/8u];o->fix[col]=(kof_fix_span){.tile=e&4095u,.column=(uint8_t)col,.row=(uint8_t)(scan&7),.palette=(uint8_t)(e>>12)};}
 return 0;
}

/* Append a vertical interval to the first 96 evaluated slots of each line.
 * The scanline limit is applied BEFORE horizontal clipping, as in LSPC. */
static void schedule_range(kof_lspc_schedule *s,unsigned n,unsigned first,unsigned end){
    if(end>224u)end=224u;
    for(unsigned y=first;y<end;++y){
        unsigned k=s->count[y];
        if(k<KOF_LINE_SPRITES){
            s->slots[y][k]=(uint16_t)n;s->count[y]=(uint8_t)(k+1u);
            kof_lspc_column *c=&s->columns[n];
            if(c->width){++c->selected_count;c->selected[y>>5]|=1u<<(y&31u);if(y<c->first)c->first=(uint16_t)y;if(y+1u>c->last)c->last=(uint16_t)(y+1u);}
        }
    }
}
int kof_lspc_schedule_build(kof_lspc_schedule *s,const uint16_t *v,uint32_t words){
    if(!s||!v||words<KOF_LSPC_WORDS)return -1;
    for(unsigned i=0;i<224u;++i)s->count[i]=0;
    unsigned x=0,y=0,rows=0,zy=0,zx=0;
    for(unsigned n=0;n<381u;++n){
        unsigned ctl=v[0x8200u+n],zoom=v[0x8000u+n];
        if(ctl&64u){x=(x+zx+1u)&511u;zx=(zoom>>8)&15u;}
        else{y=512u-(ctl>>7);x=v[0x8400u+n]>>7;zy=zoom&255u;zx=(zoom>>8)&15u;rows=ctl&63u;}
        s->columns[n].first=224u;s->columns[n].last=0u;s->columns[n].selected_count=0u;
        if(!rows)continue;
        unsigned px=x,mask=0,first=0,width=0;
        /* Common unclipped case: width is the zoom nibble + one. */
        if(x+zx<320u){mask=masks[zx];first=x;width=zx+1u;}
        else if(x<320u||x+zx>=512u){
            for(unsigned i=0;i<16u;++i)if(masks[zx]&(1u<<i)){
                if(px<320u){if(!width)first=px;mask|=1u<<i;++width;}
                px=(px+1u)&511u;
            }
        }
        s->columns[n]=(kof_lspc_column){.x=(uint16_t)first,.mask=(uint16_t)mask,.y=(uint16_t)y,
            .width=(uint8_t)width,.rows=(uint8_t)rows,.zy=(uint8_t)zy,.first=224u};
        const unsigned begin=(y-16u)&511u;
        const unsigned end=begin+16u*(rows>32u?32u:rows);
        schedule_range(s,n,begin,end);
        if(end>512u)schedule_range(s,n,0,end-512u);
    }
    return 0;
}

/* No variable shifts or per-line slots stores in the epochs-only path.
 * Aggregate selected bits a word at a time, and maintain exact counts
 * including horizontally clipped slots. */
const uint32_t kof_lspc_bit[32]={0x00000001u,0x00000002u,0x00000004u,0x00000008u,0x00000010u,0x00000020u,0x00000040u,0x00000080u,0x00000100u,0x00000200u,0x00000400u,0x00000800u,0x00001000u,0x00002000u,0x00004000u,0x00008000u,0x00010000u,0x00020000u,0x00040000u,0x00080000u,0x00100000u,0x00200000u,0x00400000u,0x00800000u,0x01000000u,0x02000000u,0x04000000u,0x08000000u,0x10000000u,0x20000000u,0x40000000u,0x80000000u};
static unsigned schedule_epoch_range(kof_lspc_schedule *s,kof_lspc_column *c,
    unsigned begin,unsigned end,unsigned first,unsigned stop){
    if(begin<first)begin=first;
    if(end>stop)end=stop;
    if(begin>=end)return 0;
    unsigned filled=0;
    if(!c->width){
        for(unsigned y=begin;y<end;++y){
            unsigned k=s->count[y];
            if(k<96u){s->count[y]=(uint8_t)(k+1u);filled+=k==95u;}
        }
        return filled;
    }
    while(begin<end){
        unsigned word=begin>>5;
        unsigned limit=(word+1u)*32u;
        if(limit>end)limit=end;
        uint32_t bit=kof_lspc_bit[begin&31u],selected=0;
        unsigned count=0;
        for(unsigned y=begin;y<limit;++y,bit<<=1){
            unsigned k=s->count[y];
            if(k<96u){
                s->count[y]=(uint8_t)(k+1u);filled+=k==95u;
                selected|=bit;++count;
                if(c->first>y)c->first=(uint16_t)y;
                c->last=(uint16_t)(y+1u);
            }
        }
        c->selected[word]|=selected;c->selected_count=(uint16_t)(c->selected_count+count);
        begin=limit;
    }
    return filled;
}
int kof_lspc_schedule_build_range(kof_lspc_schedule *s,const uint16_t *v,
    uint32_t words,unsigned first,unsigned stop){
    if(!s||!v||words<KOF_LSPC_WORDS||first>=stop||stop>224u)return -1;
    for(unsigned i=first;i<stop;++i)s->count[i]=0;
    unsigned x=0,y=0,rows=0,zy=0,zx=0,filled=0;
    for(unsigned n=0;n<381u;++n){
        kof_lspc_column *c=&s->columns[n];
        c->first=224u;c->last=0u;c->selected_count=0u;
        if(filled==stop-first)continue; /* every line reached its hardware limit */
        unsigned ctl=v[0x8200u+n],zoom=v[0x8000u+n];
        if(ctl&64u){x=(x+zx+1u)&511u;zx=(zoom>>8)&15u;}
        else{y=512u-(ctl>>7);x=v[0x8400u+n]>>7;zy=zoom&255u;zx=(zoom>>8)&15u;rows=ctl&63u;}
        if(!rows)continue;
        unsigned begin=(y-16u)&511u,end=begin+16u*(rows>32u?32u:rows);
        if(!(begin<stop&&end>first)&&!(end>512u&&end-512u>first))continue;
        unsigned px=x,mask=0,left=0,width=0;
        if(x+zx<320u){mask=masks[zx];left=x;width=zx+1u;}
        else if(x<320u||x+zx>=512u){
            unsigned bit=1u;
            for(unsigned i=0;i<16u;++i,bit<<=1)if(masks[zx]&bit){
                if(px<320u){if(!width)left=px;mask|=bit;++width;}
                px=(px+1u)&511u;
            }
        }
        c->x=(uint16_t)left;c->mask=(uint16_t)mask;c->y=(uint16_t)y;
        c->width=(uint8_t)width;c->rows=(uint8_t)rows;c->zy=(uint8_t)zy;
        for(unsigned i=0;i<7u;++i)c->selected[i]=0;
        /* Wrapped interval first so selected bounds stay monotonic. */
        if(end>512u)filled+=schedule_epoch_range(s,c,0,end-512u,first,stop);
        filled+=schedule_epoch_range(s,c,begin,end,first,stop);
    }
    return 0;
}
int kof_lspc_line_scheduled(const kof_lspc_schedule *s,const uint16_t *v,uint32_t words,
    const uint8_t *z,uint32_t nz,unsigned anim,unsigned line,int bios,kof_line_ir *o){
    if(!s||!v||words<KOF_LSPC_WORDS||!z||nz!=KOF_ZOOM_BYTES||line>=224u||!o||(bios!=0&&bios!=1))return -1;
    const unsigned scan=line+16u;
    o->count=0;o->evaluated=s->count[line];o->fix_bios=(uint8_t)bios;
    for(unsigned i=0;i<o->evaluated;++i){
        const unsigned n=s->slots[line][i];
        const kof_lspc_column *c=&s->columns[n];
        if(!c->width)continue;
        unsigned sl=(scan-c->y)&511u,zl=sl&255u,invert=sl>>8;
        if(invert)zl^=255u;
        if(c->rows>32u){
            unsigned period=((unsigned)c->zy+1u)*2u;
            /* Typical full-zoom columns never need a division. */
            if(zl>=period)zl%=period;
            if(zl>c->zy){zl=period-1u-zl;invert^=1u;}
        }
        unsigned yt=z[((unsigned)c->zy<<8)|zl],tile=yt>>4,row=yt&15u;
        if(invert){row^=15u;tile^=31u;}
        unsigned tm=n*64u+tile*2u,attr=v[tm+1u];
        uint32_t code=v[tm]|((attr&0xf0u)<<12);
        if(attr&8u)code=(code&~7u)|(anim&7u);else if(attr&4u)code=(code&~3u)|(anim&3u);
        if(attr&2u)row^=15u;
        o->sprites[o->count++]=(kof_sprite_span){.tile=code&0x7ffffu,.mask=c->mask,.x=c->x,
             .slot=(uint16_t)n,.row=(uint8_t)row,.palette=(uint8_t)(attr>>8),.flip=(uint8_t)(attr&1u),.width=c->width};
    }
    for(unsigned col=0;col<40u;++col){
        uint16_t e=v[0x7000u+col*32u+scan/8u];
        o->fix[col]=(kof_fix_span){.tile=e&4095u,.column=(uint8_t)col,.row=(uint8_t)(scan&7u),.palette=(uint8_t)(e>>12)};
    }
    return 0;
}
