#!/usr/bin/env python3
"""Extract selected C routines from a recomp page into a small rescue page.

Usage: python tools/make_rescue_page.py <src.c> <out.c> r_00C54E r_03523A ...
Brace-matches each `void <name>(void) { ... }` and writes it verbatim.
"""
import sys
from pathlib import Path


def extract(src, name):
    marker = "void %s(void) {" % name
    i = src.find(marker)
    if i < 0:
        raise SystemExit("routine not found: " + name)
    b = src.find("{", i)
    depth = 0
    for j in range(b, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i:j + 1]
    raise SystemExit("unbalanced braces: " + name)


def main():
    src = Path(sys.argv[1]).read_text(errors="replace")
    out = Path(sys.argv[2])
    names = sys.argv[3:]
    hdr = ('/* Rescue page extracted from a recomp unit. Game-derived: local only. */\n'
           '#include "recomp_common.h"\n\n')
    out.write_text(hdr + "\n\n".join(extract(src, n) for n in names) + "\n")
    print("[rescue] %d routines -> %s (%d bytes)" % (len(names), out, out.stat().st_size))


if __name__ == "__main__":
    main()
