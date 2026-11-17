#!/usr/bin/env python3
"""Generate the routine -> page map for recomp routine overlay/streaming.

`m68krecomp --files N` splits the recompiled routines round-robin into N
translation units (`<prefix>recomp_00.c` .. `<prefix>recomp_0N.c`). Each file is
one *load unit* (a "page") for the overlay manager: routines never call each
other directly (every intra-ROM transfer sets CPU.pc and returns to the
dispatcher), so a page is an independent unit that can be paged in and out.

This tool reads those generated files plus the recompiler's dispatch table and
emits a header with:

  NG_RECOMP_PAGES                      number of pages (N)
  <prefix>recomp_pages[]               { addr, fn, page } for every dispatch entry
                                       point (routine starts and intra-routine
                                       labels), sorted by addr, so the runtime
                                       resolves a PC to its page and routine in
                                       one binary search
  <prefix>recomp_pages_count
  <prefix>recomp_page_info[]           { insns, routines } per page, for eviction
                                       cost accounting and reporting

The generated code is derived from the user's own ROM and is never committed;
this header is generated into the build tree.

Usage:
  python tools/mk_recomp_pages.py --dir <generated> --prefix bios_ \
      --out <generated>/bios_recomp_pages.h
"""
import argparse
from pathlib import Path
import re

ROUTINE_RE = re.compile(r"^void ([A-Za-z0-9_]*r_([0-9A-Fa-f]{6,8}))\(void\) \{")
INSNS_RE = re.compile(r"/\* routine \$[0-9A-Fa-f]{6}: (\d+) instructions \*/")
TABLE_RE = re.compile(r"^\s*\{0x([0-9A-Fa-f]+)u,\s*([A-Za-z0-9_]+)\},")


def parse_page(path: Path, prefix: str):
    """Routine addresses and total instruction count declared in one page file."""
    routines = []
    insns = 0
    for line in path.read_text(errors="replace").splitlines():
        m = ROUTINE_RE.match(line)
        if m and m.group(1).startswith(prefix):
            routines.append((int(m.group(2), 16), m.group(1)))
        else:
            mi = INSNS_RE.search(line)
            if mi:
                insns += int(mi.group(1))
    return routines, insns


def parse_table(path: Path):
    """All externally reachable `{addr, routine}` entry points emitted by m68krecomp.

    A routine can own several entry points (a JSR return site or a branch target
    inside it); every one of them must map to the page that holds the routine,
    otherwise the dispatcher would miss on the very common case of returning into
    the middle of a recompiled routine.
    """
    entries = []
    for line in path.read_text(errors="replace").splitlines():
        m = TABLE_RE.match(line)
        if m:
            entries.append((int(m.group(1), 16), m.group(2)))
    return entries


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dir", type=Path, required=True, help="directory with the generated files")
    p.add_argument("--prefix", default="bios_")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--guard", default=None, help="include guard (default: derived from the prefix)")
    p.add_argument("--sym-prefix", default=None,
                   help="symbol prefix for the map (default: ng_recomp; use ng_game for the game map)")
    p.add_argument("--null-fn", action="store_true",
                   help="emit 0 function pointers (rawcode overlay: dispatcher falls back to interpreter)")
    p.add_argument("--rawcode", action="store_true",
                   help="emit addr->{page,off} entry type for the rawcode overlay")
    a = p.parse_args()

    def page_number(path: Path) -> int:
        return int(path.stem.rsplit("_", 1)[1])

    files = sorted(a.dir.glob(f"{a.prefix}recomp_[0-9]*.c"), key=page_number)
    if not files:
        raise SystemExit(f"no {a.prefix}recomp_NN.c in {a.dir}")
    if [page_number(p) for p in files] != list(range(len(files))):
        raise SystemExit(f"{a.prefix}recomp page files are not numbered 0..N-1")

    page_of = {}           # routine name -> page
    page_info = []
    for page, path in enumerate(files):
        routines, insns = parse_page(path, a.prefix)
        if not routines:
            raise SystemExit(f"{path.name}: no routines found")
        page_info.append((insns, len(routines)))
        for _addr, name in routines:
            page_of[name] = page

    # The page map must cover every entry point the recompiled code can be
    # entered at, not just routine starts: returning from a JSR lands inside the
    # caller, and that address is in the recomp table too.
    table_path = a.dir / f"{a.prefix}recomp_table.c"
    table = parse_table(table_path)
    if not table:
        raise SystemExit(f"{table_path.name}: no recomp table entries found")
    table_entries = []
    for addr, name in table:
        page = page_of.get(name)
        if page is None:
            raise SystemExit(f"{table_path.name}: {name} (0x{addr:06X}) is not in any page")
        table_entries.append((addr, name, page))

    entries = table_entries
    entries.sort(key=lambda e: e[0])
    addrs = [e[0] for e in entries]
    if len(set(addrs)) != len(addrs):
        raise SystemExit("duplicate routine address across pages")
    for lo, hi in zip(addrs, addrs[1:]):
        if lo == hi:
            raise SystemExit(f"duplicate 0x{lo:06X}")

    guard = a.guard or (a.prefix.upper() + "RECOMP_PAGES_H")
    sp = a.sym_prefix or "ng_recomp"   # symbol prefix: ng_recomp_pages[] vs ng_game_pages[]
    out = []
    out.append("/* Generated by tools/mk_recomp_pages.py from the recompiler output.")
    out.append(" * Derived from the user's own ROM: never commit or distribute. */")
    out.append(f"#ifndef {guard}")
    out.append(f"#define {guard}")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("#include <stddef.h>")
    out.append("#include <neogeorecomp/neogeo.h>   /* ng_func_t */")
    out.append("")
    out.append(f"#define NG_RECOMP_PAGES {len(files)}u")
    out.append("")
    out.append("/* One dispatch entry. rawcode mode: addr -> (page, offset) into the")
    out.append(" * page blob (fn = page_base + off). Otherwise: routine fn + load page. */")
    if a.rawcode:
        out.append("typedef struct { uint32_t addr; uint32_t page; uint32_t off; }")
    else:
        out.append("typedef struct { uint32_t addr; ng_func_t fn; uint8_t page; }")
    out.append(f"    {sp}_page_entry_t;")
    out.append("")
    out.append("/* Per-page cost, for eviction accounting and reporting. */")
    out.append("typedef struct { uint16_t insns; uint16_t routines; }")
    out.append(f"    {sp}_page_info_t;")
    out.append("")
    out.append(f"extern const {sp}_page_entry_t {sp}_pages[];")
    out.append(f"extern const size_t {sp}_pages_count;")
    out.append(f"extern const {sp}_page_info_t {sp}_page_info[NG_RECOMP_PAGES];")
    out.append("")
    out.append("#endif")

    a.out.write_text("\n".join(out) + "\n")

    # Definitions go to a .c next to the header so the generated files stay C.
    defs = a.out.with_name(a.out.stem + ".c")
    d = []
    d.append("/* Generated by tools/mk_recomp_pages.py from the recompiler output.")
    d.append(" * Derived from the user's own ROM: never commit or distribute. */")
    d.append(f'#include "{a.out.name}"')
    d.append("#include <neogeorecomp/neogeo.h>")
    d.append("")
    d.append("/* One extern per routine: the page files are separate translation units. */")
    if not a.null_fn:
        for name in sorted(page_of):
            d.append(f"extern void {name}(void);")
    d.append("")
    d.append(f"const {sp}_page_entry_t {sp}_pages[] = {{")
    for addr, name, page in entries:
        fn = "0" if a.null_fn else name
        d.append(f"    {{0x{addr:06X}u, {fn}, {page}u}},")
    d.append("};")
    d.append("const size_t " + sp + "_pages_count = %d;" % len(entries))
    d.append("")
    d.append(f"const {sp}_page_info_t {sp}_page_info[NG_RECOMP_PAGES] = {{")
    for insns, routines in page_info:
        d.append(f"    {{{insns}u, {routines}u}},")
    d.append("};")
    defs.write_text("\n".join(d) + "\n")

    total_insns = sum(i for i, _ in page_info)
    total_routines = sum(r for _, r in page_info)
    print(f"[mk_recomp_pages] {len(files)} pages, {total_routines} routines, "
          f"{len(entries)} entry points, {total_insns} instructions -> {a.out}")
    for page, (insns, routines) in enumerate(page_info):
        print(f"[mk_recomp_pages]   page {page}: {routines} routines, {insns} instructions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())