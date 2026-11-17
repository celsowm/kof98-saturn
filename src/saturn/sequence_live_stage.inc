/* Live stage underlay: the first 25 strips of the exact live chain are
 * published on VDP2 NBG1 and skipped in the VDP1 emission.  The stage
 * tiles/LUT/map are immutable assets appended to the live pack. */
#define LIVE_STAGE_MAP_BASE 0x20000u
#define LIVE_STAGE_TILE_BASE 0x60000u
#define LIVE_STAGE_PAL_BASE 64u
#define LIVE_STAGE_CELLS 1120u
#ifndef LIVE_STAGE_SKIP
#define LIVE_STAGE_SKIP 25u
#endif
static uint16_t live_stage_map[4096u*2u];
static void live_stage_init(void){
    read_live(LIVE_STAGE_TILE_OFFSET,scratch,LIVE_STAGE_TILE_BYTES);
    SAT_PANIC_IF_ERROR(sat_vdp2_vram_write_words(LIVE_STAGE_TILE_BASE/2u,
        (const uint16_t *)(const void*)scratch,LIVE_STAGE_TILE_BYTES/2u));
    read_live(LIVE_STAGE_LUT_OFFSET,scratch,LIVE_STAGE_LUT_BYTES);
    SAT_PANIC_IF_ERROR(sat_vdp2_palette_upload((const uint16_t *)(const void*)scratch,
        LIVE_STAGE_LUT_BYTES/2u,LIVE_STAGE_PAL_BASE*16u));
    read_live(LIVE_STAGE_MAP_OFFSET,scratch,LIVE_STAGE_MAP_BYTES);
    const uint16_t blank_hi=0x0040u,blank_lo=0x3000u; /* stage_name(0,0) */
    for(unsigned i=0;i<4096u*2u;i+=2u){live_stage_map[i]=blank_hi;live_stage_map[i+1u]=blank_lo;}
    const uint8_t *names=scratch;
    for(unsigned cell=0;cell<LIVE_STAGE_CELLS;++cell){
        uint32_t name=((uint32_t)names[cell*4u]<<24)|((uint32_t)names[cell*4u+1u]<<16)|
                      ((uint32_t)names[cell*4u+2u]<<8)|(uint32_t)names[cell*4u+3u];
        unsigned x=cell%40u,y=cell/40u,w=(y*64u+x)*2u;
        live_stage_map[w]=(uint16_t)(name>>16);live_stage_map[w+1u]=(uint16_t)name;
    }
    SAT_PANIC_IF_ERROR(sat_vdp2_vram_write_words(LIVE_STAGE_MAP_BASE/2u,
        (const uint16_t *)(const void*)live_stage_map,8192u));
    sat_vdp2_layer_config_t c;
    sat_vdp2_layer_config_default(SAT_VDP2_NBG1,&c);
    c.color_mode=SAT_VDP2_COLOR_MODE_16;c.pattern_name_words=2u;
    c.priority=4u;c.transparent=1u;c.char_bank_mask=SAT_VDP2_BANK_B1;
    for(unsigned i=0;i<4u;++i)c.plane_address[i]=LIVE_STAGE_MAP_BASE;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&c));
    SAT_PANIC_IF_ERROR(sat_vdp2_set_backdrop_color(32768));
    SAT_PANIC_IF_ERROR(sat_vdp2_layers_commit());
}