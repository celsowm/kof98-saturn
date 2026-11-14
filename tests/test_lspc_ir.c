#include "kof98/lspc_ir.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint16_t v[KOF_LSPC_WORDS];static uint8_t z[KOF_ZOOM_BYTES];
static uint32_t rng=17;
static uint32_t random32(void){rng=rng*1664525u+1013904223u;return rng;}
static const uint8_t order[16]={8,4,12,2,14,6,10,0,9,3,15,7,13,1,11,5};
static unsigned pen(uint32_t t,unsigned row,unsigned x){return (t^(t>>8)^row^(x*3u))&15u;}
static void reference(unsigned line,unsigned anim,uint32_t *pixels,unsigned *evaluated){
 int x=0,y=0,rows=0,zy=0,zx=0,scan=(int)line+16;*evaluated=0;
 for(unsigned n=0;n<381;++n){unsigned ctl=v[0x8200+n],zoom=v[0x8000+n];if(ctl&64){x=(x+zx+1)%512;zx=(zoom>>8)&15;}else{x=v[0x8400+n]>>7;y=512-(ctl>>7);rows=ctl&63;zy=zoom&255;zx=(zoom>>8)&15;}
  int sl=(scan-y)&511;if(!rows||sl>=16*(rows>32?32:rows))continue;if(*evaluated==96)break;++*evaluated;
  int q=sl%256,inv=sl>=256;if(inv)q=255-q;if(rows>32){q%=2*(zy+1);if(q>zy){q=2*zy+1-q;inv=!inv;}}
  unsigned a=z[zy*256+q],t=a/16,row=a%16;if(inv){t=31-t;row=15-row;}unsigned tm=n*64+t*2,attr=v[tm+1];uint32_t code=(v[tm]+((attr&240u)*4096u))&0x7ffffu;
  if(attr&8)code=(code/8)*8+anim%8;else if(attr&4)code=(code/4)*4+anim%4;if(attr&2)row=15-row;
  int px=x;for(unsigned i=0;i<16;i++){unsigned rank=0;while(order[rank]!=i)rank++;if(rank>(unsigned)zx)continue;unsigned p=pen(code,row,(attr&1)?15-i:i);if(px<320&&p)pixels[px]=(attr>>8)*16+p+1;px=(px+1)%512;}
 }
}
static void replay(const kof_line_ir *o,uint32_t *pixels){for(unsigned j=0;j<o->count;j++){const kof_sprite_span *s=&o->sprites[j];unsigned x=s->x;for(unsigned i=0;i<16;i++)if(s->mask&(1u<<i)){unsigned p=pen(s->tile,s->row,s->flip?15-i:i);if(p)pixels[x]=(unsigned)s->palette*16+p+1;++x;}}}
static kof_lspc_schedule schedule,range_schedule;
static int equal_ir(const kof_line_ir *a,const kof_line_ir *b){
 if(a->count!=b->count||a->evaluated!=b->evaluated||a->fix_bios!=b->fix_bios)return 0;
 for(unsigned i=0;i<a->count;++i){
  const kof_sprite_span *x=&a->sprites[i],*y=&b->sprites[i];
  if(x->tile!=y->tile||x->mask!=y->mask||x->x!=y->x||x->slot!=y->slot||x->row!=y->row||
     x->palette!=y->palette||x->flip!=y->flip||x->width!=y->width)return 0;
 }
 for(unsigned i=0;i<40;++i){
  const kof_fix_span *x=&a->fix[i],*y=&b->fix[i];
  if(x->tile!=y->tile||x->column!=y->column||x->row!=y->row||x->palette!=y->palette)return 0;
 }
 return 1;
}
static int range_equal(unsigned first,unsigned end){
 if(kof_lspc_schedule_build_range(&range_schedule,v,KOF_LSPC_WORDS,first,end))return 0;
 for(unsigned y=first;y<end;++y){
  if(schedule.count[y]!=range_schedule.count[y])return 0;
  unsigned j=0;
  for(unsigned n=0;n<381u;++n){
   const kof_lspc_column *c=&range_schedule.columns[n];
   unsigned actual=c->first<=y&&y<c->last&&(c->selected[y>>5]&kof_lspc_bit[y&31u]);
   while(j<schedule.count[y]&&!schedule.columns[schedule.slots[y][j]].width)++j;
   unsigned expected=j<schedule.count[y]&&schedule.slots[y][j]==n;
   if(!!actual!=!!expected)return 0;
   if(expected){
    const kof_lspc_column *a=&schedule.columns[n];
    if(c->x!=a->x||c->mask!=a->mask||c->y!=a->y||c->width!=a->width||c->rows!=a->rows||c->zy!=a->zy)return 0;
    ++j;
   }
  }
 }
 return 1;
}
int main(void){kof_line_ir o,fast;CHECK(kof_lspc_line(v,KOF_LSPC_WORDS,z,sizeof z,0,224,0,&o)==-1);CHECK(kof_lspc_line(v,KOF_LSPC_WORDS-1,z,sizeof z,0,0,0,&o)==-1);
 for(unsigned i=0;i<65536;i++)z[i]=(uint8_t)(random32()>>24);
 for(unsigned scenario=0;scenario<8;scenario++){
  for(unsigned i=0;i<KOF_LSPC_WORDS;i++)v[i]=(uint16_t)(random32()>>16);
  for(unsigned i=0;i<381;i++){unsigned y=(random32()>>16)&511u;unsigned rows=(random32()>>16)&63u;v[0x8200+i]=(uint16_t)(((512-y)&511u)<<7|rows|((i%3==1)?64u:0u));v[0x8400+i]=(uint16_t)((random32()>>16)&0xff80u);}
  CHECK(!kof_lspc_schedule_build(&schedule,v,KOF_LSPC_WORDS));
  for(unsigned start=0;start<224u;){
   unsigned end=start+1u+((start+scenario*17u)%45u);if(end>224u)end=224u;
   CHECK(range_equal(start,end));start=end;
  }
  for(unsigned line=0;line<224;line++){uint32_t a[320]={0},b[320]={0};unsigned evaluated;CHECK(kof_lspc_line(v,KOF_LSPC_WORDS,z,sizeof z,scenario,line,scenario&1,&o)==0);CHECK(!kof_lspc_line_scheduled(&schedule,v,KOF_LSPC_WORDS,z,sizeof z,scenario,line,scenario&1,&fast));CHECK(equal_ir(&o,&fast));reference(line,scenario,a,&evaluated);replay(&o,b);CHECK(evaluated==o.evaluated);CHECK(!memcmp(a,b,sizeof a));for(unsigned j=0;j<o.count;j++){CHECK(o.sprites[j].x+o.sprites[j].width<=320);if(j)CHECK(o.sprites[j-1].slot<o.sprites[j].slot);}for(unsigned col=0;col<40;col++){unsigned e=v[0x7000+32*col+(line+16)/8];CHECK(o.fix[col].tile==(e&4095u)&&o.fix[col].palette==(e>>12)&&o.fix[col].row==((line+16)&7));}}
 }
 /* Raster mutations: SCB1 and FIX values are sampled live, control words
  * cause a rebuild; cover off-screen slots still counting toward 96. */
 memset(v,0,sizeof v);
 for(unsigned i=0;i<381u;++i){
  v[0x8200u+i]=(uint16_t)(32u|((i%7u==1u)?64u:0u));
  v[0x8000u+i]=0xfffu;v[0x8400u+i]=(uint16_t)((i<100u?400u:i%320u)<<7);
 }
 CHECK(!kof_lspc_schedule_build(&schedule,v,KOF_LSPC_WORDS));
 for(unsigned line=0;line<224u;++line){
  if(line%13u==0){
   unsigned n=(random32()>>16)%381u;
   v[0x8200u+n]=(uint16_t)(random32()>>16);
   v[0x8000u+n]=(uint16_t)(random32()>>16);
   v[0x8400u+n]=(uint16_t)(random32()>>16);
   CHECK(!kof_lspc_schedule_build(&schedule,v,KOF_LSPC_WORDS));
  }
  v[(line*131u)%0x6000u]=(uint16_t)(random32()>>16);
  v[0x7000u+(line%40u)*32u+(line+16u)/8u]=(uint16_t)(random32()>>16);
  CHECK(!kof_lspc_line(v,KOF_LSPC_WORDS,z,sizeof z,line,line,line&1,&o));
  CHECK(!kof_lspc_line_scheduled(&schedule,v,KOF_LSPC_WORDS,z,sizeof z,line,line,line&1,&fast));
  CHECK(equal_ir(&o,&fast));
 }
 for(unsigned w=0;w<65536;w++){unsigned r=((w>>8)&15u)*2+((w>>14)&1u),g=((w>>4)&15u)*2+((w>>13)&1u),b=(w&15u)*2+((w>>12)&1u);CHECK(kof_lspc_rgb555((uint16_t)w)==(0x8000u|r|(g<<5)|(b<<10)));}
 puts("LSPC IR: 2016 differential scanlines and 65536 palette words PASS");return 0;}
