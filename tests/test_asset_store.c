#include "kof98/asset_store.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define BANK (KOF_CART_BYTES/2u)
static uint8_t bank[2][BANK], data[KOF_CART_BYTES], scratch[KOF_ASSET_CHUNK_MAX];
static unsigned source_calls, fail_source, fail_write, check_cd, bad_cd;
static uint32_t source_end;
static int source(void *ctx,uint32_t off,void *dst,uint32_t n){(void)ctx;++source_calls;if(check_cd && ((off%2048u && n>2048u-off%2048u) || (off+n<source_end && (off+n)%2048u)))++bad_cd;if(fail_source)return -1;memcpy(dst,data+off,n);return 0;}
static int read_cart(void *ctx,uint32_t off,void *dst,uint32_t n){(void)ctx;uint8_t *p=dst;while(n--){*p++=bank[off/BANK][off%BANK];++off;}return 0;}
static int write_cart(void *ctx,uint32_t off,const void *src,uint32_t n){(void)ctx;if(fail_write)return -1;const uint8_t *p=src;while(n--){bank[off/BANK][off%BANK]=*p++;++off;}return 0;}
static uint32_t crc(const uint8_t *p,uint32_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static kof_asset_result finish(kof_asset_store *s){kof_asset_result r;do{r=kof_asset_step(s,scratch,sizeof scratch);}while(r==KOF_ASSET_PENDING);return r;}
int main(void){
 kof_asset_store s; kof_asset_handle h,old; uint8_t out[64];
 for(uint32_t i=0;i<sizeof data;i++)data[i]=(uint8_t)(i^(i>>10)^(i>>20));
 CHECK(crc((const uint8_t*)"123456789",9)==0xcbf43926u);
 CHECK(kof_asset_init(&s,KOF_CART_BYTES,sizeof data,source,NULL,read_cart,write_cart,NULL)==KOF_ASSET_OK);
 kof_asset_desc d[]={{1,0,BANK-32,crc(data,BANK-32)},{2,BANK-32,64,crc(data+BANK-32,64)}};
 CHECK(kof_asset_begin(&s,d,2)==KOF_ASSET_OK); CHECK(kof_asset_find(&s,1,&h)==KOF_ASSET_MISSING); CHECK(kof_asset_enter_combat(&s)==KOF_ASSET_BUSY);
 CHECK(finish(&s)==KOF_ASSET_OK); CHECK(s.used==BANK+32); CHECK(kof_asset_find(&s,2,&h)==KOF_ASSET_OK); old=h;
 CHECK(kof_asset_read(&s,h,0,out,64)==KOF_ASSET_OK && !memcmp(out,data+BANK-32,64));
 CHECK(kof_asset_read(&s,h,UINT32_MAX,out,1)==KOF_ASSET_INVALID); CHECK(kof_asset_read(&s,h,64,NULL,0)==KOF_ASSET_OK);
 CHECK(kof_asset_enter_combat(&s)==KOF_ASSET_OK); unsigned calls=source_calls;
 CHECK(kof_asset_begin(&s,d,2)==KOF_ASSET_BUSY); CHECK(kof_asset_step(&s,scratch,64)==KOF_ASSET_BUSY); CHECK(kof_asset_find(&s,99,&h)==KOF_ASSET_MISSING); CHECK(kof_asset_read(&s,old,0,out,64)==KOF_ASSET_OK && calls==source_calls);
 CHECK(kof_asset_leave_combat(&s)==KOF_ASSET_OK); kof_asset_desc bad[]={{1,0,KOF_CART_BYTES,0},{2,0,1,0}}; CHECK(kof_asset_begin(&s,bad,2)==KOF_ASSET_FULL); CHECK(kof_asset_read(&s,old,0,out,64)==KOF_ASSET_OK);
 bad[0]=d[0];bad[1]=d[0];CHECK(kof_asset_begin(&s,bad,2)==KOF_ASSET_INVALID);bad[0].source_offset=UINT32_MAX;CHECK(kof_asset_begin(&s,bad,1)==KOF_ASSET_INVALID);
 CHECK(kof_asset_begin(&s,d+1,1)==KOF_ASSET_OK); CHECK(kof_asset_read(&s,old,0,out,1)==KOF_ASSET_MISSING); fail_source=1; CHECK(finish(&s)==KOF_ASSET_IO); fail_source=0;
 CHECK(kof_asset_begin(&s,d+1,1)==KOF_ASSET_OK); fail_write=1; CHECK(finish(&s)==KOF_ASSET_IO);fail_write=0;
 bad[0]=d[1];bad[0].crc32^=1; CHECK(kof_asset_begin(&s,bad,1)==KOF_ASSET_OK); CHECK(finish(&s)==KOF_ASSET_CRC); CHECK(kof_asset_find(&s,2,&h)==KOF_ASSET_MISSING);
 CHECK(kof_asset_begin(&s,d+1,1)==KOF_ASSET_OK); CHECK(kof_asset_abort(&s)==KOF_ASSET_OK); CHECK(kof_asset_find(&s,2,&h)==KOF_ASSET_MISSING);
 CHECK(kof_asset_begin(&s,d+1,1)==KOF_ASSET_OK); CHECK(finish(&s)==KOF_ASSET_OK); CHECK(kof_asset_find(&s,2,&h)==KOF_ASSET_OK && h.generation!=old.generation);
 CHECK(kof_asset_read(&s,h,0,out,64)==KOF_ASSET_OK && !memcmp(out,data+BANK-32,64));
 /* Unaligned disc extent: exactly one head, two large batches, one tail. */
 const uint32_t off=32u, length=2u*128u*1024u+2048u;
 kof_asset_desc cd={3,off,length,crc(data+off,length)};
 CHECK(kof_asset_begin(&s,&cd,1)==KOF_ASSET_OK);
 CHECK(kof_asset_step_cd(&s,scratch,2049)==KOF_ASSET_INVALID);
 source_end=off+length;check_cd=1;calls=source_calls;
 kof_asset_result r; do{r=kof_asset_step_cd(&s,scratch,sizeof scratch);}while(r==KOF_ASSET_PENDING);
 check_cd=0;CHECK(r==KOF_ASSET_OK && bad_cd==0 && source_calls-calls==4);
 CHECK(kof_asset_find(&s,3,&h)==KOF_ASSET_OK);
 CHECK(kof_asset_read(&s,h,length-64,out,64)==KOF_ASSET_OK && !memcmp(out,data+off+length-64,64));
 s.generation=UINT32_MAX; CHECK(kof_asset_begin(&s,d+1,1)==KOF_ASSET_FULL);
 printf("asset store: cross-bank, lifecycle, combat lock, bounds, rollback, CRC and retries PASS; metadata %zu bytes\n",sizeof s);return 0;
}
