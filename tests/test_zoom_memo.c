#include "kof98/zoom_memo.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint8_t z[65536];
static kof_zoom_memo memo;
static unsigned oracle(unsigned zy,unsigned tall,unsigned sl){
    unsigned zl=sl&255u,inv=sl>>8;
    if(inv)zl^=255u;
    if(tall){unsigned p=(zy+1u)*2u;zl%=p;if(zl>zy){zl=p-1u-zl;inv^=1u;}}
    unsigned a=z[zy*256u+zl];return inv?(a^511u):a;
}
int main(void){
    uint32_t r=19u;
    for(unsigned i=0;i<65536u;++i){r=r*1664525u+1013904223u;z[i]=(uint8_t)(r>>24);}
    for(unsigned tall=0;tall<2u;++tall)for(unsigned zy=0;zy<256u;++zy){
        const uint16_t *p=kof_zoom_memo_get(&memo,z,zy,tall);
        for(unsigned sl=0;sl<512u;++sl)CHECK(p[sl]==oracle(zy,tall,sl));
        CHECK(kof_zoom_memo_get(&memo,z,zy,tall)==p);
    }
    CHECK(memo.hits==512u&&memo.misses==512u);
    /* Same set, different key: ensure collision cannot return stale rows. */
    for(unsigned zy=0;zy<256u;++zy){
        const uint16_t *p=kof_zoom_memo_get(&memo,z,zy,1);
        for(unsigned sl=0;sl<512u;++sl)CHECK(p[sl]==oracle(zy,1,sl));
    }
    puts("zoom memo: 393216 exact samples, fold, inversion and collisions PASS");
    return 0;
}
