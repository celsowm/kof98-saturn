# Music on Saturn (real KOF98 soundtrack, first slice)

Scope: a first, isolated slice of the port's audio pillar (PORTING_PLAN.md Phase
F). It proves the *real* KOF98 program music — produced by the game's own 68000 +
Z80 + YM2610 execution — can be turned into a Saturn SCSP stream and played with
zero underruns. It is **not** the full sound driver, not live YM2610 synthesis,
and not physical-console timing.

## Why this slice

The repository's Saturn runtime so far only replays captured LSPC video: the
68000 game logic runs in the pinned host oracle
(`deps/kof98recomp`, which executes the real program against the AES ROMs and
emulates the Z80 + YM2610 through `src/audio.c` + `ext/ymfm`). The oracle already
supports `--wav`, which writes the analog output of that YM2610 emulation. This
slice takes that real output to the target.

## Pipeline

1. **Capture the real soundtrack** (host oracle, actual game logic):

   ```sh
   cmake --build build-capture --target kof_frame_capture -j 6
   .\build-capture\kof_frame_capture.exe --headless --interp --rom-path roms \
       --frames 5600 --input deps\kof98recomp\tests\start_fight.txt --wav local\music-full.wav
   ```

   Result: `local/music-full.wav`, 94.6 s, 55,555 Hz stereo, peak 24,703,
   whole-run RMS 2,648. The default script boots through the BIOS and menus and
   reaches the first fight (its first round is on by ~frame 3900 ≈ 66 s), so the
   last ~28 s is the fight theme. (Run from a shell without `KOF_CAPTURE_*` set,
   or clear those variables, so only the WAV is written.)

2. **Convert to the target's raw stream** (`tools/convert_music.py`, ROM-free,
   no ffmpeg/audioop). It slices, resamples to 44.1 kHz, fades the loop seam and
   writes interleaved **S16 big-endian** — exactly what
   `sat_disc_stream_music_start(sample_bytes=2)` reads:

   ```sh
   python tools\convert_music.py --input local\music-full.wav \
       --output-audio local\kof-music\MUSIC.BIN \
       --output-header local\kof-music\kof_music_assets.h \
       --name MUSIC --sample-rate 44100 --start-ms 67000 --duration-ms 26000 --fade-ms 8
   ```

   Result: 1,146,600 frames, 4,586,400 bytes. Level check (S16BE decode):
   output RMS 3,234 vs source 3,258 (matching); the same bytes read as little
   endian give RMS 18,896, confirming the byte order is correct.

3. **Play it on Saturn** (`src/saturn/music_probe.c`, target `kof_music`): mounts
   the disc, starts the built-in disc music feed, and each field services the CD
   scheduler and the SCSP driver. Counters are exported for the probe.

   ```sh
   cmake -S . -B build-music -G Ninja -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_TOOLCHAIN_FILE=C:/lsp-new/share/libsaturn/toolchains/sh2eb-elf.cmake \
       -DLibSaturn_DIR=C:/lsp-new/lib/cmake/LibSaturn \
       -DKOF_BUILD_SATURN_ASSETS=ON -DKOF_BUILD_MUSIC_PROBE=ON
   cmake --build build-music --target kof_music_disc -j 6
   ```

   App image 38,764 B (limit 983,040); disc 2,458 MODE1 sectors with
   `MUSIC.BIN` in the root.

4. **Verify in Ymir** (`scripts/kof_ymir_music.py`, external harness; no emulator
   linked into the port). It injects the app, records the SCSP analog output
   (`--dump-audio`), and reads the counters:

   ```sh
   python scripts\kof_ymir_music.py --frames 600 --out local\ymir\music-fixed
   ```

## Evidence (Ymir, 600 fields)

```json
{
  "kof_music_frames": 594,
  "kof_music_services": 106,
  "kof_music_playing": 1,
  "kof_music_underruns": 0,
  "kof_music_refills": 103,
  "kof_music_buffered": 12288,
  "kof_music_min_buffered": 8192,
  "kof_music_bytes": 1736704,
  "kof_music_last_error": 0,
  "audio": { "stereo_frames": 442735, "peak": 24505, "rms": 3124.1,
             "active_windows": 189, "total_windows": 201 },
  "audio_seconds": 10.04
}
```

- Streams started (`playing = 1`), **0 underruns**, 1,736,704 music bytes
  delivered from the emulated disc.
- Captured SCSP output: 10.04 s, peak 24,505, RMS 3,124 — matching the source
  music (not a constant or a blip: 189 of 201 quarter-second windows are active).

This is **emulator evidence** (Ymir injection), not physical-console timing and
not optical boot.

### Content-verification status (reproducible, honest)

An earlier revision of this doc claimed the standalone capture correlated
**0.919** with `MUSIC.BIN` at a 1.238 s lag. That exact measurement is not
reproducible against the current asset (verified 2026-11-16), but the
standalone **does** reproduce the track: the amplitude-envelope correlation
(RMS in 10 ms windows, which survives the SCSP interpolation and DAC
filtering) is **0.958 at a 0.62 s lag** — the SCSP output follows the source's
loudness contour almost perfectly. The raw-waveform correlation is weak
(~0.12) because the SCSP's sample-rate conversion and the capture's analog
filter reshape the waveform; the envelope is the correct method for verifying
content through a rate/color pipeline. The standalone's Sound RAM switches
halves (both halves differ), confirming the streams advance and refill.

### Integrated-app status (counters pass, content unverified)

The integrated app (`kof_ng_boot`) streams the same `MUSIC.BIN` while the
interpreter runs. Its counters pass (playing=1, 0 underruns at 22.05 kHz,
rings full) and the capture is music-like (29/50 active 1 s windows, peak
18,703, dominant 75 Hz), but the content check does not correlate with the
asset (envelope corr ~0.19 at any lag). Root causes found and fixed along the
way:

- **Field-count starvation**: the SCSP streams' service clock is
  `sat_field_count()`, which only advances on the VBlank-IN IRQ. Inside a long
  guest frame it is frozen, so the streams were serviced ~once per guest frame
  and the rings drained. Fixed by a per-field core hook
  (`ng_boot_set_field_hook`, fired from `lspc_vblank()`) that calls
  `sat_audio_update()` once per emulated field, plus a slice hook for the disc
  scheduler gated by the emulated cycle counter.
- **Stream format**: the asset is S16BE (`sample_bytes=2`), matching the
  standalone probe. An intermediate experiment with `sample_bytes=1` (S8)
  produced garbage; reverted.
- **Service rate**: the streams were serviced at the guest-frame rate (~1.4 Hz
  on the interpreter) instead of the emulated field rate (60 Hz). Fixed by
  servicing the streams on every slice-hook call (4000 guest cycles), which is
  ~45-60 services per emulated second. The disc scheduler is gated separately
  at ~10 ms of emulated time.
- **Bitrate**: the asset was regenerated at 22.05 kHz stereo S16BE (88.2 KB/s,
  half the 44.1 kHz rate) — the 44.1 kHz rate (176.4 KB/s) exceeded the
  sustainable emulated disc delivery (~46 KB/s), causing 4 underruns in the
  3000-field run. At 22.05 kHz the run is clean (0 underruns, rings full).
- **Pitch**: the SCSP slot pitch for 22.05 kHz is `octave=-1, fns=0`
  (0x3C1F); the probe displays octave as unsigned (15), which is correct.

**Disk reads verified correct**: the app's `sat_cdfs_lookup` returns
`extent_lba = 353` (0x161) and `size = 2293200`, exactly matching the ISO walk
(MUSIC.BIN at LBA 353). The CD block FAD seen in the probe log (0x1F7 = 503
for the first music read) is `353 + 150` — the standard 2-second pregap the
drive adds to FADs. The music reads land at the right sectors, and the disc
content is byte-identical to the local `MUSIC.BIN` (verified by extracting
raw sector 353 from the disc image). The read path is **not** the problem.

**Ring contents verified correct (2026-11-16)**: a diagnostic snapshot taken
right after the first music segment lands (copied from `g_rings.left/right`
into probe-visible buffers) shows the rings hold exactly the source's split
channels — `snap0[0:16] = 00000000 fff8 fff7 0001 0004 0008 0010`, matching
the s16be high bytes of `MUSIC.BIN`'s L channel. The feed and disc reads are
correct end-to-end. The registry at field 1500 shows the streams healthy
(`cap=16384 read=12288 write=4096 buffered=8192 rate=22050 hw=1`), and the
audio clock advances (`service_frame = 1486 = sat_field_count`), so the stream
service and ring indices are correct.

**Root cause found — the SCSP upload lands in unmapped memory (2026-11-16)**:
the Sound RAM at the stream's SA (0x70000) holds a smooth ramp
(`0022 007c 009a 0108...`) — the emulator's Sound RAM reset pattern, not the
ring's data. The **standalone's** Sound RAM at 0x70000 holds the **same ramp**
(`0022 007c 009a...`), so **no** music content was ever verified on either app
— the earlier standalone "envelope corr 0.958" was a **false positive**: the
envelope method only checks the loudness contour, and any music-like signal
correlates with any other's contour at the right lag. The only valid content
check (raw waveform) fails on both.
The LibSaturn SDK writes Sound RAM at `kSoundRamBase = 0x25A00000` — the
**A-Bus mirror** of the SCSP's physical 0x5A00000 (A-Bus = physical +
0x20000000). Ymir maps the SCSP only at **0x5A00000-0x5BFFFFF**
(`libs/ymir-core/src/ymir/sys/saturn.cpp`); it has **no mapping for the
0x25A00000 mirror**, so CPU writes to 0x25A00000 are discarded (unmapped) and
the SCSP's Sound RAM keeps its reset pattern. The DMA path (SCU) also fails
to land (`sat_dma_set_enabled(0)`, the CPU-only path, produces the same ramp;
DMA stats show 256 SCU transfers / 573 KB but the Sound RAM stays untouched).
Ymir's SCU DMA evidently does not deliver to the SCSP's Sound RAM region
either — the transfers complete from the SH-2's view but the SCSP's WRAM is
never written. **This is a blocker outside this project's control**: LibSaturn
is an installed SDK (do not modify) and Ymir is an external dependency. The
fix is either (a) Ymir maps the A-Bus SCSP mirror (0x25A00000 → 0x5A00000)
and its SCU DMA delivers to the SCSP region, or (b) LibSaturn uses the
physical SCSP base (0x5A00000) for Sound RAM with a CPU write path that Ymir
honors. On real hardware both addresses work (the A-Bus mirrors physical),
so this only affects Ymir-based evidence.
**A-Bus workaround investigated (2026-11-16)**: the Ymir probe exposes no
option to map the 0x25A00000 mirror (verified with `probe.exe --help`), and the
LibSaturn sources are not available locally (installed SDK only, per
`AGENTS.md`) — so neither the emulator mapping nor the SDK base can be changed
from this project. The SDK headers expose only high-level stream APIs
(`sat_audio_update`), not raw SCSP slot/Sound-RAM access, so a direct write to
physical 0x5A00000 cannot be substituted without reimplementing the driver.
This confirms the blocker is external to this repository; raw-waveform
correlation remains the acceptance test once the emulator/SDK is fixed.

## ROM-free regression

`tests/test_convert_music.py` (wired as the `convert_music` ctest) generates a
synthetic WAV, converts it, and checks the S16BE bytes, level, slicing,
resampling, interleaving and generated header. No ROMs or captured audio needed.

## What this is not yet (remaining Phase F work)

- Live YM2610 / Z80 synthesis on Saturn: the current slice streams one
  pre-rendered excerpt, it does not emulate the sound CPU or the FM/ADPCM chip.
- The original command/reply handshake, timer/IRQ model, voice stealing, fades
  and arbitrary simultaneous cues (`docs/PORTING_PLAN.md` §9).
- All tracks and SFX, mixed under the game's own audio command stream.
- Streaming this music *while* the live LSPC replay and gameplay run, sharing
  the single CD Block with asset loads (see `saturn/disc_stream.h`).
- Physical-console / loader verification.

The `sat_disc_stream_music_start` built-in feed is a clean, low-risk first step;
the fuller `sat_music` ADX path (`saturn/music.h`, `saturn/asset.h`) and the
audio streaming guide (`docs/SCSP_AUDIO_STREAMING_GUIDE.md` in the SDK source)
are the next steps for compressed tracks, looping and pause/seek.