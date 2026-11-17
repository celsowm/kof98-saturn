#!/usr/bin/env python3
"""Fix a RELOCS.BIN whose page-index block offsets were computed as slot
counts instead of byte sizes. The blocks themselves are written consecutively
in page order, so walk them to recover the correct offsets and rewrite the
index in place.
"""
import struct
import sys
from pathlib import Path

p = Path(sys.argv[1])
raw = bytearray(p.read_bytes())
n = struct.unpack_from(">I", raw, 0)[0]
payload = 4 + n * 4
pos = payload
offsets = []
for i in range(n):
    offsets.append(pos)
    cnt = struct.unpack_from(">I", raw, pos)[0]
    pos += 4 + cnt * 4
if pos != len(raw):
    print("[fix] WARNING: walked %d of %d bytes" % (pos, len(raw)))
for i, off in enumerate(offsets):
    struct.pack_into(">I", raw, 4 + i * 4, off)
p.write_bytes(raw)
print("[fix] rewrote %d block offsets, last=%d, bytes=%d" % (n, offsets[-1], len(raw)))
