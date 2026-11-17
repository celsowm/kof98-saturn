/* ROM-free regression for the ng_boot slice hook (services inside run_frames).
 *
 * Boots the core from a synthetic BIOS program (vector page + an endless
 * addq/bra loop in the BIOS window), then checks three things:
 *   1. a hook that only counts calls does not change guest timing or state:
 *      cycles, instructions, last PC, frames and vblanks match a no-hook run
 *      exactly, and the hook fires ~once per 4000 guest cycles;
 *   2. the slice gating: with a 40000-cycle slice the hook fires ~5x/frame;
 *   3. slice 0 means "every dispatch chunk" (~264 lines + events per frame).
 * No game ROM, system BIOS or generated data is involved.
 */
#include "ng_boot.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)

#define FRAMES 5u
#define FRAME_CYCLES 202752u                 /* 264 lines * 768 cycles */

static uint8_t bios[0x20000];

static void build_bios(void) {
    memset(bios, 0x4E, sizeof bios);          /* NOP fill: 0x4E71 words */
    for (uint32_t i = 0; i + 1 < sizeof bios; i += 2) { bios[i] = 0x4E; bios[i + 1] = 0x71; }
    /* Reset vector page: SSP $0010F300, PC $00C00400 (inside the BIOS window). */
    bios[0] = 0x00; bios[1] = 0x10; bios[2] = 0xF3; bios[3] = 0x00;
    bios[4] = 0x00; bios[5] = 0xC0; bios[6] = 0x04; bios[7] = 0x00;
    /* $C00400: move.w #0,D0 ; loop: addq.w #1,D0 ; bra.s loop */
    bios[0x400] = 0x30; bios[0x401] = 0x3C; bios[0x402] = 0x00; bios[0x403] = 0x00;
    bios[0x404] = 0x52; bios[0x405] = 0x40;
    bios[0x406] = 0x60; bios[0x407] = 0xFC;
}

struct state { uint32_t cycles, instr, last_pc, frames, vblanks, io; };
static struct state run_frames(void) {
    /* The probe counters accumulate by design; zero them so each run starts
     * from a known state (ng_cycles is reset by ng_boot_init itself). */
    ng_boot_instructions = 0;
    ng_boot_frames = 0;
    ng_boot_vblanks = 0;
    ng_boot_io_writes = 0;
    ng_boot_last_pc = 0;
    struct state s;
    ng_boot_run_frames(FRAMES);
    s.cycles = ng_boot_cycles_lo();
    s.instr = ng_boot_instructions;
    s.last_pc = ng_boot_last_pc;
    s.frames = ng_boot_frames;
    s.vblanks = ng_boot_vblanks;
    s.io = ng_boot_io_writes;
    return s;
}
static int same(struct state a, struct state b) {
    return a.cycles == b.cycles && a.instr == b.instr && a.last_pc == b.last_pc &&
           a.frames == b.frames && a.vblanks == b.vblanks && a.io == b.io;
}

static volatile uint32_t slice_calls;
static void count_hook(void) { slice_calls++; }

int main(void) {
    build_bios();
    ng_boot_set_cart(0, 0, 0, 0);

    /* Reference run: no hook. */
    ng_boot_init(bios, sizeof bios);
    struct state ref = run_frames();
    CHECK(ref.frames == FRAMES);
    CHECK(ref.vblanks == FRAMES);             /* IRQ1 raised each frame */
    CHECK(ref.last_pc >= 0x00C00400u && ref.last_pc <= 0x00C00408u);
    CHECK(ref.cycles != 0u);

    /* Hook that only counts: guest state and timing must be untouched. */
    slice_calls = 0;
    ng_boot_init(bios, sizeof bios);
    ng_boot_set_slice_hook(count_hook, 4000u);
    struct state with_hook = run_frames();
    CHECK(same(ref, with_hook));
    /* 202752/4000 = 50.7 fires per frame. */
    CHECK(slice_calls >= FRAMES * 40u && slice_calls <= FRAMES * 62u);
    uint32_t calls_4000 = slice_calls;

    /* Coarser slice: ~5 fires per frame. */
    slice_calls = 0;
    ng_boot_init(bios, sizeof bios);
    ng_boot_set_slice_hook(count_hook, 40000u);
    struct state coarse = run_frames();
    CHECK(same(ref, coarse));
    CHECK(slice_calls >= FRAMES * 3u && slice_calls <= FRAMES * 8u);
    uint32_t calls_40000 = slice_calls;

    /* Slice 0: every dispatch chunk (~one 768-cycle line each). */
    slice_calls = 0;
    ng_boot_init(bios, sizeof bios);
    ng_boot_set_slice_hook(count_hook, 0u);
    struct state per_chunk = run_frames();
    CHECK(same(ref, per_chunk));
    CHECK(slice_calls >= FRAMES * 264u && slice_calls <= FRAMES * 340u);
    uint32_t calls_chunk = slice_calls;

    /* A NULL hook clears the mechanism. */
    ng_boot_set_slice_hook(0, 4000u);
    slice_calls = 0;
    ng_boot_init(bios, sizeof bios);
    struct state cleared = run_frames();
    CHECK(same(ref, cleared));
    CHECK(slice_calls == 0u);

    printf("ng_boot_slice: ok (ref cycles=%u instr=%u slice4000=%u slice40000=%u slice0=%u)\n",
           ref.cycles, ref.instr, calls_4000, calls_40000, calls_chunk);
    return 0;
}
