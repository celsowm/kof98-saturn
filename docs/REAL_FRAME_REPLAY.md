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

## CD load optimization A (2026-11-14)

Baseline private Ymir capture: 2400 fields, 145 completed submissions, second clip still in CD load, cache on, zero reported misaligned accesses. The original loader performed 8 KiB steps at the clip's arbitrary file offset; each step could split into a head, full sectors and tail in CDFS. New sequence loader reserves 128 KiB scratch (120 KiB more WRAM), splits a partial leading sector once, and uses whole-sector batches thereafter. CRC remains IEEE CRC32 over the same exact logical bytes, now using a 1 KiB lookup table instead of eight bit iterations per byte. Both are synchronous; no combat-time read, publication, cart bank or CRC guarantee is changed. VRAM atlas uploads remain 8 KiB, and renderer/pacing are unchanged. Existing packs remain compatible. The host asset-store test now exercises a nonsector-aligned extent, expected read-call count, CRC and final bytes. Compare fresh 2400-field runs using identical pack, BIOS and probe. Configure -DKOF_SEQUENCE_LOAD_CHUNK_BYTES=8192 or -DKOF_ASSET_CRC_TABLE=OFF to isolate causes (8 KiB still uses aligned scheduling, so the original commit is the true old baseline). Target build/WRAM budget and emulator timing remain to be validated; if memory is tight, try 65536. No measured speedup claimed yet.

## VDP1 direct sprite batch experiment B

The sequence renderer now defaults to the existing LibSaturn sprite batch window (include/saturn/sprite_batch.h). Four LUT4 prefixes are initialized using SDK composition for the four flip combinations. Each CRC-verified record retains the existing bounds and flags checks; only size, LUT and texture address are filled before inline emission at the original screen position and order. The SDK writes the 16 bytes used by a normal sprite instead of the legacy 32-byte command, preserving command slot size, END quota and bank ownership. NO_CULL and KEEP_PRECLIP preserve the old path's relevant semantics; the unused normal-sprite words may differ. End-of-batch statistics must match the requested count or replay stops. No pack format, CD read size, timing policy, or static replay changes.

For an isolated A/B comparison, use the same pack, native probe, BIOS, cache setting and 2400-field budget. First configure -DKOF_SEQUENCE_BATCH=OFF -DKOF_SEQUENCE_BATCH_PRECLIP_OPT=OFF, rebuild the sequence disc and run scripts/kof_ymir.py --out local/ymir/load-only. Then configure -DKOF_SEQUENCE_BATCH=ON -DKOF_SEQUENCE_BATCH_PRECLIP_OPT=OFF, rebuild and run --out local/ymir/batch. Preserve the reports separately. Compare active playback intervals (not loading-inclusive submission rate), per-field CPU instructions and VDP1 writes, and screenshots keyed to submission counters. Delayed screenshot triggers are diagnostic; establish exact same-frame pixel parity separately before claiming equivalence. Optional preclip optimization is a separate experiment, off by default. The previous CD optimization and this batch path have not been benchmarked on the target yet; the expected reduction in command writes is not a measured speedup.

### Experiment B results and visual regression

Private Windows Ymir A/B: identical probe hash, cache on, 2400 program fields, no reported misaligned accesses. Load-only completed 375 submissions, batch completed 417. Using transfer CSV indices after excluding the 90 BIOS-only rows, first block playback is fields 640–855 inclusive (145 submissions) versus 640–784 (145); second block is 1438–1634 (132) versus 1368–1499 (132). Combined: 277/413 fields vs 277/277 fields, nominal 40.24 vs 60 submissions/s. VDP1 writes over these intervals: 4,065,834 vs 2,044,362 words; Master instructions 140,215,381 vs 92,050,518. However batch screenshots at submissions 1, 60, 300 show severe repeated-pattern corruption. The 145 screenshot is the next clip's loading screen in both, so it does not validate gameplay parity. Do not accept this speedup as a visual rendering result. A likely cause is the SDK inline emitter's incompatible uint32_t pointer reads of a locally mutated uint16_t prefix; LibSaturn commit acb2f0c399b8c65cdc9f31049aa0e26977598151 changes both inline emitters to pack named halfwords explicitly and adds a mutable-prefix regression test. Root cause and corrected performance still require target verification. This is a header-only SDK runtime change: install the updated sprite_batch.h into the SDK actually used by the KOF build (C:/lsp-new in the Windows session), rebuild and repeat batch benchmark with KEEP_PRECLIP. No ROM repack needed. Use KOF_SEQUENCE_BATCH=OFF as the known-good fallback until visual parity is demonstrated.

### Corrected batch qualification (2026-11-14)

The user's batch-fixed.zip confirms the SDK header correction acb2f0c399b8c65cdc9f31049aa0e26977598151. Same probe SHA-256 (1e32005f403840e05bbd900828603433a0a87c3b84d9ac966d9ef19153af6ba2), cache on, 2400 program fields; fixed application SHA-256 b03ddd03c16405bfef77752f939bf3e70631d3b35ac5f49e14328fa3f7918cf0. Submitted 417 frames (three complete clips); stopped loading clip 4 (zero-based index 3). No misaligned accesses. Same 277 submissions in 277 active fields across the first two complete clips (640–784 and 1368–1499), versus 413 fields for load-only: nominal 60 vs 40.24 submissions/s. The fixed batch wrote 2,044,362 VDP1 words vs 4,065,834 conventional and executed 93,244,933 master instructions vs 140,215,381 conventional. Exact RGB comparison of screenshots at submission counts 1, 60, 300: zero differences across all 215,040 pixels. For both first two active intervals the ordered run-length-collapsed compositor RGB fingerprints match the conventional path exactly (124 and 132 runs, respectively); this is fingerprint agreement, not a full pixel-by-pixel capture comparison. The corrected batch reaches 60 nominal submissions/s in those windows, not necessarily 60 distinct pictures/s: 256 RGB transitions across the 277 active fields, including interval entry transitions. Full sequence and hardware validation remain pending. The 2400-field run spent most of its time outside combat; future work should eliminate clip-boundary pauses, then qualify the FIX-to-VDP2 and cache path. Keep the corrected SDK header installed, and retain the conventional path for comparison.

## Resident replay experiment C

Build a new pack from the already RGB555-validated clip pack (no original ROM access or frame recapture):

    python tools/pack_resident.py local/sequence-pack local/resident-pack
    cmake -S . -B build-saturn -DKOF_SEQUENCE_DIR=E:/projetos/kof98-saturn/local/resident-pack
    cmake --build build-saturn --target kof_sequence_replay_disc -j 6
    python scripts/kof_ymir.py --frames 3600 --out local/ymir/resident

The packer verifies source clip CRCs and exact command/resource reconstruction; dictionary keys are padded 128-byte patterns and 32-byte LUTs. A deterministic offline LRU plans updates to 2304 pattern slots and 128 LUT slots (292 KiB atlas). It pins all resources of the current frame before evicting. Command order is unchanged; 8-byte literals and copy runs reference the previous ordered command list. Packet zero is independent, including at wrap. The full resident asset is CRC-checked once at boot and stays combat-locked; no CD read after boot. Consecutive changed slots are uploaded through the SDK fenced writer. Extra CPU/DMA overhead might reduce active cadence; measure before accepting. Counters record CD phase loads, cumulative atlas upload bytes/runs, frame index and submissions. The packer fails closed if the dictionary and packets exceed 4 MiB; the actual 600-frame pack size is not known until the user's local conversion. The original pack and renderer remain selectable by KOF_SEQUENCE_DIR. Resident experiment uses the fixed batch header, KEEP_PRECLIP and no culling, regardless of the conventional experiment toggle. This is an offline cache scheduling prototype, not yet a general live-game LSPC cache. No target test or speedup claimed yet.

### Resident replay qualification (2026-11-14)

Private user report resident.zip: same probe SHA-256 1e32005f403840e05bbd900828603433a0a87c3b84d9ac966d9ef19153af6ba2, cache on, NTSC nominal 60 Hz, application SHA-256 782aff2be65fdc4bd1c0dfa417e980ad4cd66c5d33999d6cbd325f290a53d944. 3600 program fields, 2882 submitted frames, final index 481, stage 4, one CD load phase, zero misaligned accesses. 4 complete 600-frame loops plus 482 submissions. First gameplay image at field 715; subsequent loop starts at 1316, 1917, 2518, 3119. No loading screen between loops. Each complete 600-image fingerprint sequence exactly equals the first, and all 481 available fields of the partial fifth loop match. One additional field per loop (601-field period), nominal 600*60/601 = 59.90 submissions/s across repeated full loops. Three exact RGB screenshot comparisons with batch-fixed at submission triggers 1,60,300: zero differing pixels out of 215040. Trigger 145 in the old run is a clip loading screen, not equivalent. First 144 and next 131 displayed combat fingerprints of the old complete clips agree positionally with resident fields 715..858 and 860..990. Not a complete independent oracle verification of all 600 composited frames. From field 715 onward, 2771 transitions in 2884 adjacent-field intervals (57.65 RGB changes/s, not distinct completed-frame FPS). Cumulative atlas uploads 9803424 bytes in 20258 fenced runs, about 3402 bytes and 7.03 runs per submission including loop resets; available VRAM before reservation 354272 bytes, reserved 299032, remaining 55240. Startup about 11.92 emulated seconds, 48.03 submissions/s including startup. Private capture excludes the resident pack report, so exact cart occupancy is still unreported; converter success and target load establish it fit. Boot was direct injection, no physical console timing claim. Next: qualify FIX on VDP2 as a separate experiment, preserving the resident batch as control; then move toward live LSPC input and runtime cache scheduling.

## FIX-to-VDP2 experiment D

Use the original clip pack (including frames.csv FIX suffix counts), not the resident pack:

    python tests/test_resident_sequence.py
    python tools/pack_resident.py local/sequence-pack local/fix-vdp2-pack --fix-vdp2
    cmake -S . -B build-saturn -DKOF_SEQUENCE_DIR=E:/projetos/kof98-saturn/local/fix-vdp2-pack
    cmake --build build-saturn --target kof_sequence_replay_disc -j 6
    python scripts/kof_ymir.py --frames 3600 --out local/ymir/fix-vdp2

The converter uses the known ordered FIX suffix from the validated clip report. It composites the suffix independently into transparent 8x8 cells using actual LUT RGB555 values (including split strips), then chooses a source palette or exact merged palette. Rejects cells with >15 opaque colors or dictionary overflow (4096 tiles/96 palettes), checks exact decoded colors/transparency, and retains the 4 MiB pack gate. NBG0 uses two-word names in A0 and immutable 4bpp characters in B0, priority 7 above RGB VDP1 sprites at 6; CRAM banks 0 and 7 remain reserved. Only changed map entries are published in the SDK VDP1 swap callback, with a serial check, CPU-only stores, no library calls in the callback. First packet includes every cell for reset/wrap. Guest callback timing and exact target pixel parity remain unverified; compare stable frame screenshots and ordered RGB fingerprints against resident control, inspect swap errors and cadence. No claim of savings until measured. This path depends on source frames.csv remaining associated with its CRC-checked clip pack; no new ROM capture needed. Original pack and resident control remain unchanged.


### FIX-to-VDP2 qualification (2026-11-14)

User-provided fix-vdp2.zip, compared directly against resident.zip: identical probe SHA-256 1e32005f403840e05bbd900828603433a0a87c3b84d9ac966d9ef19153af6ba2; FIX application SHA-256 4624d7ded3eb4aeb15835c2686d33e4cdc52adfef81e5d43ee1294a4fb5a1af4. Both runs use 3600 emulated fields, cache on and injection boot. FIX run submitted 2889 frames, final index 488, one CD load, zero misaligned accesses, 2889 FIX swap callbacks, zero FIX sync errors, 16759 updated cells. Gameplay starts at field 708 vs 715 in the resident control; both have 601-field loop periods. All nine submitted PNG files are byte-identical to the control (645120 RGB pixels). All 2888 compared gameplay fingerprints (four 600-field windows plus 488 partial fields) match the first resident loop positionally, excluding the extra field between loops. Nominal loop cadence is 59.90 submissions/s, not distinct-picture FPS. Across the four matched 600-field windows, excluding the extra loop fields and accounting for 90 BIOS rows in instruction/transfer CSVs, VDP1 writes were 16954712 words vs 21774936 (-22.14%); VDP2 VRAM writes were 27144 words vs zero, and CRAM writes zero in both. Master instructions 808542151 vs 816016759 (-0.92%), slave instructions zero. Instruction counts include idle/wait work, so they do not quantify available game-logic cycles. Pack report: 2805848 bytes of 4194304 (66.90%), 218 FIX chars (6976 B), 13 FIX palettes (416 B), mean 246.1 removed commands (26.87% of original 915.755), peak 252; mean remaining 669.655. Atlas reservation unchanged at 299032 bytes. No physical hardware or independent full original oracle comparison yet. Preserve this pack as the next optimization control. Next priorities: actual phase timing, a live LSPC path and conservative background extraction only where original sprite ordering can be preserved.


## Ordered VDP2 stage underlay experiment E

    python tests/test_resident_sequence.py
    python tools/pack_resident.py local/sequence-pack local/stage-vdp2-pack --stage-vdp2
    cmake -S . -B build-saturn -DKOF_SEQUENCE_DIR=E:/projetos/kof98-saturn/local/stage-vdp2-pack
    cmake --build build-saturn --target kof_sequence_replay_disc -j 6
    python scripts/kof_ymir.py --frames 3600 --out local/ymir/stage-vdp2

This experimental underlay moves only an ORDERED prefix of the non-FIX draw list; the original suffix remains on VDP1 in original order. It is not semantic stage segmentation or a hardware-scrolling/parallax engine. Every candidate prefix is flattened into exact transparent 8x8 4bpp cells, validated against the prefix's rasterized RGB555, and committed transactionally only if it fits. Shorter prefixes (or none) are tried if a cell has over 15 opaque colors or a dictionary/cell limit is reached. The cap defaults to 256 commands; --stage-prefix controls it. At most 256 nonblank underlay cells per frame, hence at most 512 actual map writes across any pair of frames including wrap. Frame zero encodes a full reset. The original clip CRC and 4 MiB resident pack gate remain. A pack that removes no commands is rejected. The background must be constant across the sequence, otherwise this mode rejects it.

VRAM: NBG0 FIX A0 map/B0 chars; NBG1 underlay A1 map/B1 chars, last 32 bytes of B1 reserved for SDK backdrop. Two-word pattern names; FIX palette indices 16..63 and stage 64..111, loading font banks 0/7 unaffected. Priorities FIX 7, RGB VDP1 6, stage 4. VDP1 erase is transparent only for this experiment; constant backdrop is supplied by VDP2. Both map updates are dispatched by the existing VDP1 serial-matched swap handler. The callback performs only CPU writes; its actual VBlank budget still needs Ymir and hardware measurement. Stage counters record map updates, maximum actual writes and swap count. Preserve the FIX-only pack as control. Compare ordered active-field fingerprints and nine screenshots, misalignment, VDP1/VDP2 transfers, and cadence. No claim of target parity or performance yet. The new host regression tests exercise overlapping prefix draws and transactional fallback. Reuses already captured source pack, no recapture required.


### Ordered stage underlay qualification (2026-11-14)

User stage-vdp2.zip compared directly with fix-vdp2.zip. Identical probe SHA-256 1e32005f403840e05bbd900828603433a0a87c3b84d9ac966d9ef19153af6ba2, 3600 emulated fields, cache on, injection; stage application SHA-256 a044dc0a6cb598f360bd9ea3c238d2ddc14e8f170b7c85296e4a1d8438ffa536. 2843 submissions, 2843 FIX and stage swap callbacks, zero FIX serial errors, one CD load, no misaligned accesses. Gameplay starts at field 754 (FIX-only 708), loop starts 754,1355,1956,2557,3158, period 601 fields. Nine PNG screenshots byte-identical with FIX-only (645120 RGB pixels). Four complete 600-field windows and 442 available partial-loop fingerprints match the FIX-only first loop positionally, excluding inter-loop extra fields. Nominal cadence 600*60/601 = 59.90 submissions/s. Over four matched 600-field windows (90 BIOS rows offset in transfer/instruction tables), stage VDP1 writes 16469080 words vs FIX-only 16954712; VDP2 writes 200328 vs 27144; CRAM writes zero. Master instructions 808446551 vs 808542151, including wait loops; not a measurement of usable CPU headroom.

Pack: 2997144 B vs 2805848 B FIX-only (+191296 B). 2665 stage tiles, 48 stage palettes (budget exhausted), fallback on all 600 frames; only 16..64 prefix draws removed, mean 25.333 additional draws, leaving 644.322 vs 669.655 FIX-only. Peak actual stage map writes 274 cells; cumulative 101598 stage map updates. No full semantic stage extraction or native parallax. Visual correctness qualified on this replay, but modest VDP1 savings at increased VDP2 traffic, storage and startup cost. Keep FIX-only as preferred control; retain stage underlay as optional experiment rather than claim a general stage optimization. Future stage work should identify original sprite chains and scrolling layers, not expand flattened prefixes blindly. Prioritize phase timing/headroom measurement and live LSPC plus runtime cache integration. No physical-console validation or interactive gameplay yet.
