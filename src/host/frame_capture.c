/* Called after the complete 320x224 reference line, including FIX. */
#include "kof98/strip.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static kof_strip strips[224u*96u];
static kof_strip_builder builder, fix_builder;
static kof_strip fix_strips[224u*40u];
static kof_line_ir fix_ir;
static kof_line_ir ir;
static uint16_t reference[224][320], backdrop[224];
static long frame=-1, selected=-1, frames=1;
static const char *out;
static int initialized;
static const char *state_dir;
static FILE *state_file;
static uint16_t state_prev[KOF_LSPC_WORDS+4096u];
static void fail(const char *m){fprintf(stderr,"capture: %s\n",m);exit(1);}
static void put16(FILE *f,unsigned n){if(fputc((n>>8)&255,f)==EOF||fputc(n&255,f)==EOF)fail("write error");}
static void put32(FILE *f,uint32_t n){put16(f,n>>16);put16(f,n&65535u);}
/* Optional raw raster-state trace. Each frame begins with the exact
 * visible-line-zero state; later lines contain changed guest words. */
static void save_state(unsigned line,const uint16_t *vram,const uint16_t *pal,unsigned anim,int bios){
    if(!state_dir)return;
    if(!line){
        char path[4096];
        int n=snprintf(path,sizeof path,"%s/frame-%08ld.kst",state_dir,frame);
        if(n<0||(size_t)n>=sizeof path)fail("state path too long");
        state_file=fopen(path,"wb");if(!state_file)fail("cannot open state trace");
        if(fwrite("KST1",1,4,state_file)!=4)fail("state magic");
        memcpy(state_prev,vram,KOF_LSPC_WORDS*2u);
        memcpy(state_prev+KOF_LSPC_WORDS,pal,8192u);
        for(unsigned i=0;i<KOF_LSPC_WORDS+4096u;++i)put16(state_file,state_prev[i]);
    }
    unsigned changed=0;
    for(unsigned i=0;i<KOF_LSPC_WORDS+4096u;++i){
        uint16_t v=i<KOF_LSPC_WORDS?vram[i]:pal[i-KOF_LSPC_WORDS];
        changed+=v!=state_prev[i];
    }
    put16(state_file,anim&7u);put16(state_file,(unsigned)bios);put16(state_file,changed);
    for(unsigned i=0;i<KOF_LSPC_WORDS+4096u;++i){
        uint16_t v=i<KOF_LSPC_WORDS?vram[i]:pal[i-KOF_LSPC_WORDS];
        if(v!=state_prev[i]){put16(state_file,i);put16(state_file,v);state_prev[i]=v;}
    }
    if(line==223){if(fclose(state_file))fail("state close");state_file=NULL;}
}
static void save(void){
    char path[4096];
    const char *dest=out;
    if(frames>1){int n=snprintf(path,sizeof path,"%s/frame-%08ld.kfr",out,frame);if(n<0||(size_t)n>=sizeof path)fail("path too long");dest=path;}
    FILE *f=fopen(dest,"wb");if(!f)fail("cannot open frame output");
    if(fwrite("KFR2",1,4,f)!=4)fail("write error");
    put32(f,(uint32_t)builder.count+fix_builder.count);
    for(unsigned y=0;y<224;++y)put16(f,backdrop[y]);
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x)put16(f,reference[y][x]);
    for(unsigned layer=0;layer<2;++layer){
      const kof_strip_builder *b=layer?&fix_builder:&builder;
      for(unsigned slot=0;slot<KOF_STRIP_SLOTS;++slot)
        for(uint16_t i=b->head[slot];i!=KOF_STRIP_END;i=b->strips[i].next){
            const kof_strip *s=&b->strips[i];
            put32(f,s->tile+(layer?524288u:0u));put16(f,s->x);put16(f,s->y);put16(f,s->mask);
            put16(f,s->height);put16(f,s->width);put16(f,s->flip);
            for(unsigned p=0;p<16;++p)put16(f,s->colors[p]);
            for(unsigned y=0;y<16;++y)if(fputc(s->rows[y],f)==EOF)fail("write error");
        }
    }
    if(fclose(f))fail("close error");
    fprintf(stderr,"capture: frame %ld, %u sprite strips, %u FIX strips -> %s\n",frame,(unsigned)builder.count,(unsigned)fix_builder.count,dest);
}
void kof_capture_line(int line,int width,const uint16_t *vram,unsigned anim,const uint8_t *zoom,const uint16_t *pal,int fix_bios,const uint32_t *argb){
    if(!initialized){
        initialized=1;state_dir=getenv("KOF_CAPTURE_STATE_DIR");out=getenv("KOF_CAPTURE_OUT");const char *s=getenv("KOF_CAPTURE_FRAME");
        if(out&&s){char *end;selected=strtol(s,&end,10);if(*end||selected<0)fail("invalid frame index");}
        const char *num=getenv("KOF_CAPTURE_COUNT");if(num){char *end;frames=strtol(num,&end,10);if(*end||frames<1||frames>600)fail("invalid frame count (1..600)");}
    }
    if(line==0)++frame;
    if(!out||selected<0||frame<selected||frame-selected>=frames)return;
    if(width!=320||line<0||line>=224)fail("capture requires 320x224");
    if(line==0&&(kof_strip_begin(&builder,strips,224u*96u)||kof_strip_begin(&fix_builder,fix_strips,224u*40u)))fail("builder init");
    save_state((unsigned)line,vram,pal,anim,fix_bios);
    if(kof_lspc_line(vram,KOF_LSPC_WORDS,zoom,KOF_ZOOM_BYTES,anim,(unsigned)line,fix_bios,&ir))fail("LSPC IR");
    if(kof_strip_push(&builder,&ir,pal,4096))fail("strip builder");
    fix_ir.count=40;
    for(unsigned c=0;c<40;++c){const kof_fix_span *p=&ir.fix[c];fix_ir.sprites[c]=(kof_sprite_span){.tile=(uint32_t)p->tile+(ir.fix_bios?4096u:0u),.mask=255,.x=(uint16_t)(c*8u),.slot=(uint16_t)c,.row=p->row,.palette=p->palette,.width=8};}
    if(kof_strip_push(&fix_builder,&fix_ir,pal,4096))fail("FIX builder");
    backdrop[line]=kof_lspc_rgb555(pal[4095]);
    for(unsigned x=0;x<320;++x){
        uint32_t c=argb[x];
        reference[line][x]=(uint16_t)(0x8000u|((c>>19)&31u)|(((c>>11)&31u)<<5)|(((c>>3)&31u)<<10));
    }
    if(line==223){if(!kof_strip_complete(&builder)||!kof_strip_complete(&fix_builder))fail("incomplete frame");save();}
}
