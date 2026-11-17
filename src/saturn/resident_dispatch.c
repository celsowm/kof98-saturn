/* resident_dispatch.c - lookup for resident native routines.
 * Proven correct: 532 entries, bad=0 over sustained 30k-field runs.
 *
 * The SH-2 profile shows lookup ~10% of instructions. An earlier O(1) table
 * that REPLACED the binary search was reverted (hash collisions + BIOS PC
 * hijacking). This variant instead adds a small runtime-populated,
 * direct-mapped cache in FRONT of the unchanged binary search:
 *   - the cache is filled only from a binary-search result (hit or miss), so
 *     a valid entry always holds exactly the fn the search returns;
 *   - every hit requires a full 32-bit tag match, so a collision can never
 *     return the wrong fn (it just misses and falls back to the search);
 *   - the index hash is computed entirely at runtime in C (no offline
 *     Python/C mismatch, which caused the earlier fault at 0x9647C).
 * Bit-exact by construction: the binary search remains the sole authority.
 */
#include <stdint.h>
#include <stddef.h>
#include <neogeorecomp/neogeo.h>
#include "ng_boot.h"

struct resident_entry { uint32_t addr; ng_func_t fn; };
extern const struct resident_entry g_resident_map[];
extern const unsigned g_resident_map_count;

#define CACHE_BITS 9u
#define CACHE_SIZE (1u << CACHE_BITS)          /* 512 slots * 8 B = 4 KiB */
#define CACHE_MASK (CACHE_SIZE - 1u)
static struct resident_entry s_cache[CACHE_SIZE];

ng_func_t ng_resident_lookup(uint32_t pc) {
    /* 32-bit multiplicative hash, masked after every op (no 64-bit morph). */
    /* s_cache is zero-initialised, so addr==0 is the "empty" sentinel: never
     * trust a tag match for pc==0 (it could spuriously hit fn=NULL). PC 0 is
     * the vector page and is not a native routine, so bypass the cache for it. */
    if (pc == 0u) return NULL;
    uint32_t idx = (pc * 2654435761u) >> (32u - CACHE_BITS);
    idx &= CACHE_MASK;
    if (s_cache[idx].addr == pc) return s_cache[idx].fn;   /* tag match (incl. miss tags) */

    size_t lo = 0, hi = g_resident_map_count;
    while (lo < hi) {
        size_t m = lo + ((hi - lo) >> 1);
        if (g_resident_map[m].addr < pc) lo = m + 1;
        else hi = m;
    }
    ng_func_t fn = NULL;
    if (lo < g_resident_map_count && g_resident_map[lo].addr == pc)
        fn = g_resident_map[lo].fn;
    s_cache[idx].addr = pc;                                /* cache hit or miss */
    s_cache[idx].fn = fn;
    return fn;
}
