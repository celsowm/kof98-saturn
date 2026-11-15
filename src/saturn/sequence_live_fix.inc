/* Hybrid FIX: constant snapshots go directly to NBG0; raster FIX remains
 * on VDP1. The swap callback is the only publisher of visible map/CRAM.
 * No tile/palette/command dictionaries from the offline resident pack. */
#include "kof98/fix_vdp2.h"
#include <saturn/vdp2_special.h>
#include <saturn/vdp2_layers.h>
#define LF_BASE 0x40000u
#define LF_PAL_BASE 16u
#define LF_CELLS KOF_FIX_VDP2_CELLS
#define LF_PAL_WORDS 256u
static uint16_t lf_codes[KOF_FIX_VDP2_CODES];
static uint32_t lf_shadow[LF_CELLS];
static uint16_t lf_pal_shadow[LF_PAL_WORDS],lf_pal_next[LF_PAL_WORDS];
typedef struct {uint16_t word,hi,lo;} lf_write;
static lf_write lf_pending[LF_CELLS];
static volatile uint32_t lf_serial,lf_count,lf_pal_dirty;
static uint8_t lf_showing;
volatile uint32_t kof_sequence_fix_updates,kof_sequence_fix_swaps,kof_sequence_fix_errors;
volatile uint32_t kof_sequence_live_fix_vdp2,kof_sequence_live_fix_vdp2_frames;
volatile uint32_t kof_sequence_live_fix_vdp2_fallbacks,kof_sequence_live_fix_vdp2_tiles;
volatile uint32_t kof_sequence_live_fix_vdp2_pal_words,kof_sequence_live_fix_vdp2_max_updates;
static uint32_t lf_name(unsigned tile,unsigned pal){
    return sat_vdp2_pattern_name_2word((uint16_t)(LF_BASE/32u+tile),
                                      (uint8_t)(LF_PAL_BASE+pal),0,0,0,0);
}
static void lf_swap(uint32_t serial,void *unused){
    (void)unused;
    if(serial!=lf_serial){++kof_sequence_fix_errors;return;}
    volatile uint16_t *vram=(volatile uint16_t *)(uintptr_t)0x25e00000u;
    volatile uint16_t *cram=(volatile uint16_t *)(uintptr_t)0x25f00000u;
    const uint32_t n=lf_count;
    if(lf_pal_dirty){
        for(unsigned i=0;i<LF_PAL_WORDS;++i)if(lf_pal_shadow[i]!=lf_pal_next[i]){
            cram[LF_PAL_BASE*16u+i]=lf_pal_next[i];
            lf_pal_shadow[i]=lf_pal_next[i];++kof_sequence_live_fix_vdp2_pal_words;
        }
    }
    for(unsigned i=0;i<n;++i){
        const lf_write *w=&lf_pending[i];
        vram[w->word]=w->hi;vram[w->word+1u]=w->lo;
    }
    kof_sequence_fix_updates+=n;++kof_sequence_fix_swaps;
    if(n>kof_sequence_live_fix_vdp2_max_updates)kof_sequence_live_fix_vdp2_max_updates=n;
    lf_count=0;lf_pal_dirty=0;lf_serial=0;
}
static void lf_init(void){
    memset(lf_codes,0xff,sizeof lf_codes);
    memset(scratch,0,32u); /* tile zero is the transparent fallback */
    unsigned count=1;
    for(unsigned i=0;i<LIVE_TILE_COUNT;++i){
        const uint32_t id=tile_ids[i];
        if(id<524288u)continue;
        unsigned code=id-524288u;
        check(code<KOF_FIX_VDP2_CODES&&count<4096u&&(count+1u)*32u<=sizeof scratch);
        _Alignas(4) uint8_t raw[128];
        read_live(LIVE_TILES_OFFSET+i*128u,raw,128u);
        kof_fix_extract(raw,scratch+count*32u);
        lf_codes[code]=(uint16_t)count++;
    }
    kof_sequence_live_fix_vdp2_tiles=count;
    SAT_PANIC_IF_ERROR(sat_vdp2_vram_write_words(LF_BASE/2u,
        (const uint16_t *)(const void*)scratch,count*16u));
    SAT_PANIC_IF_ERROR(sat_vdp2_palette_upload(lf_pal_shadow,LF_PAL_WORDS,LF_PAL_BASE*16u));
    const uint32_t blank=lf_name(0,0);
    uint16_t *map=(uint16_t *)(void*)scratch;
    check(sizeof scratch>=16384u);
    for(unsigned i=0;i<4096u;++i){map[i*2u]=(uint16_t)(blank>>16);map[i*2u+1u]=(uint16_t)blank;}
    SAT_PANIC_IF_ERROR(sat_vdp2_vram_write_words(0,map,8192u));
    for(unsigned i=0;i<LF_CELLS;++i)lf_shadow[i]=blank;
    sat_vdp2_layer_config_t c;
    sat_vdp2_layer_config_default(SAT_VDP2_NBG0,&c);
    c.color_mode=SAT_VDP2_COLOR_MODE_16;c.pattern_name_words=2u;
    c.priority=7u;c.transparent=1u;c.char_bank_mask=SAT_VDP2_BANK_B0;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&c));
    SAT_PANIC_IF_ERROR(sat_vdp2_sprite_set_priority(6u));
    SAT_PANIC_IF_ERROR(sat_wait_vblank());
    SAT_PANIC_IF_ERROR(sat_vdp2_layers_commit());
    SAT_PANIC_IF_ERROR(sat_vdp1_set_swap_handler(lf_swap,0));
}
/* Inspect all map tile IDs before staging any writes. If the selected
 * asset bank lacks a needed tile, use the established VDP1 fallback
 * (which itself fails closed if the pack cannot supply the strip). */
static int lf_prepare(void){
    check(!lf_serial&&!lf_count&&!lf_pal_dirty);
    int eligible=fix_snapshot.valid;
    if(eligible)
        for(unsigned i=0;i<LF_CELLS;++i){
            unsigned code=kof_fix_code(&fix_snapshot,kof_fix_map_entry(&fix_snapshot,i));
            if(lf_codes[code]==KOF_FIX_VDP2_MISSING){eligible=0;break;}
        }
    if(!eligible&&!lf_showing){
        ++kof_sequence_live_fix_vdp2_fallbacks;return 0;
    }
    unsigned used=0;
    const uint32_t blank=lf_name(0,0);
    for(unsigned y=0;y<28u;++y)
      for(unsigned x=0;x<40u;++x){
        unsigned i=y*40u+x;
        uint32_t name=blank;
        if(eligible){
            uint16_t e=fix_snapshot.map[x*32u+y+2u];
            name=lf_name(lf_codes[kof_fix_code(&fix_snapshot,e)],e>>12);
        }
        if(name==lf_shadow[i])continue;
        lf_shadow[i]=name;
        lf_pending[used++]=(lf_write){(uint16_t)((y*64u+x)*2u),(uint16_t)(name>>16),(uint16_t)name};
      }
    lf_count=used;
    if(eligible){
        unsigned dirty=0;
        for(unsigned i=0;i<LF_PAL_WORDS;++i){
            uint16_t c=(i&15u)?kof_lspc_rgb555(fix_snapshot.pal[i]):0u;
            lf_pal_next[i]=c;dirty|=c!=lf_pal_shadow[i];
        }
        lf_pal_dirty=dirty;
        ++kof_sequence_live_fix_vdp2_frames;
    }else ++kof_sequence_live_fix_vdp2_fallbacks;
    lf_showing=(uint8_t)eligible;
    return eligible;
}
