#!/usr/bin/env python3
"""Build the interactive disc's union tile-residency pack.

The playable target renders several guest-frame milestones (the boot/attract
sequence and the KOF98 title). Across those frames the guest uses two distinct
59-tile sets; this tool unions them into one 118-tile pack so a single TILES.BIN
covers every milestone. Tile IDs come from ng_boot_host --tile-dump; no ROM or
game-derived asset is committed (all paths are local build inputs).
"""
import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pack_tiles import build  # noqa: E402

# First frame with sprites, and the validated KOF98 title frame.
FRAMES = (13, 778)
CAPACITY = 64 * 1024


def read_til(path: Path):
    data = path.read_bytes()
    count = struct.unpack_from(">I", data, 0)[0]
    if len(data) < 4 + 4 * count:
        raise ValueError(f"short tile dump: {path}")
    return list(struct.unpack_from(f">{count}I", data, 4))


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("host_tool", type=Path, help="ng_boot_host executable")
    p.add_argument("bios", type=Path)
    p.add_argument("p1", type=Path)
    p.add_argument("p2", type=Path)
    p.add_argument("zoom", type=Path)
    p.add_argument("rom_dir", type=Path, help="directory with 242-c1..c8.bin")
    p.add_argument("output", type=Path, help="output directory for TILES.BIN + manifest")
    a = p.parse_args()

    ids = set()
    with tempfile.TemporaryDirectory() as td:
        for frame in FRAMES:
            dump = Path(td) / f"t{frame}.til"
            cmd = [str(a.host_tool), str(a.bios), str(frame), str(a.p1),
                   str(a.p2), str(a.zoom), str(dump)]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                print(r.stderr, file=sys.stderr)
                return r.returncode
            ids.update(read_til(dump))
    report = build(a.rom_dir, sorted(ids), a.output, CAPACITY)
    print(f"union tile count={report['tile_count']} bytes={report['bytes']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
