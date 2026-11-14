#ifndef KOF98_PAGED_DISPATCH_H
#define KOF98_PAGED_DISPATCH_H
#include "kof98/dispatch.h"
/* 4 KiB guest pages: fixed + four banks + 128 KiB BIOS. Directory includes
 * terminal sentinel. All arrays are caller-owned; no dynamic allocation. */
#define KOF_DISPATCH_PAGES 1312u
#define KOF_DISPATCH_MAX_ENTRIES UINT16_MAX

typedef struct {
    const uint16_t *directory;
    const uint16_t *offsets;
    const kof_dispatch_fn *functions;
    size_t count;
} kof_paged_dispatch;

/* Host/preparation helper. Requires sorted canonical entries and disjoint
 * output arrays (directory: PAGES+1, offsets/functions: capacity each).
 * Failure leaves output arrays untouched. Target can use offline-emitted arrays. */
int kof_paged_build(const kof_dispatch_entry *entries, size_t count,
                    uint16_t directory[KOF_DISPATCH_PAGES + 1u],
                    uint16_t *offsets, kof_dispatch_fn *functions, size_t capacity);
int kof_paged_validate(const kof_paged_dispatch *table);
/* Only use a validated immutable table. */
kof_dispatch_fn kof_paged_lookup(const kof_paged_dispatch *table, uint32_t pc, unsigned bank);
#endif
