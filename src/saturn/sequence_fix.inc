/* NBG0 FIX experiment: immutable chars/palettes; only map words change.
 * The VDP1 swap callback publishes the map in the corresponding VBlank.
 * No SDK calls, DMA, CD reads or texture writes inside the callback. */
#include <saturn/vdp2_special.h>
#include <saturn/vdp2_layers.h>
#define FIX_TILE_BASE 0x40000u
#define FIX_CELLS 1120u
#define FIX_PALETTE_BASE 16u
static uint32_t fix_shadow[FIX_CELLS];
typedef struct {uint16_t word,hi,lo;} fix_write;
static fix_write fix_pending[FIX_CELLS];
static volatile uint32_t fix_pending_count,fix_pending_serial;
volatile uint32_t kof_sequence_fix_updates,kof_sequence_fix_swaps,kof_sequence_fix_errors;
static void fix_swap(uint32_t serial,void *unused){
    (void)unused;
    if(serial!=fix_pending_serial){++kof_sequence_fix_errors;return;}
    volatile uint16_t *vram=(volatile uint16_t *)(uintptr_t)0x25e00000u;
    uint32_t n=fix_pending_count;
    for(uint32_t i=0;i<n;++i){
        const fix_write *w=&fix_pending[i];
        vram[w->word]=w->hi;vram[w->word+1u]=w->lo;
    }
    kof_sequence_fix_updates+=n;++kof_sequence_fix_swaps;
    fix_pending_count=0;fix_pending_serial=0;
}
static uint32_t fix_name(uint32_t tile,uint32_t palette){
    return sat_vdp2_pattern_name_2word((uint16_t)(FIX_TILE_BASE/32u+tile),
                                      (uint8_t)(FIX_PALETTE_BASE+palette),0,0,0,0);
}
static void fix_init(kof_asset_handle a){
    check(SEQ_FIX_TILE_BYTES<=0x20000u && SEQ_FIX_TILE_BYTES<=sizeof scratch &&
          SEQ_FIX_LUT_BYTES<=96u*32u && SEQ_FIX_LUT_BYTES<=sizeof scratch);
    read_asset(a,SEQ_FIX_TILE_OFFSET,scratch,SEQ_FIX_TILE_BYTES);
    SAT_PANIC_IF_ERROR(sat_vdp2_vram_write_words(FIX_TILE_BASE/2u,(const uint16_t *)(const void*)scratch,SEQ_FIX_TILE_BYTES/2u));
    read_asset(a,SEQ_FIX_LUT_OFFSET,scratch,SEQ_FIX_LUT_BYTES);
    SAT_PANIC_IF_ERROR(sat_vdp2_palette_upload((const uint16_t *)(const void*)scratch,
        SEQ_FIX_LUT_BYTES/2u,FIX_PALETTE_BASE*16u));
    uint32_t blank=fix_name(0,0);
    uint16_t *map=(uint16_t *)(void*)scratch;
    for(unsigned i=0;i<4096u;++i){map[i*2u]=(uint16_t)(blank>>16);map[i*2u+1u]=(uint16_t)blank;}
    SAT_PANIC_IF_ERROR(sat_vdp2_vram_write_words(0,map,8192u));
    for(unsigned i=0;i<FIX_CELLS;++i)fix_shadow[i]=blank;
    sat_vdp2_layer_config_t c;
    sat_vdp2_layer_config_default(SAT_VDP2_NBG0,&c);
    c.color_mode=SAT_VDP2_COLOR_MODE_16;c.pattern_name_words=2u;
    c.priority=7u;c.transparent=1u;c.char_bank_mask=SAT_VDP2_BANK_B0;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&c));
    SAT_PANIC_IF_ERROR(sat_vdp2_sprite_set_priority(6u));
    SAT_PANIC_IF_ERROR(sat_wait_vblank());
    SAT_PANIC_IF_ERROR(sat_vdp2_layers_commit());
    SAT_PANIC_IF_ERROR(sat_vdp1_set_swap_handler(fix_swap,0));
}
static void fix_prepare(const uint8_t **p,const uint8_t *end){
    check(!fix_pending_serial && !fix_pending_count);
    uint32_t n=take16(p,end),last=0,used=0;
    check(n<=FIX_CELLS);
    for(uint32_t i=0;i<n;++i){
        uint32_t cell=take16(p,end),tile=take16(p,end),pal=take16(p,end);
        check(cell<FIX_CELLS && (i==0||cell>last) &&
              tile<SEQ_FIX_TILE_BYTES/32u && pal<SEQ_FIX_LUT_BYTES/32u);
        last=cell;
        uint32_t name=fix_name(tile,pal);
        if(name==fix_shadow[cell])continue;
        fix_shadow[cell]=name;
        fix_pending[used++]=(fix_write){(uint16_t)(((cell/40u)*64u+cell%40u)*2u),
            (uint16_t)(name>>16),(uint16_t)name};
    }
    fix_pending_count=used;
}
