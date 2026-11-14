#include "kof98/paged_dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
#define N 57972u
static kof_dispatch_entry entries[N];
static uint16_t dirs[KOF_DISPATCH_PAGES+1u], offsets[N];
static kof_dispatch_fn funcs[N];
static void a(void) {}
static void b(void) {}
static uint32_t key_for_word(unsigned word)
{
    unsigned page = word / 2048u, byte = 2u * (word % 2048u);
    if (page < 256u) return page*4096u + byte;
    if (page < 512u) return 0x200000u + (page-256u)*4096u + byte;
    if (page < 544u) return 0xc00000u + (page-512u)*4096u + byte;
    unsigned n = page-544u;
    return ((1u+n/256u)<<24) | (0x200000u + (n%256u)*4096u + byte);
}
int main(void)
{
    const unsigned words = KOF_DISPATCH_PAGES*2048u;
    for (unsigned i=0; i<N; ++i) {
        entries[i].key = key_for_word((unsigned)((uint64_t)i*words/N));
        entries[i].fn = i&1u ? a : b;
    }
    CHECK(kof_paged_build(entries,N,dirs,offsets,funcs,N));
    const kof_paged_dispatch t = {dirs,offsets,funcs,N};
    CHECK(kof_paged_validate(&t));
    unsigned hits=0;
    for (unsigned w=0; w<words; ++w) {
        uint32_t key=key_for_word(w);
        unsigned bank=key>>24;
        kof_dispatch_fn ref=kof_dispatch_lookup(entries,N,key,bank);
        CHECK(kof_paged_lookup(&t,key,bank)==ref);
        hits += ref != NULL;
    }
    CHECK(hits==N);
    CHECK(kof_paged_lookup(&t,0x100000u,0)==NULL);
    CHECK(kof_paged_lookup(&t,0x200000u,4)==NULL);
    CHECK(kof_paged_lookup(&t,0xc20000u,3)==kof_paged_lookup(&t,0xc00000u,0));
    CHECK(kof_paged_lookup(&t,1u,3)==kof_paged_lookup(&t,0u,0));
    CHECK(!kof_paged_build(entries,N,dirs,offsets,funcs,N-1));
    CHECK(!kof_paged_build(entries,65536u,dirs,offsets,funcs,65536u));
    CHECK(kof_paged_validate(&t));
    uint16_t saved=offsets[0]; offsets[0]=1;
    CHECK(!kof_paged_validate(&t)); offsets[0]=saved;
    uint16_t d=dirs[1]; dirs[1]=UINT16_MAX;
    CHECK(!kof_paged_validate(&t)); dirs[1]=d;
    CHECK(kof_paged_validate(&t));
    const unsigned queries=2000000u;
    unsigned sums[2]={0,0}; double sec[2];
    for (unsigned m=0;m<2;++m) {
        uint32_t rng=1234567u; clock_t start=clock();
        for (unsigned i=0;i<queries;++i) {
            rng=rng*1664525u+1013904223u;
            uint32_t key=key_for_word(rng%words);
            kof_dispatch_fn fn=m?kof_paged_lookup(&t,key,key>>24):kof_dispatch_lookup(entries,N,key,key>>24);
            sums[m] += fn==a ? 1u : fn==b ? 2u : 0u;
        }
        sec[m]=(double)(clock()-start)/CLOCKS_PER_SEC;
    }
    CHECK(sums[0]==sums[1]);
    printf("exhaustive: %u words, %u hits; synthetic host %u queries: sparse %.6fs, paged %.6fs, checksum %u\n",words,hits,queries,sec[0],sec[1],sums[0]);
    return 0;
}
