#include "kof98/dispatch.h"

int kof_dispatch_key(uint32_t pc, unsigned bank, uint32_t *key)
{
    if (!key) return 0;
    pc &= UINT32_C(0x00fffffe);
    if (pc < UINT32_C(0x100000)) {
        *key = pc;
    } else if (pc >= UINT32_C(0x200000) && pc < UINT32_C(0x300000)) {
        if (bank >= 4u) return 0;
        *key = ((uint32_t)bank << 24) | pc;
    } else if (pc >= UINT32_C(0xc00000) && pc < UINT32_C(0xd00000)) {
        *key = UINT32_C(0xc00000) | (pc & UINT32_C(0x1fffe));
    } else {
        return 0;
    }
    return 1;
}

int kof_dispatch_validate(const kof_dispatch_entry *entries, size_t count)
{
    if (count && !entries) return 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t canonical;
        const uint32_t key = entries[i].key;
        if (!entries[i].fn ||
            !kof_dispatch_key(key, key >> 24, &canonical) ||
            canonical != key || (i && entries[i-1].key >= key)) return 0;
    }
    return 1;
}

kof_dispatch_fn kof_dispatch_lookup(const kof_dispatch_entry *entries,
                                    size_t count, uint32_t pc, unsigned bank)
{
    uint32_t key;
    if (!entries || !kof_dispatch_key(pc, bank, &key)) return NULL;
    size_t lo = 0, hi = count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (entries[mid].key < key) lo = mid + 1;
        else hi = mid;
    }
    return lo < count && entries[lo].key == key ? entries[lo].fn : NULL;
}
