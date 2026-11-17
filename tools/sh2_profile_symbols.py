#!/usr/bin/env python3
"""Aggregate a Ymir SH-2 debug-instruction profile by ELF symbol.

Ymir's ``--debug-profile`` emits a CSV with one row per sampled SH-2 PC:
``cpu,pc,instructions,execution_cycles``.  The raw PCs are SH-2 addresses
(e.g. 0x0600c9b4), so this tool maps each PC to the nearest symbol in the
bench ELF via ``sh2eb-elf-nm -n`` and sums ``instructions`` per symbol.  This
is the authoritative, emulator-side hotspot view used to drive the dense
emitter (the host profile does not match what the SH-2 actually executes).

Usage:
    python tools/sh2_profile_symbols.py local/ymir/prof/dbg.pc.csv \
        build-ng/kof_resident_bench.elf [--top 20] [--nm PATH]

The default ``--nm`` search mirrors scripts/kof_ymir_bench.py: PATH, then the
MSYS2 saturn-tools install under the user's home.
"""
import argparse
import bisect
import csv
import os
import re
import shutil
import subprocess
from collections import defaultdict
from pathlib import Path


def find_nm(explicit: str | None) -> str:
    if explicit:
        return explicit
    nm = shutil.which("sh2eb-elf-nm")
    if nm:
        return nm
    if os.name == "nt":
        msys = Path(os.getenv("LIBSATURN_MSYS2_ROOT", r"C:\msys64"))
        cand = msys / "home" / Path.home().name / "saturn-tools/bin/sh2eb-elf-nm.exe"
        if cand.is_file():
            return str(cand)
    raise SystemExit("sh2eb-elf-nm not found; pass --nm PATH")


def load_symbols(elf: Path, nm: str):
    out = subprocess.run([nm, "-n", str(elf)], text=True, capture_output=True,
                         check=True).stdout
    syms = []
    for line in out.splitlines():
        m = re.match(r"([0-9a-fA-F]+)\s+\w\s+(\S+)", line)
        if m:
            syms.append((int(m.group(1), 16), m.group(2)))
    syms.sort()
    return [a for a, _ in syms], syms


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("profile", type=Path, help="Ymir --debug-profile CSV")
    ap.add_argument("elf", type=Path, help="bench ELF to resolve symbols in")
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--nm", default=None)
    a = ap.parse_args()

    addrs, syms = load_symbols(a.elf, find_nm(a.nm))
    by_pc = defaultdict(int)
    total = 0
    with a.profile.open(newline="") as f:
        for row in csv.DictReader(f):
            try:
                pc = int(row["pc"])
                n = int(row.get("instructions", 1))
            except (KeyError, TypeError, ValueError):
                continue
            by_pc[pc] += n
            total += n

    by_sym = defaultdict(int)
    for pc, n in by_pc.items():
        i = bisect.bisect_right(addrs, pc) - 1
        name = syms[i][1] if i >= 0 else "?"
        by_sym[name] += n

    print(f"total SH-2 instructions traced: {total}")
    print(f"--- top {a.top} SH-2 symbols by instructions ---")
    for name, n in sorted(by_sym.items(), key=lambda x: -x[1])[:a.top]:
        print(f"  {name:<38} {n:>10}  ({100.0 * n / total:5.1f}%)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
