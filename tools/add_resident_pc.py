#!/usr/bin/env python3
"""Add a guest PC (and its routine) to the resident blob + map.
Usage: add_resident_pc.py 0xB696 [more PCs...]
"""
import re
import sys
from pathlib import Path

GEN = Path("local/ng_recomp_game256")
FULL = Path("local/ng_recomp_full256")
BLOB = Path("local/ymir/resident_routines.c")


def find_routine_src(name):
    for d in (GEN, FULL):
        for f in d.glob("*.c"):
            if "table" in f.name or "pages" in f.name:
                continue
            s = f.read_text(errors="replace")
            i = s.find("void %s(void) {" % name)
            if i < 0:
                continue
            b = s.find("{", i)
            depth = 0
            for k in range(b, len(s)):
                if s[k] == "{":
                    depth += 1
                elif s[k] == "}":
                    depth -= 1
                    if depth == 0:
                        return s[i:k + 1]
    return None


def main():
    pc_args = [int(x, 16) for x in sys.argv[1:]]
    # addr -> routine
    tbl = {}
    for gen, tname in ((GEN, "game_recomp_table.c"), (FULL, "recomp_table.c")):
        for line in (gen / tname).read_text(errors="replace").splitlines():
            m = re.search(r"\{0x([0-9A-Fa-f]+)u,\s*((?:game_)?r_[0-9A-Fa-f]+)\}", line)
            if m:
                tbl[int(m.group(1), 16)] = m.group(2)
    blob = BLOB.read_text()
    added = 0
    for pc in pc_args:
        r = tbl.get(pc)
        if not r:
            print("no routine for 0x%X" % pc); continue
        if ("void %s(void) {" % r) in blob:
            continue
        body = find_routine_src(r)
        if not body:
            print("no source for", r); continue
        blob += "\n\n" + body + "\n"
        added += 1
        print("added 0x%X -> %s" % (pc, r))
    BLOB.write_text(blob)
    print("added", added, "routines")


if __name__ == "__main__":
    main()
