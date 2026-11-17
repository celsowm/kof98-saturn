#!/usr/bin/env python3
"""Summarize dumped VRAM words (local helper)."""
import struct
import sys

b = open(sys.argv[1], "rb").read()
w = [struct.unpack(">H", b[i:i+2])[0] for i in range(0, len(b) - 1, 2)]
nz = sum(1 for x in w if x not in (0, 255, 65535))
print(f"{sys.argv[1]}: first8={w[:8]} distinct-nonfill={nz}/{len(w)}")
