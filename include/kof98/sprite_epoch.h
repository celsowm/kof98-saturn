#ifndef KOF98_SPRITE_EPOCH_H
#define KOF98_SPRITE_EPOCH_H
#include <stddef.h>
#include "kof98/lspc_ir.h"
#define KOF_SPRITE_SCB1_WORDS (381u*64u)
/* Control writes are always boundaries, matching control_until. SCB1
 * and palette no-op writes can be skipped: prior skipped writes to these
 * words cannot have changed their values. FIX and unused VRAM cannot
 * affect sprite decoding. Animation is sampled independently of BIOS. */
static inline unsigned kof_sprite_trace_until(const uint8_t *p,const uint8_t *end,
    unsigned line,unsigned limit,unsigned anim,const uint16_t *state){
    while(line<limit){
        if((size_t)(end-p)<6u)return line;
        unsigned next_anim=((unsigned)p[0]<<8)|p[1];
        unsigned bios=((unsigned)p[2]<<8)|p[3];
        unsigned n=((unsigned)p[4]<<8)|p[5];p+=6;
        if(next_anim!=anim||bios>1u||(size_t)(end-p)<n*4u)return line;
        unsigned last=0;
        for(unsigned j=0;j<n;++j,p+=4){
            unsigned idx=((unsigned)p[0]<<8)|p[1];
            unsigned value=((unsigned)p[2]<<8)|p[3];
            if(idx>=KOF_LSPC_WORDS+4096u||(j&&idx<=last))return line;
            last=idx;
            if(idx>=0x8000u&&idx<KOF_LSPC_WORDS)return line;
            if((idx<KOF_SPRITE_SCB1_WORDS||idx>=KOF_LSPC_WORDS)&&state[idx]!=value)return line;
        }
        ++line;
    }
    return line;
}
#endif
