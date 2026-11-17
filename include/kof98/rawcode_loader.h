#ifndef RAWCODE_LOADER_H
#define RAWCODE_LOADER_H
#include <stdint.h>

static inline uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint32_t rawcode_reloc_count(const uint8_t* relocs);
uint32_t rawcode_patch_page(uint8_t* dst, const uint8_t* relocs, uint32_t page,
                            uint32_t load_base);
/* Patch from a single raw block: u32 count followed by count*u32 offsets. */
uint32_t rawcode_patch_block(uint8_t* dst, const uint8_t* block, uint32_t load_base);
#endif
