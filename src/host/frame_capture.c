/* Called at the exact sprite-only render boundary of pinned upstream. */
#include "kof98/strip.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static kof_strip strips[224u*96u];
static kof_strip_builder builder;
static kof_line_ir ir;
static uint16_t reference[224][320], backdrop[224];
static long frame=-1, selected=-1;
static const char *out;
static int initialized;
static void fail(const char *m){fprintf(stderr,"capture: %s\n",m);exit(1);}
static void put16(FILE *f,unsigned n){if(fputc((n>>8)&255,f)==EOF||fputc(n&255,f)==EOF)fail("write error");}
static void put32(FILE *f,uint32_t n){put16(f,n>>16);put16(f,n&65535u);}
static void save(void){
    FILE *f=fopen(out,"wb");if(!f)fail("cannot open frame output");
    if(fwrite("KFR1",1,4,f)!=4)fail("write error");
    put32(f,builder.count);
    for(unsigned y=0;y<224;++y)put16(f,backdrop[y]);
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x)put16(f,reference[y][x]);
    for(unsigned slot=0;slot<KOF_STRIP_SLOTS;++slot)
        for(uint16_t i=builder.head[slot];i!=KOF_STRIP_END;i=strips[i].next){
            const kof_strip *s=&strips[i];
            put32(f,s->tile);put16(f,s->x);put16(f,s->y);put16(f,s->mask);
            put16(f,s->height);put16(f,s->width);put16(f,s->flip);
            for(unsigned p=0;p<16;++p)put16(f,s->colors[p]);
            for(unsigned y=0;y<16;++y)if(fputc(s->rows[y],f)==EOF)fail("write error");
        }
    if(fclose(f))fail("close error");
    fprintf(stderr,"capture: frame %ld, %u spans, %u strips -> %s\n",frame,(unsigned)builder.spans,(unsigned)builder.count,out);
}
void kof_capture_line(int line,int width,const uint16_t *vram,unsigned anim,const uint8_t *zoom,const uint16_t *pal,int fix_bios,const uint32_t *argb){
    if(!initialized){
        initialized=1;out=getenv("KOF_CAPTURE_OUT");const char *s=getenv("KOF_CAPTURE_FRAME");
        if(out&&s){char *end;selected=strtol(s,&end,10);if(*end||selected<0)fail("invalid frame index");}
    }
    if(line==0)++frame;
    if(!out||frame!=selected)return;
    if(width!=320||line<0||line>=224)fail("capture requires 320x224");
    if(line==0&&kof_strip_begin(&builder,strips,224u*96u))fail("builder init");
    if(kof_lspc_line(vram,KOF_LSPC_WORDS,zoom,KOF_ZOOM_BYTES,anim,(unsigned)line,fix_bios,&ir))fail("LSPC IR");
    if(kof_strip_push(&builder,&ir,pal,4096))fail("strip builder");
    backdrop[line]=kof_lspc_rgb555(pal[4095]);
    for(unsigned x=0;x<320;++x){
        uint32_t c=argb[x];
        reference[line][x]=(uint16_t)(0x8000u|((c>>19)&31u)|(((c>>11)&31u)<<5)|(((c>>3)&31u)<<10));
    }
    if(line==223){if(!kof_strip_complete(&builder))fail("incomplete frame");save();}
}
