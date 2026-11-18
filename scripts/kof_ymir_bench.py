#!/usr/bin/env python3
"""Measure how fast the SH-2 executes the real Neo Geo BIOS in Ymir.

Runs the same BIOS-only bench program twice, built from the same source with
only the 68000 core swapped:

  kof_ng_bench_interp   Musashi interpreter      (src/ng/ng_boot.c)
  kof_ng_bench_recomp   native recomp, no interp  (NG_BOOT_RECOMP_ONLY)

The program runs guest frames flat out with no vblank wait, so

    guest_frames / emulated_frames

is the speed ratio: 1.0 means the 68000 keeps up with real time (60 Hz), below
1.0 means it is slower than the original hardware. The probe also records master
SH-2 cycles per emulated frame (--profile-cycles) as a second view.

This is emulator evidence only; it is not physical-console timing. No emulator is
linked into the port.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]

COMMON_SYMBOLS = (
    "ng_boot_frames",
    "ng_boot_vblanks",
    "ng_boot_instructions",
    "ng_boot_vram_writes",
    "ng_boot_pal_writes",
    "ng_boot_io_writes",
    "ng_boot_irq_taken",
    "ng_boot_timer_irqs",
    "ng_boot_last_pc",
)
# Only the interpreter counts instructions per memory region (ng_insn_hook); the
# recomp-only build has no hook, so the linker drops those counters.
INTERP_SYMBOLS = (
    "ng_boot_bios_instr",
    "ng_boot_cart_instr",
    "ng_boot_wram_instr",
)
RECOMP_SYMBOLS = (
    "ng_boot_recomp_blocks",
    "ng_boot_recomp_interp",
    "ng_boot_recomp_bad",
)
TARGETS = {
    "interp": ("kof_ng_bench_interp", COMMON_SYMBOLS + INTERP_SYMBOLS),
    "recomp": ("kof_ng_bench_recomp", COMMON_SYMBOLS + RECOMP_SYMBOLS),
    "resident": ("kof_resident_bench", COMMON_SYMBOLS + RECOMP_SYMBOLS),
    "pruned": ("kof_resident_pruned", COMMON_SYMBOLS + RECOMP_SYMBOLS),
    "unified": ("kof_unified", COMMON_SYMBOLS + RECOMP_SYMBOLS + (
        "kof_live_sprite_count", "kof_live_sprite_hash", "kof_live_draw_strips",
        "kof_live_draw_native", "kof_live_draw_variants", "kof_live_draw_error",
        "kof_ym_samples", "kof_ym_written", "kof_ym_step")),
    "unified_music": ("kof_unified_music", COMMON_SYMBOLS + RECOMP_SYMBOLS + (
        "kof_live_sprite_count", "kof_live_sprite_hash", "kof_live_draw_strips",
        "kof_live_draw_native", "kof_live_draw_variants", "kof_live_draw_error",
        "kof_music_frames", "kof_music_services", "kof_music_playing",
        "kof_music_underruns", "kof_music_refills", "kof_music_buffered",
        "kof_music_min_buffered", "kof_music_bytes")),
    "playable": ("kof_playable", COMMON_SYMBOLS + (
        "kof_live_sprite_count", "kof_live_sprite_hash", "kof_live_draw_strips",
        "kof_live_draw_error", "kof_music_underruns")),
    "rawcode": ("kof_ng_rawcode_bench", COMMON_SYMBOLS + RECOMP_SYMBOLS + (
        "ng_boot_game_loads", "ng_boot_game_faults", "ng_boot_game_evictions",
        "ng_boot_game_errors", "ng_boot_game_resident",
        "kof_live_sprite_count", "kof_live_sprite_hash", "kof_live_draw_strips",
        "kof_live_draw_native", "kof_live_draw_variants", "kof_live_draw_error")),
}

DEFAULT_BIOS = Path(os.getenv(
    "LIBSATURN_KOF_BIOS",
    r"C:\Users\celso\Documents\projetos\libsaturn-1\bios\saturn_bios_jp.bin"))


def find_nm() -> str | None:
    nm = shutil.which("sh2eb-elf-nm")
    if nm:
        return nm
    if os.name == "nt":
        msys = Path(os.getenv("LIBSATURN_MSYS2_ROOT", r"C:\msys64"))
        cand = msys / "home" / Path.home().name / "saturn-tools/bin/sh2eb-elf-nm.exe"
        if cand.is_file():
            return str(cand)
    return None


def resolve_symbols(elf: Path, wanted) -> dict:
    """Address of each wanted 4-byte counter symbol, from nm -S or the map."""
    found = {}
    nm = find_nm()
    if nm:
        r = subprocess.run([nm, "-S", "--defined-only", str(elf)],
                           text=True, capture_output=True)
        if r.returncode == 0:
            for line in r.stdout.splitlines():
                m = re.match(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\w\s_?(\w+)$", line)
                if m and int(m[2], 16) == 4:
                    found[m[3]] = int(m[1], 16)
    for p in (elf.with_suffix(".elf.map"), elf.with_suffix(".map")):
        if not p.is_file():
            continue
        for line in p.read_text(errors="replace").splitlines():
            m = re.match(r"^\s*(0x[0-9a-fA-F]+)\s+_?(\w+)\s*$", line)
            if m:
                found.setdefault(m[2], int(m[1], 16))
    missing = [s for s in wanted if s not in found]
    if missing:
        raise ValueError(f"{elf.name}: missing symbols {missing}")
    return {k: found[k] for k in wanted}


def run_target(name: str, build: Path, bios: Path, probe: Path,
               frames: int, out_root: Path, timeout: int,
               pad_script: Path | None = None) -> dict:
    target, wanted = TARGETS[name]
    elf = build / f"{target}.elf"
    app = build / f"{target}.app.bin"
    disc = build / "disc" / f"{target}.cue"
    for f in (elf, app, disc, bios, probe):
        if not f.is_file():
            raise SystemExit(f"missing: {f}")

    out = out_root / name
    out.mkdir(parents=True, exist_ok=True)
    addresses = resolve_symbols(elf, wanted)

    cmd = [
        str(probe), "--boot-mode", "injection", "--iso", str(disc), "--bin", str(app),
        "--bios", str(bios), "--boot-frames", "90", "--frames", str(frames),
        "--sh2-cache", "on", "--ram-cart", "4m",
        "--out", "probe.json", "--misaligned-report", "misaligned.txt",
        "--profile-cycles", "cycles.csv",
    ]
    if pad_script is not None:
        cmd += ["--pad-script", str(pad_script.resolve())]
    for key, address in addresses.items():
        cmd += ["--dump-mem", f"0x{address:08x}:4:{key}.bin"]
    for field in sorted({0, frames - 1}):
        cmd += ["--screenshot", f"{field}:field-{field:05d}.png"]

    with (out / "probe.log").open("w", encoding="utf-8") as log:
        subprocess.run(cmd, cwd=str(out), stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=timeout)

    state = {}
    for k in addresses:
        b = (out / (k + ".bin")).read_bytes()
        state[k] = struct.unpack(">I", b[:4])[0] if len(b) >= 4 else 0
    report = json.loads((out / "probe.json").read_text())
    state["emulated_fields"] = report.get("frames_run", frames)
    state["app_sha256"] = hashlib.sha256(app.read_bytes()).hexdigest()

    cycles_path = out / "cycles.csv"
    if cycles_path.is_file():
        vals = []
        with cycles_path.open() as fh:
            for row in csv.DictReader(fh):
                vals.append(int(row["master_sh2_cycles"]))
        if vals:
            # Drop the first frames: one-off caches/TLB warm-up is not the loop cost.
            steady = vals[1:] or vals
            state["sh2_cycles_per_emulated_frame"] = round(sum(steady) / len(steady), 1)
    return state


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, default=ROOT / "build-ng")
    p.add_argument("--probe", type=Path)
    p.add_argument("--bios", type=Path, default=None)
    p.add_argument("--frames", type=int, default=1200,
                   help="emulated Saturn frames to run the app for")
    p.add_argument("--out", type=Path)
    p.add_argument("--timeout", type=int, default=1800)
    p.add_argument("--target", choices=list(TARGETS), default=None,
                   help="run only one target (default: all)")
    p.add_argument("--pad-script", type=Path, default=None,
                   help="optional Ymir pad-script to drive controller input")
    a = p.parse_args()

    build = a.build.resolve()
    bios = (a.bios or DEFAULT_BIOS).resolve()
    probe = a.probe.resolve() if a.probe else (build.parent / "build-ymir/probe.exe")
    if not probe.is_file():
        p.error(f"probe missing: {probe}")
    out_root = (a.out or ROOT / "local" / "ymir" /
                ("bench-" + time.strftime("%Y%m%d-%H%M%S"))).resolve()
    out_root.mkdir(parents=True, exist_ok=False)

    results = {}
    for name in ([a.target] if a.target else TARGETS):
        state = run_target(name, build, bios, probe, a.frames, out_root, a.timeout,
                           a.pad_script)
        emulated = max(1, state["emulated_fields"])
        state["guest_frames"] = state["ng_boot_frames"]
        state["guest_frames_per_emulated_frame"] = round(state["ng_boot_frames"] / emulated, 4)
        # 60 Hz is real time; this is how many times slower/faster than the 68000.
        state["guest_seconds"] = round(state["ng_boot_frames"] / 60.0, 3)
        state["emulated_seconds"] = round(emulated / 60.0, 3)
        results[name] = state

    summary = {
        "note": ("Ymir injection. Both targets run the same BIOS-only program; only the "
                 "68000 core differs. Emulator evidence, not physical-console timing."),
        "emulated_frames": a.frames,
        "targets": results,
    }
    if {"interp", "recomp"} <= results.keys():
        gi = results["interp"]["guest_frames_per_emulated_frame"]
        gr = results["recomp"]["guest_frames_per_emulated_frame"]
        summary["recomp_speedup_vs_interp"] = round(gr / gi, 3) if gi else None
    (out_root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    print(f"Evidence: {out_root}")

    for name, s in results.items():
        bad = s.get("ng_boot_recomp_bad", 0)
        tag = f" bad_dispatch={bad}" if name == "recomp" else ""
        print(f"{name:7s}: {s['guest_frames']} guest frames in {s['emulated_fields']} "
              f"emulated frames = {s['guest_frames_per_emulated_frame']:.4f}x real time"
              f"{tag}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())