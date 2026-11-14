#include "kof98/paged_dispatch.h"

/* Compact page order follows numeric canonical key order: fixed, bank 0,
 * BIOS, banks 1..3. No sparse 32-bit-address directory. */
static unsigned page_of(uint32_t key)
{
    unsigned bank = key >> 24;
    unsigned pc = key & 0xffffffu;
    if (pc < 0x100000u) return pc >> 12;
    if (pc >= 0xc00000u) return 512u + ((pc - 0xc00000u) >> 12);
    return (bank ? 544u + (bank - 1u) * 256u : 256u) + ((pc - 0x200000u) >> 12);
}

int kof_paged_build(const kof_dispatch_entry *entries, size_t count,
                    uint16_t directory[KOF_DISPATCH_PAGES + 1u],
                    uint16_t *offsets, kof_dispatch_fn *functions, size_t capacity)
{
    if (!directory || count > capacity || count > KOF_DISPATCH_MAX_ENTRIES ||
        (count && (!offsets || !functions)) || !kof_dispatch_validate(entries, count)) return 0;
    size_t i = 0;
    for (unsigned page = 0; page < KOF_DISPATCH_PAGES; ++page) {
        directory[page] = (uint16_t)i;
        while (i < count && page_of(entries[i].key) == page) {
            offsets[i] = (uint16_t)(entries[i].key & 0xfffu);
            functions[i] = entries[i].fn;
            ++i;
        }
    }
    directory[KOF_DISPATCH_PAGES] = (uint16_t)i;
    return i == count;
}

int kof_paged_validate(const kof_paged_dispatch *t)
{
    if (!t || !t->directory || t->count > KOF_DISPATCH_MAX_ENTRIES ||
        (t->count && (!t->offsets || !t->functions)) || t->directory[0] != 0 ||
        t->directory[KOF_DISPATCH_PAGES] != t->count) return 0;
    for (unsigned page = 0; page < KOF_DISPATCH_PAGES; ++page) {
        unsigned lo = t->directory[page], hi = t->directory[page+1u];
        if (lo > hi || hi > t->count) return 0;
        for (unsigned i = lo; i < hi; ++i) {
            if (!t->functions[i] || t->offsets[i] >= 4096u || (t->offsets[i] & 1u) ||
                (i > lo && t->offsets[i-1u] >= t->offsets[i])) return 0;
        }
    }
    return 1;
}

kof_dispatch_fn kof_paged_lookup(const kof_paged_dispatch *t, uint32_t pc, unsigned bank)
{
    uint32_t key;
    if (!t || !kof_dispatch_key(pc, bank, &key)) return NULL;
    unsigned page = page_of(key), target = key & 0xfffu;
    unsigned lo = t->directory[page], hi = t->directory[page+1u], end = hi;
    while (lo < hi) {
        unsigned mid = lo + (hi-lo)/2u;
        if (t->offsets[mid] < target) lo = mid+1u;
        else hi = mid;
    }
    return lo < end && t->offsets[lo] == target ? t->functions[lo] : NULL;
}
