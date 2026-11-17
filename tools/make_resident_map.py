#!/usr/bin/env python3
"""Emit the resident addr->function-pointer binary-search map (proven correct)."""
import os
import re
from pathlib import Path

GEN = Path("local/ng_recomp_game256")
FULL = Path("local/ng_recomp_full256")


def main():
    pcs = set()
    min_hits = int(os.environ.get("RESIDENT_MIN_HITS", "1"))
    with open("local/profiles/noinput1200.csv", newline="") as f:
        f.readline(); f.readline()
        for line in f:
            p = line.strip().split(",")
            if len(p) >= 2 and int(p[1]) >= min_hits:
                pcs.add(int(p[0], 16))
    for forced in [int(x, 16) for x in Path("local/ymir/resident_extra.txt").read_text().split()]:
        pcs.add(forced)
    tbl = {}
    for gen, tname in ((GEN, "game_recomp_table.c"), (FULL, "recomp_table.c")):
        for line in (gen / tname).read_text(errors="replace").splitlines():
            m = re.search(r"\{0x([0-9A-Fa-f]+)u,\s*((?:game_|wram_)?r_[0-9A-Fa-f]+)\}", line)
            if m:
                tbl[int(m.group(1), 16)] = m.group(2)
    blob = Path("local/ymir/resident_routines.c").read_text()
    present = set(re.findall(r"void ((?:game_|wram_)?r_[0-9A-F]+)\(void\) \{", blob))
    if "wram_r_100000" in present:
        for pc in (0x100000, 0x100004, 0x100006, 0x10000A, 0x10000E):
            tbl[pc] = "wram_r_100000"
    if Path("src/ng/dense_routines.c").exists():
        tbl[0x00A142] = "dense_r_00A142"
        present.add("dense_r_00A142")
        # Ymir SH-2 profile: r_0034EA is 29.8% of all instructions (bulk
        # clr.l (A0)+ clear loop). Route it to the dense register-keeping
        # handler in dense_routines.c.
        tbl[0x0034EA] = "dense_r_0034EA"
        present.add("dense_r_0034EA")
        # r_C110FC's hot VRAM-upload loop (Ymir profile 14.9%). Only the
        # loop-head PC C110E8 is routed here; the routine's other entry
        # points stay in generated code.
        tbl[0x00C110E8] = "dense_r_C110E8"
        present.add("dense_r_C110E8")
        # r_000828 word-copy loop (Ymir profile 4.4%); only PC 0x834 dispatched.
        tbl[0x00000834] = "dense_r_000834"
        present.add("dense_r_000834")
        # r_C16D20 VRAM-fill loop (Ymir profile 4.9%); single entry C16D20.
        tbl[0x00C16D20] = "dense_r_C16D20"
        present.add("dense_r_C16D20")
        # r_C11002 memory-fill loops 1-3 (Ymir profile r_C11002 13.5%).
        tbl[0x00C11050] = "dense_r_C11050"
        present.add("dense_r_C11050")
        tbl[0x00C1108A] = "dense_r_C1108A"
        present.add("dense_r_C1108A")
        tbl[0x00C110B4] = "dense_r_C110B4"
        present.add("dense_r_C110B4")
        # r_C11F78 VRAM-fill loop head (Ymir profile 2.8%).
        tbl[0x00C11F9E] = "dense_r_C11F9E"
        present.add("dense_r_C11F9E")
    entries = sorted([(pc, tbl[pc]) for pc in pcs if pc in tbl and tbl[pc] in present])

    lines = ["/* Resident native dispatch map (binary search). Local build only. */",
             '#include "game_recomp_pages.h"',
             '#include <neogeorecomp/neogeo.h>']
    for _, r in entries:
        lines.append("void %s(void);" % r)
    lines += ["struct resident_entry { uint32_t addr; ng_func_t fn; };",
              "extern const struct resident_entry g_resident_map[];",
              "const struct resident_entry g_resident_map[] = {"]
    for pc, r in entries:
        lines.append("    {0x%06X, %s}," % (pc, r))
    lines.append("};")
    lines.append("const unsigned g_resident_map_count = %d;" % len(entries))
    Path("local/ymir/resident_map.c").write_text("\n".join(lines) + "\n")
    print("resident map entries:", len(entries))


if __name__ == "__main__":
    main()
