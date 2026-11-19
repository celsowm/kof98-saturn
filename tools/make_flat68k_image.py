#!/usr/bin/env python3
"""Assemble a flat 68000 image for decompiler import.

Ghidra/REA load a raw binary at one base address. KOF98's P1, P2 and BIOS live
at different addresses, so build one sparse image with each component placed at
its real guest address. The BIOS is byte-swapped exactly as m68krecomp expects
(MAME stores these ROMs big-endian; the recomp pipeline swaps them).

All inputs/outputs are local build inputs under ignored directories.
"""
import argparse
from pathlib import Path

SIZE = 0xD00000  # through the end of the 0xC00000 BIOS window


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--p1", type=Path, required=True)
    ap.add_argument("--p2", type=Path, required=True)
    ap.add_argument("--bios", type=Path, required=True)
    ap.add_argument("--wram", type=Path, default=None,
                    help="optional live WRAM dump mapped at $100000 (runtime code)")
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()

    def swap(data: bytes) -> bytearray:
        b = bytearray(data)
        b[0::2], b[1::2] = b[1::2], b[0::2]
        return b

    img = bytearray(b"\xff" * SIZE)
    # All three program images are stored byte-swapped in the local ROM dumps
    # (MAME 68000 convention). Ghidra's big-endian 68000 needs them swapped
    # back; without this, every P1/P2 routine truncates with "bad instruction".
    img[0:0x100000] = swap(a.p1.read_bytes()[:0x100000])
    if a.wram:
        w = a.wram.read_bytes()
        img[0x100000:0x100000 + len(w)] = w
    img[0x200000:0x600000] = swap(a.p2.read_bytes()[:0x400000])
    img[0xC00000:0xC20000] = swap(a.bios.read_bytes()[:0x20000])
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_bytes(img)
    print(f"wrote {a.out} ({len(img)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
