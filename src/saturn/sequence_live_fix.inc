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
static uint32_t lf_shadow_hash,lf_pal_hash;
static uint32_t lf_published_snapshot_hash,lf_published_snapshot_valid;
static uint32_t lf_pending_snapshot_hash,lf_pending_snapshot_valid,lf_pending_eligible;
static inline uint32_t lf_mix(uint32_t w){return w*0x9e3779b1u;}
typedef struct {uint16_t word,hi,lo;} lf_write;
static lf_write lf_pending[LF_CELLS];
static volatile uint32_t lf_serial,lf_count,lf_pal_dirty;
static uint8_t lf_showing;
volatile uint32_t kof_sequence_live_fix_map_reuse,kof_sequence_live_fix_map_reused;
volatile uint32_t kof_sequence_fix_updates,kof_sequence_fix_swaps,kof_sequence_fix_errors;
volatile uint32_t kof_sequence_live_fix_vdp2,kof_sequence_live_fix_vdp2_frames;
volatile uint32_t kof_sequence_live_fix_vdp2_fallbacks,kof_sequence_live_fix_vdp2_tiles;
volatile uint32_t kof_sequence_live_fix_vdp2_pal_words,kof_sequence_live_fix_vdp2_max_updates;
static uint32_t lf_name(unsigned tile,unsigned pal){
    return sat_vdp2_pattern_name_2word((uint16_t)(LF_BASE/32u+tile),
                                      (uint8_t)(LF_PAL_BASE+pal),0,0,0,0);
}
/* Same 32-bit name without the SDK call: aux is only the 7-bit palette
 * (flips/specials are zero), char is the resident character number. */
static inline uint32_t lf_name_fast(unsigned code,unsigned pal){
    return ((uint32_t)(LF_PAL_BASE+(pal&15u))<<16)|
           (LF_BASE/32u+(uint32_t)lf_codes[code]);
}
static void lf_flush(void){
    volatile uint16_t *vram=(volatile uint16_t *)(uintptr_t)0x25e00000u;
    volatile uint16_t *cram=(volatile uint16_t *)(uintptr_t)0x25f00000u;
    const uint32_t n=lf_count;
    if(lf_pal_dirty){
        for(unsigned i=0;i<LF_PAL_WORDS;++i)if(lf_pal_shadow[i]!=lf_pal_next[i]){
            cram[LF_PAL_BASE*16u+i]=lf_pal_next[i];
            lf_pal_hash^=lf_mix(lf_pal_shadow[i])^lf_mix(lf_pal_next[i]);
            lf_pal_shadow[i]=lf_pal_next[i];++kof_sequence_live_fix_vdp2_pal_words;
        }
    }
    for(unsigned i=0;i<n;++i){
        const lf_write *w=&lf_pending[i];
        vram[w->word]=w->hi;vram[w->word+1u]=w->lo;
        uint32_t old=lf_shadow[w->word];
        uint32_t now=((uint32_t)w->lo<<16)|w->hi;
        lf_shadow_hash^=lf_mix(old)^lf_mix(now);
        lf_shadow[w->word]=now;
    }
    kof_sequence_fix_updates+=n;
    if(n>kof_sequence_live_fix_vdp2_max_updates)kof_sequence_live_fix_vdp2_max_updates=n;
    lf_count=0;lf_pal_dirty=0;
    lf_showing=lf_pending_eligible;
    lf_published_snapshot_hash=lf_pending_snapshot_hash;
    lf_published_snapshot_valid=lf_pending_snapshot_valid;
}
static void lf_swap(uint32_t serial,void *unused){
    (void)unused;
    /* The map/CRAM writes are published by lf_flush before the list swap;
     * the callback only retires the matching list serial.  Stale lists are
     * not displayed and are ignored. */
    if(serial!=lf_serial)return;
    ++kof_sequence_fix_swaps;
    lf_serial=0;
}
static void lf_init(void){
    kof_sequence_live_fix_map_reuse=KOF_LIVE_FIX_MAP_REUSE;
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
    lf_shadow_hash=0;for(unsigned i=0;i<LF_CELLS;++i)lf_shadow_hash^=lf_mix(lf_shadow[i]);
    lf_pal_hash=0;for(unsigned i=0;i<LF_PAL_WORDS;++i)lf_pal_hash^=lf_mix(lf_pal_shadow[i]);
    lf_published_snapshot_hash=0;lf_published_snapshot_valid=0;
    lf_pending_snapshot_hash=0;lf_pending_snapshot_valid=0;lf_pending_eligible=0;
    lf_showing=0;
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
    /* The latest pending map is the published-shadow diff from the map
     * currently in VRAM.  A newer frame may overwrite an unswapped pending;
     * the latest diff still contains every change since the published map. */
    if(lf_showing&&fix_snapshot.valid&&lf_published_snapshot_valid&&
       lf_published_snapshot_hash==fix_snapshot.hash){
        kof_sequence_live_fix_vdp2_frames++;
        kof_sequence_live_fix_hash_fast++;
        /* The published map is already correct; drop any stale pending and
         * republish the same snapshot identity on the next swap. */
        lf_count=0;lf_pal_dirty=0;
        lf_pending_snapshot_hash=fix_snapshot.hash;
        lf_pending_snapshot_valid=1;lf_pending_eligible=1;
        return 1;
    }
    kof_sequence_live_fix_hash_slow++;
    int eligible=fix_snapshot.valid;
    if(eligible)
        for(unsigned i=0;i<LF_CELLS;++i){
            unsigned code=kof_fix_code(&fix_snapshot,kof_fix_map_entry(&fix_snapshot,i));
            if(lf_codes[code]==KOF_FIX_VDP2_MISSING){eligible=0;break;}
        }
    if(!eligible&&!lf_showing){
        ++kof_sequence_live_fix_vdp2_fallbacks;
        lf_count=0;lf_pal_dirty=0;
        lf_pending_snapshot_hash=0;lf_pending_snapshot_valid=0;lf_pending_eligible=0;
        return 0;
    }
    unsigned used=0;
    const uint32_t blank=lf_name(0,0);
    for(unsigned x=0;x<40u;++x){
        const uint16_t *col=&fix_snapshot.map[x*32u+2u];
        for(unsigned y=0,i=x;y<28u;++y,i+=40u){
            uint32_t name=blank;
            if(eligible){
                uint16_t e=col[y];
                name=lf_name_fast(kof_fix_code(&fix_snapshot,e),e>>12);
            }
            if(name==lf_shadow[i])continue;
            lf_pending[used++]=(lf_write){(uint16_t)((y*64u+x)*2u),(uint16_t)(name>>16),(uint16_t)name};
        }
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
    lf_pending_snapshot_hash=fix_snapshot.hash;
    lf_pending_snapshot_valid=eligible;lf_pending_eligible=eligible;
    return eligible;
}
