#!/usr/bin/env python3
"""Emit a hot addr->{page,off} map sized to match the pass-1 stub.

Reads rawcode_map.c from rawcode_pack.py and picks the entries whose guest
addresses appear in the supplied profiles (plus the lowest addresses to fill).
Emits g_game_map[] / g_game_map_count with the ng_gamemap_page_entry_t type.

Usage:
  make_hot_rawcode_map.py MAP.c N OUT.c PROFILE.csv... [--force 0x9D26,...]

--force adds addresses (e.g. unprofiled PCs reached on the real target) with
top priority so they are always in the hot map without changing its size.
"""
import csv
import re
import sys
from pathlib import Path

ENTRY = re.compile(r"\{0x([0-9A-Fa-f]+),\s*(\d+),\s*(\d+)\},")


def main():
    args = sys.argv[1:]
    force = set()
    if "--force" in args:
        i = args.index("--force")
        force = set(int(x, 16) for x in args[i + 1].split(","))
        args = args[:i] + args[i + 2:]
    pack_c, n, out = Path(args[0]), int(args[1]), Path(args[2])
    profiles = [Path(p) for p in args[3:]]
    entries = [(int(a, 16), int(p), int(o)) for a, p, o in ENTRY.findall(pack_c.read_text())]
    by_addr = {a: (a, p, o) for a, p, o in entries}
    hits = {}
    for prof in profiles:
        with prof.open(newline="") as f:
            f.readline()
            for row in csv.DictReader(f):
                k = int(row["key"], 16)
                hits[k] = hits.get(k, 0) + int(row["hits"])
    # Force entries get infinite priority; others rank by profile hits.
    def priority(e):
        return (0 if e[0] in force else 1, -hits.get(e[0], 0), e[0])
    ranked = sorted(entries, key=priority)
    chosen = ranked[:n]
    chosen.sort(key=lambda e: e[0])
    lines = ["/* Hot rawcode map (pass 2). Local build only. */",
             '#include "game_recomp_pages.h"',
             "const ng_gamemap_page_entry_t g_game_map[%d] = {" % len(chosen)]
    for a, p, o in chosen:
        lines.append("    {0x%06X, %d, %d}," % (a, p, o))
    lines.append("};")
    lines.append("const unsigned g_game_map_count = %d;" % len(chosen))
    out.write_text("\n".join(lines) + "\n")
    missing_forced = [hex(a) for a in force if a not in by_addr]
    print("[hot_rawcode_map] %d entries (stub %d) -> %s; forced=%s%s" % (
        len(chosen), n, out, [hex(a) for a in sorted(force)],
        (" MISSING:" + ",".join(missing_forced)) if missing_forced else ""))
    if len(chosen) != n:
        print("WARNING: fewer hot entries than stub size")
    return 0 if not missing_forced else 1


if __name__ == "__main__":
    raise SystemExit(main())
