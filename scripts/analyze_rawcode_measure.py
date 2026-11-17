#!/usr/bin/env python3
"""Summarize a rawcode Ymir measurement and check acceptance criteria."""
import json
import sys
from pathlib import Path

p = Path(sys.argv[1])
j = json.loads(p.read_text())
r = j["targets"]["rawcode"]
print("=== rawcode native-routine measurement ===")
print("guest frames          :", r["guest_frames"])
print("emulated frames       :", r["emulated_fields"])
print("speed ratio           :", r["guest_frames_per_emulated_frame"])
print("native blocks         :", r["ng_boot_recomp_blocks"])
print("game page loads       :", r["ng_boot_game_loads"])
print("game page faults      :", r["ng_boot_game_faults"])
print("game errors           :", r["ng_boot_game_errors"])
print("dispatch misses (bad) :", r["ng_boot_recomp_bad"])
print("last guest PC         : 0x%X" % r["ng_boot_last_pc"])
print("SH2 cycles/frame      :", r["sh2_cycles_per_emulated_frame"])
bad = r["ng_boot_recomp_bad"]
loads = r["ng_boot_game_loads"]
blocks = r["ng_boot_recomp_blocks"]
errs = r["ng_boot_game_errors"]
ok = bad == 0 and loads > 0 and blocks > 100 and errs == 0
print("ACCEPT (bad=0, loads>0, blocks>100, errors=0):", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
