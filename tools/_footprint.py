import csv
import re
from pathlib import Path

pc_hits = {}
with open("local/profiles/noinput1200.csv", newline="") as f:
    f.readline()
    f.readline()
    for line in f:
        p = line.strip().split(",")
        if len(p) >= 2:
            pc_hits[int(p[0], 16)] = int(p[1])

tbl = {}
for line in Path("local/ng_recomp_game256/game_recomp_table.c").read_text(errors="replace").splitlines():
    m = re.search(r"\{0x([0-9A-Fa-f]+)u,\s*(game_r_[0-9A-Fa-f]+)\}", line)
    if m:
        tbl[int(m.group(1), 16)] = m.group(2)

rins = {}
for f in Path("local/ng_recomp_game256").glob("game_recomp_*.c"):
    if "table" in f.name:
        continue
    for m in re.finditer(r"/\* routine \$([0-9A-Fa-f]+): (\d+) instructions \*/", f.read_text(errors="replace")):
        rins["game_r_" + m.group(1)] = int(m.group(2))

hit_routines = set()
for pc in pc_hits:
    if pc in tbl:
        hit_routines.add(tbl[pc])
total = sum(rins.get(r, 0) for r in hit_routines)
print("distinct guest PCs hit:", len(pc_hits), "-> distinct routines:", len(hit_routines))
print("total guest instructions in hit routines:", total)
print("at 2 B/instr dense emitter: %.0f KB" % (total * 2 / 1024))
print("at 4 B/instr: %.0f KB" % (total * 4 / 1024))
print("fits in 4 MiB at 4 B/instr?", total * 4 < 4 * 1024 * 1024)
