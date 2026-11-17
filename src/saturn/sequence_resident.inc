/* Resident replay, included only for SEQ_RESIDENT manifests.
 * Offline cache decisions; no CD activity after the initial checked load.
 * Use the SDK's fenced upload API for all changed slots. */
#include "../bytes.h"
_Static_assert(SEQ_MAX_ATLAS==SEQ_PAT_SLOTS*128u+SEQ_LUT_SLOTS*32u,"atlas");
_Static_assert(SEQ_RESIDENT_BYTES<=KOF_CART_BYTES,"cart");
static _Alignas(32) uint16_t previous[2048][8];
static _Alignas(32) uint8_t upload[8192];
static uint32_t take16(const uint8_t **p,const uint8_t *end) {
    check((size_t)(end-*p)>=2u);
    uint32_t v=((uint32_t)(*p)[0]<<8)|(*p)[1];*p+=2;return v;
}
static void read_asset(kof_asset_handle a,uint32_t off,void *dst,uint32_t n) {
    check(kof_asset_read(&assets.store,a,off,dst,n)==KOF_ASSET_OK);
}
#ifdef SEQ_FIX_VDP2
#include "sequence_fix.inc"
#endif
static void upload_slots(kof_asset_handle a,uint32_t base,const uint8_t **p,const uint8_t *end) {
    unsigned count=take16(p,end);
    check(count<=SEQ_PAT_SLOTS+SEQ_LUT_SLOTS && (size_t)(end-*p)>=count*4u);
    uint32_t last=0;
    for(unsigned i=0;i<count;){
        uint32_t slot=take16(p,end),id=take16(p,end);
        check(i==0||slot>last);
        const uint32_t palette=slot>>15, index=slot&0x7fffu;
        const uint32_t stride=palette?32u:128u;
        const uint32_t slots=palette?SEQ_LUT_SLOTS:SEQ_PAT_SLOTS;
        const uint32_t dict=palette?SEQ_PALETTE_COUNT:SEQ_PATTERN_COUNT;
        const uint32_t origin=palette?SEQ_PALETTE_OFFSET:0u;
        const uint32_t dest=palette?SEQ_PAT_SLOTS*128u:0u;
        check(index<slots && id<dict);
        read_asset(a,origin+id*stride,upload,stride);
        unsigned run=1;
        while(i+run<count && (run+1u)*stride<=sizeof upload){
            const uint8_t *q=*p;
            uint32_t next=take16(&q,end),key=take16(&q,end);
            if(next!=slot+run || (next>>15)!=palette || index+run>=slots)break;
            check(key<dict);
            read_asset(a,origin+key*stride,upload+run*stride,stride);
            *p=q;++run;
        }
        SAT_PANIC_IF_ERROR(sat_vdp1_vram_write(base+dest+index*stride,upload,run*stride));
        kof_sequence_upload_bytes+=run*stride;++kof_sequence_upload_runs;
        last=slot+run-1u;i+=run;
    }
}
static void decode(const uint8_t *p,const uint8_t *end,unsigned count,unsigned old_count,
                   uint16_t dst[][8],uint16_t old[][8]){
    unsigned used=0;
    while(p<end){
        uint32_t token=take16(&p,end),n=token&0x7fffu;
        check(n&&n<=count-used);
        if(token&0x8000u){
            uint32_t from=take16(&p,end);
            check(from<=old_count && n<=old_count-from);
            memcpy(dst[used],old[from],n*sizeof dst[0]);
            used+=n;
        }else{
            for(unsigned j=0;j<n;++j){
                uint32_t high=take16(&p,end),low=take16(&p,end);
                uint32_t bits=(high<<16)|low,t=take16(&p,end),l=take16(&p,end);
                uint16_t *d=dst[used++];
                d[0]=bits&511u;d[1]=(bits>>9)&255u;
                d[2]=(bits&(1u<<17))?16u:8u;
                d[3]=1u+((bits>>18)&15u);d[6]=((bits>>22)&3u)<<5;d[7]=0;
                check(!(bits>>24)&&d[0]<320u&&d[1]+d[3]<=224u&&t<SEQ_PAT_SLOTS&&l<SEQ_LUT_SLOTS);
                d[4]=t*16u;d[5]=SEQ_PAT_SLOTS*16u+l*4u;
            }
        }
    }
    check(p==end&&used==count);
}
int main(void) {
    const sat_video_config_t cfg={320,224,SAT_VIDEO_NTSC,SAT_VIDEO_INTERLACE_NONE};
    kof_sequence_stage=1;
    SAT_PANIC_IF_ERROR(sat_init(&cfg));
    SAT_PANIC_IF_ERROR(sat_vdp1_set_frame_mode(SAT_VDP1_FRAME_ON_SUBMIT));
    kof_loading_init(&loading);
    kof_loading_phase(&loading,"INITIALIZING CD",1,1);
    SAT_PANIC_IF_ERROR(sat_cd_mount_default(&block,0,0,&device,0,&volume));
    SAT_PANIC_IF_ERROR(kof_saturn_assets_open(&assets,&volume,"SEQUENCE.BIN",KOF_CART_BYTES));
    uint32_t base;
    kof_sequence_vram_free=sat_vdp1_vram_free_bytes();
    kof_sequence_vram_requested=SEQ_MAX_ATLAS+24u;
    SAT_PANIC_IF_ERROR(sat_vdp1_vram_reserve(kof_sequence_vram_requested,&base));
    base=(base+31u)&~31u;
    kof_sequence_stage=2;kof_sequence_clip=0;
    kof_loading_phase(&loading,"LOADING RESIDENT REPLAY",1,1);
    const kof_asset_desc desc={1,0,SEQ_RESIDENT_BYTES,SEQ_RESIDENT_CRC};
    check(kof_asset_begin(&assets.store,&desc,1)==KOF_ASSET_OK);
    ++kof_sequence_cd_loads;
    kof_asset_result r;
    do {
        r=kof_asset_step_cd(&assets.store,scratch,sizeof scratch);
        if(r==KOF_ASSET_PENDING||r==KOF_ASSET_OK)
            kof_loading_update(&loading,r==KOF_ASSET_OK?desc.bytes:assets.store.progress,desc.bytes,0);
    }while(r==KOF_ASSET_PENDING);
    if(r==KOF_ASSET_IO)SAT_PANIC_IF_ERROR(assets.last_sdk_error);
    check(r==KOF_ASSET_OK);
    kof_asset_handle a;
    check(kof_asset_find(&assets.store,1,&a)==KOF_ASSET_OK);
    check(kof_asset_enter_combat(&assets.store)==KOF_ASSET_OK);
#ifdef SEQ_FIX_VDP2
    fix_init(a);
#endif
    sat_sprite_frame_t prefixes[4];
    for(unsigned flip=0;flip<4;++flip)
        SAT_PANIC_IF_ERROR(sat_sprite_frame_init_raw(&prefixes[flip],
            (uint16_t)(base/8u),8,1,SAT_VDP1_TEXTURE_LUT4,
            (uint16_t)(base/8u),(uint16_t)(flip<<5),0,0));
    uint16_t (*dst)[8]=commands,(*old)[8]=previous;
    for(;;){
        unsigned old_count=0;
        for(unsigned i=0;i<sizeof seq_resident_frames/sizeof seq_resident_frames[0];++i){
            const seq_resident_frame *f=&seq_resident_frames[i];
            check(f->count&&f->count<=2048u&&f->bytes<=sizeof scratch&&f->offset>=SEQ_PACKET_OFFSET);
            read_asset(a,f->offset,scratch,f->bytes);
            const uint8_t *p=scratch,*end=scratch+f->bytes;
            kof_sequence_stage=3;
            upload_slots(a,base,&p,end);
#ifdef SEQ_FIX_VDP2
            fix_prepare(&p,end);
#ifdef SEQ_STAGE_VDP2
            stage_prepare(&p,end);
#endif
#endif
            decode(p,end,f->count,old_count,dst,old);
            kof_sequence_stage=4;
#ifdef SEQ_STAGE_VDP2
            check(f->bg==SEQ_STAGE_BG);
            SAT_PANIC_IF_ERROR(sat_vdp1_set_erase_transparent());
#else
            SAT_PANIC_IF_ERROR(sat_set_clear_color(f->bg));
#endif
            SAT_PANIC_IF_ERROR(sat_begin_frame());
            sat_vdp1_command_stats_t cs;SAT_PANIC_IF_ERROR(sat_vdp1_command_stats(&cs));
            check((unsigned)cs.used+f->count+1u<=cs.capacity);
            sat_sprite_batch_window_t win;
            SAT_PANIC_IF_ERROR(sat_sprite_batch_begin(&win,f->count,SAT_SPRITE_BATCH_KEEP_PRECLIP|SAT_SPRITE_BATCH_NO_CULL));
            for(unsigned j=0;j<f->count;++j){
                const uint16_t *d=dst[j];
                sat_sprite_frame_t sprite=prefixes[d[6]>>5];
                sprite.srca=(uint16_t)(base/8u+d[4]);
                sprite.colr=(uint16_t)(base/8u+d[5]);
                sprite.size=(uint16_t)(((d[2]/8u)<<8)|d[3]);
                check(sat_sprite_batch_emit(&win,&sprite,d[0],d[1])==1);
            }
            sat_sprite_batch_stats_t stats;
            SAT_PANIC_IF_ERROR(sat_sprite_batch_end(&win,&stats));
            check(stats.submitted==f->count&&!stats.culled&&!stats.dropped&&!stats.invalid);
#ifdef SEQ_FIX_VDP2
            fix_pending_serial=sat_vdp1_list_serial()+1u;
#endif
            SAT_PANIC_IF_ERROR(sat_end_frame());
            SAT_PANIC_IF_ERROR(sat_wait_vblank());
#ifdef SEQ_FIX_VDP2
            check(!fix_pending_serial && !fix_pending_count && !kof_sequence_fix_errors);
#ifdef SEQ_STAGE_VDP2
            check(!stage_pending_count);
#endif
#endif
            kof_sequence_index=i;kof_sequence_commands=f->count;++kof_sequence_frames;
            uint16_t (*swap)[8]=old;old=dst;dst=swap;old_count=f->count;
        }
        /* First packet contains all resources needed by frame zero and
         * has no previous-frame references; no CD reads at loop wrap. */
    }
}
