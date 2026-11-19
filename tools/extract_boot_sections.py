#!/usr/bin/env python3
"""Extract the 94-routine boot set from the full executed-set Ghidra dump.

Keeps every deliverable sourced from the byte-order-corrected image.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
D = ROOT / "local" / "decomp"
addrs = set()
for line in (ROOT / "local" / "pruned" / "fresh12k.routines.txt").read_text().splitlines():
    m = re.search(r"_([0-9A-F]{6})$", line.strip())
    if m:
        addrs.add(int(m.group(1), 16))
s = (D / "all_exec.c.txt").read_text(errors="replace")
sections = re.split(r"(?=^==== [0-9A-F]+ : )", s, flags=re.M)
kept = [sec for sec in sections
        if (m := re.match(r"==== ([0-9A-F]+) :", sec)) and int(m.group(1), 16) in addrs]
(D / "hot94b.c.txt").write_text("".join(kept), encoding="utf-8")
print(f"boot sections: {len(kept)} of {len(addrs)} addresses")
