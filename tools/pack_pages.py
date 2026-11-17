#!/usr/bin/env python3
"""Compile the game recomp page units to SH-2 objects and concatenate their
.text into PAGES.BIN, each page padded to a fixed stride.

The overlay loader (ng_overlay_saturn.c) reads page N at byte offset
N * stride from the disc; the arena slot is `stride` bytes. Each page unit
(`game_recomp_NN.c`) is compiled with the SH-2 cross toolchain
(-ffreestanding -ffunction-sections), its .text extracted, and padded to
the stride.

Usage:
  python tools/pack_pages.py --dir <generated> --prefix game_ \
      --pages 64 --stride 98304 --out <PAGES.BIN> [--cc sh2eb-elf-gcc]
"""
import argparse
import os
import subprocess
from pathlib import Path


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dir", type=Path, required=True)
    p.add_argument("--prefix", default="game_")
    p.add_argument("--pages", type=int, required=True)
    p.add_argument("--stride", type=int, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--cc", default=os.environ.get("SH2_CC", "sh2eb-elf-gcc"))
    p.add_argument("--objcopy", default=os.environ.get("SH2_OBJCOPY", "sh2eb-elf-objcopy"))
    p.add_argument("--nginc", default=os.environ.get("NGINC", ""),
                   help="path to neogeorecomp/include (for bus.h/cpu.h)")
    a = p.parse_args()

    if a.stride <= 0 or a.pages <= 0:
        raise SystemExit("invalid stride/pages")
    a.out.parent.mkdir(parents=True, exist_ok=True)
    blob = bytearray()
    for page in range(a.pages):
        name = f"{a.prefix}recomp_{page:02d}.c"
        src = a.dir / name
        if not src.is_file():
            raise SystemExit(f"missing {src}")
        obj = a.out.with_suffix(f".{page:02d}.o")
        cmd = [a.cc, "-m2", "-mb", "-Os", "-std=c11", "-ffreestanding",
               "-fno-lto", "-ffunction-sections", "-fdata-sections",
               "-I" + str(a.dir), "-c", str(src), "-o", str(obj)]
        if a.nginc:
            cmd.insert(1, "-I" + a.nginc)
        subprocess.run(cmd, check=True)
        # extract .text (the compiled page code) as a raw binary
        raw = a.out.with_suffix(f".{page:02d}.bin")
        subprocess.run([a.objcopy, "-O", "binary", "--only-section=.text",
                        str(obj), str(raw)], check=True)
        code = raw.read_bytes()
        if len(code) > a.stride:
            raise SystemExit(f"page {page}: {len(code)} > stride {a.stride}")
        blob += code + bytes(a.stride - len(code))
        obj.unlink()
        raw.unlink()
    a.out.write_bytes(blob)
    print(f"[pack_pages] {a.pages} pages x {a.stride} B = {len(blob)} B -> {a.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())