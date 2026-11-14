/* ROM-free end-to-end CD / cart / VDP1 acceptance probe. */
#include <saturn/core.h>
#include <saturn/video.h>
#include <saturn/cd_mount.h>
#include "kof98/saturn_asset_store.h"
#include "kof98/saturn_strip_renderer.h"
#include "probe_manifest.h"

static sat_cd_block_t block;
static sat_cd_device_t device;
static sat_cdfs_volume_t volume;
static kof_saturn_assets assets;
static kof_tile_view view;
static kof_saturn_tile_cache tiles;
static kof_saturn_strip_renderer renderer;
static kof_strip_builder builder;
static kof_strip strips[128];
static kof_line_ir line_ir;
static uint16_t palette[4096];
static _Alignas(32) uint8_t scratch[2048], bank_test[64], bank_readback[64];

/* Read from debugger/map: stages 1..7, frame count and last planned draws. */
volatile uint32_t kof_probe_stage, kof_probe_frames, kof_probe_native, kof_probe_variants;
static void check(int ok){if(!ok)sat_panic(SAT_ERR_VERIFY_FAILED,__FILE__,__LINE__);}

static void build_frame(unsigned tick){
    check(kof_strip_begin(&builder,strips,128)==0);
    unsigned dx=(tick/3u)%16u;
    for(unsigned y=0;y<224u;++y){
        line_ir.count=0;
        for(unsigned row=0;row<6;++row){
            unsigned top=14u+row*32u;
            if(y<top||y>=top+16u)continue;
            unsigned sy=y-top;
            for(unsigned col=0;col<12;++col){
                unsigned small=col&1u;
                if(small&&sy>=12u)continue;
                unsigned r=small?sy*16u/12u:sy;
                if(row&1u)r=15u-r;
                line_ir.sprites[line_ir.count++]=(kof_sprite_span){
                    .tile=1u+(row&1u),.mask=(uint16_t)(small?0x5555u:0xffffu),
                    .x=(uint16_t)(8u+col*24u+dx),.slot=(uint16_t)col,
                    .row=(uint8_t)r,.palette=(uint8_t)(1u+(row&1u)),
                    .flip=(uint8_t)((col/2u)&1u),.width=(uint8_t)(small?8u:16u)
                };
            }
        }
        check(kof_strip_push(&builder,&line_ir,palette,4096)==0);
    }
    check(kof_strip_complete(&builder)&&builder.count==72u);
}
int main(void){
    const sat_video_config_t cfg={320,224,SAT_VIDEO_NTSC,SAT_VIDEO_INTERLACE_NONE};
    kof_probe_stage=1;
    SAT_PANIC_IF_ERROR(sat_init(&cfg));
    SAT_PANIC_IF_ERROR(sat_vdp1_set_frame_mode(SAT_VDP1_FRAME_ON_SUBMIT));
    SAT_PANIC_IF_ERROR(sat_set_clear_color(0x1084u));
    kof_probe_stage=2;
    SAT_PANIC_IF_ERROR(sat_cd_mount_default(&block,0,0,&device,0,&volume));
    kof_probe_stage=3;
    SAT_PANIC_IF_ERROR(kof_saturn_assets_open(&assets,&volume,"PROBE.BIN",KOF_CART_BYTES));
    check(kof_asset_begin(&assets.store,probe_assets,2)==KOF_ASSET_OK);
    kof_asset_result result;
    do {result=kof_asset_step(&assets.store,scratch,sizeof scratch);}while(result==KOF_ASSET_PENDING);
    if(result==KOF_ASSET_IO)SAT_PANIC_IF_ERROR(assets.last_sdk_error);
    check(result==KOF_ASSET_OK);
    kof_probe_stage=4;
    /* Cross-bank check in unused arena bytes, outside the loaded assets. */
    check(assets.arena.bank_bytes[0]>=32u&&assets.arena.bank_bytes[1]>=32u);
    uint32_t edge=assets.arena.bank_bytes[0]-32u;
    for(unsigned i=0;i<64u;++i)bank_test[i]=(uint8_t)(i^0xa5u);
    SAT_PANIC_IF_ERROR(sat_ram_cart_buffer_write_at(&assets.arena,edge,bank_test,64));
    SAT_PANIC_IF_ERROR(sat_ram_cart_buffer_read_at(&assets.arena,edge,bank_readback,64));
    for(unsigned i=0;i<64u;++i)check(bank_readback[i]==bank_test[i]);
    check(kof_tile_view_open(&view,&assets.store)==KOF_ASSET_OK);
    check(kof_asset_enter_combat(&assets.store)==KOF_ASSET_OK);
    kof_probe_stage=5;
    SAT_PANIC_IF_ERROR(kof_saturn_tile_cache_init(&tiles));
    SAT_PANIC_IF_ERROR(kof_saturn_strip_renderer_init(&renderer,&tiles));
    /* Raw Neo Geo color words. Pen 15 white, pen 0 transparent. */
    palette[17]=0x4f00;palette[18]=0x20f0;palette[19]=0x100f;palette[31]=0x7fff;
    palette[33]=0x7ff0;palette[34]=0x70ff;palette[35]=0x7f0f;palette[47]=0x7fff;
    kof_probe_stage=6;
    for(;;){
        build_frame(kof_probe_frames);
        SAT_PANIC_IF_ERROR(sat_begin_frame());
        kof_emit_stats stats;
        SAT_PANIC_IF_ERROR(kof_saturn_strip_draw(&renderer,&builder,&view,&stats));
        check(stats.native==36u&&stats.variants==36u);
        SAT_PANIC_IF_ERROR(sat_end_frame());
        check(kof_tile_cache_end(&tiles.cache)==KOF_ASSET_OK);
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
        kof_probe_native=stats.native;kof_probe_variants=stats.variants;
        ++kof_probe_frames;kof_probe_stage=7;
    }
}
