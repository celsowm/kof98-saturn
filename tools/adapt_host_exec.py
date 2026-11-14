#!/usr/bin/env python3
"""Build-local adaptation of the pinned neogeorecomp exec.c; no upstream edits."""
import argparse
from pathlib import Path

STORAGE = """/* KOF host comparison: no dense function-pointer tables. */
extern const kof_paged_dispatch kof_generated_dispatch;
static int s_nbanks;
"""
LOOKUP = """static int in_bank_window(uint32_t a) {
    a &= 0xffffffu;
    return a >= 0x200000u && a < 0x300000u;
}

static inline ng_func_t lookup(uint32_t a) {
    return kof_paged_lookup(&kof_generated_dispatch, a, (unsigned)bus_bank());
}

void exec_init(const ng_func_entry_t *funcs, size_t nfuncs, int interp_only) {
    /* Private host-player contract: NULL legacy pointer, real paged count.
     * The upstream ng_main uses the count to select interpreter-only mode. */
    s_nbanks = bus_bank_count();
    if (funcs || nfuncs != kof_generated_dispatch.count || s_nbanks != 4 ||
        !kof_paged_validate(&kof_generated_dispatch)) {
        fprintf(stderr, "fatal: invalid KOF paged dispatch configuration\\n");
        abort();
    }
    /* Keep full host profiling; never link this bitmap/log on Saturn. */
    free(s_miss_seen);
    s_miss_seen = calloc(((size_t)(1 + s_nbanks) * 0x80000u + 0x10000u) / 8u, 1);
    if (!s_miss_seen) { fprintf(stderr, "fatal: miss bitmap allocation\\n"); abort(); }
    s_nmiss = 0;
    s_miss_overflow = 0;
    s_interp_only = interp_only;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
}
"""
MISS = """static void note_miss(uint32_t a) {
    unsigned bank = in_bank_window(a) ? (unsigned)bus_bank() : 0u;
    uint32_t key;
    if (!kof_dispatch_key(a, bank, &key)) return;
    uint32_t pc = key & 0xffffffu;
    size_t bit;
    if (pc < 0x100000u) bit = pc >> 1;
    else if (pc < 0x300000u)
        bit = (size_t)(1u + bank) * 0x80000u + ((pc - 0x200000u) >> 1);
    else
        bit = (size_t)(1 + s_nbanks) * 0x80000u + ((pc - 0xc00000u) >> 1);
    if (s_miss_seen[bit >> 3] & (1u << (bit & 7u))) return;
    s_miss_seen[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
    /* Preserve the original raw tagged address for --entries. The bitmap
     * deduplicates BIOS mirrors, just like the original dense index. */
    if (s_nmiss < MISS_MAX)
        s_miss[s_nmiss++] = (a & 0xfffffeu) | ((uint32_t)bank << 24);
    else s_miss_overflow = 1;
}
"""

def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError(f'upstream anchor missing or ambiguous: {old[:80]}')
    return source.replace(old, new, 1)

def span(source, start, end, new):
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError(f'upstream span missing or ambiguous: {start}')
    lo, hi = source.index(start), source.index(end)
    if lo >= hi:
        raise ValueError(f'upstream span reversed: {start}')
    return source[:lo]+new+source[hi:]

def adapt(source, native_profile=False):
    source = replace_once(source, '#include "ng_internal.h"', '#include "ng_internal.h"\n#include "kof98/paged_dispatch.h"')
    source = span(source, '/* Entry tables for the regions', 'static int s_interp_only;', STORAGE)
    source = replace_once(source, 'static size_t s_nmiss;', 'static size_t s_nmiss;\nstatic int s_miss_overflow;')
    source = span(source, '/* The table slot for', '/* ---- interrupts ---- */', LOOKUP+'\n')
    source = span(source, 'static void note_miss(uint32_t a) {', '/* Where interpretation starts', MISS+'\n')
    source = replace_once(source, 'int exec_dump_misses(const char *path) {', 'int exec_dump_misses(const char *path) {\n    if (s_miss_overflow) { fprintf(stderr, "fatal: incomplete miss log; refusing to dump\\n"); return -1; }')
    source = replace_once(source, '    (void)total;', '    if (s_miss_overflow) fprintf(stderr, "[exec] MISS LOG OVERFLOW\\n");\n    (void)total;')
    for token in ('s_tab_fixed', 's_tab_bank', 's_tab_bios', 'slot_in(', 'slot(a)'):
        if token in source:
            raise ValueError(f'unhandled dense lookup reference: {token}')
    if native_profile:
        source=replace_once(source, '#include "kof98/paged_dispatch.h"', '#include "kof98/paged_dispatch.h"\n#include "kof98/native_profile.h"\nextern const char kof_generated_dispatch_id[];\nstatic int s_native_profile;')
        source=replace_once(source, '    s_interp_only = interp_only;', '    s_interp_only = interp_only;\n    kof_native_profile_init(getenv("KOF_NATIVE_PROFILE"), kof_generated_dispatch_id);\n    s_native_profile=kof_native_profile_enabled();')
        source=replace_once(source, '        if (fn) { if (s_verify) run_verified(fn); else fn(); exec_native_blocks++; }', '''        if (fn) {
            if (s_native_profile) {
                uint32_t pc=CPU.pc;
                unsigned bank=(unsigned)bus_bank();
                uint64_t before=ng_icount;
                int64_t tick=ng_cycles;
                if (s_verify) run_verified(fn); else fn();
                if (ng_cycles<tick || ng_icount<before) abort();
                kof_native_profile_record(pc, bank, ng_icount-before, (uint64_t)ng_cycles-(uint64_t)tick);
            } else if (s_verify) run_verified(fn); else fn();
            exec_native_blocks++;
        }''')
        source=replace_once(source,'void exec_report(void) {','void exec_report(void) {\n    kof_native_profile_finish();\n    s_native_profile=0;')
    return source

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--native-profile',action='store_true')
    a=parser.parse_args()
    if a.source.resolve()==a.output.resolve():
        parser.error('refusing to overwrite upstream source')
    try:
        result=adapt(a.source.read_text(encoding='utf-8'), a.native_profile)
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(result,encoding='utf-8')
    except (OSError,ValueError) as e:
        parser.error(str(e))

if __name__=='__main__':
    main()
