#ifndef KOF98_LSPC_IR_H
#define KOF98_LSPC_IR_H
#include <stdint.h>
#define KOF_LSPC_WORDS 0x8580u
#define KOF_ZOOM_BYTES 65536u
#define KOF_LINE_SPRITES 96u
#define KOF_LINE_FIX 40u
/* Logical source mask is before horizontal flip. Bits in ascending order
 * map to consecutive screen pixels starting at x. All x are clipped. */
typedef struct { uint32_t tile; uint16_t mask, x, slot; uint8_t row, palette, flip, width; } kof_sprite_span;
typedef struct { uint16_t tile; uint8_t column, row, palette; } kof_fix_span;
typedef struct { uint16_t count, evaluated; uint8_t fix_bios; kof_sprite_span sprites[KOF_LINE_SPRITES]; kof_fix_span fix[KOF_LINE_FIX]; } kof_line_ir;
/* NTSC 320x224, line = visible row 0..223. Input is host-endian guest VRAM;
 * caller supplies current raster state and exact 64 KiB L0 ROM. */
int kof_lspc_line(const uint16_t *vram, uint32_t words, const uint8_t *zoom, uint32_t zoom_bytes, unsigned anim, unsigned line, int fix_bios, kof_line_ir *out);
/* Derived only from SCB2/3/4. Rebuild after writes in 0x8000..0x857f
 * (or a full state restore). SCB1, FIX, palette and animation changes do
 * not invalidate the schedule; those values are still sampled per line. */
typedef struct {
    uint16_t x,mask,y;
    uint8_t width,rows,zy;
} kof_lspc_column;
typedef struct {
    kof_lspc_column columns[381];
    uint16_t slots[224][KOF_LINE_SPRITES];
    uint8_t count[224];
} kof_lspc_schedule;
int kof_lspc_schedule_build(kof_lspc_schedule*,const uint16_t *vram,uint32_t words);
int kof_lspc_line_scheduled(const kof_lspc_schedule*,const uint16_t *vram,uint32_t words,
    const uint8_t *zoom,uint32_t zoom_bytes,unsigned anim,unsigned line,int fix_bios,kof_line_ir *out);
/* Saturn RGB555: nearest available 5-bit components (dark step lost). */
uint16_t kof_lspc_rgb555(uint16_t word);
#endif
