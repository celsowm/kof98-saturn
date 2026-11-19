/* Small driver proving the generated pure-C runtime actually executes.
 * Local build artifact, game-derived. */
#include "pure_c_state.h"
#include <stdlib.h>

#include "pure_c_runtime_binding.h"

int main(int argc, char **argv) {
    uint32_t pc = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0) : 0x100000u;
    int steps = (argc > 2) ? atoi(argv[2]) : 1;
    memset(g_mem, 0, sizeof(g_mem));
    memset(&ng_cpu, 0, sizeof(ng_cpu));
    ng_cpu.a[7] = 0x10F300u;
    ng_cpu.a[4] = ng_cpu.a[5] = ng_cpu.a[6] = 0x100000u;
    ng_cpu.pc = pc;
    int hits = 0;
    for (int i = 0; i < steps; i++) {
        printf("  step %d: pc=%06X A4=%08X A5=%08X\n", i, pc, ng_cpu.a[4], ng_cpu.a[5]);
        fflush(stdout);
        if (!GUEST_RUNTIME_DISPATCH(pc)) { printf("MISS pc=%06X at step %d\n", pc, i); break; }
        hits++;
    }
    uint32_t sum = 0;
    for (uint32_t a = 0x100000; a < 0x101000; a++) sum = sum * 33u + g_mem[a];
    printf("purec_run pc=%06X steps=%d hits=%d wram_hash=%08X\n", pc, steps, hits, sum);
    return hits ? 0 : 1;
}
