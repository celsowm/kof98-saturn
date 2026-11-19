#!/usr/bin/env python3
import re
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
D = ROOT / "local" / "decomp"
addrs = set()
for line in (ROOT / "local" / "pruned" / "fresh12k.routines.txt").read_text().splitlines():
    m = re.search(r"_([0-9A-F]{6})$", line.strip())
    if m: addrs.add(int(m.group(1), 16))
s = (D / "all_exec.c.txt").read_text(errors="replace")
found = set(int(m.group(1), 16) for m in re.finditer(r"^==== ([0-9A-F]+) :", s, re.M))
print("boot addrs", len(addrs), "found in full dump", len(addrs & found))
print("missing", ["%06X" % a for a in sorted(addrs - found)])
