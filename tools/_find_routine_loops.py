#!/usr/bin/env python3
"""Find dbra loops in one generated recomp routine (local analysis helper)."""
import re
import sys
from pathlib import Path

fn, routine = sys.argv[1], sys.argv[2]
lines = Path(fn).read_text(errors="replace").splitlines()
start = next(i for i, l in enumerate(lines) if f"void {routine}(void)" in l)
end = next((i for i in range(start + 1, len(lines))
            if re.match(r"void r_[0-9A-F]+\(void\)", lines[i])), start + 600)
seg = lines[start:end]
for i, l in enumerate(seg):
    if "dbra" not in l:
        continue
    m = re.search(r"dbra\s+D\d+,\s*\$?([0-9a-fA-F]+)", l)
    tgt = m.group(1) if m else "?"
    print(f"dbra -> 0x{tgt} at line {start+i+1}")
    for x in seg[max(0, i - 5):i + 1]:
        print("   ", x.strip()[:110])
