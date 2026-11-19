#!/usr/bin/env python3
"""Compile-check machine-cleaned C with NO error suppression.

The cleaned C uses fixed-width stdint types and explicit casts for every
Ghidra loose pointer/integer assignment, so it must compile as ordinary C11.
This tool is the canonical gate; it compiles each translation unit with gcc
and fails on any error. Optional --object also emits an object file.

Game-derived inputs/outputs stay under ignored local/.
"""
import argparse
import subprocess
import sys
from pathlib import Path


def compile_one(src: Path, cc: str, out: Path | None, extra: list[str]) -> int:
    cmd = [cc, "-std=c11", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-Wno-sign-compare", "-Wno-unused-variable",
           "-Werror=implicit-function-declaration",
           "-Werror=incompatible-pointer-types", "-Werror=int-conversion",
           "-I", str(src.parent)]
    if out is None:
        cmd += ["-fsyntax-only"]
    else:
        cmd += ["-c", "-o", str(out)]
    cmd += extra + [str(src)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stderr)
        return r.returncode
    print(f"OK: {src.name}" + (f" -> {out.name}" if out else ""))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("src", nargs="+", type=Path)
    ap.add_argument("--cc", default="gcc")
    ap.add_argument("--object", action="store_true",
                    help="emit .verify.o next to each source")
    ap.add_argument("--define", action="append", default=[])
    a = ap.parse_args()
    extra: list[str] = []
    for d in a.define:
        extra += ["-D" + d]
    rc = 0
    for src in a.src:
        out = src.with_suffix(".verify.o") if a.object else None
        rc |= compile_one(src, a.cc, out, extra)
    print("ALL OK" if rc == 0 else f"FAILED ({rc})")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
