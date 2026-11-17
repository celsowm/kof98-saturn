#!/usr/bin/env python3
"""Analyze the resident dispatch map composition (local helper)."""
import re
from collections import Counter

m = open("local/ymir/resident_map.c").read()
entries = re.findall(r"\{0x([0-9A-F]{6}), (\w+)\}", m)
print("total resident map entries:", len(entries))
bios = [(p, f) for p, f in entries if p.startswith("C")]
game = [(p, f) for p, f in entries if not p.startswith("C")]
print("BIOS-address entries (0xCxxxxx):", len(bios))
print("game-address entries:", len(game))
print("BIOS entry funcs:", Counter(f for p, f in bios).most_common(8))
print("game entry funcs:", Counter(f for p, f in game).most_common(8))

tbl = open("build-ng/ng_bench_gen/bios_recomp_table.c").read()
for pc in ("C11F78", "C16D20", "C11002", "C11050", "C110E8"):
    print(f"{pc} in bios table entries:", len(re.findall(r"0x" + pc + r"u", tbl)))
