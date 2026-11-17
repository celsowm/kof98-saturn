#ifndef KOF98_STRIP_HASH_H
#define KOF98_STRIP_HASH_H
#include "kof98/strip.h"
static inline uint32_t kof_strip_hash_byte(uint32_t h,unsigned v){
    return (h^(v&255u))*16777619u;
}
static inline uint32_t kof_strip_hash_word(uint32_t h,unsigned v){
    return kof_strip_hash_byte(kof_strip_hash_byte(h,v>>8),v);
}
/* Exact KFR2 >I6H16H16B serialization, independent of CPU endianness,
 * struct padding and linked-list allocation. Fixed-size unrolled fields
 * eliminate the two per-record loop branches. */
static inline uint32_t kof_strip_hash_record(uint32_t h,const kof_strip *s,uint32_t bias){
    uint32_t tile=s->tile+bias;
    h=kof_strip_hash_word(kof_strip_hash_word(h,tile>>16),tile);
    h=kof_strip_hash_word(h,s->x);
    h=kof_strip_hash_word(h,s->y);
    h=kof_strip_hash_word(h,s->mask);
    h=kof_strip_hash_word(h,s->height);
    h=kof_strip_hash_word(h,s->width);
    h=kof_strip_hash_word(h,s->flip);
    h=kof_strip_hash_word(h,s->colors[0]);
    h=kof_strip_hash_word(h,s->colors[1]);
    h=kof_strip_hash_word(h,s->colors[2]);
    h=kof_strip_hash_word(h,s->colors[3]);
    h=kof_strip_hash_word(h,s->colors[4]);
    h=kof_strip_hash_word(h,s->colors[5]);
    h=kof_strip_hash_word(h,s->colors[6]);
    h=kof_strip_hash_word(h,s->colors[7]);
    h=kof_strip_hash_word(h,s->colors[8]);
    h=kof_strip_hash_word(h,s->colors[9]);
    h=kof_strip_hash_word(h,s->colors[10]);
    h=kof_strip_hash_word(h,s->colors[11]);
    h=kof_strip_hash_word(h,s->colors[12]);
    h=kof_strip_hash_word(h,s->colors[13]);
    h=kof_strip_hash_word(h,s->colors[14]);
    h=kof_strip_hash_word(h,s->colors[15]);
    h=kof_strip_hash_byte(h,s->rows[0]);
    h=kof_strip_hash_byte(h,s->rows[1]);
    h=kof_strip_hash_byte(h,s->rows[2]);
    h=kof_strip_hash_byte(h,s->rows[3]);
    h=kof_strip_hash_byte(h,s->rows[4]);
    h=kof_strip_hash_byte(h,s->rows[5]);
    h=kof_strip_hash_byte(h,s->rows[6]);
    h=kof_strip_hash_byte(h,s->rows[7]);
    h=kof_strip_hash_byte(h,s->rows[8]);
    h=kof_strip_hash_byte(h,s->rows[9]);
    h=kof_strip_hash_byte(h,s->rows[10]);
    h=kof_strip_hash_byte(h,s->rows[11]);
    h=kof_strip_hash_byte(h,s->rows[12]);
    h=kof_strip_hash_byte(h,s->rows[13]);
    h=kof_strip_hash_byte(h,s->rows[14]);
    h=kof_strip_hash_byte(h,s->rows[15]);
    return h;
}
#endif
