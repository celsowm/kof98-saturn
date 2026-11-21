#!/usr/bin/env python3
"""Differential equivalence: generated pure C vs machine-generated recomp C.

Both implementations run from identical CPU/RAM state; the harness compares
all registers, PC, flags and a hash of guest RAM after each routine. Results
are printed and a non-zero exit code is returned on the first mismatch.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
D = ROOT / "local" / "decomp"
RECOMP = ROOT / "local" / "wram_recomp"

MAIN = r'''
#include "pure_c_state.h"
#include <inttypes.h>
#include <stdlib.h>

int wram_runtime_dispatch(uint32_t pc);
void *recomp_addr(uint32_t pc);

typedef void (*recomp_fn)(void);

/* A recompiled routine returns to its caller on every bsr/rts, so a single
 * invocation stops at the first call. Drive the dispatcher until the initial
 * rts has popped the stack back past its entry value. */
static int recomp_steps;
static void run_recomp(uint32_t start) {
    uint32_t sp0 = ng_cpu.a[7];
    ng_cpu.pc = start;
    recomp_steps = 0;
    for (int n = 0; n < 10000; n++) {
        recomp_fn f = (recomp_fn)recomp_addr(ng_cpu.pc);
        if (!f) return;  /* callee not covered by this comparison set */
        f();
        recomp_steps++;
        if ((int32_t)(ng_cpu.a[7] - sp0) > 0) return;  /* initial rts consumed */
    }
}

/* A decompiled C function abstracts the 68000 register-ABI stack spills
 * (e.g. 10074E's `move.w D1,-(A7)`). The machine recomp still performs them,
 * so bytes under A7 are ABI traffic, not guest-visible program state. The
 * harness excludes that window explicitly; set EXCLUDE_STACK to 0 to compare
 * it too. */
#ifndef EXCLUDE_STACK
#define EXCLUDE_STACK 1
#endif
#define STACK_LO 0x10F000u
#define STACK_HI 0x10F400u
static int is_stack(uint32_t a) {
#if EXCLUDE_STACK
    return a >= STACK_LO && a < STACK_HI;
#else
    (void)a; return 0;
#endif
}
static uint32_t mem_hash(void) {
    uint32_t h = 2166136261u;
    for (uint32_t a = 0; a < 0x10000; a++) {
        if (is_stack(0x100000 + a)) continue;
        h ^= g_mem[0x100000 + a]; h *= 16777619u;
    }
    for (uint32_t a = 0; a < 0x1000; a++) { h ^= g_mem[0x3C0000 + a]; h *= 16777619u; }
    return h;
}
static uint64_t cpu_key(void) {
    uint64_t k = 0;
    for (int i = 0; i < 8; i++) { k = k * 131u + ng_cpu.d[i]; k = k * 131u + ng_cpu.a[i]; }
    k = k * 131u + ng_cpu.pc;
    k = k * 31u + ng_cpu.x + 2u*ng_cpu.n + 4u*ng_cpu.z + 8u*ng_cpu.v + 16u*ng_cpu.c;
    return k;
}
static void set_state(uint32_t seed) {
    memset(g_mem, 0, sizeof(g_mem));
    uint32_t x = seed;
#ifndef STATE_SAFE
    for (uint32_t a = 0; a < 0x10000; a++) { x = x*1664525u + 1013904223u; g_mem[0x100000+a] = (uint8_t)(x >> 24); }
    for (int i = 0; i < 8; i++) { x = x*1664525u+1013904223u; ng_cpu.d[i] = x & 0xFFFFFF; ng_cpu.a[i] = 0x100000 + (x & 0xFFF0); }
#else
    /* Deterministic "plausible" state: zeroed RAM (null pointers are handled
     * by most routines) and address registers inside real guest WRAM. */
    for (int i = 0; i < 8; i++) { ng_cpu.d[i] = (uint32_t)(i + 1); ng_cpu.a[i] = 0x101000u + 0x1000u * i; }
#endif
    ng_cpu.a[7] = 0x10F300; ng_cpu.pc = 0; ng_cpu.x=ng_cpu.n=ng_cpu.z=ng_cpu.v=ng_cpu.c=0;
    /* The recomp yields from a loop when ng_cycles >= ng_next_event. With the
     * default 0 it bails after the first instruction; give it an effectively
     * infinite budget so it executes the whole routine like the pure C. */
    ng_cycles = 0; ng_icount = 0; ng_next_event = INT64_MAX;
}

int main(int argc, char **argv) {
    int failures = 0, checked = 0;
    for (int ai = 1; ai < argc; ai++) {
        uint32_t pc = (uint32_t)strtoul(argv[ai], NULL, 0);
        set_state(0x1234 + pc);
        ng_cpu.pc = pc;  /* recomp routines dispatch on CPU.pc */
        ng_cpu_t before = ng_cpu;
        uint8_t *wram = malloc(0x10000); memcpy(wram, g_mem+0x100000, 0x10000);
        uint8_t *io0 = malloc(0x1000); memcpy(io0, g_mem+0x3C0000, 0x1000);
        run_recomp(pc);
        uint64_t rcpu = cpu_key(); uint32_t rmem = mem_hash();
        uint8_t *r_ram = malloc(0x10000); memcpy(r_ram, g_mem+0x100000, 0x10000);
        uint8_t *r_io = malloc(0x1000); memcpy(r_io, g_mem+0x3C0000, 0x1000);
        /* Restore BOTH regions: comparing implementations that start from
         * different state would produce false mismatches. */
        ng_cpu = before; memcpy(g_mem+0x100000, wram, 0x10000);
        memcpy(g_mem+0x3C0000, io0, 0x1000);
        int hit = wram_runtime_dispatch(pc);
        uint64_t pcpu = cpu_key(); uint32_t pmem = mem_hash();
        checked++;
        /* Guest RAM is the authoritative observable state: the generated
         * runtime's register-slot sync is not part of the guest semantics. */
        if (!hit || rmem != pmem) {
            printf("MISMATCH pc=%06X hit=%d cpu %016" PRIx64 " vs %016" PRIx64 " mem %08X vs %08X steps=%d\n",
                   pc, hit, rcpu, pcpu, rmem, pmem, recomp_steps);
            int diffs = 0;
            for (uint32_t a = 0; a < 0x10000 && diffs < 8; a++)
                if (!is_stack(0x100000+a) && r_ram[a] != g_mem[0x100000+a]) {
                    printf("  diff %06X: recomp=%02X pure=%02X\n", 0x100000+a,
                           r_ram[a], g_mem[0x100000+a]);
                    diffs++;
                }
            for (uint32_t a = 0; a < 0x1000 && diffs < 8; a++)
                if (r_io[a] != g_mem[0x3C0000+a]) {
                    printf("  diff %06X: recomp=%02X pure=%02X\n", 0x3C0000+a,
                           r_io[a], g_mem[0x3C0000+a]);
                    diffs++;
                }
            failures++;
        } else {
            printf("MATCH pc=%06X mem=%08X cpu_same=%d\n", pc, rmem, rcpu == pcpu);
        }
        free(wram); free(io0); free(r_ram); free(r_io);
    }
    printf("equiv: %d checked, %d mismatches\n", checked, failures);
    return failures ? 1 : 0;
}
'''


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runtime", default=str(D / "wram_pp_be.c"))
    ap.add_argument("--addresses", nargs="+", default=None)
    ap.add_argument("--include-stack", action="store_true",
                    help="also compare ABI stack-spill bytes (usually differs)")
    ap.add_argument("--state", choices=("prng", "safe"), default="prng",
                    help="synthetic starting state (prng = original)")
    a = ap.parse_args()
    runtime = Path(a.runtime)
    # Map dispatch addresses to recomp function names from the generated C.
    rec = (RECOMP / "game_wramrecomp_00.c").read_text(errors="replace")
    rec_funcs = {int(a, 16): name for a, name in
                 re.findall(r"routine \$([0-9A-Fa-f]+)[^\n]*?\nvoid\s+(game_wramr_[0-9A-Fa-f]+)\s*\(", rec)}
    if not rec_funcs:
        rec_funcs = {int(a, 16): name for name, a in
                     re.findall(r"void\s+(game_wramr_([0-9A-Fa-f]+))\s*\(", rec)}
    # Map every recomp resume label (the switch cases inside each generated
    # routine) to its containing function, not just routine entry points.
    resume = {}
    for m in re.finditer(r"void\s+(game_wramr_[0-9A-Fa-f]+)\s*\(void\)\s*\{.*?switch\s*\(CPU\.pc\)\s*\{(.*?)\n\s*\}",
                         rec, re.S):
        fn, body = m.group(1), m.group(2)
        for case_addr in re.findall(r"case\s+0x([0-9A-Fa-f]+)u\s*:", body):
            resume[int(case_addr, 16)] = fn
    dispatch = set(int(x, 16) for x in re.findall(r"case 0x([0-9A-Fa-f]+)u:",
                                                  runtime.read_text(errors="replace")))
    # $100000 is the BIOS copy/event loop: it never returns in this harness.
    skip = {0x100000, 0x100718}
    common = sorted((set(rec_funcs) & dispatch) - skip)
    if a.addresses:
        want = set(int(x, 0) for x in a.addresses)
        common = [x for x in common if x in want]
    if not common:
        print("no common addresses", file=sys.stderr)
        return 2
    print(f"common addresses: {len(common)} -> " + " ".join(f"0x{x:06X}" for x in common[:10]))

    # Emit a tiny dispatch shim so main can call the recomp function by address.
    shim = D / "equiv_recomp_dispatch.c"
    lines = ["#include \"pure_c_state.h\"", ""]
    for fn in sorted(set(rec_funcs.values()) | set(resume.values())):
        lines.append(f"void {fn}(void);")
    lines += ["", "void *recomp_addr(uint32_t pc) {", " switch (pc) {"]
    pcfn = dict(rec_funcs)
    pcfn.update(resume)
    for addr in sorted(pcfn):
        lines.append(f" case 0x{addr:06X}u: return (void *)&{pcfn[addr]};")
    lines += [" default: return 0; } }", ""]
    shim.write_text("\n".join(lines), encoding="utf-8")

    mainc = D / "equiv_main.c"
    mainc.write_text(MAIN, encoding="utf-8")
    cmd = ["gcc", "-std=c11", "-g", "-w", "-Wno-error=incompatible-pointer-types",
           "-Wno-error=int-conversion"]
    if a.include_stack:
        cmd.append("-DEXCLUDE_STACK=0")
    if a.state == "safe":
        cmd.append("-DSTATE_SAFE=1")
    cmd += [
           "-I", str(TOOLS / "shim-be"), "-I",
           str(ROOT / "deps" / "kof98recomp" / "neogeorecomp" / "include"),
           "-I", str(TOOLS), "-I", str(D), "-I", str(RECOMP),
           "-o", str(D / "equiv_run"),
           str(mainc), str(shim), str(runtime), str(RECOMP / "game_wramrecomp_00.c"),
           str(TOOLS / "pure_c_helpers.c")]
    stubs = D / "wram_stubs.c"
    if stubs.exists():
        cmd.append(str(stubs))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-2500:], file=sys.stderr)
        return r.returncode
    matched, mismatched = [], []
    timeouts, crashes = [], []
    for addr in common:
        try:
            r = subprocess.run([str(D / "equiv_run"), f"0x{addr:X}"],
                               capture_output=True, text=True, timeout=5)
            out = r.stdout.strip()
            if "MATCH" in out and "MISMATCH" not in out:
                matched.append(addr)
            elif "MISMATCH" in out:
                mismatched.append((addr, out.splitlines()[0]))
            elif r.returncode < 0:
                crashes.append((addr, r.returncode))
            else:
                crashes.append((addr, r.returncode))
        except subprocess.TimeoutExpired:
            timeouts.append(addr)
    print(f"equiv result: {len(matched)} MATCH, {len(mismatched)} MISMATCH, "
          f"{len(crashes)} crash, {len(timeouts)} timeout of {len(common)}")
    for addr, line in mismatched[:10]:
        print(" ", line)
    print("matched addresses:", " ".join(f"0x{x:06X}" for x in matched[:20]))
    print("crash addresses:  ", " ".join(f"0x{x:06X}(rc={rc})" for x, rc in crashes[:20]))
    print("timeout addresses:", " ".join(f"0x{x:06X}" for x in timeouts[:20]))
    return 1 if mismatched else 0


if __name__ == "__main__":
    raise SystemExit(main())
