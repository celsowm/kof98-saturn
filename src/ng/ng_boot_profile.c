/* ng_boot_profile.c - host-only guest PC profiler.
 * Extracted from ng_boot.c (SRP): the emulated machine and its scheduler
 * do not depend on profiling. Only built for NG_BOOT_PC_PROFILE hosts.
 */
#include "ng_boot_internal.h"

/* Called for every instruction by m68ki_instr_hook (compile-time hook). */
#ifdef NG_BOOT_PC_PROFILE
/* Host-only guest PC profiler (X1 work package). Covers the cart/WRAM window
 * (0x000000-0x2FFFFF) and the BIOS (0xC00000-0xC1FFFF) at word granularity,
 * plus 4 KB page hit counts for hot-set sizing. Never built for Saturn. */
#include <stdio.h>
#define NG_PROF_CART_BYTES 0x300000u
#define NG_PROF_CART_PAGES (NG_PROF_CART_BYTES >> 12)
#define NG_PROF_BIOS_BYTES 0x20000u
#define NG_PROF_BIOS_PAGES (NG_PROF_BIOS_BYTES >> 12)
#define NG_PROF_BITS ((NG_PROF_CART_BYTES + NG_PROF_BIOS_BYTES) >> 1)
#define NG_PROF_PAGES (NG_PROF_CART_PAGES + NG_PROF_BIOS_PAGES)
#define NG_PROF_LINES ((NG_PROF_CART_BYTES + NG_PROF_BIOS_BYTES) >> 6)
static uint8_t s_prof_words[NG_PROF_BITS / 8];
static uint32_t s_prof_page_hits[NG_PROF_PAGES];
static uint32_t s_prof_page_words[NG_PROF_PAGES];
static uint32_t s_prof_line_hits[NG_PROF_LINES];
static uint64_t s_prof_calls;
volatile uint32_t ng_profile_wram_writer_pc;

void ng_profile_record(uint32_t pc) {
    uint32_t bit, page;
    pc &= 0xFFFFFFu;
    if (pc < NG_PROF_CART_BYTES) {
        bit = pc >> 1;
        page = pc >> 12;
    } else if (pc >= 0xC00000u && pc < 0xC00000u + NG_PROF_BIOS_BYTES) {
        bit = (NG_PROF_CART_BYTES >> 1) + ((pc - 0xC00000u) >> 1);
        page = NG_PROF_CART_PAGES + ((pc - 0xC00000u) >> 12);
    } else {
        return;
    }
    uint8_t mask = (uint8_t)(1u << (bit & 7u));
    if (!(s_prof_words[bit >> 3] & mask)) {
        s_prof_words[bit >> 3] |= mask;
        s_prof_page_words[page]++;       /* distinct instruction words in this page */
    }
    s_prof_page_hits[page]++;
    s_prof_line_hits[bit >> 5]++;        /* 64-byte line, finer hot-loop view */
    s_prof_calls++;
}
void ng_boot_profile_reset(void) {
    for (unsigned i = 0; i < sizeof(s_prof_words); i++) s_prof_words[i] = 0;
    for (unsigned i = 0; i < NG_PROF_PAGES; i++) s_prof_page_hits[i] = 0;
    for (unsigned i = 0; i < NG_PROF_PAGES; i++) s_prof_page_words[i] = 0;
    for (unsigned i = 0; i < NG_PROF_LINES; i++) s_prof_line_hits[i] = 0;
    s_prof_calls = 0;
}
void ng_boot_profile_dump(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t distinct = 0, pages = 0;
    for (unsigned i = 0; i < sizeof(s_prof_words); i++) {
        uint8_t v = s_prof_words[i];
        while (v) { distinct += v & 1u; v >>= 1; }
    }
    for (unsigned i = 0; i < NG_PROF_PAGES; i++) pages += s_prof_page_hits[i] != 0;
    fprintf(f, "# kof-guest-profile-v1\n");
    fprintf(f, "instructions=%llu\ndistinct_words=%u\ndistinct_pages=%u\n",
            (unsigned long long)s_prof_calls, distinct, pages);
    for (unsigned i = 0; i < NG_PROF_PAGES; i++) {
        if (!s_prof_page_hits[i]) continue;
        uint32_t base = i < NG_PROF_CART_PAGES ? (i << 12)
                                               : (0xC00000u + ((i - NG_PROF_CART_PAGES) << 12));
        fprintf(f, "page %06X hits %u words %u\n", base, s_prof_page_hits[i],
                s_prof_page_words[i]);
    }
    for (unsigned i = 0; i < NG_PROF_LINES; i++) {
        if (!s_prof_line_hits[i]) continue;
        uint32_t base = i < (NG_PROF_CART_BYTES >> 6) ? (i << 6)
                                                       : (0xC00000u + ((i - (NG_PROF_CART_BYTES >> 6)) << 6));
        fprintf(f, "line %06X hits %u\n", base, s_prof_line_hits[i]);
    }
    fclose(f);
}
/* Word-address of a profiler bit; the two windows are contiguous in the bitmap. */
static uint32_t prof_bit_addr(uint32_t bit) {
    if (bit < (NG_PROF_CART_BYTES >> 1)) return bit << 1;
    return 0xC00000u + ((bit - (NG_PROF_CART_BYTES >> 1)) << 1);
}
void ng_boot_profile_dump_words(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "# kof-guest-words-v1\n");
    uint32_t run_start = 0;
    int in_run = 0;
    for (uint32_t bit = 0; bit <= NG_PROF_BITS; bit++) {
        int set = 0;
        if (bit < NG_PROF_BITS)
            set = (s_prof_words[bit >> 3] >> (bit & 7u)) & 1u;
        if (set && !in_run) { run_start = bit; in_run = 1; }
        else if (!set && in_run) {
            fprintf(f, "%06X-%06X\n", prof_bit_addr(run_start), prof_bit_addr(bit - 1));
            in_run = 0;
        }
    }
    fclose(f);
}
#endif /* NG_BOOT_PC_PROFILE */
