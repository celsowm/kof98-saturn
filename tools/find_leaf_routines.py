#!/usr/bin/env python3
import re
from pathlib import Path
s = (Path(__file__).resolve().parents[1] / "local" / "decomp" / "wram92_clean.c").read_text(errors="replace")
sections = re.split(r"(?=^/\* [0-9A-F]+ : )", s, flags=re.M)
out = []
for sec in sections:
    h = re.match(r"/\* ([0-9A-F]+) : \w+_fn_([0-9a-fA-F]+) \*/", sec)
    if not h:
        continue
    body = sec[sec.find("{") + 1:sec.rfind("}")]
    calls = re.findall(r"\bwram_fn_[0-9a-fA-F]+\s*\(", body)
    if not calls and "while" not in body and "goto" not in body:
        out.append((int(h.group(1), 16), len(body)))
for addr, size in sorted(out, key=lambda x: x[1])[:12]:
    print(f"0x{addr:06X} body={size}")
