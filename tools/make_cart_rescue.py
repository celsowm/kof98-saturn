#!/usr/bin/env python3
"""Create an enhanced cart-only recomp page 0 with 3 rescue routines from the
full recomp, so the no-input scenario has full native coverage.

Usage: python tools/make_cart_rescue.py <cart_gen_dir> <full_gen_dir> <out.c>
"""
import re
import sys
from pathlib import Path


def extract(src, name):
    marker = "void %s(void) {" % name
    i = src.find(marker)
    if i < 0:
        return None
    b = src.find("{", i)
    depth = 0
    for j in range(b, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i:j + 1]
    return None


def main():
    cart, full, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    names = sys.argv[4:] if len(sys.argv) > 4 else ["r_000122", "r_00C54E", "r_03523A"]
    # Find each routine's defining page in the full recomp
    pieces = []
    for name in names:
        found = False
        for pf in sorted(full.glob("recomp_*.c")):
            if not re.match(r"recomp_\d+\.c$", pf.name):
                continue
            txt = pf.read_text(errors="replace")
            body = extract(txt, name)
            if body:
                print("[cart_rescue] %s from %s" % (name, pf.name))
                pieces.append("/* rescued from full recomp %s */\n%s" % (pf.name, body))
                found = True
                break
        if not found:
            raise SystemExit("routine not found: " + name)
    base = (cart / "game_recomp_00.c").read_text(errors="replace")
    out.write_text(base + "\n\n" + "\n\n".join(pieces) + "\n")
    print("[cart_rescue] %s (%d bytes)" % (out, out.stat().st_size))


if __name__ == "__main__":
    main()
