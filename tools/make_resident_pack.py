#!/usr/bin/env python3
"""Build a fully-resident native blob from the profile's hot routines.

Instead of disc-backed paging of the full 48 MB recomp, compile only the
routines actually executed in the target scenario into one WRAM-resident blob.
Eliminates all page thrashing for the profiled scenario.
"""
import csv
import re
import subprocess
import sys
from pathlib import Path

CC = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-gcc.exe"
SZ = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-size.exe"
GEN = Path("local/ng_recomp_game256")
C32 = Path("local/ymir/c32")
INC = ["-I" + str(C32), "-I" + str(GEN), "-Ideps/kof98recomp/neogeorecomp/include"]


def extract(src_text, name):
    marker = "void %s(void) {" % name
    i = src_text.find(marker)
    if i < 0:
        return None
    b = src_text.find("{", i)
    depth = 0
    for k in range(b, len(src_text)):
        if src_text[k] == "{":
            depth += 1
        elif src_text[k] == "}":
            depth -= 1
            if depth == 0:
                return src_text[i:k + 1]
    return None


def main():
    # profile PCs (with hit counts, for optional minimum-hit trimming)
    pcs = set()
    min_hits = int(__import__("os").environ.get("RESIDENT_MIN_HITS", "1"))
    with open("local/profiles/noinput1200.csv", newline="") as f:
        f.readline(); f.readline()
        for line in f:
            p = line.strip().split(",")
            if len(p) >= 2 and int(p[1]) >= min_hits:
                pcs.add(int(p[0], 16))
    # addr -> routine (cart recomp first, then full recomp for WRAM-code PCs)
    tbl = {}
    for gen, tname in ((GEN, "game_recomp_table.c"), (Path("local/ng_recomp_full256"), "recomp_table.c")):
        for line in (gen / tname).read_text(errors="replace").splitlines():
            m = re.search(r"\{0x([0-9A-Fa-f]+)u,\s*((?:game_)?r_[0-9A-Fa-f]+)\}", line)
            if m:
                tbl[int(m.group(1), 16)] = m.group(2)
    routines = sorted(set(tbl[p] for p in pcs if p in tbl))
    # find each routine's source file (cart recomp + full recomp)
    sources = {}
    for gdir in (GEN, Path("local/ng_recomp_full256")):
        for f in gdir.glob("*.c"):
            if "table" in f.name or "pages" in f.name:
                continue
            t = f.read_text(errors="replace")
            for r in routines:
                if ("void %s(void) {" % r) in t and r not in sources:
                    sources[r] = t
    print("profile PCs:", len(pcs), "-> routines found:", len(sources), "of", len(routines))
    # emit one C file with all routines
    out = ["/* Fully-resident native blob for the no-input scenario. Local only. */",
           '#include "game_recomp_common.h"']
    for r in routines:
        body = extract(sources.get(r, ""), r)
        if body:
            out.append(body)
    src = Path("local/ymir/resident_routines.c")
    src.write_text("\n\n".join(out) + "\n")
    # compile
    obj = Path("local/ymir/resident_routines.o")
    r = subprocess.run([CC, "-m2", "-mb", "-Os", "-std=c11", "-ffreestanding",
                        "-fno-lto", "-ffunction-sections", "-I" + str(C32),
                        "-Ilocal/ymir", "-I" + str(GEN),
                        "-Ideps/kof98recomp/neogeorecomp/include",
                        "-c", str(src), "-o", str(obj)],
                       capture_output=True, text=True)
    if r.returncode:
        print("compile FAIL", r.stderr[:500]); return 1
    o = subprocess.run([SZ, str(obj)], capture_output=True, text=True).stdout.splitlines()[-1].split()
    print("resident blob text size: %s bytes (%.0f KB)" % (o[0], int(o[0]) / 1024))
    print("fits in 1 MiB WRAM-L?", int(o[0]) < 1024 * 1024)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
