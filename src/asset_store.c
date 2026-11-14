#include "kof98/asset_store.h"
#include <string.h>
static int published(const kof_asset_store *s) { return s->state==KOF_STORE_READY || s->state==KOF_STORE_COMBAT; }
static uint32_t crc_update(uint32_t crc,const uint8_t *p,uint32_t n) {
    while(n--) { crc^=*p++; for(unsigned i=0;i<8;++i) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u))); }
    return crc;
}
kof_asset_result kof_asset_init(kof_asset_store *s,uint32_t cap,uint32_t source_bytes,kof_asset_read_fn source,void *sc,kof_asset_read_fn read,kof_asset_write_fn write,void *cc) {
    if(!s || !cap || cap>KOF_CART_BYTES || !source_bytes || !source || !read || !write) return KOF_ASSET_INVALID;
    memset(s,0,sizeof(*s)); s->capacity=cap; s->source_bytes=source_bytes; s->source_read=source; s->source_ctx=sc; s->cart_read=read; s->cart_write=write; s->cart_ctx=cc; return KOF_ASSET_OK;
}
kof_asset_result kof_asset_begin(kof_asset_store *s,const kof_asset_desc *d,uint32_t n) {
    if(!s || !s->capacity || !d || !n || n>KOF_ASSET_MAX) return KOF_ASSET_INVALID;
    if(s->state==KOF_STORE_COMBAT || s->state==KOF_STORE_LOADING) return KOF_ASSET_BUSY;
    if(s->generation==UINT32_MAX) return KOF_ASSET_FULL;
    uint32_t end=0;
    for(uint32_t i=0;i<n;++i) {
        if(!d[i].id || !d[i].bytes || d[i].source_offset>s->source_bytes || d[i].bytes>s->source_bytes-d[i].source_offset) return KOF_ASSET_INVALID;
        for(uint32_t j=0;j<i;++j) if(d[i].id==d[j].id) return KOF_ASSET_INVALID;
        uint32_t start=(end+31u)&~31u;
        if(start>s->capacity || d[i].bytes>s->capacity-start) return KOF_ASSET_FULL;
        end=start+d[i].bytes;
    }
    ++s->generation; s->count=n; s->used=end; end=0;
    for(uint32_t i=0;i<n;++i) { end=(end+31u)&~31u; s->records[i].desc=d[i]; s->records[i].offset=end; end+=d[i].bytes; }
    s->current=s->progress=0; s->crc=UINT32_MAX; s->state=KOF_STORE_LOADING; return KOF_ASSET_OK;
}
kof_asset_result kof_asset_step(kof_asset_store *s,void *scratch,uint32_t bytes) {
    if(!s || !scratch || !bytes || bytes>KOF_ASSET_CHUNK_MAX) return KOF_ASSET_INVALID;
    if(s->state!=KOF_STORE_LOADING) return KOF_ASSET_BUSY;
    const kof_asset_record *r=&s->records[s->current]; uint32_t n=r->desc.bytes-s->progress; if(n>bytes)n=bytes;
    if(s->source_read(s->source_ctx,r->desc.source_offset+s->progress,scratch,n) || s->cart_write(s->cart_ctx,r->offset+s->progress,scratch,n)) { s->state=KOF_STORE_FAILED; return KOF_ASSET_IO; }
    s->crc=crc_update(s->crc,scratch,n); s->progress+=n;
    if(s->progress==r->desc.bytes) {
        if((s->crc^UINT32_MAX)!=r->desc.crc32) { s->state=KOF_STORE_FAILED; return KOF_ASSET_CRC; }
        s->progress=0; s->crc=UINT32_MAX;
        if(++s->current==s->count) { s->state=KOF_STORE_READY; return KOF_ASSET_OK; }
    }
    return KOF_ASSET_PENDING;
}
kof_asset_result kof_asset_abort(kof_asset_store *s) { if(!s || s->state!=KOF_STORE_LOADING)return KOF_ASSET_BUSY; s->state=KOF_STORE_FAILED; return KOF_ASSET_OK; }
kof_asset_result kof_asset_enter_combat(kof_asset_store *s) { if(!s || s->state!=KOF_STORE_READY)return KOF_ASSET_BUSY; s->state=KOF_STORE_COMBAT; return KOF_ASSET_OK; }
kof_asset_result kof_asset_leave_combat(kof_asset_store *s) { if(!s || s->state!=KOF_STORE_COMBAT)return KOF_ASSET_BUSY; s->state=KOF_STORE_READY; return KOF_ASSET_OK; }
kof_asset_result kof_asset_find(const kof_asset_store *s,uint32_t id,kof_asset_handle *h) {
    if(!s || !h || !published(s))return KOF_ASSET_MISSING;
    for(uint32_t i=0;i<s->count;++i)if(s->records[i].desc.id==id){ h->generation=s->generation; h->slot=i; return KOF_ASSET_OK; }
    return KOF_ASSET_MISSING;
}
kof_asset_result kof_asset_read(kof_asset_store *s,kof_asset_handle h,uint32_t offset,void *dst,uint32_t bytes) {
    if(!s || !published(s) || h.generation!=s->generation || h.slot>=s->count)return KOF_ASSET_MISSING;
    const kof_asset_record *r=&s->records[h.slot];
    if(offset>r->desc.bytes || bytes>r->desc.bytes-offset || (bytes && !dst))return KOF_ASSET_INVALID;
    if(!bytes)return KOF_ASSET_OK;
    return s->cart_read(s->cart_ctx,r->offset+offset,dst,bytes)?KOF_ASSET_IO:KOF_ASSET_OK;
}
