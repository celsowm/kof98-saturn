# Real-frame replay (local only)

The synthetic 72-sprite CD/cart/VDP1 probe built and displayed correctly in Mednafen on Windows on 2026-11-14; a user screenshot showed the expected grid. This did not measure timing or pixel-exact parity. The next gate captures actual KOF98 data from the pinned desktop oracle.

The capture build is interpreter-only (no expensive generated C needed), headless, native host compiler. It creates an instrumented copy of video.c in the build folder, leaving the upstream checkout untouched. At each visible line of the selected zero-based frame, it invokes our LSPC IR and strip coalescer, records the raw backdrop and quantized RGB555 sprite-only reference before FIX. No widescreen, no full-screen bitmap transfer. The packer independently decodes selected C ROM tiles, reconstructs all pixels and rejects any mismatch against the upstream screenshot before publishing a local atlas. It discards fully transparent strips, deduplicates patterns and LUTs, uses original patterns plus hardware flips for native strips and exact samples for variants. The static sprite replay reads the packed atlas via CD and CRC-checked cartridge store, uploads once to VDP1, then emits normal sprite commands. This diagnostic intentionally bypasses the dynamic cache and does not prove combat memory or streaming performance. FIX absent, RGB555 quantization of Neo Geo dark bit, uniform backdrop required. 384 KiB atlas and 2048 draws are initial offline limits; SDK actual free VRAM and command budget checked on Saturn.

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

Open build-saturn/disc/kof_frame_replay.cue with the same Mednafen launcher and extram4. Compare to the sprite-only reference, not full-reference.png which includes FIX. Read replay stage/frame counters via map if needed. No ROM-derived source, capture, binary, PNG or disc may be committed. The capture path, converter, and replay target were validated for the single frame documented below; no timing or physical-console claim.


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
