#include "kof98/tile_cache.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint8_t store_mem[8192],vram[KOF_TILE_CACHE_BYTES];static unsigned writes,reads,fail;
static int rd(void *ctx,uint32_t o,void *d,uint32_t n){(void)ctx;++reads;memcpy(d,store_mem+o,n);return 0;}
static int wr(void *ctx,uint32_t o,const void *d,uint32_t n){(void)ctx;++writes;if(fail)return -1;memcpy(vram+o,d,n);return 0;}
static uint32_t mix(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;return x;}
int main(void){
 kof_asset_store s={0};kof_tile_view v;kof_tile_cache c;kof_tile_binding a,b;uint32_t ids[5]; unsigned j=0;
 for(uint32_t i=0;j<5;++i)if((mix(i)&127u)==0)ids[j++]=i;
 s.state=KOF_STORE_READY;s.generation=1;s.count=2;s.cart_read=rd;s.records[0].desc.id=1;s.records[0].desc.bytes=20;s.records[1].desc.id=2;s.records[1].desc.bytes=640;s.records[1].offset=32;
 for(unsigned i=0;i<5;++i){for(unsigned j=0;j<4;++j)store_mem[4*i+j]=(uint8_t)(ids[i]>>(24-8*j));memset(store_mem+32+128*i,(int)(i+1),128);}
 CHECK(kof_tile_view_open(&v,&s)==KOF_ASSET_OK);uint8_t pixels[128];CHECK(kof_tile_read(&v,ids[4],pixels)==KOF_ASSET_OK&&pixels[0]==5);CHECK(kof_tile_read(&v,524287,pixels)==KOF_ASSET_MISSING);
 CHECK(kof_tile_cache_init(&c,wr,NULL)==KOF_ASSET_OK);CHECK(kof_tile_cache_begin(&c,&v)==KOF_ASSET_OK);CHECK(kof_tile_cache_begin(&c,&v)==KOF_ASSET_BUSY);
 uint16_t p[16]={0};for(unsigned i=1;i<16;++i)p[i]=(uint16_t)(0x8000u+i);
 CHECK(kof_tile_cache_get(&c,ids[0],p,&a)==KOF_ASSET_OK);CHECK(writes==2&&vram[a.pixels_offset]==1);unsigned r=reads;
 CHECK(kof_tile_cache_get(&c,ids[0],p,&b)==KOF_ASSET_OK&&reads==r&&writes==2&&a.pixels_offset==b.pixels_offset&&a.lut_offset==b.lut_offset);
 p[1]^=1;CHECK(kof_tile_cache_get(&c,ids[0],p,&b)==KOF_ASSET_OK&&a.pixels_offset==b.pixels_offset&&writes==3);p[1]^=1;
 for(unsigned i=1;i<4;++i)CHECK(kof_tile_cache_get(&c,ids[i],p,&b)==KOF_ASSET_OK);
 CHECK(kof_tile_cache_get(&c,ids[4],p,&b)==KOF_ASSET_FULL&&c.blocked==1);CHECK(kof_tile_cache_end(&c)==KOF_ASSET_OK);
 CHECK(kof_tile_cache_begin(&c,&v)==KOF_ASSET_OK);fail=1;CHECK(kof_tile_cache_get(&c,ids[4],p,&b)==KOF_ASSET_IO);fail=0;CHECK(kof_tile_cache_get(&c,ids[4],p,&b)==KOF_ASSET_OK&&vram[b.pixels_offset]==5);
 CHECK(kof_tile_cache_end(&c)==KOF_ASSET_OK);++s.generation;CHECK(kof_tile_cache_begin(&c,&v)==KOF_ASSET_MISSING);CHECK(kof_tile_view_open(&v,&s)==KOF_ASSET_OK);CHECK(kof_tile_cache_begin(&c,&v)==KOF_ASSET_OK);unsigned w=writes;CHECK(kof_tile_cache_get(&c,ids[4],p,&b)==KOF_ASSET_OK&&writes==w+2);CHECK(kof_tile_cache_end(&c)==KOF_ASSET_OK);
 store_mem[4]=store_mem[0];store_mem[5]=store_mem[1];store_mem[6]=store_mem[2];store_mem[7]=store_mem[3];CHECK(kof_tile_view_open(&v,&s)==KOF_ASSET_INVALID);
 printf("tile cache PASS: metadata %zu bytes, VRAM %u bytes\n",sizeof c,KOF_TILE_CACHE_BYTES);return 0;
}
