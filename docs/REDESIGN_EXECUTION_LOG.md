# Architecture redesign — execution log

Evidence log for executing `docs/ARCHITECTURE_REDESIGN.md`. Emulator numbers
are Ymir evidence only, never physical-console timing. All generated/profiling
artifacts live under `local/` and are not committed.

## P0 — pinned baseline (2026-11-17)

### Pins (SHA-256 unless noted)

| Item | Value |
|---|---|
| app commit | `b4533127f2550a0699ffaf48fd058691dab1f42c` |
| upstream kof98recomp commit | `44f873a3cca3827a48fca30fa8091da746311af0` |
| LibSaturn pin (PORTING_PLAN.md:14) | `dbc679a3ac213cbb6a975030212c96b21bd684ad` |
| BIOS `saturn_bios_jp.bin` | `dcfef4b99605f872b6c3b6d05c045385cdea3d1b702906a0ed930df7bcb7deac` |
| Ymir `probe.exe` | `1e32005f403840e05bbd900828603433a0a87c3b84d9ac966d9ef19153af6ba2` |
| `local/profiles/noinput1200.csv` | `dad2a8c08c591b484361706f67f0b32b6199d5305602ce8fb600c802eefb8064` |
| `local/profiles/fight.csv` | `20538429e048dabad6d9906d1e4d0123f23e874152ff79aac9d36ec5ee94d83f` |
| `local/profiles/playlong.csv` | `e1e8c180189b8389c7ef40ff2ba6282eca2a73a2eb66091c1da66328f985fc69` |
| `tests/input_script_sample.txt` | `bb76acc74101e263a4aae5770bb28dcc613f50b62faa6f6fc972faa170898d98` |

### Reproduction

The checked-in `build-ng/` binaries were stale (app SHA-256 `82dbef…`, built
14:18, source mtimes 14:25). Rebuilt `kof_resident_bench` from current sources:

- fresh app: 973,336 B (limit 983,040), SHA-256
  `833dadce3420704bec05f9a85a39c138c04635c5fec9f9334910391a2a6a72fd`.
- command: `python scripts/kof_ymir_bench.py --target resident --frames N
  --out local/ymir/p0-repro-resident-*`.

| fields | guest frames | ratio | `bad` | steady SH-2 cycles/field | evidence |
|---:|---:|---:|---:|---:|---|
| 3,000 | 2,013 | 0.6710× | 0 | 449,204 | `local/ymir/p0-repro-resident` (stale binary) |
| 6,000 | 5,216 | 0.8693× | 0 | 449,204 | `local/ymir/p0-repro-resident-fresh` |
| 30,000 | 30,718 | **1.0239×** | 0 | 449,204 | `local/ymir/p0-repro-resident-30k` |

The short-run ratio is depressed by one-off boot/attract setup; at 30k fields
the current source is real-time (slightly above 1.0×). This reproduces the
historical ~1.0× figure in `local/ymir/wrapfix-long/summary.json` (0.9973×)
with current sources and is the baseline for P2. `ng_boot_recomp_interp=0`,
`ng_boot_recomp_bad=0` in every run.

## P1 — executed-routine profiles and the native working set

Tools added (source only; no generated code committed):
- `tools/sh2_executed_routines.py`: resolve a Ymir `--debug-profile` PC CSV
  against the matching SH-2 ELF into a routine-name list + histogram. (The
  profile `pc` column is **decimal**, e.g. `100679680` = `0x06004000`.)
- `tools/make_pruned_native.py`: select already-generated routine bodies and
  rebuild the resident + BIOS dispatch tables (P2).

### Native working set (authoritative for the bench workload)

| Profile | SH-2 instructions traced | executed routines | bytes |
|---|---:|---:|---:|
| old binary, 3k fields (`resprof`) | ~1.0e9 | 94 | 105 KiB |
| old binary, 12k fields (`resprof12k`) | 3,967,753,867 | 94 | 107,576 B |
| **fresh binary, 12k fields (`p1-prof`)** | 3,967,464,498 | **94** | **107,576 B** |

The two 94-routine lists are byte-identical (SHA-256
`62421b4b6cb0b2da5cd430a255acbfc011b08fa52aab4f068f88493a02c83757`). The set
is stable across rebuilds and run lengths; 585 of 679 linked routines (818 KiB)
never execute in this workload. Routine families executed: 8 dense handlers,
83 `r_*` game routines, 2 `bios_r_*`, 1 `wram_r_100000`.

### Coverage analysis (R1 / P6 — honest limitation)

The host-oracle guest-PC profiles cover more modes than the bench's no-input
attract loop:

| profile | PCs | mapped routines | routines with ≥100 hits |
|---|---:|---:|---:|
| `noinput1200.csv` | 524 | 381 | 139 |
| `fight.csv` | 2,485 | 1,756 | 421 |
| `playlong.csv` | 3,617 | 2,401 | 588 |
| union | — | 2,411 | — |

Full-game native coverage therefore needs **2,411 routines**, which cannot fit
in 1 MiB WRAM-H. This is the documented reason for P6 phase overlays (cart
*data* staged into WRAM-H at safe points). The current baseline build feeds no
pad input (`resident_bench_main.c` never calls `sat_input_poll`), so all three
capability builds — including the 140-strip hybrid — execute the same no-input
attract workload; the 94-routine set is the correct native set for the P2–P5
unified gate. A dispatch miss in any other mode is fail-loud (`bad++`, guest
halted), never silently masked.

## P2 — pruned RECOMP_ONLY target (PASS)

`tools/make_pruned_native.py` selected 94 routines (85 generated bodies copied,
9 dense handlers + 1 WRAM rescue linked from hand-written sources) and rebuilt
both dispatch tables:

| artifact | full | pruned |
|---|---:|---:|
| native `.text` | ~904 KiB | **52,324 B** |
| resident map entries | 532 | 167 |
| BIOS table entries | 1,150 | 4 |
| app `.bin` | 973,336 B | **133,376 B** |
| WRAM-H headroom (to 16 KiB stack guard) | 21,720 B | **862,472 B** |

Build: `cmake -DKOF_BUILD_PRUNED_BENCH=ON`, target `kof_resident_pruned` (+ the
`kof_resident_pruned_disc` phony for the CUE). Ymir 30k fields
(`local/ymir/p2-pruned-30k`): **1.0233×**, `bad=0`, `interp=0`, 30,699 guest
frames. Per-frame guest work is identical to the full bench within 0.003%
(instructions 16,797.27 vs 16,797.79; VRAM 3,342.22 vs 3,342.33; same final
PC `0xC10990`). The 19-frame total difference is SH-2 I-cache/layout speed, not
guest-state divergence. 39/39 ROM-free host tests pass.

## P3/P4/P5 — unified target (build achieved; speed/render gates NOT met)

`src/saturn/unified_main.c` + `KOF_BUILD_UNIFIED` produce a **single** target
(`kof_unified`) that links all three capabilities: the pruned RECOMP_ONLY
native guest, the gated LSPC→VDP1 strip renderer, and the live Z80/YM2610
driver feeding one SCSP mono stream. Build command:
`cmake -DKOF_BUILD_PRUNED_BENCH=ON -DKOF_BUILD_UNIFIED=ON`, targets
`kof_unified` and `kof_unified_disc`.

- app: **210,516 B** (limit 983,040).
- `.text` 210,016 B, `.bss` 350,212 B; `__bss_end=0x0608CE34` → **455,116 B
  (≈444 KiB) WRAM-H headroom** above the 16 KiB stack guard. WRAM-L within
  guard. 4 MiB cart is data-only; all native code is WRAM-H resident.
- Ymir 16k fields (`local/ymir/p5-unified-long`): guest 778 frames,
  `bad=0`, `interp=0`, **605,353 YM2610 samples generated and written to
  SCSP**, `kof_ym_step=3`, no panic. This is the first single binary with
  native logic + renderer code + live game audio linked and running.

**Gates that FAIL (honest):**
1. **Speed:** 0.0486× (778 guest frames / 16,000 fields), far below the ~1.0×
   gate. SH-2 profile (`local/ymir/p5-prof`, 619M instructions): ymfm FM 9.9%
   + SSG 10.5% + ADPCM 2.3% + resampler 1.9% ≈ 24%; Z80
   `exec_opcode`/`z_read`/`z80_step`/`audio_sync`/timers ≈ 22%; CD init ≈ 17%;
   `__udivsi3` 4.5%. Live YM2610 synthesis is inherently SH-2 work — MIN
   fidelity was already measured as no speedup (`X2_NATIVE_RESIDENCY.md:1154`).
2. **Renderer:** at frame 778, `kof_live_sprite_count=0`, `draw_strips=0`
   (FNV-basis hash = empty snapshot), `draw_error=0`. The native path's guest
   state at guest frame 778 differs from the interpreter path (timing-driven
   divergence documented in R3), so the frame the tile pack was built from
   contains no LSPC sprites here. Reaching a gameplay frame with sprites would
   need a later render frame and a fresh tile pack for the native state.

**Conclusion:** the redesign solved the *memory* blocker (842 KiB free in the
pruned core; unified links with 444 KiB free), but the *runtime* cost of live
YM2610 synthesis is the new hard ceiling for a ~1.0× single build. P5 is not
met. This is emulator evidence only; P7 physical-console timing remains
blocked on user-supplied hardware.

Final clean-source binaries (diagnostic counters removed):
`kof_resident_pruned.app.bin` SHA-256 `ea6ce319…` (the P2 evidence binary),
`kof_unified.app.bin` SHA-256 `60bf98c5…`. The latter was re-verified for
1500 fields (`local/ymir/p5-final-check`): `bad=0`, `interp=0`, 19,408 live
samples written, `kof_ym_step=3`.

## P5 follow-up — can audio cost be offloaded or cut?

A full SH-2 symbol profile of the unified run (`local/ymir/p5-prof`, 619M
instructions) attributes **52.4% of all instructions to audio**: ymfm
(FM+SSG+ADPCM) 28.8%, Z80 core 17.1%, glue 6.6%. The YM2610 generates ~933
stereo samples per 60 Hz guest frame (55,555 Hz output; `SAMPLE_CLOCKS=72`).
There is no redundant generation and no fidelity knob: for YM2610
`OPN_FIDELITY_MIN` and `OPN_FIDELITY_MED` are the **same branch**
(`ymfm_opn.cpp:2140-2155`), which is why the earlier "MIN" test changed
nothing. `audio_sync()` batching cannot help — Z80 work is a function of total
guest cycles and must be available at each `$320000` latch read.

**Slave SH-2 offload is feasible but cannot meet P5.** The SDK supports bounded
message-passing jobs (`sat_dual_sh2_*` in `dual_sh2.h`; depth-1 uncached mailbox
at `0x06002000`, doorbell MMIO write `0x21000000`), with a working precedent in
`src/saturn/sequence_live_slave_hash.inc` (341/341 jobs, 0 errors,
`docs/REAL_FRAME_REPLAY.md:325`). `CROSS_PORT_ENGINEERING.md:60` permits Slave
"stage audio blocks" jobs. But moving even *all* audio off Master is capped:

| offloaded (zero-cost, perfect overlap) | max speedup | resulting ratio |
|---|---:|---:|
| all ymfm (28.8%) | 1.40× | 0.068× |
| ymfm + glue (35.4%) | 1.55× | 0.075× |
| all audio (52.4%) | 2.11× | 0.102× |

against the ~1.0× gate. In practice the Z80 must stay on Master (its replies
are read synchronously by the guest at `$320000`), so only the ymfm share is
movable. The normative alternative (`PORTING_PLAN.md:99`: "Generate music ADX
offline from the original driver… consider a hybrid driver approach, not a
full YM2610 software emulation assumption") is pre-rendered audio driven by
guest commands — that is a different deliverable from "live YM2610 synthesis"
and is not what P4 measured. P5 remains unmet for this reason.

## P6 — phase-overlay feasibility (attempted, negative for whole modes)

`tools/make_phase_pack.py` maps host-oracle profile PCs to generated routines,
extracts their bodies and measures compiled footprint. Calibrated against the
measured pruned set (94 routines -> 52,324 B compiled, 0.1716 B/B of source),
the whole-mode sets are:

| mode | routines | estimated compiled | fits 900 KiB WRAM-H? |
|---|---:|---:|---|
| boot (`noinput1200`) | 381 | 278 KiB | yes |
| fight (`fight`) | 1,756 | 2,158 KiB | **no** |
| playlong (`playlong`) | 2,401 | 3,491 KiB | **no** |

Whole-mode phase overlays are therefore **not feasible** for fight/ending;
only a boot phase fits. P6 would require finer sub-phases (per-stage/per-
character), which has not been attempted here.

## Render-frame search — native path produces no attract sprites

`KOF_SPRITE_SCAN` samples `ng_live_sprite_strips` every 16th guest frame. With
no pad input the guest never leaves attract mode: 88 sampled frames up to guest
frame 1530 produced `kof_scan_hits=0`, `kof_scan_max_count=0`
(`local/ymir/p5-spritescan2/scan.json`). I also found and fixed a latent bug:
the unified main inherited the rawcode bench's `rendered` freeze, which halted
the guest at frame 778 and would itself violate P5's "increasing frames" gate.
`KOF_PAD_INPUT` + authored tape `local/profiles/tapes/fight_entry.txt` were
added to drive the guest into a fight. With guest-frame pad cadence the app
observed held buttons on 18/142 guest frames (`kof_pad_nonzero`,
`local/ymir/pad-diag-gf`), confirming the tape reaches the guest, but a full
30k-field scan still found `kof_scan_hits=0`, `kof_scan_max_count=0` through
guest frame 1529 (`local/ymir/p5-spritescan-input/scan.json`). The native path
therefore never produces gameplay LSPC sprites in this workload, consistent
with the earlier finding that it cannot execute the guest-copied WRAM code
(`X2_NATIVE_RESIDENCY.md:12-26`). Finding a render frame with sprites on the
native path needs the P6 phase overlays / WRAM-code solution first; it is not
achievable by moving the render-frame constant.

## P5 experiment — pre-rendered / hybrid audio (new, decisive A/B)

Built `kof_unified_music` (`src/saturn/unified_music_main.c`,
`KOF_BUILD_UNIFIED_MUSIC`): the SAME pruned RECOMP_ONLY guest and gated
renderer, but game audio is the offline-rendered `MUSIC.BIN` (2,293,200 B,
22.05 kHz stereo, generated by the oracle — `PORTING_PLAN.md:99` permits a
hybrid driver) fed by the single `sat_disc_stream` scheduler to SCSP. No
ymfm/Z80 sources are linked. App 165,952 B, WRAM-H headroom 518,316 B.

Ymir 1,200 fields (`local/ymir/p5-music-smoke`): **0.0542×**, `bad=0`,
`kof_music_playing=1`, 6 refills, **0 underruns**, 163,840 music bytes.
This is a real game-audio signal, but the speed gate is still far off.

This **falsifies** the earlier "live synthesis is the blocker" conclusion:
removing ~52% of SH-2 instructions (all synthesis) improved the ratio only
~11% (0.0486× -> 0.0542×). Two further A/B diagnostics on the same build:
skipping `sat_audio_update()` -> 0.0617× (`p5-music-noupdate`), skipping
`sat_disc_stream_service()` -> 0.0625× (`p5-music-nodisc`). Neither is the cap.

A full SH-2 profile of the pre-rendered build (`local/ymir/p5-music-prof`,
275.9M instructions) attributes: native guest 28.0%, CD/disc 27.1%, cart
load 12.4%, dispatch 9.7%, audio service 3.3%, other 19.4% (notably libgcc
`__ashrsi3`/`__udivdi3`/`__ashlsi3` ~7%). The expensive `wait_hirq` spin comes
from the CD block, which is emulated on the SH-2 in Ymir; on real hardware it
is a separate processor, so this portion of the ratio is emulator-specific.

Because the pruned bench itself climbed 0.671× (3k) -> 1.024× (30k) as boot
amortized, a 30k-field run was needed to separate boot amortization from a
steady-state cap. Result (`local/ymir/p5-music-30k`): **0.0989×** (2,967 guest
frames / 30,000 fields, 36.6 MB of music streamed, `bad=0`). So the cap is
real, not boot: even the cheapest audio path plus the gated renderer stays
about 10× below real time. P5 is **not met** by the pre-rendered/hybrid
approach either.

This is an important correction to the redesign's premise: the ~0.05x unified
ratio is **not** caused primarily by YM2610 synthesis. Removing all synthesis
gave +11%, skipping `sat_audio_update` +14%, skipping disc service +15%, and a
30k run only 0.099x. The remaining gap is spread across CD-block emulation in
Ymir (27% of instructions, a separate processor on real hardware), libgcc
64-bit helpers (~7%), and the renderer/cart-load path. A renderer-only
isolation build (`KOF_MUSIC_DISABLE`) measured **0.291×** at 3k fields
(`local/ymir/p5-nomusic-isolate`, 873 guest frames, `bad=0`): removing audio
entirely recovers ~3× versus live audio, but the renderer build is still about
half the pruned core's short-run ratio, and re-adding pre-rendered audio costs
another ~3×. No single subsystem explains the ~1.0× gap: it is the combination
of renderer, disc/audio service and Ymir's SH-2-side CD-block emulation. No
physical-hardware evidence is available for the CD portion.

Full A/B matrix (all Ymir, `bad=0`):

| build | fields | ratio | evidence |
|---|---:|---:|---|
| pruned core, no renderer/audio | 3k / 30k | 0.671× (stale bin) / 1.024× | `p0-repro-*`, `p2-pruned-30k` |
| unified, live YM2610 | 16k | 0.0486× | `p5-unified-long` |
| unified, pre-rendered MUSIC.BIN | 1.2k / 30k | 0.0542× / 0.0989× | `p5-music-smoke`, `p5-music-30k` |
| pre-rendered, no `sat_audio_update` | 1.2k | 0.0617× | `p5-music-noupdate` |
| pre-rendered, no disc service | 1.2k | 0.0625× | `p5-music-nodisc` |
| renderer only (`KOF_MUSIC_DISABLE`) | 3k | **0.291×** | `p5-nomusic-isolate` |

The pre-rendered/hybrid variant provides real game audio (36.6 MB streamed,
`kof_music_playing=1`) but does not meet P5's speed gate.

## Playable disc for manual emulator use

Added `KOF_BUILD_PLAYABLE` / `kof_playable`: the full-app interpreter path
(the only one that reaches gameplay sprites) with `KOF_PLAYABLE` polling the
Saturn pad every guest frame (Z=coin, START=start, D-pad+A/B/C). It renders at
the single frame the tile pack was built from (778) and keeps the vblank-paced
loop. Disc: `build-ng/disc/kof_playable.cue` (8,851,280-byte ISO, 4,190
sectors). It boots in Mednafen (window "kof playable", 4 MiB cart, JP BIOS)
and in the Ymir probe. A 36k-field Ymir run with the authored tape confirms
the render path end-to-end: `local/ymir/playable-verify/verify.json` =
`ng_boot_frames=778`, `kof_live_sprite_count=140`,
`kof_live_draw_strips=140`, `draw_error=0`, `music_underruns=0`.

Caveats measured, not guessed: the interpreter path runs at ~0.0227×
(`local/ymir/playable-check`, 68 guest frames / 3,000 fields), so it is far
from real-time play; and `TILES.BIN` is a one-frame residency pack, so
rendering other frames fails with `SAT_ERR_NOT_FOUND` (135 sprites detected
but 0 strips drawn when forced continuous in the earlier attempt). A genuinely
playable real-time build needs the P5 speed work and a full dynamic tile
cache — it is not claimed here.

### Making the graphical disc watchable (2026-11-19)

The reported symptom was "music/voices audible, screen all black". Root causes
found and fixed, all measured in Ymir:

1. **The render was gated to guest frame 778**, which at 0.023× is ~9 minutes
   of emulated time. `ng_boot_host` scans showed the first sprites at guest
   frame 13 (102 strips), and only **two distinct 59-tile sets** across frames
   13–778 (frames 13–600 and 700–778). `tools/make_union_tile_pack.py` unions
   them into one 118-tile, 15,584-byte `TILES.BIN` (reproduced byte-for-byte:
   CRCs `D3ED1894`/`57F4A920`). `RENDER_FRAME` is now overridable and set to 13
   for `kof_playable`.
2. **The one-shot render froze the guest at RENDER_FRAME**, so pad input did
   nothing afterwards. The post-render loop now keeps calling
   `ng_boot_run_frames(1)` without opening a VDP1 frame, so the last submitted
   list stays on screen while input stays live. Milestone re-renders at
   frames 13/70/147/300/600/778 advance the picture through boot/attract and
   end on the KOF98 title.
3. **Black screen during boot**: LibSaturn's native `sat_loading_screen_draw_bar`
   now shows progress from frame 0 to 13.

Verified (`local/ymir/playable-*`): loading bar visible at field 1,200;
"NEO GEO" BIOS splash visible at field 4,000; KOF98 title visible at field
35,999 with the union pack; 36k-field harness `guest=2884`, `sprites=140`,
`strips=140`, `draw_error=0`, `music_underruns=0`, and speed improved to
0.0801× (the post-render loop no longer pays frame-setup cost). This is still
not real-time play, and the picture updates only at the six milestones.

Separately, `unified_main.c` no longer calls `ng_live_sprite_strips()` every
frame after a successful render; a 30k-field pad-input run improved 0.0523× →
0.0748× with `bad=0`. The native path still produces 0 gameplay sprites (the
documented runtime-WRAM limitation), so this does not make it playable.

## X2 follow-up — offline recompile of the guest-WRAM code (2026-11-19)

Built an end-to-end flow for the runtime-generated 68000 code instead of a JIT:
`ng_boot_host_prof` + `KOF_PROF_WORDS` profile executed word addresses; the host
guard PC profiler covers `$100000-$1FFFFF`. Over 3,000 guest frames there are
only **476 distinct executed WRAM instruction words**, all in one 4 KB page,
spanning `$100000-$10094A`. Ymir `--dump-mem` captured the live 64 KB `s_wram`
at `$0028c400`; `m68krecomp --raw ... --base 0x100000 --entry 0x100000`
recompiled it into **42 native routines / 92 dispatch entries**, covering all
476 executed PCs (the observed `$100078`, `$1000d4`, `$1008b0` are internal
labels inside owning routines). `tools/make_wram_native.py` merges those
entries into the binary-search resident map (258 total). New opt-in targets
`kof_resident_wram` (206,520 B) and `kof_unified_wram` (283,748 B) build with
`bad=0` over 30k fields (`local/ymir/wram-sprite-30k`).

**Decisive negative result:** the native guest still produces **0 sprites**
(`kof_scan_hits=0` over 126 samples) and only **15 WRAM dispatch hits**. A
Ymir `--dump-mem` of `s_wram` at 2,135 guest frames shows the native WRAM code
region is **entirely zero** (0 nonzero bytes in `$100000-$100FFF`), while the
interpreter's dump has 3,325 nonzero bytes by ~frame 25. The first writer in
the interpreter is BIOS PC `C11056` (`movem.l D0-D3,-(A0)`); the corresponding
native routine `r_C11002` *is* in the resident map and contains `L_C11056`.
So the blocker is not the missing WRAM recompilation: **the static-recompiled
guest never executes the BIOS block that copies the routine into WRAM**, i.e.
its execution path diverges earlier during boot. Fixing that needs a separate
bit-exactness investigation of the native BIOS/game path (compare dispatch PC
traces between native and interpreter from the first guest frame). The WRAM
native infrastructure is kept as opt-in, off by default.
