#ifndef KOF98_LUT_MEMO_H
#define KOF98_LUT_MEMO_H
#include <stdint.h>
/* Raw guest palette -> converted LUT slot. The resident LUT cache remains
 * authoritative; callers publish only after successful lookup/upload.
 * A slot must receive a new generation before reusing its storage.
 * Reset the entire memo before generation zero is reused (wrap).
 * Entry keys omit pen zero, which is always transparent in VDP1 LUT4.
 * All other guest bits are compared; different raw keys that convert to
 * equal RGB555 can still share a slot through the authoritative cache. */
#define KOF_LUT_MEMO_SETS 128u
#define KOF_LUT_MEMO_WAYS 2u
#define KOF_LUT_MEMO_NONE UINT16_MAX
typedef struct {
    uint16_t raw[16];
    uint32_t generation;
    uint16_t slot;
} kof_lut_memo_entry;
typedef struct {
    kof_lut_memo_entry entries[KOF_LUT_MEMO_SETS][KOF_LUT_MEMO_WAYS];
    uint8_t next[KOF_LUT_MEMO_SETS];
} kof_lut_memo;
static inline unsigned kof_lut_memo_set(const uint16_t *p){
    /* A cheap sampled hash is only an accelerator, never identity.
     * Mix differently positioned words without dynamic shifts. */
    uint32_t h=(uint32_t)p[1]+((uint32_t)p[3]<<3)+((uint32_t)p[5]<<7);
    h^=(uint32_t)p[8]+((uint32_t)p[11]<<5)+((uint32_t)p[15]<<11);
    h^=h>>7;h^=h>>14;
    return h&(KOF_LUT_MEMO_SETS-1u);
}
static inline int kof_lut_memo_equal(const uint16_t *a,const uint16_t *b){
    return ((a[1]^b[1])|(a[2]^b[2])|(a[3]^b[3])|
        (a[4]^b[4])|(a[5]^b[5])|(a[6]^b[6])|(a[7]^b[7])|
        (a[8]^b[8])|(a[9]^b[9])|(a[10]^b[10])|(a[11]^b[11])|
        (a[12]^b[12])|(a[13]^b[13])|(a[14]^b[14])|(a[15]^b[15]))==0;
}
static inline uint16_t kof_lut_memo_find(kof_lut_memo *m,const uint16_t *raw,
    const uint32_t *generations,unsigned count){
    const unsigned set=kof_lut_memo_set(raw);
    for(unsigned w=0;w<KOF_LUT_MEMO_WAYS;++w){
        const kof_lut_memo_entry *e=&m->entries[set][w];
        if(e->generation&&e->slot<count&&generations[e->slot]==e->generation&&
           kof_lut_memo_equal(e->raw,raw)){
            m->next[set]=(uint8_t)(w^1u);
            return e->slot;
        }
    }
    return KOF_LUT_MEMO_NONE;
}
static inline void kof_lut_memo_publish(kof_lut_memo *m,const uint16_t *raw,
    uint16_t slot,const uint32_t *generations,unsigned count){
    if(slot>=count||!generations[slot])return;
    const unsigned set=kof_lut_memo_set(raw);
    unsigned way=m->next[set];
    for(unsigned w=0;w<KOF_LUT_MEMO_WAYS;++w){
        const kof_lut_memo_entry *e=&m->entries[set][w];
        if(!e->generation||e->slot>=count||generations[e->slot]!=e->generation||
           kof_lut_memo_equal(e->raw,raw)){way=w;break;}
    }
    kof_lut_memo_entry *e=&m->entries[set][way];
    for(unsigned i=1;i<16u;++i)e->raw[i]=raw[i];
    e->slot=slot;e->generation=generations[slot];
    m->next[set]=(uint8_t)(way^1u);
}
#endif
