#ifndef KOF98_SATURN_ASSET_STORE_H
#define KOF98_SATURN_ASSET_STORE_H
#include "kof98/asset_store.h"
#include <saturn/ram_cart.h>
#include <saturn/cdfs.h>
/* Stable address, caller-owned, zero-initialize once. The CDFS volume
 * must already be mounted and remain live; do not reset the cart or
 * change clocks until every cart consumer has been drained. */
typedef struct {
    kof_asset_store store;
    sat_ram_cart_buffer_t arena;
    sat_cdfs_volume_t *volume;
    sat_cdfs_file_t file;
    sat_result_t last_sdk_error;
} kof_saturn_assets;
/* Master only. No other CD owner/stream may be active during open/load.
 * Reserve the arena once; no reset, implicit CD mount, or individual free. */
sat_result_t kof_saturn_assets_open(kof_saturn_assets*,sat_cdfs_volume_t*,const char* pack_path,uint32_t capacity);
#endif
