import subprocess

CC = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-gcc.exe"
SZ = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-size.exe"
OD = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-objdump.exe"
src = "local/ymir/resident_routines.c"
inc = ["-Ilocal/ymir/c32", "-Ilocal/ng_recomp_game256", "-Ilocal/ymir",
       "-Ideps/kof98recomp/neogeorecomp/include"]

variants = {
    "base": ["-ffunction-sections"],
    "nofuncsec": [],
    "nofuncsec-O2": ["-O2"],
}
for tag, extra in variants.items():
    obj = "local/ymir/dense_%s.o" % tag
    cmd = [CC, "-m2", "-mb", "-Os", "-std=c11", "-ffreestanding", "-fno-lto"] + inc + extra + ["-c", src, "-o", obj]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(tag, "FAIL", r.stderr[:120]); continue
    out = subprocess.run([SZ, obj], capture_output=True, text=True).stdout.splitlines()[-1].split()
    d = subprocess.run([OD, "-d", "--disassemble=_r_007E34", obj], capture_output=True, text=True).stdout
    import re
    code = [l for l in d.splitlines() if re.search(r"\t[a-z]", l)]
    print("%-16s text=%8s  r_007E34 insns=%4d jsr=%3d bsr=%3d" % (
        tag, out[0], len(code),
        sum(1 for l in code if "\tjsr" in l and "@r" in l),
        sum(1 for l in code if "\tbsr" in l)))
