#ifndef KOF98_FIX_VDP2_H
#define KOF98_FIX_VDP2_H
#include "kof98/fix_state.h"
#define KOF_FIX_VDP2_CELLS 1120u
#define KOF_FIX_VDP2_CODES 8192u
#define KOF_FIX_VDP2_MISSING UINT16_MAX
/* The raw FIX source is an 8x8 tile in the left half of a 16x16
 * LUT4 tile. Preserve the exact nibble ordering used by VDP1. */
static inline void kof_fix_extract(const uint8_t raw[128],uint8_t out[32]){
    for(unsigned y=0;y<8u;++y)
        for(unsigned x=0;x<4u;++x)out[y*4u+x]=raw[y*8u+x];
}
static inline uint16_t kof_fix_map_entry(const kof_fix_snapshot *s,unsigned cell){
    return s->map[(cell%40u)*32u+cell/40u+2u];
}
static inline unsigned kof_fix_code(const kof_fix_snapshot *s,uint16_t entry){
    return (entry&4095u)+(s->bios?4096u:0u);
}
#endif
