#!/usr/bin/env python3
"""End-to-end test: packed page blob + RELOCS.BIN reconstruct a direct link.

Loads page 0 from a rawcode_pack output at a test base, applies the internal
relocation patch, and compares byte-for-byte against linking that page directly
at the same base with the same symbol addresses.
"""
import os
import struct
import subprocess
import sys
from pathlib import Path

NM = os.environ.get("SH2_NM", "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-nm.exe")
LD = os.environ.get("SH2_LD", "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-ld.exe")
CC = os.environ.get("SH2_CC", "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-gcc.exe")
OBJCOPY = os.environ.get("SH2_OBJCOPY", "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-objcopy.exe")
PLACE = 0x0E000000
BASE = 0x06000000


def main():
    gen, elf, pack = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    src = Path(sys.argv[4]) if len(sys.argv) > 4 else gen / "game_recomp_00.c"
    work = Path("local/ymir/rawverify")
    work.mkdir(parents=True, exist_ok=True)
    out = subprocess.run([NM, str(elf)], capture_output=True, text=True).stdout
    syms = {l.split()[-1]: int(l.split()[0], 16) for l in out.splitlines() if len(l.split()) >= 3}
    obj, linked, raw = work / "v.o", work / "v.elf", work / "v.bin"
    subprocess.run([CC, "-m2", "-mb", "-Os", "-std=c11", "-ffreestanding", "-fno-lto",
                    "-ffunction-sections", "-fdata-sections", "-I" + str(gen),
                    "-Ideps/kof98recomp/neogeorecomp/include",
                    "-c", str(src), "-o", str(obj)], check=True, capture_output=True)
    uout = subprocess.run([NM, "-u", str(obj)], capture_output=True, text=True).stdout
    undef = sorted(set(l.split()[-1] for l in uout.splitlines() if l.strip()))
    Path(work / "base.ld").write_text(
        "SECTIONS { .text 0x%X : { *(.text .text.*) } .data 0x0 : { *(.data .data.*) } "
        ".bss 0x0 : { *(.bss .bss.*) } /DISCARD/ : { *(.comment .note.*) } }" % BASE)
    cmd = [LD, "-T", str(work / "base.ld"), "-o", str(linked)]
    miss = 0
    for s in undef:
        a = syms.get(s)
        if a is None:
            a = PLACE + miss * 0x40
            miss += 1
        cmd += ["--defsym", "%s=0x%X" % (s, a)]
    subprocess.run(cmd + [str(obj)], check=True, capture_output=True)
    subprocess.run([OBJCOPY, "-O", "binary", "--only-section=.text", str(linked), str(raw)],
                   check=True, capture_output=True)
    direct = raw.read_bytes()
    blob = (pack / "PAGES.BIN").read_bytes()[:len(direct)]
    rb = (pack / "RELOCS.BIN").read_bytes()
    npg = int.from_bytes(rb[0:4], "big")
    blk = int.from_bytes(rb[4 + 0 * 4:4 + 1 * 4], "big")  # page 0 block offset
    cnt = int.from_bytes(rb[blk:blk + 4], "big")
    patched = bytearray(blob)
    for i in range(cnt):
        off = int.from_bytes(rb[blk + 4 + 4 * i:blk + 8 + 4 * i], "big")
        v = int.from_bytes(patched[off:off + 4], "big")
        patched[off:off + 4] = (v + BASE).to_bytes(4, "big")
    Path("local/ymir/rawverify/patched.bin").write_bytes(patched)
    diff = sum(1 for a, b in zip(patched, direct) if a != b)
    print("[rawverify] direct=%d relocs=%d diffbytes=%d equal=%s" %
          (len(direct), cnt, diff, diff == 0 and len(direct) == len(patched)))
    return 0 if diff == 0 and len(direct) == len(patched) else 1


if __name__ == "__main__":
    raise SystemExit(main())
