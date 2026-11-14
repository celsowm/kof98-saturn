#include "kof98/saturn_asset_store.h"
static int source_read(void *ctx,uint32_t off,void *dst,uint32_t n) {
    kof_saturn_assets *a=ctx; uint32_t got=0;
    a->last_sdk_error=sat_cdfs_read_at(a->volume,&a->file,off,dst,n,&got);
    if(a->last_sdk_error==SAT_OK && got!=n)a->last_sdk_error=SAT_ERR_IO;
    return a->last_sdk_error!=SAT_OK;
}
static int cart_read(void *ctx,uint32_t off,void *dst,uint32_t n) {
    kof_saturn_assets *a=ctx;
    a->last_sdk_error=sat_ram_cart_buffer_read_at(&a->arena,off,dst,n);
    return a->last_sdk_error!=SAT_OK;
}
static int cart_write(void *ctx,uint32_t off,const void *src,uint32_t n) {
    kof_saturn_assets *a=ctx;
    a->last_sdk_error=sat_ram_cart_buffer_write_at(&a->arena,off,src,n);
    return a->last_sdk_error!=SAT_OK;
}
sat_result_t kof_saturn_assets_open(kof_saturn_assets *a,sat_cdfs_volume_t *volume,const char *path,uint32_t cap) {
    if(!a || !volume || !volume->mounted || !path || !*path || !cap || cap>KOF_CART_BYTES)return SAT_ERR_INVALID_ARG;
    if(a->store.capacity)return SAT_ERR_ALREADY_EXISTS;
    sat_ram_cart_info_t info;
    SAT_TRY(sat_ram_cart_init());
    SAT_TRY(sat_ram_cart_info(&info));
    if(info.type!=SAT_RAM_CART_4MB || info.capacity!=KOF_CART_BYTES || info.bank_count!=2)return SAT_ERR_UNSUPPORTED;
    if(info.free_bytes<cap)return SAT_ERR_CAPACITY;
    sat_cdfs_file_t file;
    SAT_TRY(sat_cdfs_lookup(volume,path,&file));
    if(file.directory || !file.size)return SAT_ERR_INVALID_ARG;
    SAT_TRY(sat_ram_cart_buffer_alloc(cap,32,&a->arena));
    a->volume=volume; a->file=file; a->last_sdk_error=SAT_OK;
    /* With validated parameters this cannot fail. */
    return kof_asset_init(&a->store,cap,file.size,source_read,a,cart_read,cart_write,a)==KOF_ASSET_OK?SAT_OK:SAT_ERR_INVALID_ARG;
}
