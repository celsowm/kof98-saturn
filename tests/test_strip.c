#include "kof98/strip.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static kof_strip strips[2048];static uint16_t pal[4096],a[224][320],b[224][320];
static unsigned pen(unsigned t,unsigned y,unsigned x){return (t+y*3u+x*5u)&15u;}
static void tile(unsigned t,uint8_t out[128]){for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;x+=2)out[y*8+x/2]=(uint8_t)(pen(t,y,x)*16+pen(t,y,x+1));}
int main(void){
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
