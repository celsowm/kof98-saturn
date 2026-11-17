#!/usr/bin/env python3
"""P6 feasibility: estimate the compiled size of a mode's native routine set.

Given host-oracle guest-PC profile CSVs, map their PCs to generated m68krecomp
routines and estimate the WRAM-H footprint of each set. The estimate is
calibrated against the measured pruned blob (94 routines -> 52,324 B compiled)
using source-byte ratio, so it is fast enough for large mode sets (a full
compile of 2,400 routines takes many minutes).

All inputs/outputs are local build artifacts. No game-derived code is committed.
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_pruned_native import extract_body

GEN = Path("local/ng_recomp_game256")
FULL = Path("local/ng_recomp_full256")
BIOS = Path("build-ng/ng_bench_gen")
WRAM_H_BUDGET = 900 * 1024  # conservative: 1 MiB minus stack + runtime
CAL_ROUTINES = Path("local/pruned/noinput12k.routines.txt")
CAL_COMPILED = 52324  # measured native_text_size for the 94-routine pruned set

FUNC = re.compile(r"void ((?:game_|bios_|wram_)?r_[0-9A-Fa-f]+)\(void\)\s*\{")
ENTRY = re.compile(r"\{0x([0-9A-Fa-f]+)u,\s*((?:game_|bios_|wram_)?r_[0-9A-Fa-f]+)\}")


def load_table() -> dict:
    tbl = {}
    for gen, tname in ((GEN, "game_recomp_table.c"), (FULL, "recomp_table.c"),
                       (BIOS, "bios_recomp_table.c")):
        p = gen / tname
        if not p.exists():
            continue
        for line in p.read_text(errors="replace").splitlines():
            m = ENTRY.search(line)
            if m:
                tbl[int(m.group(1), 16)] = m.group(2)
    return tbl


def index_bodies() -> dict:
    """One pass over the generated sources: routine name -> body text."""
    bodies = {}
    for gdir in (GEN, FULL, BIOS):
        for f in gdir.glob("*.c"):
            if "table" in f.name or "pages" in f.name:
                continue
            t = f.read_text(errors="replace")
            for m in FUNC.finditer(t):
                name = m.group(1)
                if name in bodies:
                    continue
                b = t.find("{", m.start())
                depth = 0
                for k in range(b, len(t)):
                    if t[k] == "{":
                        depth += 1
                    elif t[k] == "}":
                        depth -= 1
                        if depth == 0:
                            bodies[name] = t[m.start():k + 1]
                            break
    return bodies


def profile_routines(csv_path: Path, tbl: dict) -> set:
    rs = set()
    with csv_path.open(newline="") as fh:
        for line in fh:
            if line.startswith("#") or line.startswith("key"):
                continue
            parts = line.strip().split(",")
            if not parts or not parts[0].strip():
                continue
            try:
                pc = int(parts[0].strip(), 16)
            except ValueError:
                continue
            if pc in tbl:
                rs.add(tbl[pc])
    return rs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--profiles", nargs="+", required=True, type=Path)
    ap.add_argument("--labels", nargs="+", required=True)
    ap.add_argument("--out", type=Path, default=Path("local/phasepack"))
    a = ap.parse_args()
    if len(a.profiles) != len(a.labels):
        raise SystemExit("--profiles and --labels must have equal length")

    tbl = load_table()
    bodies = index_bodies()
    kept = [l.strip() for l in CAL_ROUTINES.read_text().splitlines() if l.strip()]
    cal_src = sum(len(bodies.get(n, "")) for n in kept)
    ratio = CAL_COMPILED / cal_src

    a.out.mkdir(parents=True, exist_ok=True)
    rows = []
    for label, csv_path in zip(a.labels, a.profiles):
        rs = profile_routines(csv_path, tbl)
        src = sum(len(bodies.get(n, "")) for n in rs)
        est = int(src * ratio)
        indexed = sum(1 for n in rs if n in bodies)
        fits = est <= WRAM_H_BUDGET
        rows.append({"mode": label, "routines": len(rs), "indexed": indexed,
                     "est_compiled_bytes": est, "fits_wram_h": fits})
        print(f"{label:12s} routines={len(rs):5d} indexed={indexed:5d} "
              f"est_compiled={est:9d} B ({est/1024:7.0f} KiB) "
              f"fits 900 KiB WRAM-H: {'YES' if fits else 'NO'}")
    print(f"\ncalibration: {len(kept)} routines -> {cal_src} src B -> {CAL_COMPILED} compiled B "
          f"({ratio:.4f} compiled/src)")
    print("P6 verdict: a whole-mode phase overlay is feasible only if its set fits WRAM-H.")
    (a.out / "phase_sizes.json").write_text(json.dumps(
        {"calibration": {"routines": len(kept), "src_bytes": cal_src,
                         "compiled_bytes": CAL_COMPILED, "ratio": ratio},
         "modes": rows}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
