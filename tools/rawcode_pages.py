#!/usr/bin/env python3
"""Self-test: prove page .text is position-independent via relocation patching.

Links a page unit twice at two VMA bases with external symbols pinned to fixed
addresses, then checks every R_SH_DIR32 slot:
  - internal refs (into the page) shift by the base delta
  - external refs (app globals/libgcc) are unchanged
If this holds, a loader can copy the base-0 blob into the arena and add page_base
to only the internal slots.
"""
import re
import subprocess
import sys
from pathlib import Path

LD = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-ld.exe"
OBJCOPY = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-objcopy.exe"
OBJDUMP = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-objdump.exe"
NM = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-nm.exe"

EXTERN_BASE = 0x06000000


def externs(obj):
    out = subprocess.check_output([NM, "-u", obj], text=True, stderr=subprocess.DEVNULL)
    syms = sorted(set(line.split()[-1] for line in out.splitlines() if line.strip()))
    return syms


def link(obj, base, exts, out):
    ld_script = Path(out).with_suffix(".ld")
    ld_script.write_text(
        "SECTIONS { .text 0x%X : { *(.text .text.*) } "
        ".data 0x0 : { *(.data .data.*) } .bss 0x0 : { *(.bss .bss.*) } "
        "/DISCARD/ : { *(.comment .note.*) } }\n" % base
    )
    cmd = [LD, "--emit-relocs", "-T", str(ld_script), "-o", str(out)]
    for i, s in enumerate(exts):
        cmd += ["--defsym", "%s=0x%X" % (s, EXTERN_BASE + i * 0x40)]
    cmd.append(obj)
    subprocess.run(cmd, check=True, stderr=subprocess.PIPE)
    return out


def blob(elf, out):
    subprocess.run([OBJCOPY, "-O", "binary", "--only-section=.text", str(elf), str(out)],
                   check=True, stderr=subprocess.DEVNULL)
    return Path(out).read_bytes()


def relocs(elf):
    out = subprocess.check_output([OBJDUMP, "-r", str(elf)], text=True, stderr=subprocess.DEVNULL)
    r = []
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+)\s+R_SH_DIR32\s+(\S+)", line)
        if m:
            r.append((int(m.group(1), 16), m.group(2)))
    return r


def u32(b, off):
    return int.from_bytes(b[off:off + 4], "big")


def main():
    page = sys.argv[1] if len(sys.argv) > 1 else "local/ymir/page00.o"
    work = Path("local/ymir/rawtest")
    work.mkdir(parents=True, exist_ok=True)
    exts = externs(page)
    base0, base1 = 0, 0x20000
    e0 = link(page, base0, exts, work / "a0.elf")
    e1 = link(page, base1, exts, work / "a1.elf")
    b0 = blob(e0, work / "a0.bin")
    b1 = blob(e1, work / "a1.bin")
    rel = relocs(e0)
    internal = external = bad = 0
    for off, sym in rel:
        v0, v1 = u32(b0, off), u32(b1, off)
        if v0 >= EXTERN_BASE:
            external += 1
            if v0 != v1:
                bad += 1
        else:
            internal += 1
            if v1 - v0 != base1 - base0:
                bad += 1
                if bad < 4:
                    print("MISMATCH off=0x%x sym=%s v0=0x%x v1=0x%x" % (off, sym, v0, v1))
    print("blob=%d B relocs=%d external=%d internal=%d bad=%d" %
          (len(b0), len(rel), external, internal, bad))
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
