#!/usr/bin/env python3
"""Run the KOF98 real-soundtrack target (kof_music) in Ymir and verify that the
Saturn SCSP is actually playing the game's music.

It injects the app over the BIOS, services the emulated disc, records the SCSP
analog output (--dump-audio), dumps the kof_music_* counters from the ELF/map,
and reports ring fill / underruns plus an RMS check of the captured audio.

No emulator is linked into the port; this is an external harness, like
scripts/kof_ymir.py. Emulator injection is not optical boot and not physical
console timing.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
from array import array
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SYMBOLS = (
    "kof_music_frames",
    "kof_music_services",
    "kof_music_playing",
    "kof_music_underruns",
    "kof_music_refills",
    "kof_music_buffered",
    "kof_music_min_buffered",
    "kof_music_bytes",
    "kof_music_last_error",
)
DEFAULT_BIOS = Path(
    os.getenv(
        "LIBSATURN_KOF_BIOS",
        r"C:\Users\celso\Documents\projetos\libsaturn-1\bios\saturn_bios_jp.bin",
    )
)


def resolve_symbols(elf: Path) -> dict:
    nm = shutil.which("sh2eb-elf-nm")
    if not nm and os.name == "nt":
        msys = Path(os.getenv("LIBSATURN_MSYS2_ROOT", r"C:\msys64"))
        candidate = msys / "home" / Path.home().name / "saturn-tools/bin/sh2eb-elf-nm.exe"
        if candidate.is_file():
            nm = str(candidate)
    found = {}
    if nm:
        r = subprocess.run([nm, "-S", "--defined-only", str(elf)],
                           text=True, capture_output=True)
        if r.returncode == 0:
            for line in r.stdout.splitlines():
                m = re.match(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\w\s+_?(kof_music_\w+)$", line)
                if m and int(m[2], 16) == 4:
                    found[m[3]] = int(m[1], 16)
    for p in (elf.with_suffix(".elf.map"), elf.with_suffix(".map")):
        if not p.is_file():
            continue
        for line in p.read_text(errors="replace").splitlines():
            m = re.match(r"^\s*(0x[0-9a-fA-F]+)\s+_?(kof_music_\w+)\s*$", line)
            if m:
                found.setdefault(m[2], int(m[1], 16))
    missing = set(SYMBOLS) - found.keys()
    if missing:
        raise ValueError(f"missing ELF/map symbols: {sorted(missing)}")
    return {k: found[k] for k in SYMBOLS}


def analyze_audio(path: Path):
    data = path.read_bytes()
    n = len(data) // 2
    if n == 0:
        return {"stereo_frames": 0}
    a = array("h")
    a.frombytes(data[: n * 2])
    if os.sys.byteorder != "little":
        a.byteswap()
    peak = max(abs(x) for x in a) if a else 0
    rms = (sum(x * x for x in a) / len(a)) ** 0.5 if a else 0.0
    # Count 10 ms windows that are clearly non-silent, to prove music vs. a
    # constant DC or a short blip.
    win = max(1, len(a) // 200)
    active = 0
    for i in range(0, len(a), win):
        seg = a[i:i + win]
        if seg and (sum(x * x for x in seg) / len(seg)) ** 0.5 > 200.0:
            active += 1
    return {
        "stereo_frames": n // 2,
        "peak": peak,
        "rms": round(rms, 1),
        "active_windows": active,
        "total_windows": (len(a) + win - 1) // win,
    }


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, default=ROOT / "build-music")
    p.add_argument("--probe", type=Path)
    p.add_argument("--bios", type=Path, default=None)
    p.add_argument("--name", default="kof_music",
                   help="target base name (kof_music, kof_ng_boot, ...)")
    p.add_argument("--out", type=Path)
    p.add_argument("--frames", type=int, default=1200)
    p.add_argument("--jobs", type=int, default=6)
    p.add_argument("--timeout", type=int, default=1200)
    a = p.parse_args()

    build = a.build.resolve()
    elf = build / f"{a.name}.elf"
    app = build / f"{a.name}.app.bin"
    disc = build / "disc" / f"{a.name}.cue"
    bios = (a.bios or DEFAULT_BIOS).resolve()
    for f in (elf, app, disc, bios):
        if not f.is_file():
            p.error(f"missing: {f}")

    out = (a.out or ROOT / "local/ymir" / ("music-" + __import__("time").strftime("%Y%m%d-%H%M%S"))).resolve()
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
        "--dump-audio", "scsp.pcm", "--dump-sound-ram", "sound-ram.bin",
    ]
    for key, address in addresses.items():
        cmd += ["--dump-mem", f"0x{address:08x}:4:{key}.bin"]
    for field in (0, a.frames // 3, 2 * a.frames // 3, a.frames - 1):
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
    state["audio"] = analyze_audio(out / "scsp.pcm")
    state["audio_seconds"] = round(state["audio"].get("stereo_frames", 0) / 44100.0, 2)
    state["note"] = ("Ymir injection boot; SCSP analog output captured, not physical "
                     "console timing. Rings fill before playback starts.")
    (out / "summary.json").write_text(json.dumps(state, indent=2) + "\n")
    print(json.dumps(state, indent=2))
    print(f"Evidence: {out}")

    ok = (state["kof_music_playing"] == 1
          and state["audio"].get("active_windows", 0) > 0
          and state["kof_music_underruns"] == 0)
    print("MUSIC PLAYING ON SATURN SCSP" if ok else "MUSIC NOT CONFIRMED")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())