/* Included by sequence_fix.inc after fix_write/take16/read_asset.
 * NBG1 A1 map, B1 immutable chars, disjoint from FIX A0/B0. */
#define STAGE_MAP_BASE 0x20000u
#define STAGE_TILE_BASE 0x60000u
#define STAGE_PALETTE_BASE 64u
#define STAGE_MAX_WRITES 512u
static uint32_t stage_shadow[FIX_CELLS];
static fix_write stage_pending[STAGE_MAX_WRITES];
static volatile uint32_t stage_pending_count;
volatile uint32_t kof_sequence_stage_updates,kof_sequence_stage_swaps,kof_sequence_stage_max_updates;
static uint32_t stage_name(uint32_t tile,uint32_t pal){
    return sat_vdp2_pattern_name_2word((uint16_t)(STAGE_TILE_BASE/32u+tile),
                                      (uint8_t)(STAGE_PALETTE_BASE+pal),0,0,0,0);
}
static void stage_init(kof_asset_handle a){
    check(SEQ_FIX_LUT_BYTES<=48u*32u &&
          SEQ_STAGE_TILE_BYTES<=4095u*32u && SEQ_STAGE_TILE_BYTES<=sizeof scratch &&
          SEQ_STAGE_LUT_BYTES<=48u*32u && SEQ_STAGE_LUT_BYTES<=sizeof scratch);
    read_asset(a,SEQ_STAGE_TILE_OFFSET,scratch,SEQ_STAGE_TILE_BYTES);
    SAT_PANIC_IF_ERROR(sat_vdp2_vram_write_words(STAGE_TILE_BASE/2u,
        (const uint16_t *)(const void*)scratch,SEQ_STAGE_TILE_BYTES/2u));
    read_asset(a,SEQ_STAGE_LUT_OFFSET,scratch,SEQ_STAGE_LUT_BYTES);
    SAT_PANIC_IF_ERROR(sat_vdp2_palette_upload((const uint16_t *)(const void*)scratch,
        SEQ_STAGE_LUT_BYTES/2u,STAGE_PALETTE_BASE*16u));
    const uint32_t blank=stage_name(0,0);
    uint16_t *map=(uint16_t *)(void*)scratch;
    for(unsigned i=0;i<4096u;++i){map[i*2u]=(uint16_t)(blank>>16);map[i*2u+1u]=(uint16_t)blank;}
    SAT_PANIC_IF_ERROR(sat_vdp2_vram_write_words(STAGE_MAP_BASE/2u,map,8192u));
    for(unsigned i=0;i<FIX_CELLS;++i)stage_shadow[i]=blank;
    sat_vdp2_layer_config_t c;
    sat_vdp2_layer_config_default(SAT_VDP2_NBG1,&c);
    c.color_mode=SAT_VDP2_COLOR_MODE_16;c.pattern_name_words=2u;
    c.priority=4u;c.transparent=1u;c.char_bank_mask=SAT_VDP2_BANK_B1;
    for(unsigned i=0;i<4;++i)c.plane_address[i]=STAGE_MAP_BASE;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&c));
    SAT_PANIC_IF_ERROR(sat_vdp2_set_backdrop_color(SEQ_STAGE_BG));
    SAT_PANIC_IF_ERROR(sat_vdp1_set_erase_transparent());
}
static void stage_prepare(const uint8_t **p,const uint8_t *end){
    check(!stage_pending_count);
    uint32_t n=take16(p,end),last=0,used=0;
    check(n<=FIX_CELLS);
    for(uint32_t i=0;i<n;++i){
        uint32_t cell=take16(p,end),tile=take16(p,end),pal=take16(p,end);
        check(cell<FIX_CELLS&&(i==0||cell>last)&&
              tile<SEQ_STAGE_TILE_BYTES/32u&&pal<SEQ_STAGE_LUT_BYTES/32u);
        last=cell;
        const uint32_t name=stage_name(tile,pal);
        if(name==stage_shadow[cell])continue;
        check(used<STAGE_MAX_WRITES);
        stage_shadow[cell]=name;
        stage_pending[used++]=(fix_write){(uint16_t)(((cell/40u)*64u+cell%40u)*2u),
            (uint16_t)(name>>16),(uint16_t)name};
    }
    stage_pending_count=used;
}
static void stage_publish(volatile uint16_t *vram){
    const uint32_t n=stage_pending_count;
    volatile uint16_t *map=vram+STAGE_MAP_BASE/2u;
    for(uint32_t i=0;i<n;++i){
        const fix_write *w=&stage_pending[i];
        map[w->word]=w->hi;map[w->word+1u]=w->lo;
    }
    kof_sequence_stage_updates+=n;++kof_sequence_stage_swaps;
    if(n>kof_sequence_stage_max_updates)kof_sequence_stage_max_updates=n;
    stage_pending_count=0;
}
