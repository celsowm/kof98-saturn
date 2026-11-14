/* Live raster-state replay: no precomputed draw commands, no CD after boot.
 * LSPC and strip coalescing execute on Master SH-2. This diagnostic draws
 * FIX through VDP1 too, independently of the optimized resident control. */
#include "kof98/strip.h"
#include "../bytes.h"
#define LIVE_STATE_WORDS (KOF_LSPC_WORDS+4096u)
#define LIVE_PAT_SLOTS 2048u
#define LIVE_LUT_SLOTS 128u
#define LIVE_ATLAS (LIVE_PAT_SLOTS*128u+LIVE_LUT_SLOTS*32u)
#define LIVE_BUCKETS 4096u
#define NONE UINT16_MAX
static _Alignas(32) uint16_t live_state[LIVE_STATE_WORDS];
static _Alignas(32) uint8_t zoom[KOF_ZOOM_BYTES];
static _Alignas(32) uint32_t tile_ids[LIVE_TILE_COUNT];
static kof_strip sprite_strips[LIVE_MAX_SPRITES],fix_strips[LIVE_MAX_FIX];
static kof_strip_builder sprites,fix;
static kof_line_ir ir,fix_ir;
static kof_asset_handle asset;
static uint32_t atlas_base,stamp;
volatile uint32_t kof_sequence_live_verified,kof_sequence_live_hash_errors;
volatile uint32_t kof_sequence_live_cache_hits,kof_sequence_live_cache_misses;
volatile uint32_t kof_sequence_live_lut_hits,kof_sequence_live_lut_misses;
volatile uint32_t kof_sequence_live_trace_fields,kof_sequence_live_lspc_fields,kof_sequence_live_draw_fields;
volatile uint32_t kof_sequence_live_frame_count;
typedef struct {uint32_t tile;uint16_t mask;uint8_t h,flip,rows[16];} pattern_key;
typedef struct {pattern_key key;uint32_t stamp;uint16_t next;uint8_t valid,opaque;} pattern_entry;
typedef struct {uint16_t colors[16];uint32_t stamp;uint8_t valid;} lut_entry;
_Static_assert(sizeof(pattern_key)==24u,"pattern key layout");
static pattern_entry patterns[LIVE_PAT_SLOTS];
static lut_entry luts[LIVE_LUT_SLOTS];
static uint16_t buckets[LIVE_BUCKETS],victim,lut_victim;

static void read_live(uint32_t off,void *dst,uint32_t n){
    check(kof_asset_read(&assets.store,asset,off,dst,n)==KOF_ASSET_OK);
}
static uint32_t take(const uint8_t **p,const uint8_t *end){
    check((size_t)(end-*p)>=2u);
    uint32_t n=((uint32_t)(*p)[0]<<8)|(*p)[1];*p+=2;return n;
}
static uint32_t hash_byte(uint32_t h,unsigned v){return (h^(v&255u))*16777619u;}
static uint32_t hash_word(uint32_t h,unsigned v){return hash_byte(hash_byte(h,v>>8),v);}
static uint32_t strip_hash(uint32_t h,const kof_strip_builder *b,unsigned bias){
    for(unsigned slot=0;slot<KOF_STRIP_SLOTS;++slot)
      for(uint16_t i=b->head[slot];i!=KOF_STRIP_END;i=b->strips[i].next){
        const kof_strip *s=&b->strips[i];uint32_t tile=s->tile+bias;
        h=hash_word(hash_word(h,tile>>16),tile);
        h=hash_word(h,s->x);h=hash_word(h,s->y);h=hash_word(h,s->mask);
        h=hash_word(h,s->height);h=hash_word(h,s->width);h=hash_word(h,s->flip);
        for(unsigned j=0;j<16u;++j)h=hash_word(h,s->colors[j]);
        for(unsigned j=0;j<16u;++j)h=hash_byte(h,s->rows[j]);
      }
    return h;
}
static void build_strips(const seq_live_frame *f){
    uint32_t start=sat_field_count();
    read_live(f->offset,scratch,f->bytes);
    kof_sequence_live_trace_fields+=sat_field_count()-start;
    start=sat_field_count();
    const uint8_t *p=scratch,*end=scratch+f->bytes;
    check(!kof_strip_begin(&sprites,sprite_strips,LIVE_MAX_SPRITES));
    check(!kof_strip_begin(&fix,fix_strips,LIVE_MAX_FIX));
    for(unsigned y=0;y<224u;++y){
        unsigned anim=take(&p,end),bios=take(&p,end),n=take(&p,end);
        check(anim<8u&&bios<2u&&n<=LIVE_STATE_WORDS);
        unsigned last=0;
        for(unsigned j=0;j<n;++j){
            unsigned idx=take(&p,end),val=take(&p,end);
            check(idx<LIVE_STATE_WORDS&&(!j||idx>last));last=idx;
            live_state[idx]=(uint16_t)val;
        }
        const uint16_t *pal=live_state+KOF_LSPC_WORDS;
        check(kof_lspc_rgb555(pal[4095])==f->bg);
        check(!kof_lspc_line(live_state,KOF_LSPC_WORDS,zoom,sizeof zoom,anim,y,(int)bios,&ir));
        check(!kof_strip_push(&sprites,&ir,pal,4096u));
        fix_ir.count=40;
        for(unsigned col=0;col<40u;++col){
            const kof_fix_span *q=&ir.fix[col];
            fix_ir.sprites[col]=(kof_sprite_span){.tile=(uint32_t)q->tile+(ir.fix_bios?4096u:0u),
                .mask=255u,.x=(uint16_t)(col*8u),.slot=(uint16_t)col,.row=q->row,
                .palette=q->palette,.width=8u};
        }
        check(!kof_strip_push(&fix,&fix_ir,pal,4096u));
    }
    check(p==end&&kof_strip_complete(&sprites)&&kof_strip_complete(&fix));
    const uint32_t h=strip_hash(strip_hash(2166136261u,&sprites,0),&fix,524288u);
    if(h!=f->hash||sprites.count!=f->sprites||fix.count!=f->fix){
        ++kof_sequence_live_hash_errors;check(0);
    }
    ++kof_sequence_live_verified;
    kof_sequence_live_lspc_fields+=sat_field_count()-start;
}
static unsigned key_bucket(const pattern_key *key){
    uint32_t h=2166136261u;
    const uint8_t *p=(const uint8_t *)(const void*)key;
    for(unsigned j=0;j<sizeof *key;++j)h=hash_byte(h,p[j]);
    return h&(LIVE_BUCKETS-1u);
}
static void raw_tile(uint32_t id,uint8_t out[128]){
    unsigned lo=0,hi=LIVE_TILE_COUNT;
    while(lo<hi){unsigned m=(lo+hi)/2u;if(tile_ids[m]<id)lo=m+1u;else hi=m;}
    check(lo<LIVE_TILE_COUNT&&tile_ids[lo]==id);
    read_live(LIVE_TILES_OFFSET+lo*128u,out,128u);
}
static void upload_live(uint32_t off,const void *src,uint32_t bytes){
    SAT_PANIC_IF_ERROR(sat_vdp1_vram_write(atlas_base+off,src,bytes));
    kof_sequence_upload_bytes+=bytes;++kof_sequence_upload_runs;
}
static unsigned pattern_get(const kof_strip *s,unsigned bias,uint16_t *width,uint16_t *flags){
    uint8_t h=0,v=0;
    const int native=kof_strip_native(s,&h,&v);
    pattern_key key={0};
    key.tile=s->tile+bias;key.mask=s->mask;key.h=s->height;
    key.flip=native?0:s->flip;
    for(unsigned j=0;j<s->height;++j)key.rows[j]=native?(uint8_t)j:s->rows[j];
    *width=(s->width+7u)&~7u;
    *flags=native?(uint16_t)((h?0x20u:0u)|(v?0x40u:0u)):0u;
    unsigned bucket=key_bucket(&key);
    for(uint16_t i=buckets[bucket];i!=NONE;i=patterns[i].next){
        if(!kof_memcmp(&patterns[i].key,&key,sizeof key)){
            ++kof_sequence_live_cache_hits;patterns[i].stamp=stamp;return i;
        }
    }
    ++kof_sequence_live_cache_misses;
    unsigned i=LIVE_PAT_SLOTS;
    for(unsigned j=0;j<LIVE_PAT_SLOTS;++j){
        unsigned k=(victim+j)&(LIVE_PAT_SLOTS-1u);
        if(!patterns[k].valid||patterns[k].stamp!=stamp){i=k;break;}
    }
    check(i<LIVE_PAT_SLOTS);
    victim=(uint16_t)((i+1u)&(LIVE_PAT_SLOTS-1u));
    pattern_entry *e=&patterns[i];
    if(e->valid){
        uint16_t *link=&buckets[key_bucket(&e->key)];
        while(*link!=i){check(*link!=NONE);link=&patterns[*link].next;}
        *link=e->next;
    }
    _Alignas(4) uint8_t raw[128],converted[128];
    raw_tile(key.tile,raw);
    const uint8_t *pixels=raw;
    if(!native){check(!kof_strip_pixels(s,raw,converted,width));pixels=converted;}
    unsigned opaque=0;
    for(unsigned j=0;j<128u;++j)opaque|=pixels[j];
    if(opaque)upload_live(i*128u,pixels,128u);
    e->key=key;e->stamp=stamp;e->valid=1;e->opaque=opaque!=0;
    e->next=buckets[bucket];buckets[bucket]=(uint16_t)i;
    return i;
}
static unsigned lut_get(const kof_strip *s){
    _Alignas(4) uint16_t colors[16]={0};
    for(unsigned p=1;p<16u;++p)colors[p]=kof_lspc_rgb555(s->colors[p]);
    for(unsigned i=0;i<LIVE_LUT_SLOTS;++i)
        if(luts[i].valid&&!kof_memcmp(luts[i].colors,colors,32u)){
            ++kof_sequence_live_lut_hits;luts[i].stamp=stamp;return i;
        }
    ++kof_sequence_live_lut_misses;
    unsigned i=LIVE_LUT_SLOTS;
    for(unsigned j=0;j<LIVE_LUT_SLOTS;++j){
        unsigned k=(lut_victim+j)&(LIVE_LUT_SLOTS-1u);
        if(!luts[k].valid||luts[k].stamp!=stamp){i=k;break;}
    }
    check(i<LIVE_LUT_SLOTS);lut_victim=(uint16_t)((i+1u)&(LIVE_LUT_SLOTS-1u));
    upload_live(LIVE_PAT_SLOTS*128u+i*32u,colors,32u);
    memcpy(luts[i].colors,colors,32u);luts[i].valid=1;luts[i].stamp=stamp;
    return i;
}
static unsigned emit_live(sat_sprite_batch_window_t *win,const kof_strip_builder *b,unsigned bias,
                          const sat_sprite_frame_t prefixes[4]){
    unsigned count=0;
    for(unsigned slot=0;slot<KOF_STRIP_SLOTS;++slot)
      for(uint16_t i=b->head[slot];i!=KOF_STRIP_END;i=b->strips[i].next){
        const kof_strip *s=&b->strips[i];uint16_t width,flags;
        check(kof_strip_valid(s));
        unsigned t=pattern_get(s,bias,&width,&flags);
        if(!patterns[t].opaque)continue;
        unsigned l=lut_get(s);
        sat_sprite_frame_t sprite=prefixes[flags>>5];
        sprite.srca=(uint16_t)(atlas_base/8u+t*16u);
        sprite.colr=(uint16_t)(atlas_base/8u+LIVE_PAT_SLOTS*16u+l*4u);
        sprite.size=(uint16_t)(((width/8u)<<8)|s->height);
        check(sat_sprite_batch_emit(win,&sprite,s->x,s->y)==1);
        ++count;
      }
    return count;
}
int main(void){
    const sat_video_config_t cfg={320,224,SAT_VIDEO_NTSC,SAT_VIDEO_INTERLACE_NONE};
    kof_sequence_stage=1;
    SAT_PANIC_IF_ERROR(sat_init(&cfg));
    SAT_PANIC_IF_ERROR(sat_vdp1_set_frame_mode(SAT_VDP1_FRAME_ON_SUBMIT));
    kof_loading_init(&loading);kof_loading_phase(&loading,"INITIALIZING CD",1,1);
    SAT_PANIC_IF_ERROR(sat_cd_mount_default(&block,0,0,&device,0,&volume));
    SAT_PANIC_IF_ERROR(kof_saturn_assets_open(&assets,&volume,"SEQUENCE.BIN",KOF_CART_BYTES));
    kof_sequence_vram_free=sat_vdp1_vram_free_bytes();
    kof_sequence_vram_requested=LIVE_ATLAS+24u;
    SAT_PANIC_IF_ERROR(sat_vdp1_vram_reserve(kof_sequence_vram_requested,&atlas_base));
    atlas_base=(atlas_base+31u)&~31u;
    const kof_asset_desc desc={1,0,LIVE_BYTES,LIVE_CRC};
    kof_sequence_stage=2;kof_loading_phase(&loading,"LOADING LSPC TRACE",1,1);
    check(kof_asset_begin(&assets.store,&desc,1)==KOF_ASSET_OK);
    ++kof_sequence_cd_loads;
    kof_asset_result r;
    do{
        r=kof_asset_step_cd(&assets.store,scratch,sizeof scratch);
        if(r==KOF_ASSET_PENDING||r==KOF_ASSET_OK)
            kof_loading_update(&loading,r==KOF_ASSET_OK?desc.bytes:assets.store.progress,desc.bytes,0);
    }while(r==KOF_ASSET_PENDING);
    check(r==KOF_ASSET_OK);
    check(kof_asset_find(&assets.store,1,&asset)==KOF_ASSET_OK);
    check(kof_asset_enter_combat(&assets.store)==KOF_ASSET_OK);
    read_live(LIVE_ZOOM_OFFSET,zoom,sizeof zoom);
    read_live(LIVE_INDEX_OFFSET,tile_ids,sizeof tile_ids);
    memset(buckets,0xff,sizeof buckets);
    sat_sprite_frame_t prefixes[4];
    for(unsigned i=0;i<4u;++i)
        SAT_PANIC_IF_ERROR(sat_sprite_frame_init_raw(&prefixes[i],(uint16_t)(atlas_base/8u),
          8u,1u,SAT_VDP1_TEXTURE_LUT4,(uint16_t)(atlas_base/8u),(uint16_t)(i<<5),0,0));
    kof_sequence_live_frame_count=sizeof seq_live_frames/sizeof seq_live_frames[0];
    for(;;){
        read_live(0,live_state,sizeof live_state);
        for(unsigned i=0;i<kof_sequence_live_frame_count;++i){
            const seq_live_frame *f=&seq_live_frames[i];
            check(f->bytes<=sizeof scratch);
            kof_sequence_stage=5;
            build_strips(f);
            uint32_t start=sat_field_count();
            if(++stamp==0){for(unsigned j=0;j<LIVE_PAT_SLOTS;++j)patterns[j].stamp=0;
                for(unsigned j=0;j<LIVE_LUT_SLOTS;++j)luts[j].stamp=0;stamp=1;}
            SAT_PANIC_IF_ERROR(sat_set_clear_color(f->bg));
            SAT_PANIC_IF_ERROR(sat_begin_frame());
            sat_sprite_batch_window_t win;
            SAT_PANIC_IF_ERROR(sat_sprite_batch_begin(&win,sprites.count+fix.count,
                SAT_SPRITE_BATCH_KEEP_PRECLIP|SAT_SPRITE_BATCH_NO_CULL));
            unsigned count=emit_live(&win,&sprites,0,prefixes);
            count+=emit_live(&win,&fix,524288u,prefixes);
            sat_sprite_batch_stats_t stats;
            SAT_PANIC_IF_ERROR(sat_sprite_batch_end(&win,&stats));
            check(count==stats.submitted&&!stats.culled&&!stats.dropped&&!stats.invalid);
            kof_sequence_live_draw_fields+=sat_field_count()-start;
            kof_sequence_stage=4;
            SAT_PANIC_IF_ERROR(sat_end_frame());
            SAT_PANIC_IF_ERROR(sat_wait_vblank());
            kof_sequence_index=i;kof_sequence_commands=count;++kof_sequence_frames;
        }
    }
}
