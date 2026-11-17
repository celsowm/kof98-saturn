import csv
import re
from collections import defaultdict
from pathlib import Path

h = {}
with open("local/profiles/noinput1200.csv", newline="") as f:
    f.readline(); f.readline()
    for line in f:
        p = line.strip().split(",")
        if len(p) >= 2:
            h[int(p[0], 16)] = int(p[1])
tbl = {}
for gen, t in ((Path("local/ng_recomp_game256"), "game_recomp_table.c"),
               (Path("local/ng_recomp_full256"), "recomp_table.c")):
    for line in (gen / t).read_text(errors="replace").splitlines():
        m = re.search(r"\{0x([0-9A-Fa-f]+)u,\s*((?:game_)?r_[0-9A-Fa-f]+)\}", line)
        if m:
            tbl[int(m.group(1), 16)] = m.group(2)
hits = defaultdict(int)
for pc, v in h.items():
    if pc in tbl:
        hits[tbl[pc]] += v
sizes = {}
for gen in (Path("local/ng_recomp_game256"), Path("local/ng_recomp_full256")):
    for f in gen.glob("*.c"):
        if "table" in f.name or "pages" in f.name:
            continue
        for m in re.finditer(r"/\* routine \$([0-9A-Fa-f]+): (\d+) instructions \*/", f.read_text(errors="replace")):
            sizes["r_" + m.group(1)] = int(m.group(2))
            sizes["game_r_" + m.group(1)] = int(m.group(2))
total = sum(sizes.get(r, 0) for r in hits)
low = sum(sizes.get(r, 0) for r, v in hits.items() if v < 10)
print("total guest insns in blob:", total, "from routines with <10 hits:", low,
      "(%.0f%%)" % (100 * low / total))
