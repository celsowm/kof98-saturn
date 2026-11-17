#!/usr/bin/env python3
"""Pack recompiled page units into position-independent raw SH-2 pages.

For each page unit (C source emitted by m68krecomp):
  - compile to an object with -ffunction-sections
  - link all .text.* into ONE .text at VMA 0 with --emit-relocs, pinning
    external symbols to their real addresses from the app ELF
  - extract the .text blob (padded to `stride`) -> PAGES.BIN
  - collect internal R_SH_DIR32 slots (value < blob size) -> RELOCS.BIN
    (the loader adds page_base to these; external slots are already final)
  - record each routine's offset within the blob

Then emit an addr -> (page, offset) dispatch map so the core can compute
fn = page_base + offset at runtime.

Usage:
  python tools/rawcode_pack.py --gen <gen_dir> --elf <app.elf> \
      --out <dir> --pages 256 --stride 131072 [--nginc <path>]
"""
import argparse
import os
import re
import subprocess
from pathlib import Path

LD = os.environ.get("SH2_LD", "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-ld.exe")
CC = os.environ.get("SH2_CC", "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-gcc.exe")
OBJCOPY = os.environ.get("SH2_OBJCOPY", "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-objcopy.exe")
OBJDUMP = os.environ.get("SH2_OBJDUMP", "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-objdump.exe")
NM = os.environ.get("SH2_NM", "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-nm.exe")
PLACEHOLDER = 0x0E000000  # for externals missing from the ELF (libgcc dead-stripped)

RELOC_RE = re.compile(r"\s*([0-9a-f]+)\s+R_SH_DIR32\s+(\S+)")
TABLE_RE = re.compile(r"\{\s*0x([0-9A-Fa-f]+)u\s*,\s*([A-Za-z0-9_]+)\s*\},")


def elf_symbols(elf):
    out = subprocess.run([NM, str(elf)], capture_output=True, text=True).stdout
    d = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 3:
            try:
                d[p[-1]] = int(p[0], 16)
            except ValueError:
                pass
    return d


def undefined(obj):
    out = subprocess.run([NM, "-u", str(obj)], capture_output=True, text=True).stdout
    return sorted(set(line.split()[-1] for line in out.splitlines() if line.strip()))


def build_defsyms(undef, syms):
    cmd = []
    miss = 0
    for s in undef:
        addr = syms.get(s)
        if addr is None:
            addr = PLACEHOLDER + miss * 0x40
            miss += 1
        cmd += ["--defsym", "%s=0x%X" % (s, addr)]
    return cmd, miss


def link_page(obj, out, defsyms):
    ldscript = Path(out).with_suffix(".ld")
    ldscript.write_text(
        "SECTIONS { .text 0x0 : { *(.text .text.*) } "
        ".data 0x0 : { *(.data .data.*) } .bss 0x0 : { *(.bss .bss.*) } "
        "/DISCARD/ : { *(.comment .note.*) } }\n"
    )
    subprocess.run([LD, "--emit-relocs", "-T", str(ldscript), "-o", str(out)] + defsyms + [str(obj)],
                   check=True, capture_output=True)
    return out


def page_blob(elf, out):
    subprocess.run([OBJCOPY, "-O", "binary", "--only-section=.text", str(elf), str(out)],
                   check=True, capture_output=True)
    return Path(out).read_bytes()


def internal_relocs(elf, blob):
    out = subprocess.run([OBJDUMP, "-r", str(elf)], capture_output=True, text=True).stdout
    n = len(blob)
    offs = []
    for line in out.splitlines():
        m = RELOC_RE.match(line)
        if m:
            off = int(m.group(1), 16)
            val = int.from_bytes(blob[off:off + 4], "big")
            if val < n:  # internal -> needs page_base added at load time
                offs.append(off)
    return sorted(offs)


def routine_offsets(elf):
    out = subprocess.run([NM, "-n", str(elf)], capture_output=True, text=True).stdout
    d = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 3:
            name = p[-1]
            # sh2eb-elf-nm prefixes C symbols with an underscore
            if name.startswith("_"):
                name = name[1:]
            # accept both r_ (full recomp) and game_r_ (cart-only) routine names
            if re.search(r"^.*r_[0-9A-Fa-f]{6,8}$", name):
                d[name] = int(p[0], 16)
    return d


def parse_table(path: Path):
    """addr -> routine name from the m68krecomp dispatch table."""
    d = {}
    txt = path.read_text(errors="replace")
    for m in TABLE_RE.finditer(txt):
        d[int(m.group(1), 16)] = m.group(2)
    return d


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--gen", type=Path, required=True)
    p.add_argument("--elf", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--pages", type=int, required=True)
    p.add_argument("--stride", type=int, required=True)
    p.add_argument("--nginc", default="deps/kof98recomp/neogeorecomp/include")
    p.add_argument("--name", default="game", help="generated file prefix (game or empty for full recomp)")
    p.add_argument("--table", default="game_recomp_table.c")
    p.add_argument("--rescue", action="append", default=[], metavar="PAGE:FILE",
                   help="replace an oversized page with a small hand-extracted source (e.g. 69:rescue69.c)")
    p.add_argument("--extra-entry", action="append", default=[], metavar="ADDR:NAME",
                   help="add an addr->routine map entry (e.g. 0x000122:r_000122)")
    p.add_argument("--jobs", type=int, default=8, help="parallel page compilations")
    p.add_argument("--skip-pages", default="", help="comma-separated page numbers to skip (oversized, e.g. 123,195,252)")
    a = p.parse_args()

    rescue = {}
    for r in a.rescue:
        pg, path = r.split(":", 1)
        rescue[int(pg)] = Path(path)
        if not rescue[int(pg)].is_file():
            raise SystemExit("rescue source missing: " + path)

    a.out.mkdir(parents=True, exist_ok=True)
    syms = elf_symbols(a.elf)
    table = parse_table(a.gen / a.table)
    for e in a.extra_entry:
        addr, name = e.split(":", 1)
        table[int(addr, 16)] = name
    pages_bin = bytearray()
    reloc_blocks = []  # (count, offsets) per page
    page_index = []    # byte offset of each page's block in the reloc payload
    routine_page = {}  # routine name -> (page, offset)
    total_internal = 0

    def pack_one(pg):
        if pg in skip_pages:
            return pg, bytes(a.stride), [], {}, True
        fname = ("%srecomp_%02d.c" % (a.name + "_" if a.name else "", pg))
        src = rescue.get(pg, a.gen / fname)
        obj = a.out / ("p%03d.o" % pg)
        subprocess.run([CC, "-m2", "-mb", "-Os", "-std=c11", "-ffreestanding", "-fno-lto",
                        "-ffunction-sections", "-fdata-sections", "-I" + str(a.gen),
                        "-I" + a.nginc, "-c", str(src), "-o", str(obj)],
                       check=True, capture_output=True)
        defsyms, miss = build_defsyms(undefined(obj), syms)
        linked = a.out / ("p%03d.elf" % pg)
        link_page(obj, linked, defsyms)
        blob = page_blob(linked, a.out / ("p%03d.bin" % pg))
        rel = internal_relocs(linked, blob)
        skipped = len(blob) > a.stride
        if skipped:
            print("[rawcode_pack] page %d: %d B > stride %d — SKIP" % (pg, len(blob), a.stride))
            blob = bytes(a.stride)
            rel = []
        routs = routine_offsets(linked) if rel else {}
        obj.unlink()
        linked.unlink()
        (a.out / ("p%03d.bin" % pg)).unlink()
        return pg, blob, rel, routs, skipped

    skip_pages = set(int(x) for x in a.skip_pages.split(",") if x.strip())
    # Pages are independent; compile/link in parallel, then assemble in order.
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=max(2, (a.jobs if hasattr(a, "jobs") else 8))) as ex:
        results = list(ex.map(pack_one, range(a.pages)))
    for pg, blob, rel, routs, skipped in results:
        pages_bin += blob + bytes(a.stride - len(blob))
        total_internal += len(rel)
        # block byte size = u32 count + count*u32 offsets
        page_index.append(sum(4 + 4 * len(b) for _, b in reloc_blocks))  # payload-relative offset
        reloc_blocks.append((len(rel), rel))
        if rel:  # only map routines from successfully packed pages
            for name, off in routs.items():
                routine_page[name] = (pg, off)

    (a.out / "PAGES.BIN").write_bytes(pages_bin)
    # RELOCS.BIN: u32 page_count, u32[page_count] block byte offsets (into the
    # payload after the index), then for each page: u32 count + u32 offsets.
    relocs_bin = bytearray()
    relocs_bin += a.pages.to_bytes(4, "big")
    idx_bytes = a.pages * 4
    payload_base = 4 + idx_bytes
    for off in page_index:
        relocs_bin += (payload_base + off).to_bytes(4, "big")
    for cnt, offs in reloc_blocks:
        relocs_bin += cnt.to_bytes(4, "big")
        for off in offs:
            relocs_bin += off.to_bytes(4, "big")
    (a.out / "RELOCS.BIN").write_bytes(relocs_bin)

    # addr -> (page, offset) map
    lines = ["/* Generated by tools/rawcode_pack.py. Local build only. */",
             "const struct { unsigned addr, page, off; } rawcode_map[] = {"]
    mapped = 0
    for addr, name in sorted(table.items()):
        if name in routine_page:
            pg, off = routine_page[name]
            lines.append("    {0x%06X, %d, %d}," % (addr, pg, off))
            mapped += 1
    lines.append("};")
    lines.append("const unsigned rawcode_map_count = %d;" % mapped)
    (a.out / "rawcode_map.c").write_text("\n".join(lines) + "\n")

    print("[rawcode_pack] pages=%d pages.bin=%d B relocs=%d internal=%d "
          "map=%d/%d" %
          (a.pages, len(pages_bin), len(relocs_bin), total_internal,
           mapped, len(table)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
