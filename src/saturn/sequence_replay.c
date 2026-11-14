/* Diagnostic, bounded clip playback. No full-screen bitmap or per-frame CD IO. */
#include <saturn/core.h>
#include <saturn/video.h>
#include <saturn/vdp1.h>
#include <saturn/sprite_batch.h>
#ifndef KOF_SEQUENCE_BATCH
#define KOF_SEQUENCE_BATCH 1
#endif
#ifndef KOF_SEQUENCE_BATCH_PRECLIP_OPT
#define KOF_SEQUENCE_BATCH_PRECLIP_OPT 0
#endif
#include <saturn/cd_mount.h>
#include "kof98/saturn_asset_store.h"
#include "loading_screen.h"
typedef struct {uint32_t source_offset,bytes,crc,atlas_bytes,first,count;} seq_clip;
typedef struct {uint32_t offset;uint16_t count,bg;} seq_frame;
#include "sequence_manifest.h"
static sat_cd_block_t block;
static sat_cd_device_t device;
static sat_cdfs_volume_t volume;
static kof_saturn_assets assets;
static kof_loading loading;
#ifndef KOF_SEQUENCE_LOAD_CHUNK_BYTES
#define KOF_SEQUENCE_LOAD_CHUNK_BYTES 131072u
#endif
#define KOF_SEQUENCE_UPLOAD_CHUNK_BYTES 8192u
_Static_assert(KOF_SEQUENCE_LOAD_CHUNK_BYTES<=KOF_ASSET_CHUNK_MAX &&
               KOF_SEQUENCE_LOAD_CHUNK_BYTES>=KOF_SEQUENCE_UPLOAD_CHUNK_BYTES &&
               KOF_SEQUENCE_LOAD_CHUNK_BYTES%2048u==0u,"invalid load chunk");
static _Alignas(32) uint8_t scratch[KOF_SEQUENCE_LOAD_CHUNK_BYTES];
static _Alignas(32) uint16_t commands[2048][8];
volatile uint32_t kof_sequence_stage,kof_sequence_frames,kof_sequence_clip,kof_sequence_index,kof_sequence_commands;
volatile uint32_t kof_sequence_vram_free,kof_sequence_vram_requested;
static void check(int ok){if(!ok)sat_panic(SAT_ERR_VERIFY_FAILED,__FILE__,__LINE__);}
int main(void){
    const sat_video_config_t cfg={320,224,SAT_VIDEO_NTSC,SAT_VIDEO_INTERLACE_NONE};
    kof_sequence_stage=1;
    SAT_PANIC_IF_ERROR(sat_init(&cfg));
    SAT_PANIC_IF_ERROR(sat_vdp1_set_frame_mode(SAT_VDP1_FRAME_ON_SUBMIT));
    const unsigned clips=sizeof seq_clips/sizeof seq_clips[0];
    kof_loading_init(&loading);
    kof_loading_phase(&loading,"INITIALIZING CD",1,clips);
    SAT_PANIC_IF_ERROR(sat_cd_mount_default(&block,0,0,&device,0,&volume));
    SAT_PANIC_IF_ERROR(kof_saturn_assets_open(&assets,&volume,"SEQUENCE.BIN",KOF_CART_BYTES));
    uint32_t base;
    kof_sequence_vram_free=sat_vdp1_vram_free_bytes();
    kof_sequence_vram_requested=SEQ_MAX_ATLAS+24u;
    if(kof_sequence_vram_requested>kof_sequence_vram_free)
        sat_panic(SAT_ERR_CAPACITY,__FILE__,__LINE__);
    SAT_PANIC_IF_ERROR(sat_vdp1_vram_reserve(kof_sequence_vram_requested,&base));
    base=(base+31u)&~31u;
#if KOF_SEQUENCE_BATCH
    /* All replay records are plain LUT4 sprites with four possible flip
     * combinations. Prepare SDK command prefixes once; addresses and size
     * remain per record. Pivot zero preserves the original top-left origin. */
    sat_sprite_frame_t prefixes[4];
    for(unsigned flip=0;flip<4;++flip)
        SAT_PANIC_IF_ERROR(sat_sprite_frame_init_raw(&prefixes[flip],
            (uint16_t)(base/8u),8,1,SAT_VDP1_TEXTURE_LUT4,
            (uint16_t)(base/8u),(uint16_t)(flip<<5),0,0));
#endif
    for(;;){
      for(unsigned ci=0;ci<clips;++ci){
        const seq_clip *c=&seq_clips[ci];
        kof_sequence_stage=2;kof_sequence_clip=ci;
        kof_loading_phase(&loading,"LOADING CD TO RAM CART",ci+1,clips);
        check(c->atlas_bytes<=SEQ_MAX_ATLAS&&c->first+c->count<=sizeof seq_frames/sizeof seq_frames[0]);
        const kof_asset_desc desc={1,c->source_offset,c->bytes,c->crc};
        check(kof_asset_begin(&assets.store,&desc,1)==KOF_ASSET_OK);
        kof_asset_result r;
        do{
            r=kof_asset_step_cd(&assets.store,scratch,sizeof scratch);
            if(r==KOF_ASSET_PENDING||r==KOF_ASSET_OK)
                kof_loading_update(&loading,r==KOF_ASSET_OK?c->bytes:assets.store.progress,c->bytes,0);
        }while(r==KOF_ASSET_PENDING);
        if(r==KOF_ASSET_IO)SAT_PANIC_IF_ERROR(assets.last_sdk_error);
        check(r==KOF_ASSET_OK);
        kof_asset_handle a;
        check(kof_asset_find(&assets.store,1,&a)==KOF_ASSET_OK);
        check(kof_asset_enter_combat(&assets.store)==KOF_ASSET_OK);
        kof_sequence_stage=3;
        kof_loading_phase(&loading,"UPLOADING VDP1 TEXTURES",ci+1,clips);
        for(uint32_t p=0;p<c->atlas_bytes;){
            uint32_t n=c->atlas_bytes-p;if(n>KOF_SEQUENCE_UPLOAD_CHUNK_BYTES)n=KOF_SEQUENCE_UPLOAD_CHUNK_BYTES;
            check(kof_asset_read(&assets.store,a,p,scratch,n)==KOF_ASSET_OK);
            /* SDK VRAM write fences previous VDP1 work before replacing atlas. */
            SAT_PANIC_IF_ERROR(sat_vdp1_vram_write(base+p,scratch,n));p+=n;
            kof_loading_update(&loading,p,c->atlas_bytes,0);
        }
        kof_sequence_stage=4;
        for(unsigned i=0;i<c->count;++i){
            const seq_frame *f=&seq_frames[c->first+i];
            check(f->count&&f->count<=2048&&f->offset>=c->atlas_bytes);
            check(kof_asset_read(&assets.store,a,f->offset,commands,(uint32_t)f->count*16u)==KOF_ASSET_OK);
            SAT_PANIC_IF_ERROR(sat_set_clear_color(f->bg));
            SAT_PANIC_IF_ERROR(sat_begin_frame());
            sat_vdp1_command_stats_t cs;SAT_PANIC_IF_ERROR(sat_vdp1_command_stats(&cs));
            check((unsigned)cs.used+f->count+1u<=cs.capacity);
#if KOF_SEQUENCE_BATCH
            sat_sprite_batch_window_t win;
            const uint32_t options=SAT_SPRITE_BATCH_NO_CULL |
                (KOF_SEQUENCE_BATCH_PRECLIP_OPT?0u:SAT_SPRITE_BATCH_KEEP_PRECLIP);
            SAT_PANIC_IF_ERROR(sat_sprite_batch_begin(&win,f->count,options));
#endif
            for(unsigned j=0;j<f->count;++j){
                const uint16_t *d=commands[j];
                uint32_t t=(uint32_t)d[4]*8u,l=(uint32_t)d[5]*8u;
                check(d[0]<320&&d[1]<224&&(d[2]==8||d[2]==16)&&d[3]&&d[3]<=16&&d[1]+d[3]<=224&&t+(uint32_t)d[2]*d[3]/2u<=c->atlas_bytes&&l+32u<=c->atlas_bytes&&!(d[6]&~0x60u)&&d[7]==0);
#if KOF_SEQUENCE_BATCH
                sat_sprite_frame_t sprite=prefixes[d[6]>>5];
                sprite.srca=(uint16_t)((base+t)/8u);
                sprite.colr=(uint16_t)((base+l)/8u);
                sprite.size=(uint16_t)(((d[2]/8u)<<8)|d[3]);
                check(sat_sprite_batch_emit(&win,&sprite,d[0],d[1])==1);
#else
                const sat_vdp1_texture_t tex={.srca=(uint16_t)((base+t)/8u),.palette=(uint16_t)((base+l)/8u),.width=d[2],.height=d[3],.valid=1,.format=SAT_VDP1_TEXTURE_LUT4};
                const sat_sprite_cmd_t cmd={.x=((int32_t)d[0]-160)*SAT_FX16_ONE,.y=((int32_t)d[1]-112)*SAT_FX16_ONE,.width=d[2],.height=d[3],.texture=&tex,.flags=d[6]};
                SAT_PANIC_IF_ERROR(sat_draw_sprite(&cmd));
#endif
            }
#if KOF_SEQUENCE_BATCH
            sat_sprite_batch_stats_t stats;
            SAT_PANIC_IF_ERROR(sat_sprite_batch_end(&win,&stats));
            check(stats.submitted==f->count&&!stats.culled&&!stats.dropped&&!stats.invalid);
#endif
            SAT_PANIC_IF_ERROR(sat_end_frame());
            SAT_PANIC_IF_ERROR(sat_wait_vblank());
            kof_sequence_index=c->first+i;kof_sequence_commands=f->count;++kof_sequence_frames;
        }
        /* Only synchronous cartridge reads; no renderer references cart memory. */
        check(kof_asset_leave_combat(&assets.store)==KOF_ASSET_OK);
      }
    }
}
