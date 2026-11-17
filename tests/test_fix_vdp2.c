#include "kof98/fix_vdp2.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static kof_fix_snapshot s;
int main(void){
    uint8_t raw[128],tile[32];
    for(unsigned i=0;i<128u;++i)raw[i]=(uint8_t)i;
    kof_fix_extract(raw,tile);
    for(unsigned y=0;y<8u;++y)for(unsigned x=0;x<4u;++x)CHECK(tile[y*4u+x]==raw[y*8u+x]);
    for(unsigned i=0;i<KOF_FIX_MAP_WORDS;++i)s.map[i]=(uint16_t)i;
    for(unsigned y=0;y<28u;++y)for(unsigned x=0;x<40u;++x)
        CHECK(kof_fix_map_entry(&s,y*40u+x)==x*32u+y+2u);
    CHECK(kof_fix_code(&s,0xa123u)==0x123u);
    s.bios=1;CHECK(kof_fix_code(&s,0xffffu)==8191u);
    puts("FIX VDP2 tile extraction, map coordinates and BIOS bank PASS");return 0;
}
