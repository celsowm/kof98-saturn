#!/usr/bin/env python3
"""One-shot SRP extraction of the stats counters out of ng_boot.c (local)."""
from pathlib import Path

p = Path("src/ng/ng_boot.c")
s = p.read_text()

start = s.index("/* ---- stats ---- */\n")
end = s.index("\n#ifndef NG_BOOT_RECOMP_ONLY", start)
block = s[start:end]

module = (
    "/* ng_boot_stats.c - probe-visible emulated-device counters.\n"
    " * Extracted from ng_boot.c (SRP): counting is a separate responsibility\n"
    " * from emulating the hardware. All counters are declared in ng_boot.h.\n"
    " */\n"
    "#include \"ng_boot.h\"\n\n"
    + block.rstrip() + "\n"
)
Path("src/ng/ng_boot_stats.c").write_text(module)

s = s[:start] + s[end + 1:]
p.write_text(s)
print("extracted stats block:", len(block), "bytes")
print("module:\n", module)
