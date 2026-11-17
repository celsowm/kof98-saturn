import subprocess
from pathlib import Path

CC = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-gcc.exe"
SZ = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-size.exe"
OD = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-objdump.exe"
src = "local/ymir/resident_routines.c"
base = Path("local/ymir/c32/neogeorecomp/cpu.h").read_text()

variants = {
    "base": base,
    # NG_INSN only counts instructions; cycles batched by recomp_step after fn()
    "batched": base.replace("#define NG_INSN(cyc) (ng_cycles += (cyc), ng_icount++)",
                            "#define NG_INSN(cyc) ((void)(cyc), ng_icount++)"),
}
for tag, cpu in variants.items():
    d = Path("local/ymir/bat_%s/neogeorecomp" % tag)
    d.mkdir(parents=True, exist_ok=True)
    (d / "cpu.h").write_text(cpu)
    inc = ["-Ilocal/ymir/bat_%s" % tag, "-Ilocal/ymir", "-Ilocal/ng_recomp_game256",
           "-Ideps/kof98recomp/neogeorecomp/include"]
    obj = "local/ymir/bat_%s.o" % tag
    r = subprocess.run([CC, "-m2", "-mb", "-Os", "-std=c11", "-ffreestanding", "-fno-lto",
                        "-ffunction-sections"] + inc + ["-c", src, "-o", obj],
                       capture_output=True, text=True)
    if r.returncode:
        print(tag, "FAIL", r.stderr[:150]); continue
    out = subprocess.run([SZ, obj], capture_output=True, text=True).stdout.splitlines()[-1].split()
    dd = subprocess.run([OD, "-d", "--disassemble=_r_007E34", obj], capture_output=True, text=True).stdout
    import re
    code = [l for l in dd.splitlines() if re.search(r"\t[a-z]", l)]
    print("%-10s text=%8s r_007E34 insns=%4d" % (tag, out[0], len(code)))
