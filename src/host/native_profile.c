#include "kof98/native_profile.h"
#include "kof98/dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#define CAP 131072u
typedef struct { uint32_t key; uint64_t hits, instructions, cycles; } sample;
static sample *slots;
static const char *output, *identity;
static void fail(void) { fprintf(stderr,"fatal: native profile invalid, overflow or I/O failure\n"); abort(); }
void kof_native_profile_init(const char *path, const char *id)
{
    if (slots) fail();
    if (!path || !*path) return;
    if (!id || strlen(id)!=64 || strspn(id,"0123456789abcdef")!=64) fail();
    slots=calloc(CAP,sizeof(*slots));
    if (!slots) fail();
    output=path; identity=id;
}
int kof_native_profile_enabled(void) { return slots!=NULL; }
void kof_native_profile_record(uint32_t pc, unsigned bank, uint64_t instructions, uint64_t cycles)
{
    if (!slots) return;
    uint32_t key;
    if (!kof_dispatch_key(pc,bank,&key)) fail();
    unsigned h=((key>>1)*UINT32_C(2654435761))&(CAP-1u);
    for (unsigned i=0;i<CAP;++i,h=(h+1u)&(CAP-1u)) {
        sample *s=&slots[h];
        if (!s->hits || s->key==key) {
            if (s->hits==UINT64_MAX || UINT64_MAX-s->instructions<instructions || UINT64_MAX-s->cycles<cycles) fail();
            s->key=key; ++s->hits; s->instructions+=instructions; s->cycles+=cycles; return;
        }
    }
    fail();
}
void kof_native_profile_finish(void)
{
    if (!slots) return;
    FILE *f=fopen(output,"w");
    if (!f) fail();
    if (fprintf(f,"# kof-native-profile-v1 %s\nkey,hits,instructions,cycles\n",identity)<0) fail();
    for (unsigned i=0;i<CAP;++i) {
        const sample *s=&slots[i];
        if (s->hits && fprintf(f,"%08" PRIx32 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",s->key,s->hits,s->instructions,s->cycles)<0) fail();
    }
    if (fclose(f)) fail();
    free(slots); slots=NULL;
}
