#!/usr/bin/env python3
"""Build a KOF98 sprite tile-residency pack from a live frame's tile IDs.

Reads the big-endian u32 tile-ID list produced by ng_boot_host
(--tile-dump, argv[6]) and packs the corresponding 16x16 4bpp tiles from the
local C ROM, reusing pack_tiles.build (same asset format the Saturn tile cache
consumes). No ROMs are committed; the pack is a local build input.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pack_tiles import build  # noqa: E402


def read_til(path: Path):
    data = path.read_bytes()
    if len(data) < 4:
        raise ValueError("short tile list")
    cnt = struct.unpack_from(">I", data, 0)[0]
    if len(data) < 4 + 4 * cnt:
        raise ValueError("truncated tile list")
    ids = [struct.unpack_from(">I", data, 4 + 4 * i)[0] for i in range(cnt)]
    for t in ids:
        if not 0 <= t < 524288:
            raise ValueError(f"tile id {t} out of sprite range")
    return ids


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("tiles", type=Path, help="big-endian u32 tile-ID list from ng_boot_host")
    p.add_argument("rom_dir", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--capacity", type=int, default=64 * 1024)
    a = p.parse_args()
    ids = read_til(a.tiles)
    report = build(a.rom_dir, ids, a.output, a.capacity)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())