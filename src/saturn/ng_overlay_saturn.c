/* ng_overlay_saturn.c - Saturn platform hook for the rawcode game overlay.
 *
 * The core owns paging policy. Page blobs live in PAGES.BIN (one stride per
 * page); internal relocation slots live in RELOCS.BIN. Loading a page copies
 * its blob into the WRAM-L arena and adds the arena base to each internal slot
 * so the page's routines are callable at their new address.
 */
#include <stdint.h>
#include <saturn/cd_mount.h>
#include <saturn/cdfs.h>
#include "ng_boot.h"
#include "ng_recomp_overlay_config.h"
#include "game_recomp_pages.h"
#include "kof98/rawcode_loader.h"

/* One resident page slot in WRAM-L (the staging area). */
uint8_t g_page_arena[NG_RECOMP_ARENA_PAGES][NG_RECOMP_PAGE_BYTES]
    __attribute__((section(".wram_l")));
/* Scratch for one page's reloc block (count + offsets). */
static uint8_t s_reloc_block[64 * 1024] __attribute__((section(".wram_l")));

static sat_cdfs_volume_t* s_volume;
static sat_cdfs_file_t s_pages_file;
static sat_cdfs_file_t s_relocs_file;
static uint32_t s_pages_bytes, s_relocs_bytes;

/* Direct-mapped slot: page P always lives in slot P % ARENA_PAGES.  The
 * dispatcher and the loader agree via the same formula, so no residency
 * table is needed. */
uint8_t* ng_overlay_page_base(unsigned page) {
    return g_page_arena[page & (NG_RECOMP_ARENA_PAGES - 1u)];
}

void ng_overlay_saturn_init(sat_cdfs_volume_t* volume) {
    s_volume = volume;
    s_pages_bytes = s_relocs_bytes = 0;
    if (sat_cdfs_lookup(s_volume, "PAGES.BIN", &s_pages_file) == SAT_OK)
        s_pages_bytes = s_pages_file.size;
    if (sat_cdfs_lookup(s_volume, "RELOCS.BIN", &s_relocs_file) == SAT_OK)
        s_relocs_bytes = s_relocs_file.size;
}

int ng_overlay_loader(unsigned page) {
    if (!s_volume || !s_pages_bytes || !s_relocs_bytes) return -1;
    uint32_t off = page * NG_RECOMP_PAGE_BYTES;
    if (off + NG_RECOMP_PAGE_BYTES > s_pages_bytes) return -1;
    uint8_t* dst = ng_overlay_page_base(page);
    uint32_t got = 0;
    if (sat_cdfs_read_at(s_volume, &s_pages_file, off, dst, NG_RECOMP_PAGE_BYTES, &got) != SAT_OK ||
        got != NG_RECOMP_PAGE_BYTES) return -1;

    /* Read this page's reloc block from RELOCS.BIN: index has u32 page_count
     * then u32 block_offset[pages]; each block is u32 count + u32 offsets. */
    uint8_t idx[4];
    if (sat_cdfs_read_at(s_volume, &s_relocs_file, 4 + page * 4, idx, 4, &got) != SAT_OK ||
        got != 4) return -1;
    uint32_t blk = be32(idx);
    if (blk + 4 > s_relocs_bytes) return -1;
    uint8_t cntbuf[4];
    if (sat_cdfs_read_at(s_volume, &s_relocs_file, blk, cntbuf, 4, &got) != SAT_OK ||
        got != 4) return -1;
    uint32_t nslots = be32(cntbuf);
    uint32_t nbytes = 4 + nslots * 4;
    if (nbytes > sizeof(s_reloc_block) || blk + nbytes > s_relocs_bytes) return -1;
    if (sat_cdfs_read_at(s_volume, &s_relocs_file, blk, s_reloc_block, nbytes, &got) != SAT_OK ||
        got != nbytes) return -1;
    rawcode_patch_block(dst, s_reloc_block, (uint32_t)(uintptr_t)dst);
    return 0;
}
