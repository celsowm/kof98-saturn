#!/usr/bin/env python3
"""Rank guest routines by aggregated profile hits (hottest first)."""
import csv
import re
from collections import defaultdict
from pathlib import Path

GEN = Path("local/ng_recomp_game256")
FULL = Path("local/ng_recomp_full256")

# addr -> routine
tbl = {}
for gen, tname in ((GEN, "game_recomp_table.c"), (FULL, "recomp_table.c")):
    for line in (gen / tname).read_text(errors="replace").splitlines():
        m = re.search(r"\{0x([0-9A-Fa-f]+)u,\s*((?:game_)?r_[0-9A-Fa-f]+)\}", line)
        if m:
            tbl[int(m.group(1), 16)] = m.group(2)

# routine sizes from generated C
sizes = {}
for gen in (GEN, FULL):
    for f in gen.glob("*.c"):
        if "table" in f.name or "pages" in f.name:
            continue
        for m in re.finditer(r"/\* routine \$([0-9A-Fa-f]+): (\d+) instructions \*/", f.read_text(errors="replace")):
            sizes["r_" + m.group(1)] = int(m.group(2))
            sizes["game_r_" + m.group(1)] = int(m.group(2))

hits = defaultdict(int)
with open("local/profiles/noinput1200.csv", newline="") as f:
    f.readline(); f.readline()
    for line in f:
        p = line.strip().split(",")
        if len(p) >= 2:
            pc = int(p[0], 16)
            if pc in tbl:
                hits[tbl[pc]] += int(p[1])

total = sum(hits.values())
print("total native dispatches (profile):", total)
cum = 0
for i, (r, h) in enumerate(sorted(hits.items(), key=lambda x: -x[1])[:20]):
    cum += h
    print("%2d  %-16s hits=%7d  insns=%4d  cum%%=%5.1f" % (i, r, h, sizes.get(r, -1), 100 * cum / total))
