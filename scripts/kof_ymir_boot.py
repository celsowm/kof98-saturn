#!/usr/bin/env python3
"""Run the native 68000 Neo Geo BIOS boot (kof_ng_boot) in Ymir.

Injects the app, emulates the SH-2 executing the real BIOS, and dumps the
ng_boot_* counters: instructions executed by Musashi on the SH-2, frames, vblank
IRQs, LSPC VRAM/palette writes. Proves native 68000 execution of the actual Neo
Geo BIOS on Saturn. Emulator evidence only; not physical-console timing.

No emulator is linked into the port.
"""
import argparse
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
SYMBOLS = (
    "ng_boot_instructions",
    "ng_boot_frames",
    "ng_boot_vblanks",
    "ng_boot_timer_irqs",
    "ng_boot_vram_writes",
    "ng_boot_pal_writes",
    "ng_boot_bios_instr",
    "ng_boot_cart_instr",
    "ng_boot_wram_instr",
    "ng_boot_irq_taken",
    "ng_boot_io_writes",
    "ng_boot_last_pc",
    "kof_live_sprite_count",
    "kof_live_sprite_hash",
    "kof_live_ng_cycles",
    "kof_live_draw_strips",
    "kof_live_draw_native",
    "kof_live_draw_variants",
    "kof_live_tile_uploads",
    "kof_live_draw_error",
    "kof_ym_samples",
    "kof_ym_written",
)
DEFAULT_BIOS = Path(os.getenv(
    "LIBSATURN_KOF_BIOS",
    r"C:\Users\celso\Documents\projetos\libsaturn-1\bios\saturn_bios_jp.bin"))


def resolve_symbols(elf: Path) -> dict:
    nm = shutil.which("sh2eb-elf-nm")
    if not nm and os.name == "nt":
        msys = Path(os.getenv("LIBSATURN_MSYS2_ROOT", r"C:\msys64"))
        c = msys / "home" / Path.home().name / "saturn-tools/bin/sh2eb-elf-nm.exe"
        if c.is_file():
            nm = str(c)
    found = {}
    if nm:
        r = subprocess.run([nm, "-S", "--defined-only", str(elf)], text=True, capture_output=True)
        if r.returncode == 0:
            for line in r.stdout.splitlines():
                m = re.match(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\w\s+_?((?:ng_boot|kof_live|kof_ym)_\w+)$", line)
                if m and int(m[2], 16) == 4:
                    found[m[3]] = int(m[1], 16)
    for p in (elf.with_suffix(".elf.map"), elf.with_suffix(".map")):
        if not p.is_file():
            continue
        for line in p.read_text(errors="replace").splitlines():
            m = re.match(r"^\s*(0x[0-9a-fA-F]+)\s+_?((?:ng_boot|kof_live)_\w+)\s*$", line)
            if m:
                found.setdefault(m[2], int(m[1], 16))
    missing = set(SYMBOLS) - found.keys()
    if missing:
        raise ValueError(f"missing ELF/map symbols: {sorted(missing)}")
    return {k: found[k] for k in SYMBOLS}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, default=ROOT / "build-ng")
    p.add_argument("--probe", type=Path)
    p.add_argument("--bios", type=Path, default=None)
    p.add_argument("--out", type=Path)
    p.add_argument("--frames", type=int, default=300)
    p.add_argument("--timeout", type=int, default=1200)
    p.add_argument("--target", default="kof_ng_boot",
                   help="application target name (e.g. kof_ng_hybrid_bench)")
    p.add_argument("--dump-audio", action="store_true",
                   help="dump the emulated SCSP audio to audio.pcm")
    a = p.parse_args()

    build = a.build.resolve()
    elf = build / (a.target + ".elf")
    app = build / (a.target + ".app.bin")
    disc = build / ("disc/" + a.target + ".cue")
    bios = (a.bios or DEFAULT_BIOS).resolve()
    for f in (elf, app, disc, bios):
        if not f.is_file():
            p.error(f"missing: {f}")

    out = (a.out or ROOT / "local/ymir" / ("boot-" + time.strftime("%Y%m%d-%H%M%S"))).resolve()
    out.mkdir(parents=True, exist_ok=False)
    addresses = resolve_symbols(elf)
    probe = a.probe.resolve() if a.probe else (build.parent / "build-ymir/probe.exe")
    if not probe.is_file():
        p.error(f"probe missing: {probe}")

    cmd = [
        str(probe), "--boot-mode", "injection", "--iso", str(disc), "--bin", str(app),
        "--bios", str(bios), "--boot-frames", "90", "--frames", str(a.frames),
        "--sh2-cache", "on", "--ram-cart", "4m",
        "--out", "probe.json", "--misaligned-report", "misaligned.txt",
    ]
    if a.dump_audio:
        cmd += ["--dump-audio", "audio.pcm"]
    for key, address in addresses.items():
        cmd += ["--dump-mem", f"0x{address:08x}:4:{key}.bin"]
    if "ng_boot_frames" in addresses:
        cmd += ["--screenshot-when",
                f"0x{addresses['ng_boot_frames']:08x}:778+2:render-0778.png"]
    for field in sorted({0, a.frames - 1}):
        cmd += ["--screenshot", f"{field}:field-{field:05d}.png"]

    with (out / "probe.log").open("w", encoding="utf-8") as log:
        subprocess.run(cmd, cwd=str(out), stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=a.timeout)

    state = {}
    for k in addresses:
        b = (out / (k + ".bin")).read_bytes()
        state[k] = struct.unpack(">I", b[:4])[0] if len(b) >= 4 else 0
    report = json.loads((out / "probe.json").read_text())
    state["emulated_fields"] = report.get("frames_run", a.frames)
    state["app_sha256"] = hashlib.sha256(app.read_bytes()).hexdigest()
    state["note"] = ("Ymir injection; SH-2 executes the real Neo Geo BIOS and, with the cart, "
                     "the game's own P ROM code via Musashi. Emulator evidence, not physical-console timing.")
    (out / "summary.json").write_text(json.dumps(state, indent=2) + "\n")
    print(json.dumps(state, indent=2))
    print(f"Evidence: {out}")

    executed = state["ng_boot_instructions"] > 0 and state["ng_boot_bios_instr"] > 0
    game = state["ng_boot_cart_instr"] > 0
    print("GAME P ROM CODE EXECUTING ON SATURN SH-2" if game
          else "68000 BIOS EXECUTING ON SATURN SH-2" if executed
          else "NO 68000 EXECUTION")
    return 0 if executed else 1


if __name__ == "__main__":
    raise SystemExit(main())