#include "kof98/strip.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static kof_strip strips[2048],fast_strips[2048];static uint16_t pal[4096],a[224][320],b[224][320];
static unsigned pen(unsigned t,unsigned y,unsigned x){return (t+y*3u+x*5u)&15u;}
static void tile(unsigned t,uint8_t out[128]){for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;x+=2)out[y*8+x/2]=(uint8_t)(pen(t,y,x)*16+pen(t,y,x+1));}
static int versioned_test(void){
 kof_strip_builder ref,fast;
 uint16_t versions[256]={0};
 kof_line_ir ir={0};
 memset(pal,0,sizeof pal);
 CHECK(!kof_strip_begin(&ref,strips,2048));
 CHECK(!kof_strip_begin(&fast,fast_strips,2048));
 for(unsigned y=0;y<224u;++y){
  /* Different IDs with identical colors must merge; mutations to pen 0,
   * high bits and individual words must split only if contents differ. */
  if(y==5||y==6||y==77||y==78){
   pal[16u+3u]^=0x8000u;++versions[1];
  }
  if(y==20||y==21){
   pal[32u]^=1u;++versions[2];
  }
  if(y==40){memcpy(pal+32u,pal+16u,32u);++versions[2];}
  ir.count=2;
  ir.sprites[0]=(kof_sprite_span){.tile=y/31u,.mask=0xffffu,.x=10,.slot=0,.row=y%16u,
    .palette=(uint8_t)((y>=40&&y<60)?2u:1u),.width=16};
  ir.sprites[1]=(kof_sprite_span){.tile=55u,.mask=0x8001u,.x=318,.slot=9,.row=y%16u,.palette=2,.flip=1,.width=2};
  if(y==91){ir.count=1;ir.sprites[0]=ir.sprites[1];}
  CHECK(!kof_strip_push(&ref,&ir,pal,4096u));
  CHECK(!kof_strip_push_versioned(&fast,&ir,pal,4096u,versions));
 }
 CHECK(kof_strip_complete(&ref)&&kof_strip_complete(&fast));
 CHECK(ref.count==fast.count&&ref.spans==fast.spans);
 CHECK(!memcmp(ref.head,fast.head,sizeof ref.head)&&!memcmp(ref.tail,fast.tail,sizeof ref.tail));
 for(unsigned i=0;i<ref.count;++i){
  const kof_strip *x=&strips[i],*z=&fast_strips[i];
  CHECK(x->tile==z->tile&&x->x==z->x&&x->y==z->y&&x->mask==z->mask&&x->next==z->next&&
    x->height==z->height&&x->width==z->width&&x->flip==z->flip);
  CHECK(!memcmp(x->colors,z->colors,sizeof x->colors)&&!memcmp(x->rows,z->rows,sizeof x->rows));
 }
 /* All 16-bit masks tested indirectly with valid and invalid widths.
  * Reset one small builder repeatedly; no hidden state between frames. */
 for(unsigned m=1;m<=65535u;++m){
  unsigned count=0;for(unsigned i=0;i<16u;++i)count+=(m>>i)&1u;
  ir.count=1;ir.sprites[0]=(kof_sprite_span){.mask=(uint16_t)m,.width=(uint8_t)count};
  CHECK(!kof_strip_begin(&fast,fast_strips,1));
  CHECK(!kof_strip_push_versioned(&fast,&ir,pal,4096u,versions));
  CHECK(!kof_strip_begin(&fast,fast_strips,1));
  ir.sprites[0].width=(uint8_t)(count+1u);
  CHECK(kof_strip_push_versioned(&fast,&ir,pal,4096u,versions)==-1);
 }
 return 0;
}
static kof_lspc_schedule epoch_schedule;
static uint16_t epoch_vram[KOF_LSPC_WORDS];
static uint8_t epoch_zoom[KOF_ZOOM_BYTES];
static kof_strip epoch_fix[2048],ref_fix[2048];
static int same_chain(const kof_strip_builder *a,const kof_strip_builder *b){
 if(a->count!=b->count||a->spans!=b->spans)return 0;
 for(unsigned slot=0;slot<381u;++slot){
  uint16_t i=a->head[slot],j=b->head[slot];
  while(i!=KOF_STRIP_END&&j!=KOF_STRIP_END){
   const kof_strip *x=&a->strips[i],*y=&b->strips[j];
   if(x->tile!=y->tile||x->x!=y->x||x->y!=y->y||x->mask!=y->mask||
      x->height!=y->height||x->width!=y->width||x->flip!=y->flip||
      memcmp(x->colors,y->colors,32)||memcmp(x->rows,y->rows,16))return 0;
   i=x->next;j=y->next;
  }
  if(i!=KOF_STRIP_END||j!=KOF_STRIP_END)return 0;
 }
 return 1;
}
static kof_zoom_memo epoch_memo;
static int epoch_test(void){
 kof_strip_builder a,b,af,bf;kof_line_ir ir,fi={0};
 uint16_t versions[256]={0};
 memset(epoch_vram,0,sizeof epoch_vram);memset(pal,0,sizeof pal);
 for(unsigned i=0;i<65536u;++i)epoch_zoom[i]=(uint8_t)(i&255u);
 for(unsigned i=0;i<4096u;++i)pal[i]=(uint16_t)i;
 for(unsigned j=0;j<10u;++j){
  epoch_vram[0x8200u+j]=(uint16_t)(((512u-(j*23u+500u)%512u)&511u)<<7|((j&1u)?33u:8u)|((j%3u==1u)?64u:0u));
  epoch_vram[0x8000u+j]=(uint16_t)(0x0fffu-((j%5u)<<8));
  epoch_vram[0x8400u+j]=(uint16_t)(((j==2u)?509u:j*41u)<<7);
  for(unsigned k=0;k<32u;++k){
   epoch_vram[j*64u+k*2u]=(uint16_t)(j*3u+k/2u);
   epoch_vram[j*64u+k*2u+1u]=(uint16_t)(((j%3u)<<8)|(k&15u));
  }
 }
 for(unsigned x=0;x<40u;++x)for(unsigned y=0;y<32u;++y)
  epoch_vram[0x7000u+x*32u+y]=(uint16_t)(((x%4u)<<12)|(y%3u));
 CHECK(!kof_strip_begin(&a,strips,2048));CHECK(!kof_strip_begin(&b,fast_strips,2048));
 CHECK(!kof_strip_begin(&af,ref_fix,2048));CHECK(!kof_strip_begin(&bf,epoch_fix,2048));
 for(unsigned start=0;start<224u;){
  unsigned end=start+1u+(start%27u);if(end>224u)end=224u;
  unsigned anim=start&7u,bios=(start/60u)&1u;
  epoch_vram[0x8400u+3u]=(uint16_t)(((start+304u)&511u)<<7);
  epoch_vram[4u]=(uint16_t)start;pal[17]^=0x4000u;++versions[1];
  CHECK(!kof_lspc_schedule_build_segments(&epoch_schedule,epoch_vram,KOF_LSPC_WORDS,start,end,0));
  for(unsigned y=start;y<end;++y){
   CHECK(!kof_lspc_line(epoch_vram,KOF_LSPC_WORDS,epoch_zoom,sizeof epoch_zoom,anim,y,(int)bios,&ir));
   CHECK(!kof_strip_push(&a,&ir,pal,4096u));fi.count=40;
   for(unsigned x=0;x<40u;++x){
    const kof_fix_span *q=&ir.fix[x];
    fi.sprites[x]=(kof_sprite_span){.tile=(uint32_t)q->tile+(bios?4096u:0u),.mask=255u,.x=(uint16_t)(x*8u),
       .slot=(uint16_t)x,.row=q->row,.palette=q->palette,.width=8u};
   }
   CHECK(!kof_strip_push(&af,&fi,pal,4096u));
  }
  CHECK(!kof_strip_push_sprite_epoch_cached(&b,&epoch_schedule,epoch_vram,epoch_zoom,pal,versions,anim,end,&epoch_memo));
  CHECK(!kof_strip_push_fix_epoch(&bf,epoch_vram,pal,versions,(int)bios,end));
  start=end;
 }
 CHECK(kof_strip_complete(&b)&&kof_strip_complete(&bf));
 CHECK(same_chain(&a,&b)&&same_chain(&af,&bf));
 return 0;
}
int main(void){
 CHECK(!versioned_test());CHECK(!epoch_test());
 memset(a,0,sizeof a);memset(b,0,sizeof b);
 kof_strip_builder builder;kof_line_ir ir={0};CHECK(kof_strip_begin(&builder,strips,2048)==0);
 for(unsigned y=0;y<224;++y){
  for(unsigned i=0;i<4096;++i)pal[i]=(uint16_t)(i+1+(y>=100?4096:0));
  ir.count=2;ir.sprites[0]=(kof_sprite_span){.tile=y/16,.mask=65535,.x=10,.slot=0,.row=y%16,.palette=1,.width=16};
  ir.sprites[1]=(kof_sprite_span){.tile=100+y/16,.mask=0x5555,.x=15,.slot=7,.row=15-y%16,.palette=2,.flip=1,.width=8};
  if(y==50){ir.count=1;ir.sprites[0]=ir.sprites[1];}
  CHECK(kof_strip_push(&builder,&ir,pal,4096)==0);
  for(unsigned j=0;j<ir.count;++j){kof_sprite_span *p=&ir.sprites[j];unsigned x=p->x;for(unsigned i=0;i<16;++i)if(p->mask&(1u<<i)){unsigned c=pen(p->tile,p->row,p->flip?15-i:i);if(c)a[y][x]=pal[16*p->palette+c];++x;}}
 }
 CHECK(kof_strip_complete(&builder));CHECK(builder.spans==447&&builder.count==31);
 unsigned native=0,seen=0;for(unsigned slot=0;slot<381;++slot)for(uint16_t i=builder.head[slot];i!=KOF_STRIP_END;i=strips[i].next){const kof_strip *s=&strips[i];uint8_t raw[128],pixels[128],h,v;uint16_t w;tile(s->tile,raw);CHECK(kof_strip_pixels(s,raw,pixels,&w)==0);native+=kof_strip_native(s,&h,&v);++seen;for(unsigned y=0;y<s->height;++y)for(unsigned x=0;x<w;++x){unsigned p=(pixels[y*w/2+x/2]>>(x%2?0:4))&15u;if(p){CHECK(s->x+x<320);b[s->y+y][s->x+x]=s->colors[p];}}}
 CHECK(seen==builder.count&&native==12);CHECK(!memcmp(a,b,sizeof a));
 kof_strip test={.mask=0x8001,.width=2,.height=1,.x=318,.flip=1};test.rows[0]=3;uint8_t raw[128],out[128];uint16_t w;tile(5,raw);CHECK(kof_strip_pixels(&test,raw,out,&w)==0&&w==8&&out[0]==(pen(5,3,15)*16+pen(5,3,0))&&out[1]==0);
 CHECK(kof_strip_begin(&builder,strips,1)==0);ir.count=2;CHECK(kof_strip_push(&builder,&ir,pal,4096)==-1&&!kof_strip_complete(&builder));CHECK(kof_strip_push(&builder,&ir,pal,4096)==-1);
 CHECK(kof_strip_begin(&builder,strips,4)==0);ir.sprites[1].slot=0;CHECK(kof_strip_push(&builder,&ir,pal,4096)==-1);
 printf("strip replay: 447 spans -> 31 strips, 12 native, 71680 pixels equal, strip %zu bytes, builder %zu bytes\n",sizeof(kof_strip),sizeof builder);return 0;
}
