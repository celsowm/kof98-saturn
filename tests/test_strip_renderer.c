#include "kof98/saturn_strip_renderer.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint8_t vram[512*1024],cart[256];static uint32_t cursor=0x20000;static unsigned used=3,capacity=100,fail_draw,fail_rollback,writes,draws;
static sat_sprite_cmd_t commands[8];static sat_vdp1_texture_t textures[8];
uint16_t sat_video_width(void){return 320;}uint16_t sat_video_height(void){return 224;}
sat_result_t sat_vdp1_vram_reserve(uint32_t n,uint32_t *o){*o=cursor;cursor+=n;return SAT_OK;}
sat_result_t sat_vdp1_vram_write(uint32_t o,const void *p,uint32_t n){if(o+n>sizeof vram)return SAT_ERR_INVALID_ARG;memcpy(vram+o,p,n);++writes;return SAT_OK;}
sat_result_t sat_vdp1_command_checkpoint(sat_vdp1_command_checkpoint_t *c){*c=(sat_vdp1_command_checkpoint_t){.used=(uint16_t)used};return SAT_OK;}
sat_result_t sat_vdp1_command_rollback(const sat_vdp1_command_checkpoint_t *c){if(fail_rollback)return SAT_ERR_BUSY;used=c->used;return SAT_OK;}
sat_result_t sat_vdp1_command_stats(sat_vdp1_command_stats_t *s){*s=(sat_vdp1_command_stats_t){.used=(uint16_t)used,.capacity=(uint16_t)capacity};return SAT_OK;}
sat_result_t sat_draw_sprite(const sat_sprite_cmd_t *c){if(fail_draw&&draws==1)return SAT_ERR_IO;commands[draws]=*c;textures[draws]=*c->texture;++draws;++used;return SAT_OK;}
static int rd(void *c,uint32_t o,void *p,uint32_t n){(void)c;memcpy(p,cart+o,n);return 0;}
int main(void){
 kof_asset_store s={0};s.state=KOF_STORE_READY;s.generation=1;s.count=2;s.cart_read=rd;s.records[0].desc.id=1;s.records[0].desc.bytes=4;s.records[1].desc.id=2;s.records[1].desc.bytes=128;s.records[1].offset=32;cart[3]=1;memset(cart+32,0x1f,128);
 kof_tile_view view;CHECK(kof_tile_view_open(&view,&s)==KOF_ASSET_OK);kof_saturn_tile_cache cache={0};CHECK(kof_saturn_tile_cache_init(&cache)==SAT_OK);kof_saturn_strip_renderer r={0};CHECK(kof_saturn_strip_renderer_init(&r,&cache)==SAT_OK);
 kof_strip strips[2]={0};kof_strip_builder b;CHECK(kof_strip_begin(&b,strips,2)==0);b.line=224;b.count=2;b.head[0]=b.tail[0]=0;b.head[1]=b.tail[1]=1;
 strips[0]=(kof_strip){.tile=1,.mask=65535,.width=16,.height=16,.next=KOF_STRIP_END,.flip=1};for(unsigned i=0;i<16;++i)strips[0].rows[i]=(uint8_t)(15-i);
 strips[1]=(kof_strip){.tile=1,.mask=0x5555,.width=8,.height=1,.x=312,.y=223,.next=KOF_STRIP_END};
 kof_emit_stats st;CHECK(kof_strip_preflight(&b,2,&st)==0&&st.native==1&&st.variants==1);CHECK(kof_strip_preflight(&b,1,&st)==-1);strips[1].next=1;CHECK(kof_strip_preflight(&b,2,&st)==-1);strips[1].next=KOF_STRIP_END;
 CHECK(kof_saturn_strip_draw(&r,&b,&view,&st)==SAT_OK);CHECK(draws==2&&used==5&&writes==3&&cache.cache.active);CHECK(commands[0].x==-160*65536&&commands[0].y==-112*65536&&commands[0].flags==0x60u);CHECK(commands[1].x==152*65536&&commands[1].y==111*65536&&commands[1].flags==0&&textures[1].width==8&&textures[1].height==1);CHECK(vram[textures[1].srca*8u]==0x11&&textures[1].palette==textures[0].palette);
 CHECK(kof_saturn_strip_draw(&r,&b,&view,&st)==SAT_ERR_BUSY);CHECK(kof_tile_cache_end(&cache.cache)==KOF_ASSET_OK);
 used=3;draws=0;capacity=5;unsigned w=writes;CHECK(kof_saturn_strip_draw(&r,&b,&view,&st)==SAT_ERR_CAPACITY&&writes==w&&!cache.cache.active);capacity=100;fail_draw=1;
 CHECK(kof_saturn_strip_draw(&r,&b,&view,&st)==SAT_ERR_IO&&used==3&&!cache.cache.active&&r.rollback_error==SAT_OK);
 draws=0;fail_rollback=1;CHECK(kof_saturn_strip_draw(&r,&b,&view,&st)==SAT_ERR_IO&&cache.cache.active&&r.rollback_error==SAT_ERR_BUSY);
 puts("strip renderer mock: preflight, native/variant, coordinates, palette reuse, rollback and pin retention PASS");return 0;
}
