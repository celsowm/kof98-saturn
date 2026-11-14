#ifndef KOF98_SATURN_STRIP_RENDERER_H
#define KOF98_SATURN_STRIP_RENDERER_H
#include "kof98/strip_emit.h"
#include "kof98/saturn_tile_cache.h"
/* One owner, zero-initialized, stable address, valid until next sat_init. */
typedef struct { kof_saturn_tile_cache *tiles; uint32_t base; sat_result_t rollback_error; kof_asset_result asset_error; } kof_saturn_strip_renderer;
sat_result_t kof_saturn_strip_renderer_init(kof_saturn_strip_renderer*,kof_saturn_tile_cache*);
/* Call after sat_begin_frame, before any other user of this tile cache.
 * Success retains pins; caller ends cache only after actual submission or
 * discard. On failure with failed rollback, pins remain active: do not submit. */
sat_result_t kof_saturn_strip_draw(kof_saturn_strip_renderer*,const kof_strip_builder*,const kof_tile_view*,kof_emit_stats*);
#endif
