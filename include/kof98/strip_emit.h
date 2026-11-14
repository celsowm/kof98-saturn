#ifndef KOF98_STRIP_EMIT_H
#define KOF98_STRIP_EMIT_H
#include "kof98/strip.h"
#define KOF_EMIT_MAX 2048u
#define KOF_VARIANT_SLOTS 256u
#define KOF_VARIANT_BYTES (KOF_VARIANT_SLOTS*128u)
typedef struct { uint16_t native, variants; } kof_emit_stats;
/* Validate all chains, painter order and capacity before touching backend. */
int kof_strip_preflight(const kof_strip_builder*, unsigned command_budget, kof_emit_stats*);
#endif
