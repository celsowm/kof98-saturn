#ifndef KOF98_DISPATCH_H
#define KOF98_DISPATCH_H
#include <stddef.h>
#include <stdint.h>

/* Native function retains upstream's void(void) convention. Entries must
 * be sorted by canonical key. Caller owns immutable storage. */
typedef void (*kof_dispatch_fn)(void);
typedef struct { uint32_t key; kof_dispatch_fn fn; } kof_dispatch_entry;

/* Matches upstream dispatch alias policy (not guest bus exception policy).
 * Returns zero for unmapped regions or invalid banks; never modifies key
 * on failure. Bank applies only in the banked ROM window. */
int kof_dispatch_key(uint32_t pc, unsigned bank, uint32_t *key);
int kof_dispatch_validate(const kof_dispatch_entry *entries, size_t count);
kof_dispatch_fn kof_dispatch_lookup(const kof_dispatch_entry *entries,
                                    size_t count, uint32_t pc, unsigned bank);
#endif
