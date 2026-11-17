/* rawcode_loader.c - apply relocation patches to a disc-loaded rawcode page.
 *
 * A page blob from PAGES.BIN is linked at VMA 0. All its R_SH_DIR32 slots
 * point either to fixed app addresses (external) or to offsets within the
 * blob (internal page helpers). Loading the blob at `page_base` requires
 * adding `page_base` to exactly the internal slots. RELOCS.BIN describes
 * those slots per page:
 *
 *   u32 page_count
 *   u32 block_offset[page_count]   (absolute byte offsets into RELOCS.BIN)
 *   ... blocks: u32 count, then count * u32 slot offsets ...
 *
 * This file is host-testable (no Saturn SDK dependency).
 */
#include "kof98/rawcode_loader.h"

uint32_t rawcode_reloc_count(const uint8_t* relocs) {
    return be32(relocs);
}

/* Patch a page blob resident at `dst` using page `page`'s reloc block, adding
 * `load_base` to each internal slot. In the real app dst == (uint8_t*)load_base;
 * the split lets host tests load into a heap buffer with a simulated base. */
uint32_t rawcode_patch_page(uint8_t* dst, const uint8_t* relocs, uint32_t page,
                            uint32_t load_base) {
    uint32_t npages = be32(relocs);
    if (page >= npages) return 0;
    uint32_t blk = be32(relocs + 4 + page * 4);
    return rawcode_patch_block(dst, relocs + blk, load_base);
}

uint32_t rawcode_patch_block(uint8_t* dst, const uint8_t* block, uint32_t load_base) {
    uint32_t count = be32(block);
    const uint8_t* slots = block + 4;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = be32(slots + i * 4);
        uint32_t v = be32(dst + off) + load_base;
        /* write big-endian explicitly: correct on the SH-2 and host-testable */
        dst[off]     = (uint8_t)(v >> 24);
        dst[off + 1] = (uint8_t)(v >> 16);
        dst[off + 2] = (uint8_t)(v >> 8);
        dst[off + 3] = (uint8_t)v;
    }
    return count;
}
