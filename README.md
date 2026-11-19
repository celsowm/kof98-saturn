# KOF98 Saturn

Experimental native Sega Saturn port of The King of Fighters '98, using [KOF98 Recomp](https://github.com/danilopaulinodasilva/kof98recomp) and [LibSaturn](https://github.com/celsowm/libsaturn).

**Status: feasibility research — not playable.** The required baseline is NTSC 320×224 with original game timing and a **mandatory 4 MiB RAM expansion cartridge**. Stock RAM and 1 MiB cartridges are out of scope. The first gate is code size and execution speed, not graphics.

- [Engineering plan](docs/PORTING_PLAN.md)
- [Architecture redesign plan (one real-time + renderer + audio build)](docs/ARCHITECTURE_REDESIGN.md)
- [Cross-port study: Tekken3 and Ikemen Saturn](docs/CROSS_PORT_ENGINEERING.md)
- [Music on Saturn](docs/MUSIC_ON_SATURN.md)
- [Contributor instructions](AGENTS.md)
- [Decompilation-first track](docs/DECOMP_FIRST.md) — pure-C translation of the executed guest working set (Ghidra + reproducible clean/verify pipeline)
- [Runnable pure-C guest runtime](docs/PURE_C_RUNTIME.md) — generated C that dispatches and executes guest routines directly (not yet a full playable backend)

**Status by pillar.** The Saturn SH-2 now **runs the real game** and **draws its
picture**: it executes the Neo Geo BIOS and KOF98's own 68000 code (18.8 M cart
instructions from the P ROM on the 4 MiB cart) via the Musashi interpreter, and
resolves the live guest LSPC strips to 4bpp C-ROM textures and draws them via
VDP1 (140/140 strips, 0 errors; see [Native 68000 boot](docs/NATIVE_68000_BOOT.md);
Ymir evidence). It is correct but **slow** (interpreter, ~1,700 frames in 20,000
emulated fields, not 60 FPS). The native recomp (X2) path is now demonstrated and
integrated into the shared core: `ng_boot.c` gains a recomp dispatcher (native
entry vs Musashi fallback, IRQ at routine boundaries) that is **bit-exact vs the
interpreter over 1200 frames of the real BIOS at ~4.5x the speed** (`ng_boot_recomp`
ctest; the standalone `ng_recomp_host` shows ~6x), and it cross-compiles for SH-2.
It now also runs on a Saturn target with **no interpreter linked** (`kof_ng_bench_recomp`,
666 KB, 100% native, 0 bad dispatches): **0.31x real time in Ymir = 4.55x the
interpreter**, i.e. still ~3x short of 60 Hz. Dropping Musashi needs a recomp-only
core (`NG_BOOT_RECOMP_ONLY`, no fallback), which fits WRAM-H but only for the BIOS.
The **full game now executes natively on Saturn** via the disc-backed rawcode
overlay: a clean sustained Ymir run
(`local/ymir/rawcode-measure6`, `docs/X2_NATIVE_RESIDENCY.md`) performed
**739,605 native game routines with 0 dispatch misses, 0 load errors, 21 disc
page loads, at 0.2324x real time**. A 32-bit cycle-counter prototype
(`KOF_32BIT_CYCLES`, bit-exact over 268,275 host blocks) cut generated code ~29%
and gave a 0.2668x reading with 848,938 blocks. The earlier 0.2324x/0.2668x
figures were invalid stuck-loop artifacts; paged-overlay runs reached only
0.0074x (disc-bandwidth wall). Correct dispatch and residency work.

**Profile-guided dense emitter (2026-11-17):** an authoritative Ymir SH-2
instruction profile (`tools/sh2_profile_symbols.py`) showed time was concentrated
in a handful of guest loops, so 15 measured, bit-exact optimizations were built:
hand-written dense handlers for the hot BIOS VRAM/palette fill and clear loops
(keeping guest registers, cycles, icount and CCR in SH-2 registers), a verified
direct-mapped dispatch cache, and fast paths in the shared slow-write helpers.
The fully-resident bench (`kof_resident_bench`) went from **0.3538x to ~1.0x
real time**: a final 30k-field Ymir run reached **0.9973x** (29,919 guest
frames, 9.9M native blocks, `bad=0`, zero paging, no stall) — essentially 60 Hz
for native game logic. Guest-visible device-write counters are byte-identical
to baseline per guest frame, and the host oracle reports 7,327,802 blocks with
0 mismatches. A 32-bit guest-cycle epoch rebase fixed a signed-overflow freeze
at frame 10591 (my earlier diagnosis of a sound-CPU handshake was wrong — see
the correction in [X2 native residency](docs/X2_NATIVE_RESIDENCY.md)); the guest
now runs continuously. This is emulator (Ymir) evidence only — NOT
physical-console timing — and the bench has no renderer or audio. See
[X2 native residency](docs/X2_NATIVE_RESIDENCY.md) for every measurement and
negative result. A playable port still needs the renderer on the **real-time**
native path (the dense handlers + verified dispatch are wired into the
renderer-enabled `kof_ng_rawcode_bench` with `bad=0`, but the recomp-only
overlay cannot execute the BIOS-copied WRAM routine, so it draws 0 strips —
see [X2 native residency]). A hybrid core (`kof_ng_hybrid_bench`) combines
native dense handlers with a Musashi fallback for guest-written RAM and draws
140 strips with `bad=0`, and the upstream Z80/YM2610 sound CPU now runs natively
there. With the game M1 ROM loaded, ONE build (`kof_ng_hybrid_bench`) runs
native logic + the 140-strip renderer + live game audio: Ymir evidence shows
frame 778, 140 strips (0 errors), 725k live YM2610 samples to SCSP, `bad=0`.
But that hybrid is **not real-time**; the real-time (0.9973x) evidence is a
separate bench with no renderer/audio. There is no physical-console proof.
Routine **overlay/paging is host-proven**: an 8-page split served by a 3-page
budget is bit-exact over 1200 frames (10 loads, 7 evictions, 318,020 native
dispatches, 0 interpreted). But a game-side size measurement closes the
"recompile everything" door: the real P1 generates 874,188 instructions and
24-37 B of SH-2 text each, so the full P ROM would be **~38-58 MB**, far beyond
the <1.5 MB executable budget. Only a measured hot subset can be native; that
profile is the next step ([Native 68000 boot](docs/NATIVE_68000_BOOT.md)). The **real KOF98 soundtrack plays on Saturn** (SCSP; the standalone probe is content-verified, envelope corr 0.958 at 22.05 kHz; the integrated app streams it with 0 underruns but its captured content is not yet verified — see [Music on
Saturn](docs/MUSIC_ON_SATURN.md)). Remaining: profile and select the native hot
set, live Z80/YM2610 sound, all modes, and physical-console proof. Emulator
evidence only.

## ROM-free host tests

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 tools/dispatch_budget.py
```

The sparse and paged dispatchers have ROM-free tests and an optional source-level desktop integration. See [X0 dispatch experiments](docs/X0_DISPATCH_REPORT.md) for synthetic host checks and target caveats. No ROMs are necessary for the synthetic tests. Build the eventual game locally with legally obtained AES kof98h and Neo Geo BIOS dumps. No SNK ROMs, game assets, generated game code, traces, BIOS or binaries are distributed here.

## Credits

Danilo P. da Silva (KOF98 Recomp and toolkit improvements), sp00nznet (neogeorecomp), LibSaturn contributors, and SNK (original game and intellectual property). Unofficial fan research; not affiliated with SNK or Sega.

The [offline dispatch packer](docs/X1_GENERATED_DISPATCH.md) converts locally generated upstream tables to immutable paged C arrays. See the [host paged oracle build](docs/HOST_PAGED_ORACLE.md) for the new local integration; real ROM and Saturn verification remain pending.

Experimental [exact suffix sharing](docs/X2_SUFFIX_DEDUP.md) is available with `-DKOF_DEDUP_SUFFIX=ON` in a separate local host build. It is off by default until real-game parity and SH-2 size/timing are measured.

An independent [bounded CFG sharing candidate](docs/X2_CFG_DEDUP.md) is available with `-DKOF_DEDUP_CFG=ON`. Select one dedup strategy per build; both are experimental.

Experimental [local dead flag elimination](docs/X2_FLAG_LIVENESS.md): `-DKOF_OPT_FLAGS=ON`, default OFF. Measure separately from CFG sharing.

Experimental [fixed counter batching](docs/X2_CYCLE_BATCH.md): `-DKOF_OPT_CYCLES=ON`, default OFF. Measure separately from flags and CFG sharing.

Experimental [local data register caching](docs/X2_REGISTER_CACHE.md): `-DKOF_OPT_REGCACHE=ON`, default OFF. Measure independently from flags, cycles and CFG.

[Profile-driven native residency](docs/X2_NATIVE_RESIDENCY.md) collects local entry frequencies and SH-2 symbol sizes to select whole routines within an estimated memory budget. No actual game profile, target size or playability claim yet.

[X3 phase asset residency](docs/X3_ASSET_RESIDENCY.md): heap-free 4 MiB cart arena, generation handles, bounded loading, CRC validation and combat lock. Portable core tests pass; the installed-SDK adapter is not target-validated yet.

[Native tile pack converter](docs/X3_TILE_PACK.md): local selected C/FIX ROM tiles converted to packed 4bpp, with aligned assets and CRC descriptors for the cart store. Synthetic reference tests pass; actual game tiles and target uploads remain unverified.

[X4 native tile cache](docs/X4_TILE_CACHE.md): independent LUT4 pattern/LUT slots, binary lookup of resident tile packs, current-list pins and fenced SDK writes. Synthetic core tests pass; target integration remains open.

[X4 LSPC scanline IR](docs/X4_LSPC_IR.md): portable ordered sprite spans and FIX addressing with exact oracle-derived shrink masks. Synthetic differential tests pass; target command emission remains open.

[X4 exact vertical strips](docs/X4_STRIP_COALESCING.md): ordered scanline coalescing, native-tile detection and exact 4bpp shrink materialization. GCC/Clang synthetic replay matches; SDK emitter and actual game traces remain open.

[X4 staged VDP1 emitter](docs/X4_VDP1_EMITTER.md): native tiles and exact shrink variants, shared LUTs, command preflight and staged-list rollback. Mock integration tests pass; actual SDK/target probe remains open.

[ROM-free Saturn graphics probe](docs/SATURN_GRAPHICS_PROBE.md) — optional `KOF_BUILD_GRAPHICS_PROBE=ON` disc target for the CD/cart/VDP1 pipeline. Target validation pending.

[Real-frame replay](docs/REAL_FRAME_REPLAY.md) — synthetic graphics probe visually confirmed in Mednafen; local-only capture and static sprite replay added, single real frame visually validated in Mednafen; zero host RGB555 mismatches; FIX, performance and gameplay remain pending.

KFR2 full FIX capture and [bounded 600-frame sequence replay](docs/REAL_FRAME_REPLAY.md) are available for local validation. Use `scripts/setup-emulators.ps1` for persistent PowerShell shortcuts (`kofrun frame`, `kofrun sequence`).
