import subprocess

CC = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-gcc.exe"
SZ = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-size.exe"
src = "local/ng_recomp_game256/game_recomp_234.c"
inc = ["-Ilocal/ymir/c32", "-Ilocal/ng_recomp_game256",
       "-Ideps/kof98recomp/neogeorecomp/include"]

variants = {
    "base": ["-Os", "-fno-lto"],
    "O2": ["-O2", "-fno-lto"],
    "Os-lto": ["-Os", "-flto"],
    "Os-noreorder": ["-Os", "-fno-lto", "-fno-reorder-blocks"],
}
for tag, flags in variants.items():
    obj = "local/ymir/cf_%s.o" % tag
    cmd = [CC, "-m2", "-mb", "-std=c11", "-ffreestanding",
           "-ffunction-sections", "-fdata-sections"] + flags + inc + ["-c", src, "-o", obj]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(tag, "FAIL", r.stderr[:100]); continue
    out = subprocess.run([SZ, obj], capture_output=True, text=True).stdout.splitlines()[-1].split()
    print("%-16s text=%s B" % (tag, out[0]))
