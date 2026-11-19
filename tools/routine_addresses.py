#!/usr/bin/env python3
"""Print guest addresses for m68krecomp routine names (first decomp milestone)."""
import re
import sys
from pathlib import Path

p = Path(sys.argv[1] if len(sys.argv) > 1 else "local/pruned/fresh12k.routines.txt")
addrs = []
for line in p.read_text().splitlines():
    m = re.search(r"_([0-9A-F]{6})$", line.strip())
    if m:
        addrs.append("0x" + m.group(1))
print(" ".join(addrs))
print(f"# {len(addrs)} addresses", file=sys.stderr)
