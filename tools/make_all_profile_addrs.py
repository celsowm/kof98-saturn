#!/usr/bin/env python3
"""Union of every routine address observed in the saved native profiles.

Extends the earlier top-500-per-profile seed (925 addresses) to full coverage of
the captured execution traces, so the pure-C dump includes every routine the
oracle actually ran, not just the hottest ones.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILES = ROOT / "local" / "profiles"
OUT = ROOT / "local" / "decomp" / "all_profile_addrs.txt"


def main() -> None:
    addrs = set()
    for name in ("fight.csv", "noinput1200.csv", "playlong.csv"):
        for line in (PROFILES / name).read_text().splitlines():
            if line.startswith("#") or line.startswith("key"):
                continue
            parts = line.split(",")
            if len(parts) >= 2:
                try:
                    addrs.add(int(parts[0], 16))
                except ValueError:
                    pass
    # Plus the existing seed (WRAM dispatch entries + boot routines).
    for line in (ROOT / "local" / "decomp" / "all_exec_addrs.txt").read_text().split():
        addrs.add(int(line, 16))
    ordered = sorted(a for a in addrs if a < 0xD00000)
    OUT.write_text(" ".join("0x%06X" % a for a in ordered))
    print(f"{len(ordered)} unique profile routine addresses -> {OUT}")


if __name__ == "__main__":
    main()
