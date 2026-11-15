#include "kof98/sprite_epoch.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint16_t state[KOF_LSPC_WORDS+4096u];
static unsigned encode(uint8_t *p,unsigned anim,unsigned bios,unsigned n,
    const uint16_t *idx,const uint16_t *val){
    p[0]=0;p[1]=(uint8_t)anim;p[2]=0;p[3]=(uint8_t)bios;p[4]=(uint8_t)(n>>8);p[5]=(uint8_t)n;
    for(unsigned j=0;j<n;++j){
        p[6+4*j]=(uint8_t)(idx[j]>>8);p[7+4*j]=(uint8_t)idx[j];
        p[8+4*j]=(uint8_t)(val[j]>>8);p[9+4*j]=(uint8_t)val[j];
    }
    return 6+4*n;
}
int main(void){
    uint8_t p[128];
    uint16_t idx[]={0x7000u},val[]={123u};
    unsigned end=encode(p,3,1,1,idx,val);
    idx[0]=0;val[0]=0;end+=encode(p+end,3,0,1,idx,val);
    idx[0]=KOF_LSPC_WORDS;end+=encode(p+end,3,0,1,idx,val);
    idx[0]=KOF_LSPC_WORDS;val[0]=1;end+=encode(p+end,3,0,1,idx,val);
    CHECK(kof_sprite_trace_until(p,p+end,10,224,3,state)==13);
    CHECK(kof_sprite_trace_until(p,p+end,10,12,3,state)==12);
    CHECK(kof_sprite_trace_until(p,p+end,10,224,2,state)==10);
    CHECK(kof_sprite_trace_until(p,p+5,10,224,3,state)==10);
    CHECK(kof_sprite_trace_until(p,p+7,10,224,3,state)==10);
    idx[0]=0x8000u;val[0]=0;end=encode(p,3,0,1,idx,val);
    CHECK(kof_sprite_trace_until(p,p+end,10,224,3,state)==10);
    idx[0]=KOF_SPRITE_SCB1_WORDS-1u;val[0]=1;end=encode(p,3,0,1,idx,val);
    CHECK(kof_sprite_trace_until(p,p+end,10,224,3,state)==10);
    idx[0]=KOF_SPRITE_SCB1_WORDS;end=encode(p,3,0,1,idx,val);
    CHECK(kof_sprite_trace_until(p,p+end,10,11,3,state)==11);
    idx[0]=KOF_LSPC_WORDS+4096u;end=encode(p,3,0,1,idx,val);
    CHECK(kof_sprite_trace_until(p,p+end,10,11,3,state)==10);
    puts("sprite epoch: independent FIX, no-op, control, animation and malformed boundaries PASS");
    return 0;
}
