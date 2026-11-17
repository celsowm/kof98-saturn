#!/usr/bin/env python3
"""Concrete density prototype: hand-written SH-2 vs C-recomp for the hottest
guest loop (0x00A142 tst.b / 0x00A146 beq, 114,920 profile hits).

Counts instructions in the steady-state inner loop body for one guest
iteration. The C count is read from the disassembly of the generated routine
(the segment repeated per iteration), the asm count from the written template.
"""
import re
import subprocess
from pathlib import Path

OD = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-objdump.exe"

# (1) hand-written template inner loop
out = subprocess.run([OD, "-d", "local/ymir/poll_dense.o"], capture_output=True, text=True).stdout
lines = out.splitlines()
ls = next(i for i, l in enumerate(lines) if "loop>:" in l)
le = next(i for i, l in enumerate(lines[ls + 1:], ls + 1) if "\trts" in l or "<done>" in l)
asm = [l for l in lines[ls:le] if re.search(r"\t[a-z]", l)]
# remove the exit branch/nop (taken once) -> steady loop = mov.b,extu,add,tst,bf,add,cmp,bt,bra
steady_asm = [l for l in asm if "bf" not in l and "bt" not in l and "\tnop" not in l]
print("hand-written inner loop: %d insns/iteration (incl. branches)" % len(asm))

# (2) C recomp: count the instruction block that is repeated by the goto loop.
# From the disassembly, the tst.b path reloads CPU.a[5] (mov.l @(52,r14)),
# computes address, calls ng_r8 (jsr), calls ng_logic8 (jsr), updates cycles
# (load/store), then the beq path repeats. Count the segment from the a5 load
# through the branch back.
obj = "build-ng/CMakeFiles/kof_resident_bench.dir/local/ymir/resident_routines.c.obj"
out = subprocess.run([OD, "-dr", obj], capture_output=True, text=True).stdout
lines = out.splitlines()
start = next(i for i, l in enumerate(lines) if re.search(r"<_(game_)?r_00A252>:", l))
end = next(i for i, l in enumerate(lines[start + 1:], start + 1) if l.strip().endswith(">:"))
seg = lines[start:end]
code = [l for l in seg if re.search(r"\t[a-z]", l)]
# the hot loop per iteration: from 'mov.l @(52,r14),r4' (A5 reload) through the
# 'mov.l @r10,r2' cycle store and backward branch. Use the 2785 literal as anchor.
idx = [i for i, l in enumerate(seg) if "2785" in l][0]
# find the start of this guest instruction (a5 reload just before) and the beq back-branch
s2 = max(0, idx - 4)
e2 = min(len(seg), idx + 34)
block = [l for l in seg[s2:e2] if re.search(r"\t[a-z]", l)]
print("C-recomp hot path: %d insns in tst.b+beq block (2 jsr calls)" % len(block))
print("  (of which %d are jsr calls to ng_r8/ng_logic8)" % sum(1 for l in block if "\tjsr" in l))
print("density ratio (C / hand-written): %.1fx" % (len(block) / max(1, len(asm))))
