/* ng_boot_host.c - host validator for the native 68000 Neo Geo BIOS boot.
 *
 * Loads the AES system BIOS, puts it in 68000 byte order, boots it on the
 * Musashi 68000 core with the same memory map the Saturn target uses, runs N
 * video frames and reports how far real 68000 execution got.
 *
 * Usage: ng_boot_host <bios.bin> [frames]
 */
#include "ng_boot.h"
#ifndef NG_BOOT_RECOMP_ONLY
#include "m68k.h"       /* diagnostics only; absent in the interpreter-free build */
#endif
#ifdef NG_BOOT_OVERLAY
#include "bios_recomp_pages.h"   /* NG_RECOMP_PAGES for the overlay report */
#endif
#include "kof98/ng_live_strips.h"
#include "ng_input_script.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

uint8_t  ng_boot_read8(uint32_t a);
uint16_t ng_boot_read16(uint32_t a);
uint32_t ng_boot_read32(uint32_t a);

static void swap16(uint8_t* p, size_t n) {
    for (size_t i = 0; i + 1 < n; i += 2) { uint8_t t = p[i]; p[i] = p[i + 1]; p[i + 1] = t; }
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <bios.bin> [frames] [p1.bin] [p2.bin]\n", argv[0]); return 2; }
    uint32_t frames = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 60;
    /* Start can be suppressed (NG_START_FRAME=-1) to profile the idle attract
     * sequence, or delayed to reach a specific screen. */
    long start_frame = 300;
    const char* start_env = getenv("NG_START_FRAME");
    if (start_env) start_frame = strtol(start_env, NULL, 0);

    FILE* f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    static uint8_t bios[0x20000];
    size_t n = fread(bios, 1, sizeof(bios), f);
    fclose(f);
    if (n < 0x20000) { fprintf(stderr, "%s is %zu bytes, need 131072\n", argv[1], n); return 1; }
    swap16(bios, 0x20000);                     /* file is byte-swapped, to 68k order */

    /* Optional cart: P1 (1 MiB) at cart 0, P2 banks 0..2 (3 MiB) at 0x100000. */
    static uint8_t cart[0x400000];
    if (argc > 4) {
        FILE* f1 = fopen(argv[3], "rb");
        FILE* f2 = fopen(argv[4], "rb");
        if (!f1 || !f2) { fprintf(stderr, "cannot open p1/p2\n"); return 1; }
        size_t n1 = fread(cart, 1, 0x100000, f1); fclose(f1);
        size_t n2 = fread(cart + 0x100000, 1, 0x300000, f2); fclose(f2);  /* first 3 MiB */
        swap16(cart, 0x100000);
        swap16(cart + 0x100000, 0x300000);
        if (n1 < 0x100000 || n2 < 0x300000) { fprintf(stderr, "short p1/p2\n"); return 1; }
        ng_boot_set_cart(cart, 0x200000, cart + 0x200000, 0x200000);
        printf("cart: P1 1 MiB + P2[0..2] 3 MiB\n");
    }

    const char* budget = NULL;
#ifdef NG_BOOT_OVERLAY
    /* The budget decides how many pages start resident, so read it before
     * ng_boot_init() runs the recomp/overlay initialisation. */
    budget = getenv("NG_OVERLAY_BUDGET");
    if (budget) ng_overlay_set_budget((unsigned)strtoul(budget, NULL, 0));
#endif
    ng_boot_init(bios, 0x20000);
#ifdef NG_BOOT_OVERLAY
    printf("overlay: pages=%u budget=%u pinned_reset=1\n",
           (unsigned)NG_RECOMP_PAGES, ng_overlay_budget());
#endif
    printf("reset SSP=%08X PC=%08X\n", ng_boot_initial_ssp(), ng_boot_initial_pc());
    printf("PC in BIOS range: %s\n",
           (ng_boot_initial_pc() >= 0xC00000 && ng_boot_initial_pc() < 0xD00000) ? "yes" : "no");

    /* Scripted input (same text format as the reference toolkit). Without a
     * script, keep the legacy behaviour: hold P1 Start from NG_START_FRAME. */
    static ng_input_event_t input_events[NG_INPUT_SCRIPT_MAX_EVENTS];
    int input_count = 0;
    const char* input_path = getenv("NG_INPUT");
    if (input_path && *input_path) {
        char err[256];
        input_count = ng_input_script_load(input_path, input_events,
                                           NG_INPUT_SCRIPT_MAX_EVENTS, err, sizeof(err));
        if (input_count < 0) {
            fprintf(stderr, "input script: %s\n", err);
            return 1;
        }
        printf("input: %s (%d events)\n", input_path, input_count);
    }
    long trace_every = 60;
    const char* trace_env = getenv("NG_TRACE_EVERY");
    if (trace_env) trace_every = strtol(trace_env, NULL, 0);
    if (trace_every <= 0) trace_every = 60;
    long profile_from = -1;
#ifdef NG_BOOT_PC_PROFILE
    {
        const char* from_env = getenv("NG_PROFILE_FROM");
        if (from_env) profile_from = strtol(from_env, NULL, 0);
    }
#endif
    fflush(stdout);

#ifndef NG_BOOT_RECOMP_ONLY
    /* Optional disassembly of a guest address (diagnostic for a profile page). */
    {
        const char* dis = getenv("NG_DISASM");
        if (dis && *dis) {
            char* endp = NULL;
            unsigned long addr = strtoul(dis, &endp, 0);
            int count = (endp && *endp == ':') ? atoi(endp + 1) : 32;
            if (count <= 0) count = 32;
            printf("disasm @ %06lX:\n", addr);
            uint32_t pc = (uint32_t)addr;
            for (int i = 0; i < count; i++) {
                char buf[128];
                unsigned n = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
                printf("  %06X  %s\n", pc, buf);
                if (!n) break;
                pc += n;
            }
        }
    }
#endif

#ifdef NG_BOOT_PC_PROFILE
    const char* profile_out = getenv("NG_PROFILE_OUT");
    if (profile_out) ng_boot_profile_reset();
#endif

    for (long frame = 0; frame < (long)frames; frame++) {
#ifdef NG_BOOT_PC_PROFILE
        if (profile_from >= 0 && frame == profile_from && profile_out)
            ng_boot_profile_reset();
#endif
        if (input_count > 0) {
            ng_input_state_t in;
            memset(&in, 0, sizeof(in));
            ng_input_script_apply(input_events, input_count, frame, &in);
            ng_boot_set_p1(in.p1);
            ng_boot_set_p2(in.p2);
            ng_boot_set_start(in.start1);
            ng_boot_set_start2(in.start2);
            ng_boot_set_select1(in.select1);
            ng_boot_set_select2(in.select2);
            ng_boot_set_coin1(in.coin1);
            ng_boot_set_coin2(in.coin2);
            ng_boot_set_service(in.service);
            ng_boot_set_test(in.test);
        } else {
            ng_boot_set_start(start_frame >= 0 && frame >= start_frame);
        }
        ng_boot_run_frames(1);
        long done = frame + 1;
        if (done % trace_every == 0 || done == (long)frames) {
            printf("frame %4ld  PC=%06X  instr=%u  bios=%u  cart=%u  wram=%u  irq=%u  vram_w=%u  vblanks=%u  pal_w=%u  io_w=%u  stage=%02X match=%04X p1=%02X%02X%02X cpu=%02X%02X%02X mode=%02X flag=%02X hp1=%04X hp2=%04X\n",
                   done, ng_boot_pc(), ng_boot_instructions, ng_boot_bios_instr,
                   ng_boot_cart_instr, ng_boot_wram_instr, ng_boot_irq_taken, ng_boot_vram_writes,
                   ng_boot_vblanks, ng_boot_pal_writes, ng_boot_io_writes,
                   ng_boot_read8(0x10A7EA), ng_boot_read16(0x10A796),
                   ng_boot_read8(0x10A7ED), ng_boot_read8(0x10A7EE), ng_boot_read8(0x10A7EF),
                   ng_boot_read8(0x10A85F), ng_boot_read8(0x10A860), ng_boot_read8(0x10A861),
                   ng_boot_read8(0x10DBA4), ng_boot_read8(0x10DBA5),
                   ng_boot_read16(0x108338), ng_boot_read16(0x108438));
            fflush(stdout);
        }
    }
    printf("DONE frames=%u cycles=%u instructions=%u bios=%u cart=%u wram=%u irq=%u vblanks=%u timer_irqs=%u vram_writes=%u pal_writes=%u io_writes=%u last_pc=%06X\n",
           ng_boot_frames, ng_boot_cycles_lo(), ng_boot_instructions, ng_boot_bios_instr, ng_boot_cart_instr,
           ng_boot_wram_instr, ng_boot_irq_taken, ng_boot_vblanks, ng_boot_timer_irqs, ng_boot_vram_writes,
           ng_boot_pal_writes, ng_boot_io_writes, ng_boot_last_pc);
    /* Game state that says a script reached a fight: stage index at $10A7EA
     * and the ladder match counter at A5+$2796 ($102796). Host diagnostic. */
    printf("guest state: stage[10A7EA]=%02X match[10A796]=%04X p1=%02X%02X%02X cpu=%02X%02X%02X mode[10DBA4]=%02X flag[10DBA5]=%02X\n",
           ng_boot_read8(0x10A7EA), ng_boot_read16(0x10A796),
           ng_boot_read8(0x10A7ED), ng_boot_read8(0x10A7EE), ng_boot_read8(0x10A7EF),
           ng_boot_read8(0x10A85F), ng_boot_read8(0x10A860), ng_boot_read8(0x10A861),
           ng_boot_read8(0x10DBA4), ng_boot_read8(0x10DBA5));
    printf("input reads: status_b=%u with_start=%u\n",
           ng_boot_status_reads, ng_boot_status_start_reads);
#ifdef NG_BOOT_PC_PROFILE
    if (profile_out) {
        ng_boot_profile_dump(profile_out);
        /* Read the header back: a silent fopen failure inside the dump must not
         * turn into a passing test, and ctest matches this line in stdout. */
        FILE* pf = fopen(profile_out, "rb");
        if (!pf) {
            fprintf(stderr, "profile: cannot read back %s\n", profile_out);
            return 1;
        }
        char head[64] = {0};
        if (!fgets(head, sizeof(head), pf)) head[0] = 0;
        fclose(pf);
        printf("%s", head);
        printf("profile: %s\n", profile_out);
    }
    {
        const char* words_out = getenv("NG_PROFILE_WORDS");
        if (words_out) {
            ng_boot_profile_dump_words(words_out);
            printf("profile words: %s\n", words_out);
        }
    }
#endif
#ifdef NG_BOOT_RECOMP
    printf("recomp: native_blocks=%u interp_instrs=%u bad_dispatch=%u\n",
           ng_boot_recomp_blocks, ng_boot_recomp_interp, ng_boot_recomp_bad);
#ifdef NG_BOOT_OVERLAY
    {
        /* Derived evidence that the run actually paged: a fault happened, pages
         * were brought in, the budget was respected, and more pages were loaded
         * than the budget allows (so some were evicted). Budget 0 = all resident. */
        unsigned budget = ng_overlay_budget();
        int paging_ok = ng_boot_overlay_faults > 0 && ng_boot_overlay_loads > 0 &&
                        ng_boot_overlay_errors == 0 &&
                        (budget == 0 ? ng_boot_overlay_resident == NG_RECOMP_PAGES
                                     : (ng_boot_overlay_resident <= budget &&
                                        ng_boot_overlay_loads > budget));
        printf("overlay: faults=%u loads=%u evictions=%u errors=%u resident=%u budget=%u paging_ok=%d\n",
               ng_boot_overlay_faults, ng_boot_overlay_loads, ng_boot_overlay_evictions,
               ng_boot_overlay_errors, ng_boot_overlay_resident, budget, paging_ok);
    }
#endif
#else
    /* Disassemble the hot loop the boot is stuck in (diagnostic). */
    {
        uint32_t pc = (ng_boot_last_pc & ~1u) - 8;
        printf("hot loop @ %06X:\n", pc);
        for (int i = 0; i < 12; i++) {
            char buf[128];
            unsigned n = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
            printf("  %06X  %s\n", pc, buf);
            if (n == 0) break;
            pc += n;
        }
    }
    {
        uint32_t a5 = (uint32_t)m68k_get_reg(NULL, M68K_REG_A5);
        unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR);
        printf("diag: A5=%06X flag@A5+7e8c=%02X  SR=%04X (IPL=%u S=%u)\n",
               a5, ng_boot_read8(a5 + 0x7e8c), sr, (sr >> 8) & 7, (sr >> 13) & 1);
        uint32_t v1 = ng_boot_read32(0x64);   /* IRQ1 autovector */
        uint32_t v2 = ng_boot_read32(0x68);   /* IRQ2 (timer) autovector */
        printf("diag: IRQ1 vec=%06X  IRQ2 vec=%06X  [10fd80]=%02X [10fe8c]=%02X [10fee3]=%02X [10fee4]=%02X\n",
               v1, v2, ng_boot_read8(0x10fd80), ng_boot_read8(0x10fe8c),
               ng_boot_read8(0x10fee3), ng_boot_read8(0x10fee4));
        printf("BIOS vblank handler @ $C1137A:\n");
        {
            uint32_t pc = 0xC1137A;
            for (int i = 0; i < 24; i++) {
                char buf[128];
                unsigned n = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
                printf("  %06X  %s\n", pc, buf);
                if (n == 0) break;
                pc += n;
            }
        }
        if (v1 < 0x300000 && !(v1 >= 0x100000 && v1 < 0x200000)) {
            uint32_t pc = v1 & ~1u;
            printf("IRQ1 handler:\n");
            for (int i = 0; i < 10; i++) {
                char buf[128];
                unsigned n = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
                printf("  %06X  %s\n", pc, buf);
                if (n == 0) break;
                pc += n;
            }
        }
    }
#endif
    /* Optional L0 zoom ROM (first 64 KiB), for the live strip hash. */
    static uint8_t zoom[0x10000];
    {
        const char* zp = argc > 5 ? argv[5] : "roms/000-lo.lo";
        FILE* fz = fopen(zp, "rb");
        if (fz) { size_t zn = fread(zoom, 1, sizeof(zoom), fz); fclose(fz);
                  printf("L0 zoom: %s (%zu bytes)\n", zp, zn); }
        else printf("L0 zoom: %s not found (strip hash skipped)\n", zp);
    }

    /* Live LSPC -> strip decode of the final guest raster state. */
    {
        static uint16_t vram[0x8580];
        static uint16_t pal[4096];
        unsigned anim = 0; int fix_bios = 0;
        uint32_t h = 0;
        uint32_t n = 0xFFFFFFFFu;
        if (zoom[0] || 1) {   /* always attempt; zoom may be zeroed if missing */
            ng_boot_lspc_snapshot(vram, pal, &anim, &fix_bios);
            n = ng_live_sprite_strips(vram, zoom, pal, anim, fix_bios, &h);
        }
        printf("live sprite strips: count=%u hash=%08X anim=%u fix_bios=%d\n", n, h, anim, fix_bios);
        /* Optional: dump the sprite C ROM tile IDs of this frame (big-endian
         * u32 count + ids) for building a tile-residency pack. */
        if (argc > 6) {
            static uint32_t tiles[4096];
            uint32_t nt = ng_live_collect_sprite_tiles(tiles, 4096u);
            FILE* ft = fopen(argv[6], "wb");
            if (!ft) { fprintf(stderr, "cannot write %s\n", argv[6]); return 1; }
            uint32_t cnt = nt;
            fputc((cnt >> 24) & 255, ft); fputc((cnt >> 16) & 255, ft);
            fputc((cnt >> 8) & 255, ft); fputc(cnt & 255, ft);
            for (uint32_t k = 0; k < nt; k++) {
                uint32_t v = tiles[k];
                fputc((v >> 24) & 255, ft); fputc((v >> 16) & 255, ft);
                fputc((v >> 8) & 255, ft); fputc(v & 255, ft);
            }
            fclose(ft);
            printf("live sprite tiles: %u -> %s\n", nt, argv[6]);
        }
    }
    return 0;
}