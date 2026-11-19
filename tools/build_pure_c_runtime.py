#!/usr/bin/env python3
"""Build a runnable pure-C guest runtime from a cleaned Ghidra translation unit.

Pipeline (all outputs under ignored local/decomp/):
 1. make_pure_c_runtime.py generates bindings + dispatch table;
 2. gcc compiles it, reporting called-only routines with no translated body;
 3. stubs are generated for those routines so the executable links;
 4. a smoke test executes a leaf guest routine and checks it mutates RAM.

This proves the decompiled C is not just compilable but executable. It is NOT
yet a playable game: hardware I/O, boot state and frame output still need to be
wired to a real Neo Geo bus model.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
D = ROOT / "local" / "decomp"


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--clean", default=str(D / "wram92_clean.c"))
    ap.add_argument("--header", default=str(D / "wram92_clean.h"))
    ap.add_argument("--prefix", default="wram")
    ap.add_argument("--pc", default="0x100130")
    ap.add_argument("--exe", default=str(D / "purec_run"))
    a = ap.parse_args()

    clean, header = Path(a.clean), Path(a.header)
    runtime = D / f"{a.prefix}_runtime.c"
    stubs = D / f"{a.prefix}_stubs.c"
    exe = Path(a.exe)

    r = run([sys.executable, str(TOOLS / "make_pure_c_runtime.py"),
             str(clean), str(header), str(runtime), "--prefix", a.prefix])
    print(r.stdout.strip())
    if r.returncode:
        print(r.stderr, file=sys.stderr)
        return r.returncode

    base = [str(runtime), str(TOOLS / "pure_c_helpers.c"), str(TOOLS / "pure_c_main.c")]
    common = ["-std=c11", "-Wno-error=incompatible-pointer-types",
              "-Wno-error=int-conversion", "-w",
              "-I", str(TOOLS / "shim"),
              "-I", str(ROOT / "deps" / "kof98recomp" / "neogeorecomp" / "include"),
              "-I", str(TOOLS), "-I", str(D)]
    r = run(["gcc", *common, "-o", str(exe), *base])
    if r.returncode:
        missing = sorted(set(re.findall(r"undefined reference to `([^']+)'", r.stderr)))
        if not missing:
            print(r.stderr[-2000:], file=sys.stderr)
            return r.returncode
        lines = ["/* Auto-generated stubs for called-only guest routines. Local only. */",
                 '#include "pure_c_state.h"', ""]
        for sym in missing:
            if "dispatch" in sym:
                lines.append(f"int {sym}(void) {{ return 0; }}")
            elif sym.startswith(a.prefix + "_ptrfn_"):
                lines.append(f"uintptr_t {sym};")
            else:
                lines.append(f"uint32_t {sym}() {{ return 0; }}")
        stubs.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"generated {len(missing)} stubs -> {stubs.name}")
        r = run(["gcc", *common, "-o", str(exe), *base, str(stubs)])
        if r.returncode:
            print(r.stderr[-2000:], file=sys.stderr)
            return r.returncode
    elif stubs.exists():
        # A prior build may have generated stubs for routines that are now
        # translated. Leaving them around would cause duplicate definitions
        # (and silently re-mask the very calls we just implemented).
        stubs.unlink()
        print("removed stale stub file")

    r = run([str(exe), a.pc, "1"])
    print(r.stdout.strip())
    if "purec_run" in r.stdout and "hits=1" in r.stdout:
        print(f"SMOKE PASS: {a.pc} executed natively from decompiled C")
        return 0
    print("SMOKE FAIL", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
