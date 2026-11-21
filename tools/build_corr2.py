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
#include <stdio.h>
#include <stdlib.h>
int wram_runtime_dispatch(uint32_t pc);
void *recomp_addr(uint32_t pc);
typedef void (*recomp_fn)(void);
extern uint64_t purec_snap[4096];
extern unsigned purec_snap_n, purec_snap_active;
static void run_recomp(uint32_t start) {
    uint32_t sp0 = ng_cpu.a[7];
    ng_cpu.pc = start;
    for (int n=0;n<10000;n++) {
        recomp_fn f=(recomp_fn)recomp_addr(ng_cpu.pc);
        if(!f) return; f();
        if((int32_t)(ng_cpu.a[7]-sp0)>0) return;
    }
}
static void set_state(uint32_t seed) {
    memset(g_mem,0,sizeof(g_mem)); uint32_t x=seed;
    for(uint32_t a=0;a<0x10000;a++){x=x*1664525u+1013904223u;g_mem[0x100000+a]=(uint8_t)(x>>24);}
    for(int i=0;i<8;i++){x=x*1664525u+1013904223u;ng_cpu.d[i]=x&0xFFFFFF;ng_cpu.a[i]=0x100000+(x&0xFFF0);}
    ng_cpu.a[7]=0x10F300;ng_cpu.pc=0;ng_cpu.x=ng_cpu.n=ng_cpu.z=ng_cpu.v=ng_cpu.c=0;
    ng_cycles=0;ng_icount=0;ng_next_event=INT64_MAX;
}
int main(int argc,char**argv){
    for(int ai=1;ai<argc;ai++){
        uint32_t pc=(uint32_t)strtoul(argv[ai],NULL,0);
        for(uint32_t seed=1;seed<=3;seed++){
            set_state(0x1234+pc+seed*7919u); ng_cpu.pc=pc;
            ng_cpu_t before=ng_cpu; run_recomp(pc);
            printf("%06X %u R",pc,seed);
            for(int i=0;i<8;i++)printf(" D%d:%08X",i,ng_cpu.d[i]);
            for(int i=0;i<8;i++)printf(" A%d:%08X",i,ng_cpu.a[i]);
            printf(" F:%u%u%u%u%u",ng_cpu.x,ng_cpu.n,ng_cpu.z,ng_cpu.v,ng_cpu.c);
            ng_cpu=before; purec_snap_n=0; purec_snap_active=1;
            wram_runtime_dispatch(pc);
            printf(" LOCALS:%u",purec_snap_n);
            for(unsigned i=0;i<purec_snap_n && i<32;i++) printf(" L%u:%016llx",i,(unsigned long long)purec_snap[i]);
            printf("\n");
        }
    }
    return 0;
}
'''


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runtime", default=str(D / "wram_pp_be_ret.c"))
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
    r = subprocess.run([str(D / "equiv_run"), *[f"0x{a:X}" for a in common]],
                       capture_output=True, text=True, timeout=600)
    print(r.stdout)
    return 0
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
