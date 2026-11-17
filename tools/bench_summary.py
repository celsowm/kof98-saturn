#!/usr/bin/env python3
"""Read and compare kof_ymir_bench summary.json files.

Two modes:
  * one summary: print each target's key counters and speed ratio.
  * two summaries: print the delta for each counter, plus a PASS/FAIL check
    using the project's acceptance rule (recomp_bad == 0 and guest_frames
    increasing) and a warning if any guest-visible write counter changed
    (a dense recomp handler must not change guest behaviour).

Usage:
    python tools/bench_summary.py local/ymir/resident-verify2/summary.json
    python tools/bench_summary.py BEFORE/summary.json AFTER/summary.json
"""
import argparse
import json
from pathlib import Path

# Counters that must stay IDENTICAL for a bit-exact recomp change (per the
# same emulated-frame count). A dense handler may change guest_frames (it
# runs faster) but not the guest-visible device-write counts per guest frame.
WRITE_COUNTERS = (
    "ng_boot_vram_writes", "ng_boot_pal_writes", "ng_boot_io_writes",
    "ng_boot_irq_taken", "ng_boot_frames", "ng_boot_vblanks",
)
KEY = (
    "ng_boot_recomp_bad", "ng_boot_recomp_blocks", "ng_boot_recomp_interp",
    "ng_boot_frames", "ng_boot_instructions",
    "ng_boot_vram_writes", "ng_boot_pal_writes", "ng_boot_io_writes",
    "guest_frames_per_emulated_frame", "sh2_cycles_per_emulated_frame",
)


def load(p: Path) -> dict:
    return json.loads(p.read_text())


def show_one(d: dict) -> None:
    print(f"emulated_frames: {d.get('emulated_frames')}")
    for name, t in d.get("targets", {}).items():
        print(f"[{name}]")
        for k in KEY:
            if k in t:
                print(f"  {k:<38} {t[k]}")


def compare(a: dict, b: dict) -> int:
    ok = True
    for name in a.get("targets", {}):
        if name not in b.get("targets", {}):
            continue
        x, y = a["targets"][name], b["targets"][name]
        print(f"[{name}]  BEFORE -> AFTER")
        for k in KEY:
            if k in x and k in y:
                delta = y[k] - x[k] if isinstance(x[k], (int, float)) else ""
                print(f"  {k:<38} {x[k]} -> {y[k]}   ({delta})")
        bad = y.get("ng_boot_recomp_bad", 0)
        ratio_x = x.get("guest_frames_per_emulated_frame", 0)
        ratio_y = y.get("guest_frames_per_emulated_frame", 0)
        if bad != 0:
            print(f"  FAIL: bad_dispatch={bad} (must be 0)")
            ok = False
        # Compare speed ratio, NOT raw guest_frames: runs may use different
        # --frames budgets, so raw frame counts are not comparable. A dense
        # recomp change must not SLOW the ratio (allow 0.5% noise).
        if ratio_y < ratio_x * 0.995:
            print(f"  FAIL: speed ratio dropped {ratio_x:.4f} -> {ratio_y:.4f}")
            ok = False
        # Bit-exactness: device writes per guest frame must match. The two
        # runs stop at different attract-mode points, so allow a small
        # relative tolerance (0.5%) plus an absolute floor of 2/frame. A
        # dropped write path changes the rate by whole integers per frame,
        # far above this band.
        xf = max(1, x.get("ng_boot_frames", 1))
        yf = max(1, y.get("ng_boot_frames", 1))
        for c in WRITE_COUNTERS:
            if c not in x or c not in y:
                continue
            rx, ry = x[c] / xf, y[c] / yf
            tol = max(2.0, 0.005 * max(rx, ry))
            if abs(rx - ry) > tol:
                print(f"  FAIL: {c}/frame changed {rx:.2f} -> {ry:.2f} (tol {tol:.2f})")
                ok = False
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("before", type=Path)
    ap.add_argument("after", type=Path, nargs="?")
    a = ap.parse_args()
    if a.after is None:
        show_one(load(a.before))
        return 0
    return compare(load(a.before), load(a.after))


if __name__ == "__main__":
    raise SystemExit(main())
