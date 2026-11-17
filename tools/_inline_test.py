import subprocess
from pathlib import Path

CC = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-gcc.exe"
SZ = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-size.exe"
OD = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-objdump.exe"
src = "local/ymir/resident_routines.c"

base = Path("local/ymir/c32/neogeorecomp/cpu.h").read_text()

variants = {
    "base": base,
    "inline_logic": base.replace("static inline T ng_nz", "static inline __attribute__((always_inline)) T ng_nz")
                        .replace("static inline T ng_logic", "static inline __attribute__((always_inline)) T ng_logic"),
}
for tag, cpu in variants.items():
    d = Path("local/ymir/inl_%s/neogeorecomp" % tag)
    d.mkdir(parents=True, exist_ok=True)
    (d / "cpu.h").write_text(cpu)
    inc = ["-Ilocal/ymir/inl_%s" % tag, "-Ilocal/ymir", "-Ilocal/ng_recomp_game256",
           "-Ideps/kof98recomp/neogeorecomp/include"]
    obj = "local/ymir/inl_%s.o" % tag
    r = subprocess.run([CC, "-m2", "-mb", "-Os", "-std=c11", "-ffreestanding", "-fno-lto",
                        "-ffunction-sections"] + inc + ["-c", src, "-o", obj],
                       capture_output=True, text=True)
    if r.returncode:
        print(tag, "FAIL", r.stderr[:150]); continue
    out = subprocess.run([SZ, obj], capture_output=True, text=True).stdout.splitlines()[-1].split()
    dd = subprocess.run([OD, "-d", "--disassemble=_r_007E34", obj], capture_output=True, text=True).stdout
    import re
    code = [l for l in dd.splitlines() if re.search(r"\t[a-z]", l)]
    print("%-16s text=%8s r_007E34 insns=%4d jsr=%3d" % (
        tag, out[0], len(code), sum(1 for l in code if "\tjsr" in l and "@r" in l)))
