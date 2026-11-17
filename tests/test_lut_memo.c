#include "kof98/lut_memo.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static kof_lut_memo memo;
int main(void){
    uint32_t gen[4]={1,1,1,1};
    uint16_t a[16]={0},b[16]={0},c[16]={0},alias[16]={0};
    a[1]=0x1245;memcpy(b,a,sizeof a);memcpy(c,a,sizeof a);
    b[2]=0x3421;c[4]=0x8012; /* unsampled words: force exact-key collisions */
    CHECK(kof_lut_memo_set(a)==kof_lut_memo_set(b));
    CHECK(kof_lut_memo_set(a)==kof_lut_memo_set(c));
    CHECK(kof_lut_memo_find(&memo,a,gen,4)==KOF_LUT_MEMO_NONE);
    kof_lut_memo_publish(&memo,a,0,gen,4);
    kof_lut_memo_publish(&memo,b,1,gen,4);
    CHECK(kof_lut_memo_find(&memo,a,gen,4)==0);
    CHECK(kof_lut_memo_find(&memo,b,gen,4)==1);
    CHECK(kof_lut_memo_find(&memo,c,gen,4)==KOF_LUT_MEMO_NONE);
    memcpy(alias,a,sizeof a);alias[0]=0xffff;
    CHECK(kof_lut_memo_find(&memo,alias,gen,4)==0);
    CHECK(kof_lut_memo_find(&memo,a,gen,4)==0); /* make b LRU */
    kof_lut_memo_publish(&memo,c,2,gen,4);
    CHECK(kof_lut_memo_find(&memo,b,gen,4)==KOF_LUT_MEMO_NONE);
    CHECK(kof_lut_memo_find(&memo,c,gen,4)==2);
    ++gen[0]; /* authoritative slot now contains another palette */
    CHECK(kof_lut_memo_find(&memo,a,gen,4)==KOF_LUT_MEMO_NONE);
    CHECK(kof_lut_memo_find(&memo,c,gen,4)==2);
    kof_lut_memo_publish(&memo,b,0,gen,4);
    CHECK(kof_lut_memo_find(&memo,b,gen,4)==0);
    CHECK(kof_lut_memo_find(&memo,a,gen,4)==KOF_LUT_MEMO_NONE);
    /* Different raw keys may point at the same converted LUT. */
    kof_lut_memo_publish(&memo,a,0,gen,4);
    CHECK(kof_lut_memo_find(&memo,a,gen,4)==0);
    CHECK(kof_lut_memo_find(&memo,b,gen,4)==0);
    ++gen[0];
    CHECK(kof_lut_memo_find(&memo,a,gen,4)==KOF_LUT_MEMO_NONE);
    CHECK(kof_lut_memo_find(&memo,b,gen,4)==KOF_LUT_MEMO_NONE);
    puts("LUT memo: exact raw keys, collision, LRU, alias and generation invalidation PASS");
    return 0;
}
