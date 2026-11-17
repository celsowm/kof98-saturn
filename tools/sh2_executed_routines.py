#!/usr/bin/env python3
"""Derive the set of native guest routines actually executed by an SH-2 run.

Input : a Ymir ``--debug-profile`` PC CSV (columns ``pc,instructions,...``)
        plus the matching SH-2 ELF (for symbol sizes/addresses).
Output: a sorted list of routine names and a per-routine instruction histogram.

This is evidence tooling only. It reads local profiles/ELFs and writes local
artifacts under ``local/pruned/``; nothing here is game-derived asset content
and nothing is committed. The routine *names* are stable across rebuilds (they
are the recompiler's PC-derived symbols), so a list produced from an earlier
ELF can be re-validated against a fresh profile.
"""
import argparse
import bisect
import collections
import csv
import re
import subprocess
import shutil
from pathlib import Path

ROUTINE = re.compile(r"^_?(?:dense_r_|r_|bios_r_|game_r_|wram_r_)")


def find_nm() -> str:
    nm = shutil.which("sh2eb-elf-nm")
    if nm:
        return nm
    for cand in (
        Path("C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-nm.exe"),
    ):
        if cand.is_file():
            return str(cand)
    raise SystemExit("sh2eb-elf-nm not found")


def load_symbols(elf: Path):
    out = subprocess.run([find_nm(), "-S", "--defined-only", str(elf)],
                         text=True, capture_output=True, check=True).stdout
    syms = []
    for m in re.finditer(r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[TtWw]\s+(\S+)", out):
        syms.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
    syms.sort()
    return syms, [s[0] for s in syms]


def resolver(syms, addrs):
    def name(pc: int) -> str:
        i = bisect.bisect_right(addrs, pc) - 1
        if i >= 0 and syms[i][0] <= pc < syms[i][0] + max(1, syms[i][1]):
            return syms[i][2]
        return "?"
    return name


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--profile", required=True, type=Path, help="Ymir debug-profile PC CSV")
    ap.add_argument("--elf", required=True, type=Path, help="matching SH-2 ELF")
    ap.add_argument("--out", required=True, type=Path, help="output directory (local)")
    ap.add_argument("--label", default="run", help="label for the manifest")
    a = ap.parse_args()

    syms, addrs = load_symbols(a.elf)
    resolve = resolver(syms, addrs)
    counts = collections.Counter()
    total = 0
    with a.profile.open(newline="") as fh:
        for row in csv.DictReader(fh):
            # Ymir debug-profile PCs are decimal (e.g. 100679680 = 0x06004000).
            pc = int(row.get("pc", "0"), 10)
            insn = int(row.get("instructions", row.get("count", "1")))
            total += insn
            n = resolve(pc)
            if ROUTINE.match(n):
                counts[n.lstrip("_")] += insn

    sizes = {n.lstrip("_") + ("_" if n.startswith("_") else ""): s
             for addr, s, n in syms if ROUTINE.match(n)}
    # canonical key = strip any single leading underscore
    canon = {}
    for addr, s, n in syms:
        if ROUTINE.match(n):
            canon[n.lstrip("_")] = s

    used = sorted(n for n, c in counts.items() if c > 0)
    used_bytes = sum(canon.get(n, 0) for n in used)
    all_bytes = sum(canon.values())

    a.out.mkdir(parents=True, exist_ok=True)
    list_path = a.out / f"{a.label}.routines.txt"
    list_path.write_text("\n".join(used) + "\n")
    hist_path = a.out / f"{a.label}.hist.csv"
    with hist_path.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["routine", "instructions", "bytes"])
        for n in sorted(used, key=lambda x: -counts[x]):
            w.writerow([n, counts[n], canon.get(n, 0)])

    print(f"[{a.label}] total SH-2 instructions traced: {total}")
    print(f"[{a.label}] executed native routines: {len(used)} "
          f"({used_bytes} B) of {len(canon)} linked ({all_bytes} B)")
    print(f"[{a.label}] wrote {list_path} and {hist_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
