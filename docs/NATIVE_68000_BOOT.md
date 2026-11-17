# Native 68000 execution on Saturn (BIOS + game)

Scope: native 68000 execution on the Saturn SH-2. The SH-2 now runs the **real
Neo Geo BIOS and, with the cart attached, the game's own P ROM code** — not a
captured trace. The BIOS boots, then hands off to KOF98's own code (fixed bank
P1 and banked P2 on the cart), which renders (LSPC VRAM/palette) under the
vblank/timer IRQs.

This is the interpreter path (`PORTING_PLAN` §3, "compact 68000 interpreter
fallback"): Musashi on the SH-2 with the guest P ROM on the data-only cart. It
executes the real game, but **slowly** — a correctness milestone, not 60 FPS.
Native recomp (X2) is the path to speed and is still future work.

## What runs

`src/ng/ng_boot.c` is a compact, freestanding Neo Geo 68000 system built on the
upstream Musashi core (`deps/.../ext/musashi`, MIT) with a minimal AES memory
map reimplemented from neogeorecomp (`bus.c/lspc.c/io.c/video.c`) but with no
libc, no allocation and no renderer:

| 68000 region | Content |
|---|---|
| `$000000` | reset vectors (BIOS at boot; cart P1 after SWPPROM) |
| `$100000` | 64 KB work RAM |
| `$200000` | P2 bank window (bank register at `$2FFFF0`) |
| `$300000` | `$300000` I/O (controllers, `$320000` sound latch, `$3A0000` system latch) |
| `$3C0000` | LSPC VRAM/registers, raster counter, timer IRQ2, vblank IRQ1 |
| `$400000` | palette RAM (two banks) |
| `$C00000` | 128 KB system BIOS (WRAM) |

With a cart, the fixed bank reads **P1** and `$200000` reads **P2** from the
CPU-mapped 4 MiB expansion cart; without one, `$000000` is the BIOS vector page.
The frame loop (`ng_boot_run_frames`) runs 264 lines of 768 68000 clocks, raising
IRQ1 at line 240 and firing the LSPC timer, as `neogeorecomp` does.

Memory: the Neo Geo RAM/ROM images live in Work RAM Low (`.wram_l`, ~464 KB) so
Musashi's opcode/cycle tables (~590 KB) fit in Work RAM High. App image 334,464 B
(limit 983,040).

## Cartridge layout (the game fits)

The AES P ROM is 5 MiB (P1 1 MiB + P2 4 MiB), larger than the 4 MiB data-only
cart, so it does **not** all fit. The cart holds **P1 (1 MiB) + P2 banks 0..2
(3 MiB) = 4 MiB**; P2 bank 3 is absent (reads as open bus). KOF98 boots and runs
within this window. Data reads only — no execution from the cart.

## Host validation

`src/host/ng_boot_host.c` boots the same core; with P1/P2 the BIOS hands off to
the game:

```
frame   60  PC=1008AC  cart=27282    wram=915250   (BIOS/WRAM boot)
frame  900  PC=006124  cart=4414426  wram=11729968  vram_w=119745   (game code)
frame 1200  PC=007ED6  cart=9892468  wram=11729968  vram_w=245514   (gameplay)
```

Every instruction is counted by range: the WRAM boot (~11.7 M, matching the
plan's estimate), then the game's own code from the cart (`cart`), with active
VRAM rendering. Pressing Start (after the title) changes the screen.

## Saturn target (`kof_ng_boot`)

`src/saturn/ng_boot_main.c` mounts the disc, loads the real BIOS (`BIOS.BIN`)
and the game P ROM (`P1.BIN`, `P2.BIN`) into the CPU-mapped 4 MiB cart
(byte-swapped to 68000 order), boots the BIOS and runs one Neo Geo frame per
display field. Build:

```sh
cmake -S . -B build-ng -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=C:/lsp-new/share/libsaturn/toolchains/sh2eb-elf.cmake \
  -DLibSaturn_DIR=C:/lsp-new/lib/cmake/LibSaturn \
  -DKOF_BUILD_SATURN_ASSETS=ON -DKOF_BUILD_NG_BOOT=ON \
  -DKOF_UPSTREAM_DIR=E:/projetos/kof98-saturn/deps/kof98recomp
cmake --build build-ng --target kof_ng_boot_disc -j 6
python scripts/kof_ymir_boot.py --frames 20000 --out local/ymir/game-boot-2
```

### Evidence (Ymir injection, 20000 fields)

```
ng_boot_frames       = 1,686
ng_boot_instructions = 31,117,066
ng_boot_cart_instr   = 18,827,288   ← KOF98's own code from the cart P1/P2
ng_boot_wram_instr   = 11,729,968   ← boot (matches the host's 11.7 M)
ng_boot_bios_instr   = 559,810
ng_boot_vram_writes  = 489,969      ← LSPC video init + rendering
ng_boot_irq_taken    = 1,682
ng_boot_last_pc      = 0x005072     (P1 cart)
app_sha256           = 83504a75286884d5174c9e2c982a98969917f9a0d4a048073d8d6827ea9e0f48
```

The SH-2 executes the real Neo Geo BIOS, hands off to **KOF98's own code on the
cart** (18.8 M instructions), and renders (489,969 VRAM words) under vblank IRQs.
This is **emulator evidence** (Ymir injection): the game runs, but slowly (Musashi
on the emulated SH-2 does ~1,686 frames in 20,000 fields, not 60 FPS). No
physical-console timing is claimed.

## Notes / limits

- Musashi opcode tables are generated at build time from `m68k_in.c` with a host
  C compiler (`m68kmake`); no generated file is committed.
- Freestanding shims (`src/ng/shim/`, `src/ng/ng_setjmp_shim.c`) let Musashi
  compile/link with `-nostdlib`; the bus never raises a bus error, so the
  `setjmp` trap is inert.
- The 4th P2 bank is absent; a game path that reads it sees open bus.
- Slow: interpreter, not recomp. 60 FPS needs the native recomp (X2) and Slave
  partitioning, plus the live LSPC→renderer and audio integration.

## Live guest LSPC -> strip pipeline (host<->Saturn verified)

`src/ng/ng_live_strips.c` runs the existing LSPC/strip pipeline
(`kof_lspc_line` + `kof_strip_push`) on the **live guest raster state**
(`ng_boot_vram()`/`ng_boot_pal()`/`ng_boot_anim()`/`ng_boot_fix_bios()`,
decoded from the guest's own LSPC VRAM/palette/animation — no C ROM or tile
pixels needed) and hashes the ordered strips with the existing
`kof_strip_hash_record`. The Saturn target computes it once per field; the host
validator computes it after N frames. Because `ng_boot` is deterministic, the
same guest cycle count gives the same state on both, so the cycle count, strip
count and FNV-1a hash must all match. (The probe may sample mid-iteration,
between `ng_boot_frames++` and the cycle/hash update; compare at N or N-1 — the
pair below is exact.)

| Guest frame | ng_cycles | Sprite strips | FNV-1a hash | Match |
|---:|---:|---:|---|---|
| 147 | 29,804,548 | 72 | `0x06E106A6` (Saturn) = `0x06E106A6` (host) | yes |
| 778 | 157,741,060 | 140 | `0xDFF4C1CA` (Saturn) = `0xDFF4C1CA` (host) | yes |

At frame 778 the guest is running its own cart code (host `cart=2,189,566`), so
the match covers the game's real graphics, not just the BIOS boot. This proves
the live guest LSPC -> ordered-strip stage works identically on the SH-2. It is
**not** yet a pixel render: resolving the strips to 4bpp textures needs the C ROM
tile residency (the next block). Evidence: `local/ymir/live-strip-4` (frame 147)
and `local/ymir/live-strip-5` (frame 778).

## C ROM tile residency -> VDP1 render (live strips drawn)

The verified live strips now resolve to 4bpp textures and draw via VDP1:

- `tools/pack_live_frame.py` reads the tile IDs of a live frame (from
  `ng_boot_host --tile-dump`) and packs the corresponding 16x16 4bpp tiles from
  the local C ROM into a tile-residency pack (`TILES.BIN`, 59 tiles, 7,808 B)
  in the same asset format the Saturn tile cache consumes.
- The target loads the pack into a **Work RAM** arena (the cart is full of P ROM)
  and drives the existing `kof_saturn_strip_renderer` / `kof_saturn_tile_cache`:
  each strip's tile is resolved (LUT4) and emitted as a VDP1 sprite. After the
  render the app stops submitting new lists so the picture stays displayed.

### Evidence (Ymir, render frame 778)

```json
{
  "kof_live_draw_strips": 140,     // every strip drawn
  "kof_live_draw_native": 140,     // all as native 16x16 4bpp patterns
  "kof_live_draw_variants": 0,
  "kof_live_tile_uploads": 60,     // 59 tiles + 1 palette LUT uploaded to VDP1
  "kof_live_draw_error": 0,        // SAT_OK; no capacity/tile/miss errors
  "app_sha256": "4bae06032dcc4bc241944f70adc05dcb4f0d9ccbfe94cd968decd750c6a93299"
}
```

The captured framebuffer (`local/ymir/render-freeze/field-26999.png`) is no
longer blank: 5,145 non-black pixels (7.2% of 320x224) in a coherent band
x[17..303], y[80..147] — the game's boot-screen graphics, drawn from the live
guest strips through VDP1. This is **emulator evidence** (Ymir injection).

## Native recomp (X2): equivalence + measured speedup

The interpreter is correct but slow. X2 recompiles executed 68000 routines to C so
they run as native code, with Musashi kept only as a fallback for addresses that
have no native entry (`PORTING_PLAN` §3). To prove the mechanism and measure its
worth before touching the target, `src/host/ng_recomp_host.c` is a self-contained
harness that boots the **real BIOS from reset twice** over an identical compact
AES bus:

- **(A) native** - the recompiled routines (`bios_recomp_*.c`, generated by
  `m68krecomp` from the user's own `roms/neo-po.bin`) plus a Musashi fallback;
- **(B) pure Musashi** - the same instruction stream interpreted.

Both run **exactly the same instruction count** (the count A executed), before
any vblank IRQ, so the architectural state must match bit-for-bit. `m68krecomp`
emits 287 routines / 1,148 dispatch entries covering 9,354 instructions from the
BIOS reset/init path; the reset entry `$C00402` is covered, so native dispatch
starts at reset.

### Evidence (host x86-64, `ctest -R ng_recomp_host`, reproducible via `KOF_BUILD_NG_RECOMP_HOST=ON`)

```
reset SSP=0010F300 PC=00C00402  native entries=1148  target_cycles=400000
native: 46148 instructions (15380 blocks, 0 interpreted), cycles=400014
interp: 46148 instructions, cycles=400014
speedup (end-to-end): 5.91x
speedup (compute-only): 6.01x  (~418 M native instr/s)
state: MATCH (native == interpreter)
```

MATCH covers all eight data/address registers, PC, the other stack pointer, the
whole SR (S/IPL/XNZVC) and CRC32 checksums of 64 KB work RAM and 128 KB VRAM -
i.e. the recompiled code is state-equivalent to the interpreter, not just
"usually right". The measured gain is ~6x on the host.

### SH-2 size and honest limits

- The generated BIOS recomp is **585,656 B** of SH-2 text at `-O2` (625,840 B at
  the current `-O3` build, 287 routines) and must
  sit in WRAM alongside the Musashi fallback tables (~590 KB): tight but the
  BIOS-only slice is near the WRAM-H budget.
- This is a **host x86-64** measurement; the SH-2 will be slower, and the number
  excludes the memory-system stalls that dominate real code.
- It is **not yet integrated into the Saturn target**, and only the **BIOS** is
  recompiled - the game's own P1/P2 code (where the frame cost actually is) is
  still interpreted. Full-game real time needs the game recompiled (far more
  routines), IRQ/timer-correct hand-over at routine boundaries, and Slave/SH-2
  partitioning.
- The harness tests a pure instruction stream (no IRQ injection); the IRQ-at-
  boundary difference between native (routine granularity) and Musashi
  (per-instruction) is not exercised here.

So X2 is now **demonstrated feasible and bit-exact with a ~6x host speedup**, but
real-time on Saturn is not yet achieved.

## Recomp dispatcher integrated into the shared core (ng_boot)

The X2 dispatcher is now part of `src/ng/ng_boot.c` (the same core the Saturn
target uses), behind `-DNG_BOOT_RECOMP` (default OFF, so the interpreter build is
untouched). It:

- keeps CPU state in the recomp `ng_cpu` file, with Musashi synced in/out only
  around fallback instructions;
- dispatches to a recompiled routine when `CPU.pc` is a native entry (binary
  search over the sorted `bios_ng_recomp_table`), else interprets one instruction
  in Musashi;
- takes IRQ1/IRQ2 at **routine boundaries** as real 68000 exceptions (push SR/PC,
  jump through the autovector), exactly as `neogeorecomp`'s `exec.c` does;
- yields on back-edges via `ng_next_event`, so a native routine can never spin.

### Evidence (host x86-64, `ctest -R ng_boot_recomp`, reproducible via `KOF_BUILD_NG_RECOMP_HOST=ON`)

The recompiled dispatcher and the pure interpreter run the real BIOS over the
**full frame loop** (vblank, timer, LSPC, I/O). Over **1200 frames** (19.6 M
instructions) they agree exactly:

```
INTERP: frames=1200 cycles=243302402 instructions=19672601 vram_writes=205824 pal_writes=32769 io_writes=9011967 last_pc=C15434
RECOMP: frames=1200 cycles=243302402 instructions=19672601 vram_writes=205824 pal_writes=32769 io_writes=9011967 last_pc=C15434
recomp: native_blocks=318020 interp_instrs=0 bad_dispatch=0
```

- All architectural counters match; **100 % of the executed BIOS instructions
  ran native** (`interp_instrs=0`), with no bad dispatches.
- Wall time for 1200 frames: **interp 0.232 s vs recomp 0.052 s = ~4.5x faster**.
- `ng_boot.c` with `NG_BOOT_RECOMP` cross-compiles for SH-2: `ng_boot_recomp.o`
  = 6,632 B text + 475,138 B `.wram_l` (the Neo Geo RAM/ROM images).

### The memory wall (why this is not yet on the Saturn disc target)

The recompiled **BIOS** slice alone is 585,656 B of SH-2 text. The current
Saturn `kof_ng_boot` app is ~355 KB and already holds Musashi (~285 KB text +
~589 KB opcode/cycle tables in WRAM-H). Adding the BIOS recomp would need
~940 KB text + 589 KB tables in a 1,008 KB WRAM-H - it does not fit, and WRAM-L
is already ~475 KB of Neo Geo images. A **recomp-only** BIOS build (drop Musashi;
the BIOS slice showed 100 % coverage) would fit, but the **game** needs a fallback
and its own P1/P2 code recompiled (megabytes), so the target needs recomp routine
**overlay/streaming** rather than a resident blob. That is the real X2 engineering
task; the dispatch mechanism it rests on is now proven and in-tree.

## Recomp-only BIOS on the Saturn SH-2 (`kof_ng_bench_recomp`)

The recomp dispatcher now runs on a real Saturn target with **no interpreter at
all**. `NG_BOOT_RECOMP_ONLY` drops Musashi entirely: a dispatch miss is a hard
fault (`ng_boot_recomp_bad`, guest halted) instead of a silent fallback, and the
reset state is seeded straight from the reset vector page. That removes the
589 KB Musashi table set, so the recompiled BIOS fits:

| target | app.bin | vs 983,040 B limit |
|---|---|---|
| `kof_ng_bench_interp` (Musashi) | 332,732 B | fits |
| `kof_ng_bench_recomp` (recomp only) | 666,444 B | fits |

`sh2eb-elf-nm` shows **no `m68k_*` symbols in the recomp ELF**, confirming the
interpreter is genuinely absent.

### Equivalence is preserved without the interpreter

`ng_boot_host_recomp_only` (host, same core, `-DNG_BOOT_RECOMP_ONLY`) must
reproduce the interpreter's numbers exactly:

```
DONE frames=1200 cycles=243302402 instructions=19672601 vram_writes=205824 pal_writes=32769 io_writes=9011967 last_pc=C15434
recomp: native_blocks=318020 interp_instrs=0 bad_dispatch=0
```

Identical to the interpreter and to the recomp-with-fallback build (ctest
`ng_boot_recomp_only`), i.e. removing Musashi changes nothing observable.

### SH-2 speed, measured in Ymir (emulator evidence)

`scripts/kof_ymir_bench.py` runs the **same BIOS-only program** (one guest frame
per loop, no vblank wait, so the emulator's frame clock decides how many fit)
against both cores; only `ng_boot.c`'s core differs. `guest_frames /
emulated_frames` is the speed ratio: **1.0 = real time (60 Hz)**.

```
$ python scripts/kof_ymir_bench.py --frames 1200
interp : 82 guest frames in 1200 emulated frames = 0.0683x real time
recomp : 373 guest frames in 1200 emulated frames = 0.3108x real time bad_dispatch=0
recomp_speedup_vs_interp: 4.551
```

- Recomp-only BIOS: **0.311x real time**, i.e. ~18.7 guest frames per emulated
  second against the 60 Hz the 68000 needs. Still **~3x short of real time**.
- Speed-up over the interpreter on the SH-2: **4.55x** (the host showed 4.5-6x,
  so the SH-2 result is in the same range).
- `interp_instrs=0` and `bad_dispatch=0`: 100 % of the executed BIOS instructions
  ran native, none fell back, none faulted.
- The 68000 executed 6,785,679 guest instructions in 20 emulated seconds
  (~340k guest instr/s) - enough to show the mechanism works, not enough to keep
  up with a 12 MHz 68000.

This is the first time the recomp path has been timed on the SH-2 rather than
modelled. It is **Ymir, not physical-console** timing, and it covers the BIOS
only - the game's own P1/P2 code is still interpreted.

## Recomp routine overlay (paging), host-proven

The dispatcher can now page recompiled routines in and out instead of requiring
every routine resident. `m68krecomp --files N` already splits the program into N
independent translation units ("pages"); routines never call each other directly
(every intra-ROM transfer sets `CPU.pc` and returns to the dispatcher), so a page
is a load unit. `tools/mk_recomp_pages.py` turns the generated files into a sorted
`PC -> {routine, page}` map, and `ng_boot.c` (behind `NG_BOOT_OVERLAY`) keeps a
residency bitmap, a page budget and LRU eviction. The only platform-specific part
is `ng_overlay_loader(page)`: the host build is a stub (pages are already linked),
the Saturn build will copy the page bytes from the disc into a fixed arena.

The map must cover **every entry point the recompiled code can be entered at**,
not just routine starts. `m68krecomp` emits 1,148 dispatch entries for the BIOS
slice (287 routines): a JSR return lands in the middle of the caller, and that
address is an entry too. An earlier map built only from routine starts made the
dispatcher miss on the first return and fall back to Musashi
(`native_blocks=12`); mapping all 1,148 entries restored `native_blocks=318020`,
`interp_instrs=0`. `tests/test_mk_recomp_pages.py` pins that rule (plus duplicate
address and unknown-routine rejection) without any ROM.

### Evidence (host x86-64, `ctest -R overlay`)

```
recomp: native_blocks=318020 interp_instrs=0 bad_dispatch=0
overlay: faults=10 loads=10 evictions=7 errors=0 resident=3 budget=3 paging_ok=1
DONE frames=1200 cycles=243302402 instructions=19672601 bios=0 cart=0 wram=0 irq=0 vblanks=1200 timer_irqs=0 vram_writes=205824 pal_writes=32769 io_writes=9011967 last_pc=C15434
```

With a 3-page budget the run is **bit-exact** against the interpreter (same
cycles, instructions, VRAM/palette/I/O writes, last PC) while only 10 page loads
and 7 evictions served 318,020 dispatches: the BIOS working set has enough
locality for paging. That is mechanism validation, not a game result.

## Game-side recomp size: the full-program native path does not fit

The BIOS slice is small. To decide whether recompiling the game is feasible, the
same generator was run on the real P1 (1 MiB) from the local ROMs:

```
$ python -c "byte-swap roms/242-pn1.bin -> p1-68k.bin"
$ m68krecomp --raw p1-68k.bin --base 0 --entry 0x122 --prefix p1_ --out local/recomp-p1 --files 1
[m68krecomp] pointer scan: 23307 seeds
[m68krecomp] 874188 instructions emitted across routines
[m68krecomp] 24508 routines, 39927 dispatch entries

$ m68krecomp ... --files 64
[mk_recomp_pages] 64 pages, 24508 routines, 39927 entry points, 874188 instructions
[mk_recomp_pages]   page 0: 383 routines, 13880 instructions
```

Compiling page 0 with the same SH-2 toolchain as the Saturn build:

| build | SH-2 text | bytes / guest instruction |
|---|---:|---:|
| P1 page 0, `-Os` | 334,724 B | 24.1 |
| P1 page 0, `-O2` | 479,428 B | 34.5 |
| P1 page 0, `-O3` | 514,020 B | 37.0 |
| BIOS reset slice, `-O2` (reference) | 585,656 B | 62.6 |
| BIOS reset slice, `-O3` (current bench) | 625,840 B | 66.9 |

The BIOS is the dense outlier (short, branch-heavy routines with many entry
labels); the game's own code is 24-37 B of SH-2 text per 68000 instruction. Scale
that to the whole program:

| program | instructions | `-Os` | `-O2` | `-O3` |
|---|---:|---:|---:|---:|
| P1 only (measured) | 874,188 | 21.1 MB | 30.2 MB | 32.4 MB |
| full P ROM (upstream baseline, 1,555,625) | 1,555,625 | 37.5 MB | 53.7 MB | 57.6 MB |

The Saturn has 2 MB of work RAM for everything. After the Neo Geo RAM images
(~475 KB), the app, sound driver, stacks and any Musashi fallback tables
(~590 KB), the executable budget is **well under 1.5 MB**. A resident full-game
native recomp is therefore **not possible**, and neither is paging it from CD at
60 Hz: at 2-4x CD speed only a few hundred KB/s could stream, while the fight
path would need to move megabytes per second if the working set were large.

This does not invalidate the dispatcher, the overlay mechanism or the exactness
work: it invalidates the assumption that **the whole program** can be native.
The next step is the one `PORTING_PLAN` and `CROSS_PORT_ENGINEERING` prescribe -
profile the real game, size the executed routine set, and keep only a bounded hot
subset native (or a compact block form) with a correct interpreter fallback.

## Game profile (X1): where the fight instructions actually run

`ng_boot.c` has a host-only guest PC profiler (`NG_BOOT_PC_PROFILE`): executed
instruction words, 4 KB page hits, 64-byte line hits and an optional executed-word
range dump. The host driver (`ng_boot_host_prof`) runs the real game with the
pinned reference's authored script (`deps/kof98recomp/tests/start_fight.txt`)
and resets the profiler at a chosen frame, so the fight window is measured
without the boot/menu/select path.

Evidence (`local/profile/`, ROM-derived, never committed):

```
NG_INPUT=deps/kof98recomp/tests/start_fight.txt \
NG_PROFILE_FROM=3900 NG_PROFILE_OUT=fight_3900_5000.txt \
NG_PROFILE_WORDS=fight_3900_5000.words \
ng_boot_host_prof roms/neo-po.bin 5000 roms/242-pn1.bin roms/242-p2.bin
```

The script reaches a live round: P1/CPU teams `18191A` from frame 3100, stage 1
at 3400, P2 life `$108438` = `0067` until the scripted attacks at 4000-4180 drop
it to `0062`. The no-attack control stays at `0067` through 4600, so the window
really contains a fight, not a demo or an idle screen.

| window (frames 3900-5000) | instructions | distinct words | pages |
|---|---:|---:|---:|
| scripted fight | 20,969,061 | 7,369 | 48 |
| idle/attract control | 20,590,561 | 8,338 | 58 |
| union | 41,559,622 | 12,594 | 66 |

Per region the fight window is **98.8% P1** (`$000000-$0FFFFF`), 1.2% BIOS and
no P2 bank execution; the idle window is the same shape. The fight is dominated
by four P1 pages: `$00A000` (36.1%), `$006000` (24.8%), `$007000` (9.8%) and
`$005000` (9.4%). The idle window puts 61.2% in `$00A000`, which is the same
frame-loop/attract code, not a different engine.

`tools/profile_guest.py` turns the profile into a size/coverage estimate
(`tests/test_profile_guest.py` pins the arithmetic without ROMs):

```
python tools/profile_guest.py fight.txt idle.txt --words fight.words --words idle.words
```

Cumulative coverage by guest 16 KiB region, union of fight+idle, with the
measured SH-2 text sizes (`-Os` 334,724 B, `-O2` 479,428 B, `-O3` 514,020 B per
P1 unit; BIOS slice 585,656 B `-O2` / 625,840 B `-O3` for all 8 units):

| covered | units | instructions | share | `-Os` | `-O2` | `-O3` |
|---:|---:|---:|---:|---:|---:|---:|
| 50% | 1 | 21,769,095 | 52.38% | 334,724 | 479,428 | 514,020 |
| 80% | 2 | 37,341,082 | 89.85% | 669,448 | 958,856 | 1,028,040 |
| 90% | 3 | 38,264,183 | 92.07% | 1,004,172 | 1,438,284 | 1,542,060 |
| 95% | 5 | 39,793,374 | 95.75% | 1,673,620 | 2,397,140 | 2,570,100 |
| 99% | 11 | 41,186,561 | 99.10% | 3,420,447 | 4,867,487 | 5,218,430 |

The coverage is good news for correctness (a small code region runs most of the
game) but **not a native-size result yet**: these are guest address regions, not
the recompiler's load units. `m68krecomp --files 64` assigns routines
round-robin, so one 16 KiB guest region is spread over many generated files and
no 334 KB file corresponds to it. Selecting whole generated pages by dynamic
hits would select a roughly uniform 1/64 slice, not the hot region. The native
subset therefore has to be selected at **routine granularity** (the existing
`tools/select_native.py` mechanism) using per-routine counts; the current line
profile is enough to rank regions but not to emit the selection. This is the
next profiling change, not a solved budget.

Even with a perfect hot subset, the measured ceiling is the problem: the
recompiled BIOS on the SH-2 runs at **0.3108x real time** (below), i.e. full
native code would still be ~3.2x short of 60 Hz on this dispatcher/code shape.
A native subset can only be slower than that. So the CPU work is now two
tracks, both required before any playable claim:

1. **Selection**: add per-routine hit counts to the profiler, emit the routine
   selection with `select_native.py`, and measure a hybrid native+interpreter
   build on the SH-2. Report the exact fallback share and its cost.
2. **Code shape**: measure and reduce the ~90 SH-2 cycles per recompiled guest
   instruction (register caching, compact page-sliced dispatch, removing
   per-block verification bookkeeping, flag-helper cost). Without this, even a
   100% native build does not reach real time.

No playable-performance claim is made from the profile or from emulator timing.


## Next blocks (toward the full port)

1. **Profile the game** on the host oracle: executed PCs/blocks and per-frame
   counts in menus, a fight and the boss path (X1), then size the hot set against
   the <1.5 MB executable budget and select the native subset (X2).
2. **Speed**: only after (1), recompile the selected hot routines and measure the
   hybrid native + interpreter core on the SH-2; feed the live guest LSPC state
   into the existing VDP1/VDP2 renderer.
3. **Live sound**: the Z80 + YM2610 driver (Phase F); `docs/MUSIC_ON_SATURN.md`
   is the pre-rendered first step.
4. All modes / team play / boss paths, real hardware / optical boot.

Nothing here changes the graphics replay path or the host oracle; this is an
independent native-execution experiment (`KOF_BUILD_NG_BOOT`, default OFF).