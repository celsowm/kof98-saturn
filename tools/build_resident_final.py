#!/usr/bin/env python3
"""One-shot: regenerate the resident blob and map with all extras included."""
import os
import subprocess
import sys
from pathlib import Path

# regenerate base blob from profile tables (RESIDENT_MIN_HITS trims low-hit routines)
env = dict(os.environ)
subprocess.run([sys.executable, "tools/make_resident_pack.py"], check=True, env=env)
# add runtime-discovered PCs' routines (may not be in profile)
extra = [int(x, 16) for x in Path("local/ymir/resident_extra.txt").read_text().split()]
subprocess.run([sys.executable, "tools/add_resident_pc.py"] + ["0x%X" % p for p in extra], check=True)
# WRAM routine (special recompilation of runtime-copied code) - use unique name
wram = Path("local/ymir/wram_routine.txt")
if wram.exists():
    p = Path("local/ymir/resident_routines.c")
    s = p.read_text()
    body = wram.read_text()
    if "void wram_r_100000(void)" not in s:
        # rename to avoid clashing with game_r_100000 from cart recomp
        s += "\n\n" + body.replace("game_r_100000", "wram_r_100000") + "\n"
        p.write_text(s)
        print("added wram_r_100000")
# regenerate map (reads extra list + present routines)
subprocess.run([sys.executable, "tools/make_resident_map.py"], check=True)
print("resident blob + map ready")
