#ifndef KOF98_ASSET_STORE_H
#define KOF98_ASSET_STORE_H
#include <stddef.h>
#include <stdint.h>
#define KOF_CART_BYTES (4u*1024u*1024u)
#define KOF_ASSET_MAX 64u
#define KOF_ASSET_CHUNK_MAX (128u*1024u)
/* All callbacks are synchronous, exact-length, non-reentrant; 0 = success. */
typedef int (*kof_asset_read_fn)(void*,uint32_t,void*,uint32_t);
typedef int (*kof_asset_write_fn)(void*,uint32_t,const void*,uint32_t);
typedef enum { KOF_ASSET_OK, KOF_ASSET_PENDING, KOF_ASSET_INVALID, KOF_ASSET_BUSY, KOF_ASSET_FULL, KOF_ASSET_MISSING, KOF_ASSET_IO, KOF_ASSET_CRC } kof_asset_result;
typedef enum { KOF_STORE_EMPTY, KOF_STORE_LOADING, KOF_STORE_READY, KOF_STORE_COMBAT, KOF_STORE_FAILED } kof_store_state;
typedef struct { uint32_t id, source_offset, bytes, crc32; } kof_asset_desc;
typedef struct { uint32_t generation, slot; } kof_asset_handle;
typedef struct { kof_asset_desc desc; uint32_t offset; } kof_asset_record;
typedef struct {
    kof_asset_read_fn source_read, cart_read;
    kof_asset_write_fn cart_write;
    void *source_ctx, *cart_ctx;
    uint32_t capacity, source_bytes, generation, count, used, current, progress, crc;
    kof_store_state state;
    kof_asset_record records[KOF_ASSET_MAX];
} kof_asset_store;
/* One owner (master SH-2), one initialization per arena lifetime. No raw pointers escape. */
kof_asset_result kof_asset_init(kof_asset_store*, uint32_t capacity, uint32_t source_bytes, kof_asset_read_fn, void*, kof_asset_read_fn, kof_asset_write_fn, void*);
/* Preflight is nondestructive; an accepted plan invalidates the previous phase.
 * A failed transfer publishes NOTHING. Retrying reloads the entire phase. */
kof_asset_result kof_asset_begin(kof_asset_store*,const kof_asset_desc*,uint32_t);
kof_asset_result kof_asset_step(kof_asset_store*,void* scratch,uint32_t bytes);
/* CD-oriented variant: split a partial leading sector into its own read,
 * then issue aligned whole-sector batches and one final short tail.
 * Scratch must be at least bytes long; bytes must be a multiple of 2048. */
kof_asset_result kof_asset_step_cd(kof_asset_store*,void* scratch,uint32_t bytes);
kof_asset_result kof_asset_abort(kof_asset_store*);
kof_asset_result kof_asset_enter_combat(kof_asset_store*);
/* Caller must drain VDP1/DMA/slave consumers before ending combat. */
kof_asset_result kof_asset_leave_combat(kof_asset_store*);
kof_asset_result kof_asset_find(const kof_asset_store*,uint32_t id,kof_asset_handle*);
kof_asset_result kof_asset_read(kof_asset_store*,kof_asset_handle,uint32_t offset,void*,uint32_t bytes);
#endif
