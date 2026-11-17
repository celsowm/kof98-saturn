#!/usr/bin/env python3
"""Resolve ng_boot trace symbols via nm, dump them from Ymir, decode frames.
Local diagnostic helper. Usage: python tools/_dump_trace.py <elf> <disc> <bin> <fields>
"""
import struct
import subprocess
import sys
from pathlib import Path

NM = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-nm.exe"
PROBE = "build-ymir/probe.exe"
BIOS = r"C:\Users\celso\Documents\projetos\libsaturn-1\bios\saturn_bios_jp.bin"

elf, disc, app, fields, tag = sys.argv[1:6]
disc, app, elf = str(Path(disc).resolve()), str(Path(app).resolve()), str(Path(elf).resolve())
out = Path(f"local/ymir/tracediff/auto_{tag}")
out.mkdir(parents=True, exist_ok=True)

nmout = subprocess.run([NM, "-S", elf], capture_output=True, text=True).stdout
syms = {}
for line in nmout.splitlines():
    parts = line.split()
    if len(parts) >= 4 and parts[3].startswith("_g_frame_trace"):
        syms[parts[3]] = (int(parts[0], 16), int(parts[1], 16))

naddr, nsize = syms["_g_frame_trace_n"]
taddr, tsize = syms["_g_frame_trace"]
assert tsize == 256 * 24, tsize

cmd = [PROBE, "--boot-mode", "injection", "--iso", disc, "--bin", app,
       "--bios", BIOS, "--boot-frames", "90", "--frames", str(fields),
       "--sh2-cache", "on", "--ram-cart", "4m",
       "--dump-mem", f"0x{naddr:08x}:4:{tag}_n.bin",
       "--dump-mem", f"0x{taddr:08x}:{tsize}:{tag}_t.bin",
       "--out", "probe.json"]
subprocess.run(cmd, cwd=str(out), capture_output=True, check=True, timeout=600)

n = struct.unpack(">I", (out / f"{tag}_n.bin").read_bytes())[0]
b = (out / f"{tag}_t.bin").read_bytes()
print(f"{tag}: {n} trace records")
for i in range(min(n, 12)):
    f, pc, instr, vram, blocks, sr, stopped, irq = struct.unpack(">IIIIIHBB", b[i*24:(i+1)*24])
    print(f"  frame={f:3d} pc=0x{pc:08X} sr=0x{sr:04X} stopped={stopped} irq=0x{irq:02X} instr={instr:8d} vram={vram:8d}")
