#!/usr/bin/env python3
"""Merge offline-recompiled guest-WRAM routines into the resident native core.

The guest copies a small 68000 routine into work RAM at $100000 and executes
it; the static P-ROM recompiler cannot see those bytes. This tool takes the
m68krecomp output for a dump of that WRAM region (all inputs local build
artifacts, never committed) and adds its routines + dispatch entries to the
profile-pruned resident core. The binary-search map remains the authority.
"""
import argparse
import re
from pathlib import Path

ENTRY = re.compile(r"\{\s*0x([0-9A-Fa-f]+)u?\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\}")


def parse(path: Path):
    entries = {}
    for addr, fn in ENTRY.findall(path.read_text(errors="replace")):
        entries[int(addr, 16)] = fn
    return entries


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--base-map", type=Path, required=True)
    ap.add_argument("--wram-table", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()

    entries = parse(a.base_map)
    wram = parse(a.wram_table)
    added = {k: v for k, v in wram.items() if k not in entries}
    entries.update(added)
    a.out.mkdir(parents=True, exist_ok=True)

    lines = [
        "/* Merged resident dispatch map: pruned P-ROM core + guest-WRAM routines.",
        " * Local build artifact only. Game-derived native code is never committed. */",
        '#include "game_recomp_pages.h"',
        "#include <neogeorecomp/neogeo.h>",
    ]
    for fn in sorted(set(entries.values())):
        lines.append(f"void {fn}(void);")
    lines.append("struct resident_entry { uint32_t addr; ng_func_t fn; };")
    lines.append("extern const struct resident_entry g_resident_map[];")
    lines.append("const struct resident_entry g_resident_map[] = {")
    for addr, fn in sorted(entries.items()):
        lines.append(f"    {{0x{addr:06X}u, {fn}}},")
    lines.append("};")
    lines.append(f"const unsigned g_resident_map_count = {len(entries)};")
    (a.out / "wram_resident_map.c").write_text("\n".join(lines) + "\n")
    print(f"base={len(wram) - len(added)} wram_added={len(added)} total={len(entries)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
