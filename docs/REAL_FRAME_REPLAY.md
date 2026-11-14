# Real-frame replay (local only)

The synthetic 72-sprite CD/cart/VDP1 probe built and displayed correctly in Mednafen on Windows on 2026-11-14; a user screenshot showed the expected grid. This did not measure timing or pixel-exact parity. The next gate captures actual KOF98 data from the pinned desktop oracle.

The capture build is interpreter-only (no expensive generated C needed), headless, native host compiler. It creates an instrumented copy of video.c in the build folder, leaving the upstream checkout untouched. KFR2 captures both sprite and FIX strips and the quantized RGB555 completed reference after FIX. Old KFR1 captures remain readable and sprite-only. No widescreen, no full-screen bitmap transfer. The packer independently decodes selected C ROM tiles, reconstructs all pixels and rejects any mismatch against the upstream screenshot before publishing a local atlas. It discards fully transparent strips, deduplicates patterns and LUTs, uses original patterns plus hardware flips for native strips and exact samples for variants. The static sprite replay reads the packed atlas via CD and CRC-checked cartridge store, uploads once to VDP1, then emits normal sprite commands. This diagnostic intentionally bypasses the dynamic cache and does not prove combat memory or streaming performance. RGB555 quantization of Neo Geo dark bit and uniform backdrop required. FIX is emitted last as transparent LUT4 sprites; VDP2 optimization is deferred. 320 KiB atlas and 2048 draws are initial offline limits; SDK actual free VRAM and command budget checked on Saturn.

## Windows PowerShell (from KOF root)

Use a fresh checkout of the pinned oracle in deps:

    git clone https://github.com/danilopaulinodasilva/kof98recomp.git deps/kof98recomp
    git -C deps/kof98recomp checkout 44f873a3cca3827a48fca30fa8091da746311af0
    git -C deps/kof98recomp submodule update --init --recursive

All extracted ROMs must be present in roms, including neo-po.bin (CRC32 16d0c132). Use a native C/C++ compiler (e.g. MSYS2 UCRT64 gcc/g++ or VS developer prompt), never sh2eb-elf.

    cmake -S . -B build-capture -G Ninja -DCMAKE_BUILD_TYPE=Release -DKOF_BUILD_FRAME_CAPTURE=ON -DKOF_UPSTREAM_DIR=E:/projetos/kof98-saturn/deps/kof98recomp -DBUILD_TESTING=OFF
    cmake --build build-capture --target kof_frame_capture --parallel 6
    New-Item -ItemType Directory -Force local
    $env:KOF_CAPTURE_FRAME = "4350"
    $env:KOF_CAPTURE_OUT = "$PWD\local\fight.kfr"
    .\build-capture\kof_frame_capture.exe --headless --interp --rom-path roms --frames 4351 --input deps/kof98recomp/tests/start_fight.txt --screenshot 4350:local/full-reference.png
    python tools/pack_frame.py local/fight.kfr roms local/fight-frame

Inspect report.json and reference.ppm/replay.ppm. Zero mismatch is required. If an unsupported case is reported, retain the report and choose a different capture index or improve the renderer; do not silently truncate.

    cmake -S . -B build-saturn -DKOF_FRAME_DIR=E:/projetos/kof98-saturn/local/fight-frame
    cmake --build build-saturn --target kof_frame_replay_disc --parallel 6

Open build-saturn/disc/kof_frame_replay.cue with the same Mednafen launcher and extram4. For KFR2 compare to the complete reference; legacy KFR1 is sprite-only. Read replay stage/frame counters via map if needed. No ROM-derived source, capture, binary, PNG or disc may be committed. The legacy KFR1 path was validated for the single frame documented below. KFR2 and sequence playback require separate real-ROM validation; no timing or physical-console claim.


## First real frame acceptance — 2026-11-14

User-reported Windows build and Mednafen 1.32.1 run, pinned upstream, AES set, frame 4350 of tests/start_fight.txt. The original screenshot shows Kyo versus Yamazaki and the replay screenshot visually shows the matching sprite-layer scene without FIX. No game images or ROM-derived outputs are stored in the repository.

| Metric | Observed |
|---|---:|
| Scanline spans | 10,599 |
| Coalesced strips | 707 |
| Fully transparent strips removed | 45 |
| Emitted sprites | 662 |
| Native sprites | 553 |
| Exact transformed variants | 109 |
| Unique source tile IDs | 543 |
| Unique atlas patterns | 593 |
| Unique atlas palettes | 34 |
| Atlas bytes (reported before final padding) | 71,376 |
| Host RGB555 pixel mismatches | 0 / 71,680 |
| Backdrop colors | 1 |
| Saturn initialized application bytes | 47,416 |

The host comparison is between the sprite-only upstream raster (quantized to RGB555) and final atlas command replay, not a pixel comparison of the emulator framebuffer. The Saturn image booted with 4 MiB expansion and visually reproduced the frame. FIX/HUD remains absent by design. Optional Mednafen SBI, override configuration and cheat-file warnings did not block boot.

The original static atlas capacity is not evidence of dynamic gameplay residency. Next gates: add FIX replay with correct palette and raster semantics; capture multiple frames and measure texture/CLUT churn, list sizes, cache conflicts and upload bytes; then obtain target draw/CPU timing, and eventually integrate game execution. Keep a VDP1-only reference path before experimenting with moving suitable layers to VDP2.


## Complete FIX and consecutive frame replay

KFR2 adds FIX tile identifiers 524288..532479 (cart and BIOS banks) without altering the existing strip record format. A second coalescer captures 40 FIX columns per visible line; FIX is appended after the main sprite layer. Both FIX ROMs are decoded locally. The reference is now the final complete raster. Capture 1..600 frames using KOF_CAPTURE_COUNT; if greater than 1, KOF_CAPTURE_OUT must name an existing directory. Use a fresh directory for every run.

PowerShell from the repository root:

    git pull --ff-only origin main
    cmake --build build-capture --target kof_frame_capture --parallel 6
    .\scripts\setup-emulators.ps1
    $env:KOF_CAPTURE_COUNT = "1"
    $env:KOF_CAPTURE_FRAME = "4350"
    $env:KOF_CAPTURE_OUT = "$PWD\local\fight-fix.kfr"
    .\build-capture\kof_frame_capture.exe --headless --interp --rom-path roms --frames 4351 --input deps/kof98recomp/tests/start_fight.txt --screenshot 4350:local/full-reference.png
    python tools/pack_frame.py local/fight-fix.kfr roms local/fight-fix
    cmake -S . -B build-saturn -DKOF_FRAME_DIR=E:/projetos/kof98-saturn/local/fight-fix
    cmake --build build-saturn --target kof_frame_replay_disc --parallel 6
    kofrun frame

The setup script preserves user PATH entries and backs up the existing PowerShell profile before replacing only its managed block. Default JP/4 MiB launcher settings match the validated Mednafen run. It discovers Mednafen (including WinGet), Kronos and Yaba Sanshiro in known locations; optional -YmirExe accepts an installed Ymir executable. It does not install Ymir. The current session and future PowerShell sessions get saturn <cue> and kofrun probe/frame/sequence; the latter assumes the KOF repository root. Profiles are specific to the PowerShell edition used for installation.

Then capture 600 frames (4350..4949 inclusive):

    New-Item -ItemType Directory -Path local\fight-sequence
    $env:KOF_CAPTURE_COUNT = "600"
    $env:KOF_CAPTURE_OUT = "$PWD\local\fight-sequence"
    .\build-capture\kof_frame_capture.exe --headless --interp --rom-path roms --frames 4950 --input deps/kof98recomp/tests/start_fight.txt
    python tools/pack_sequence.py local/fight-sequence roms local/sequence-pack --start 4350 --count 600
    cmake -S . -B build-saturn -DKOF_SEQUENCE_DIR=E:/projetos/kof98-saturn/local/sequence-pack
    cmake --build build-saturn --target kof_sequence_replay_disc --parallel 6
    kofrun sequence

Each frame is independently validated against the quantized reference. The sequence packer rejects unsupported raster backdrop, mismatches, and over-budget frames rather than dropping primitives. It produces a CSV with per-frame command count, ideal upload bytes (resources absent in immediately preceding frame; not actual cache misses), and texture/palette reuse; the JSON summarizes the run. Sequence data consists of an aligned, deduplicated atlas and ordered 16-byte sprite records per frame. It is split into phases at 320 KiB atlas or 4 MiB cart capacity, each with CRC and no CD reads inside the phase. At the boundaries the previous atlas is replaced after the SDK fence; CD loading causes pauses. No real-game dynamic cache, seamless streaming, 60 FPS claim, or SH-2 game execution is implied. Next measure actual target display cadence and implement persistent bounded dynamic cache against these traces.

ROM-free regression: python tests/test_frame_sequence.py. The real KFR2 and sequence paths have not yet been executed against the local ROMs or target emulator.

## Sequence capacity regression (2026-11-14)

User run: complete KFR2 frame 4350 visually passed in Mednafen (914 commands, 252 FIX commands, 75,776 atlas bytes, zero host RGB555 mismatches). All 600 consecutive captures 4350–4949 passed offline (zero mismatches, mean 915.755 commands, peak 970; mean ideal adjacent-frame upload 3,466.925 bytes excluding first). Original 384 KiB pack produced five clips and max atlas 392,832 bytes. Target failed SAT_ERR_CAPACITY at sequence_replay.c:26, at the initial atlas reservation. Pinned LibSaturn VDP1 has a 32-byte root and two banks of 64 KiB commands plus 16 KiB Gouraud each, leaving at most 360,416 bytes before other allocations. The packer now limits aligned atlas size to 320 KiB, with ~32 KiB headroom for SDK and alignment. The target records available/requested bytes before reservation. Repack existing captures and rebuild; no recapture needed. New target result still pending.


## Loading feedback (2026-11-14)

The user reported successful animated sequence playback after repacking to 320 KiB and rebuilding; initial CD loading appeared as a black screen. The frame and sequence replays now share the LibSaturn loading screen used by sa2-saturn (src/platform/saturn_glue.c), with optional SDK HUD text, clip index, percentage and a progress bar. The initial CD mount screen has no percentage until the file size is known; each clip then reports actual CRC-checked bytes copied from CD to cartridge and bytes uploaded to VDP1. Updates are limited to approximately every 4 percentage points plus stage boundaries. The font is initialized before checking the atlas VRAM budget. Loading progress does not bypass CRC or modify draw lists during combat. No recapture or repack is required. The new loading screen has not yet been built/run on the user's target. This is feedback, not an optimization of CD transfer time; clip boundaries still block.


## Automated external Ymir harness

Use the existing GPL LibSaturn harness as an external process rather than manually timing Mednafen or linking any emulator code into the port. From the KOF root after building the current disc, run:

    python scripts/kof_ymir.py

The script reuses LIBSATURN_ROOT (set by setup-emulators.ps1), a user-owned BIOS, and LIBSATURN_YMIR_PROBE if provided. It checks the known SDK harness build locations, otherwise builds the pinned probe in ignored build-ymir/ on the KOF drive using the native compiler. It runs 2400 emulated fields by default, with cache on and 4 MiB expansion. --frames overrides the field budget, --probe selects an existing probe, --bios overrides the IPL, and --pc optionally records a slower CPU PC profile. Each run creates a new ignored local/ymir/ evidence directory. It exports per-field compositor observations, CPU instruction counts and VRAM/CRAM writes, screenshots keyed to guest submissions, raw big-endian symbol dumps and summary.json. Captures use the existing ELF/map to resolve telemetry addresses. Submission-triggered screenshots delayed two fields are diagnostic, not exact oracle-frame captures. The end-of-run rate includes loading pauses and is not distinct-picture FPS. For active-window analysis use the existing LibSaturn observer analyzer with an appropriate field interval/crop; no physical-hardware timing claim. Direct injection does not establish optical boot. If zero frames are submitted, preserve the logs and stage report rather than declaring success. The new wrapper has not yet been executed on the user's Windows installation.

### Windows stale-probe recovery

A previously installed probe was found but rejected --sh2-cache (exit 2); its usage also lacked --video-observations. The wrapper now probes CLI capabilities before selecting a binary. Incompatible auto-discovered binaries are skipped, and a compatible probe is built on the KOF drive from the LibSaturn harness source if necessary. Explicit --probe or LIBSATURN_YMIR_PROBE selections fail clearly if incompatible. If the source itself lacks the required options, update the existing LibSaturn checkout without discarding local changes. No cache-off fallback or ROM recapture is performed.
