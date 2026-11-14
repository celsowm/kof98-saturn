#ifndef KOF98_STRIP_H
#define KOF98_STRIP_H
#include "kof98/lspc_ir.h"
#define KOF_STRIP_END UINT16_MAX
#define KOF_STRIP_SLOTS 381u
#define KOF_STRIP_ROWS 16u
typedef struct {
    uint32_t tile;
    uint16_t x,y,mask,next;
    uint16_t colors[16]; /* exact guest palette words */
    uint8_t rows[16],height,width,flip;
} kof_strip;
typedef struct {
    kof_strip *strips;
    uint16_t capacity,count,line;
    uint16_t head[KOF_STRIP_SLOTS],tail[KOF_STRIP_SLOTS];
    uint32_t spans;
    uint8_t failed;
} kof_strip_builder;
/* Caller-owned bounded workspace. A failed frame must be discarded. */
int kof_strip_begin(kof_strip_builder*,kof_strip*,uint16_t capacity);
/* Sprite-only IR, lines sequential from 0; current palette bank (4096 words). */
int kof_strip_push(kof_strip_builder*,const kof_line_ir*,const uint16_t*,uint32_t palette_words);
int kof_strip_complete(const kof_strip_builder*);
/* Iterate head[0..380], then next: preserves slot painter order. */
/* 1: original 16x16 pattern reusable, flips returned separately. */
int kof_strip_valid(const kof_strip*);
int kof_strip_native(const kof_strip*,uint8_t *flip_h,uint8_t *flip_v);
/* Pack exact sampled pixels; width padded to 8/16 with transparent pen 0. */
int kof_strip_pixels(const kof_strip*,const uint8_t tile[128],uint8_t out[128],uint16_t *width);
#endif
