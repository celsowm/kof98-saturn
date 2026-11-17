#!/usr/bin/env python3
"""Decode and compare two ng_frame_trace buffers (local diagnostic)."""
import struct
import sys

def load(name):
    b = open(f"local/ymir/tracediff/{name}_tr.bin", "rb").read()
    recs = []
    for i in range(0, len(b), 24):
        f, pc, instr, vram, blocks, sr, stopped, irq = struct.unpack(">IIIIIHBB", b[i:i+24])
        recs.append((f, pc, instr, vram, blocks, sr, stopped, irq))
    return recs

a = load(sys.argv[1])
b = load(sys.argv[2])
print(f"{'frame':>5} | {'recomp pc':>10} {'sr':>6} {'instr':>9} {'vram':>8} | {'interp pc':>10} {'sr':>6} {'instr':>9} {'vram':>8}")
first = None
for x, y in zip(a, b):
    mark = "  " if (x[1] == y[1] and x[5] == y[5] and x[2] == y[2]) else "<<"
    if mark == "<<" and first is None:
        first = x[0]
    if x[0] < 20 or mark == "<<" or x[0] % 20 == 0:
        print(f"{x[0]:>5} | {x[1]:>10X} {x[5]:>6X} {x[2]:>9} {x[3]:>8} | {y[1]:>10X} {y[5]:>6X} {y[2]:>9} {y[3]:>8} {mark}")
    if first is not None and x[0] > first + 8:
        break
print(f"\nfirst divergence at guest frame {first}")
