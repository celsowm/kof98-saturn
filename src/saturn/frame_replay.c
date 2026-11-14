/* Immutable one-frame sprite atlas; no full-screen image or bitmap blit. */
#include <saturn/core.h>
#include <saturn/video.h>
#include <saturn/vdp1.h>
#include <saturn/cd_mount.h>
#include "kof98/saturn_asset_store.h"
#include "loading_screen.h"
typedef struct {uint16_t x,y,w,h;uint32_t tex,lut;uint16_t flags;} frame_draw;
#include "frame_manifest.h"
static sat_cd_block_t block;
static sat_cd_device_t device;
static sat_cdfs_volume_t volume;
static kof_saturn_assets assets;
static kof_loading loading;
static _Alignas(32) uint8_t scratch[8192];
volatile uint32_t kof_replay_stage,kof_replay_frames;
static void check(int ok){if(!ok)sat_panic(SAT_ERR_VERIFY_FAILED,__FILE__,__LINE__);}
int main(void){
    const sat_video_config_t cfg={320,224,SAT_VIDEO_NTSC,SAT_VIDEO_INTERLACE_NONE};
    kof_replay_stage=1;
    SAT_PANIC_IF_ERROR(sat_init(&cfg));
    SAT_PANIC_IF_ERROR(sat_vdp1_set_frame_mode(SAT_VDP1_FRAME_ON_SUBMIT));
    kof_loading_init(&loading);
    kof_loading_phase(&loading,"INITIALIZING CD",1,1);
    kof_replay_stage=2;
    SAT_PANIC_IF_ERROR(sat_cd_mount_default(&block,0,0,&device,0,&volume));
    SAT_PANIC_IF_ERROR(kof_saturn_assets_open(&assets,&volume,"FRAME.BIN",KOF_CART_BYTES));
    kof_loading_phase(&loading,"LOADING CD TO RAM CART",1,1);
    const kof_asset_desc desc={1,0,FRAME_BYTES,FRAME_CRC};
    check(kof_asset_begin(&assets.store,&desc,1)==KOF_ASSET_OK);
    kof_asset_result result;
    do {
        result=kof_asset_step(&assets.store,scratch,sizeof scratch);
        if(result==KOF_ASSET_PENDING||result==KOF_ASSET_OK)
            kof_loading_update(&loading,result==KOF_ASSET_OK?FRAME_BYTES:assets.store.progress,FRAME_BYTES,0);
    }while(result==KOF_ASSET_PENDING);
    check(result==KOF_ASSET_OK);
    kof_asset_handle asset;
    check(kof_asset_find(&assets.store,1,&asset)==KOF_ASSET_OK);
    check(kof_asset_enter_combat(&assets.store)==KOF_ASSET_OK);
    kof_replay_stage=3;
    kof_loading_phase(&loading,"UPLOADING VDP1 TEXTURES",1,1);
    uint32_t base;
    SAT_PANIC_IF_ERROR(sat_vdp1_vram_reserve(FRAME_BYTES+24u,&base));
    base=(base+31u)&~31u;
    for(uint32_t p=0;p<FRAME_BYTES;){
        uint32_t n=FRAME_BYTES-p;if(n>sizeof scratch)n=sizeof scratch;
        check(kof_asset_read(&assets.store,asset,p,scratch,n)==KOF_ASSET_OK);
        SAT_PANIC_IF_ERROR(sat_vdp1_vram_write(base+p,scratch,n));p+=n;
        kof_loading_update(&loading,p,FRAME_BYTES,0);
    }
    SAT_PANIC_IF_ERROR(sat_set_clear_color(FRAME_BG));
    kof_replay_stage=4;
    const unsigned count=sizeof frame_draws/sizeof frame_draws[0];
    for(;;){
        SAT_PANIC_IF_ERROR(sat_begin_frame());
        sat_vdp1_command_stats_t cs;SAT_PANIC_IF_ERROR(sat_vdp1_command_stats(&cs));
        check((unsigned)cs.used+count+1u<=cs.capacity);
        for(unsigned i=0;i<count;++i){
            const frame_draw *d=&frame_draws[i];
            const sat_vdp1_texture_t tex={.srca=(uint16_t)((base+d->tex)/8u),.palette=(uint16_t)((base+d->lut)/8u),.width=d->w,.height=d->h,.valid=1,.format=SAT_VDP1_TEXTURE_LUT4};
            const sat_sprite_cmd_t cmd={.x=((int32_t)d->x-160)*SAT_FX16_ONE,.y=((int32_t)d->y-112)*SAT_FX16_ONE,.width=d->w,.height=d->h,.texture=&tex,.flags=d->flags};
            SAT_PANIC_IF_ERROR(sat_draw_sprite(&cmd));
        }
        SAT_PANIC_IF_ERROR(sat_end_frame());
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
        ++kof_replay_frames;kof_replay_stage=5;
    }
}
