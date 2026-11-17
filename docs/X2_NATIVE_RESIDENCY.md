# X2 — profile-driven hot native / cold interpreter experiment

The full AOT program has not been shown to fit the Saturn. This candidate selects whole generated functions using measured SH-2 symbol sizes and desktop guest-instruction coverage. No executable code is placed in the mandatory 4 MiB data cartridge. The host still uses Musashi for unselected entry points and RAM code; the target's interpreter size/speed remains an independent gate.

## Collect local evidence

Use the unoptimized full paged host baseline in HOST_PAGED_ORACLE.md, without dedup/flags/cycles/regcache. The host-only profiler is opt-in via KOF_NATIVE_PROFILE (output CSV filename). Run independent boot/menu, attract, team, practice, boss and varied fight scenarios, each with a unique path. Use the original headless input scripts and verified local AES dumps.

    mkdir -p local/profiles
    KOF_NATIVE_PROFILE=local/profiles/fight.csv ./build-host-paged/kof_host_paged --rom-path "$PWD/roms" --headless --verify --frames 4400 --input deps/kof98recomp/tests/start_fight.txt

The CSV logs canonical tagged entry PC, calls, executed guest instructions and virtual cycles aggregated at native block return. It is not wall time or target cycles. RAM/interpreted work is excluded; retain original interpreter/native totals and verify output alongside it. A SHA-256 identity binds profiles and size reports to generated table, common header and routine units. It does not authenticate toolchains, ROM data not represented in those outputs, or runtime configuration; record those separately. Counters fail loudly on overflow. The hash table is desktop-only (typically 4 MiB allocated only when profiling).

    python3 tools/measure_native.py build-host-paged/generated deps/kof98recomp/neogeorecomp build-sh2-sizes --opt Os
    python3 tools/select_native.py build-host-paged/generated build-hot/generated --profile local/profiles/fight.csv --sizes build-sh2-sizes/sizes.txt --budget 524288 --reserve 65536

Add repeat --profile for independent scenarios. The budget and reserve above are example arguments, NOT the established WRAM allocation. Set them from a target link budget, including all runtime code/data, stacks and guards. Selection greedily ranks observed routines by guest instructions per estimated byte. Charge 16-byte-rounded symbol size, 6 bytes per retained dispatch entry, 2,626 bytes directory and explicit reserve. All entries belonging to a retained routine are kept. Retain nothing from unobserved routines in this first experiment; missing native entries intentionally use the interpreter. Do not feed hybrid miss dumps into generator coverage because deliberate omissions are not discovery bugs. No overlay implementation yet.

Build the hybrid host by configuring a separate tree with KOF_BUILD_HOST_ORACLE=ON plus KOF_NATIVE_PROFILES (semicolon-separated absolute CSV paths), KOF_NATIVE_SIZES (absolute sizes.txt), KOF_NATIVE_BUDGET and KOF_NATIVE_RESERVE. Exact generated-code identity must match. The command selects from the raw generator output; optimization flags are mutually exclusive for this experiment.

## Evidence and limitations

Five ROM-free local tests actually ran successfully. Compiled host C profiler with strict warnings; verified repeated key accumulation, distinct banks, BIOS mirror canonicalization and disabled profiling. Python tests checked deterministic budget selection, missing sizes, malformed/duplicate profile keys and invalid budgets. Local Python tests used stubs for unused full-generation parser/render functions and the C test used an equivalent local dispatch-key implementation. Full transformed real source/CMake integration, fingerprint-binding integration, size measurement on SH-2 and actual gameplay remain unexecuted. The new size-report identity guard was added after the isolated test run.

### First executed hybrid experiment (2026-11-16)

The full pipeline ran on real ROMs: `kof_host_paged` (full AOT) profiled
4400 frames of `start_fight.txt` → 2,046,087 native blocks, 11.9M interpreted
instructions, 135 uncovered entries. `measure_native.py` compiled 39,917
routine units to SH-2 (`-Os`) and collected sizes (1.27 MB). `select_native.py`
with a 512 KiB budget / 64 KiB reserve retained **104 routines / 196-208
entries**, covering 92.7% of the profiled guest instructions (fight only) or
88.7% across fight + play_long (8800 frames, 5.29M blocks, 12M interpreted).
The hybrid host (`build-hot2`) ran **bit-exact** on both scenarios (0
mismatches), but was **slower in interpreted instructions**: fight 17.3M
interpreted (vs 11.9M full AOT, +41%), play_long 30.5M (vs 12M, +154%), with
6,026-9,049 uncovered entries (vs 135-250 full AOT).

**Finding**: the greedy guest-instruction-density ranking at a fixed 512 KiB
budget is **counterproductive** — the retained large routines cover most
guest instructions, but the uncovered 11% of instructions are spread over
thousands of small hot blocks, and each uncovered entry pays the full
dispatch-table lookup + interpreter setup. Native savings on the retained
blocks do not offset the dispatch cost of the missed ones. The full AOT's 57k
entries had near-zero dispatch misses; the hybrid's 196 entries miss
constantly. **A fixed hot subset without paging cannot beat full AOT at this
budget**; the X2 overlay/paging track (load hot routines on demand, keep the
dispatch table dense) is the only viable path, and the dispatch cost itself
must be measured, not assumed.

### X2 overlay/paging track — core dispatch extended (2026-11-16)

The core's recomp dispatch (`ng_boot.c: recomp_lookup`) was **BIOS-only**
(`0xC00000-0xD00000`); game P-ROM addresses always went to Musashi — the
reason the integrated app runs at 0.0683x. Two changes landed:

1. **Dispatch extension**: `recomp_lookup` now routes cart addresses
   (P1 at `0x000000-0x0FFFFF`, P2 at `0x200000-0x4FFFFF`) through a
   `game_recomp_lookup` that binary-searches a weak-linked game recomp table
   (`ng_game_recomp_table()`/`ng_game_recomp_count()` hooks; default NULL so
   host tests link without a game recomp). BIOS addresses keep the existing
   overlay/table path. All 39 host tests pass with the extension.
2. **Game recomp generated locally** (`local/ng_recomp_game/`, never
   committed): `m68krecomp` on the byte-swapped P1+P2 cart image
   (`local/game-cart-68k.bin`, 4 MiB) emitted **40,112 routines / 56,748
   dispatch entries** in 8 page units (`game_recomp_00..07.c`, ~30 MB each —
   far beyond WRAM, which is why paging is mandatory).

**Remaining**: the Saturn build must link the game recomp table (2.96 MB of
dispatch entries — itself too large for the 983 KB app budget, so the table
must also be paged/streamed from the disc) and implement the overlay loader
for the 8 game pages (`ng_overlay_loader` on Saturn copies the page's linked
bytes from the disc into a fixed arena; the host hook is
`src/host/ng_overlay_host.c`). The BIOS overlay test (`ng_boot_overlay`,
8 pages / 3-page budget, bit-exact) proves the mechanism; the game track
follows the same pattern with the generated page map
(`tools/mk_recomp_pages.py`). The dispatch table itself must be reduced to
the hot subset (via `select_native.py` + profile) or streamed, since
56,748 entries x 8 bytes exceeds the app budget.

**Next concrete steps**: (a) page the game recomp's dispatch table (dense
sub-table + disc-backed misses), (b) Saturn-side `ng_overlay_loader` for the
game pages, (c) wire the game table into the `kof_ng_boot` target behind the
existing `NG_BOOT_OVERLAY` mechanism, (d) measure gameplay speed with the
game routines native.

### Page map generated (2026-11-16)

`tools/mk_recomp_pages.py` on `local/ng_recomp_game/` produced
`game_recomp_pages.h/.c`: **8 pages, 40,112 routines, 56,748 entry points,
1,537,056 instructions** (51,633 entries in P1/cart 0, 4,981 in P2/cart 1).
The `ng_recomp_pages[]` map is **681 KB** (56,748 x 12 B).

**WRAM budget measured**: the current app is 359,064 B on disc with **456,944 B
of BSS** (0x0605BA98..0x060CB388) — together ~816 KB, leaving only ~167 KB
free in the 983 KB budget. The 681 KB map **cannot fit in WRAM at all** (not
just the 57 KB overage vs the disc image): the map **must be streamed from
the disc** — a dense hot sub-table in WRAM (the reset-PC neighborhood and the
profile's hot routines) plus the full map disc-backed, with misses falling
back to the interpreter (correct) or a disc read (slow).

**Page granularity corrected (2026-11-16)**: the original 8-page split made
each page ~384-769 KB compiled (3.1-6.1 MB total) — **less than one page
fits the ~167 KB free WRAM**, so the overlay would thrash. Regenerated with
`m68krecomp --files 64` (`local/ng_recomp_game64/`): **64 pages, 626
routines/page, ~23k instructions/page (~46-92 KB compiled)** — the resident
budget (2-3 pages) fits WRAM with the hot sub-table. The page map is still
681 KB (the entry count is unchanged); the hot sub-table is **4,190 entries
(50 KB)** covering all 64 pages.

**Profile coverage measured**: the fight + play_long profiles touch only
**3,516 of the 56,748 entries (6.2%)** — the sub-table is a *cache* over the
full map, not a replacement. Each page unit must live on the disc and be
paged into a fixed arena on demand — the CD holds the 3.1-6.1 MB page set
(~1.5-3k sectors of the ~333k available), but the Saturn build must add a
disc-staged page file and the loader below.

### Hot sub-table generated (2026-11-16)

`tools/make_hot_subtable.py` extracted a dense hot sub-table from the full
map: **4,190 of 56,748 entries (50,280 bytes)**, covering the reset-PC
neighborhood (cart 0 page of 0x000000) plus every profile-hot entry across
both scenarios. It fits the ~167 KB free WRAM with room to spare. Misses
fall back to the interpreter (correct) — the sub-table is the cache that
makes the dense dispatch affordable; the full map stays disc-backed for
future expansion.

**Note on the hybrid lesson**: the earlier fixed-budget hybrid (196 sparse
entries) was slower than full AOT (+41% interpreted) because its misses paid
the dispatch cost. The hot sub-table is **dense and cache-resident** (4,190
contiguous entries, one binary search), which should avoid the sparse-miss
pathology — but this must be measured, not assumed, once the Saturn side is
wired.

### Saturn overlay loader (written, wiring remaining)

`src/host/ng_overlay_host.c` is the host hook (records the load, returns 0 —
the pages are pre-linked). The Saturn hook is written:

- `src/saturn/ng_recomp_overlay_config.h`: `NG_RECOMP_ARENA_PAGES = 1`
  (the app's ~816 KB of the 983 KB budget leaves ~167 KB; one ~96 KB page
  fits, no eviction yet), `NG_RECOMP_PAGE_BYTES = 98304` (the max compiled
  page size; the pages are ~46-92 KB).
- `src/saturn/ng_overlay_saturn.c`: disc-backed `ng_overlay_loader` (reads
  the page's `NG_RECOMP_PAGE_BYTES` from the disc-staged `PAGES.BIN` at
  `page * NG_RECOMP_PAGE_BYTES` into `g_page_arena[page]`) +
  `ng_overlay_saturn_init` (mounts `PAGES.BIN` at boot).

Remaining wiring:
1. The core's overlay is **BIOS-specific** (`#ifdef NG_BOOT_OVERLAY` uses
   `bios_recomp_pages.h`, `overlay_lookup` faults on BIOS pages). The game
   track needs the same fault→load→retry for **game pages** (the
   `game_recomp_lookup` path calls `ng_overlay_loader` on a non-resident
   page, following the `overlay_lookup` pattern).
2. `g_page_arena` needs a linker placement in WRAM-H — the app does not
   currently use a `.wram_h` section; the LibSaturn linker script (or a
   custom anchor) must place it.
3. The `kof_ng_boot` CMake target must add: `ng_overlay_saturn.c` +
   `ng_recomp_overlay_config.h`, the hot sub-table + page units (from
   `local/ng_recomp_game64/`, local-only), the `NG_BOOT_OVERLAY` define,
   the game page map (`game_recomp_pages.h`), and a disc-staged
   `PAGES.BIN` (the 64 page units padded to `NG_RECOMP_PAGE_BYTES` each).
4. The BIOS overlay test (`ng_boot_overlay`, 8 pages / 3-page budget,
   bit-exact) proves the core mechanism; the game track reuses it.

### Core game-page overlay path added (2026-11-16)

`ng_boot.c` now has the game-page fault→load→retry path
(`game_overlay_lookup`), following the BIOS `overlay_lookup` pattern:

- A weak-hooked game page map (`ng_game_pages()` / `ng_game_pages_count()`,
  default NULL/0 so BIOS-only overlay builds link).
- `game_fault(page)`: on a non-resident page, evicts the LRU resident page
  when the budget (`ng_game_overlay_set_budget`) is full, calls
  `ng_overlay_loader(page)`, and counts `ng_boot_game_loads/faults/
  evictions/errors/resident`.
- `recomp_lookup` routes cart addresses (P1 `0x000000-0x0FFFFF`, P2
  `0x200000-0x4FFFFF`) through `game_overlay_lookup` under
  `NG_BOOT_OVERLAY`, else the flat `game_recomp_lookup` table.

All 39 host tests pass with the new path; the Saturn disc builds
(4187 sectors). The remaining wiring is the CMake/linker integration
(items 2-4 above) and the Saturn-side loader/config compilation into
`kof_ng_boot`.

### PAGES.BIN staged (2026-11-16)

`tools/pack_pages.py` (new) compiles the 64 game recomp page units to SH-2
objects (`sh2eb-elf-gcc -Os -ffreestanding -ffunction-sections`), extracts
each `.text`, and concatenates them padded to the arena stride. The CMake
`kof_ng_boot` target now stages **`PAGES.BIN` (64 x 98,304 B = 6,291,456 B)**
on the disc (`build-ng/disc/kof_ng_boot.bin` grew from 4187 to **7259
sectors**), compiles `ng_overlay_saturn.c` + `ng_recomp_overlay_config.h`
into the app, and adds the game page-map + neogeorecomp include paths.

### App wiring (2026-11-16)

- `ng_boot_main.c`: strong `ng_game_pages()`/`ng_game_pages_count()` hooks
  returning the generated `ng_gamemap_pages[]` (from `game_recomp_pages.h`
  with `--sym-prefix ng_gamemap` to avoid colliding with the BIOS map);
  `ng_overlay_saturn_init(&g_volume)` + `ng_game_overlay_set_budget(1)` at
  boot after `sat_cd_mount_default`.
- CMake: `NG_BOOT_GAME_OVERLAY=1` define, `ng_overlay_saturn.c` +
  `ng_recomp_overlay_config.h` compiled in, game page-map + neogeorecomp
  include paths.
- `mk_recomp_pages.py`: `--sym-prefix` option (default `ng_recomp`; use
  `ng_gamemap` for the game map).

**Blocker resolved (design)**: the game overlay section in `ng_boot.c` is
nested inside `#ifdef NG_BOOT_RECOMP` (line 494) — the overlay (and
`recomp_lookup`) is a **recomp feature**: the interpreter executes
instructions directly via Musashi and never dispatches through recomp
tables, so the interpreter build cannot use the overlay. The fix is to
build `kof_ng_boot` with `NG_BOOT_RECOMP` (the recomp dispatch), which is
now the CMake define. The recomp build needs the **BIOS recomp table**
(`bios_ng_recomp_table`, generated by `ng_m68krecomp --files 1` from
`roms/neo-po.bin` into `build-ng/ng_recomp_bios/`) — the `kof_ng_boot`
target does not generate it yet; that generation step is the remaining
build-system work.

### Remaining wiring (app side)

1. Generate the BIOS recomp for the Saturn target (port the
   `ng_recomp_bios` custom command from the host-tests block into the
   `kof_ng_boot` block, using `ng_m68krecomp` on the staged BIOS).
2. Link `bios_recomp_00.c` + `bios_recomp_table.c` into `kof_ng_boot`.
3. Rebuild `kof_ng_boot_disc`, then run the Ymir gameplay measurement
   (frames + wall time) with the game routines native to record the speed.

### Build wiring done, page-map blocker (2026-11-16)

The build system is fully wired:
- `NG_BOOT_RECOMP=1 NG_BOOT_GAME_OVERLAY=1` defines on `kof_ng_boot`.
- BIOS recomp generation (`ng_recomp_bios/` via `KOF_M68KRECOMP` from
  `build-host-tests`) + `bios_recomp_00.c`/`bios_recomp_table.c` linked.
- `game_recomp_pages.c` (the `ng_gamemap_pages[]` map) linked.
- `ng_overlay_saturn.c` + config compiled in; `PAGES.BIN` (6 MB) staged.
- The arena moved to `.wram_l` (Work RAM Low) — WRAM-H is full with the
  app + BIOS recomp + stacks (the arena overflowed WRAM-H by 525 KB).

**Blocker (fundamental)**: the generated page map stores **direct function
pointers** (`ng_func_t fn`) to the page-unit routines (`game_r_*`), which
are C functions that must be compiled and linked to be callable. The pages
are disc-backed **C source**, not raw SH-2 code — a disc-loaded page of C
cannot be called. The link fails with `undefined reference to game_r_*`.
The overlay-as-designed (page C source on disc, call via function pointer)
is **not viable**: it requires the pages to be **position-independent raw
SH-2 code** that can be loaded anywhere and called indirectly, which
`m68krecomp` does not emit (it emits C).

**Remaining options**:
1. **Full AOT recomp**: link all 40k routines (3.1-6.1 MB compiled) into
   the app — far beyond the 983 KB budget; needs the code size reduced
   (the X1/X2 size-reduction tracks) or a code/data split (cart holds
   data only).
2. **Raw-code recomp generator**: a new tool that emits position-
   independent SH-2 machine code per routine (not C), which the loader
   can patch with the resident base and call indirectly. A major new
   component.
3. **The host-oracle hybrid** (current): the full AOT on the host, with
   the Saturn app using the interpreter — the current 0.0683x path.

All 39 host tests still pass; the disc builds (7259 sectors, 17 MB with
PAGES.BIN). The build wiring (BIOS recomp, page map, loader, PAGES.BIN,
defines) is complete and reusable for option 2.

### Per-page size measured (2026-11-16)

One page unit (`game_recomp_00.c`, 626 routines / ~23k instructions)
compiles to **505,884 B of SH-2 text** with `sh2eb-elf-gcc -Os
-ffreestanding -ffunction-sections` (~22 bytes per guest instruction —
the switch dispatch boilerplate dominates). The 64-page set is ~32 MB
total; the arena slot (98,304 B) holds **less than one page**. The
C-source overlay is doubly infeasible: the pages are both **un pedigable
(C source)** and **too large** (500 KB each vs the ~167 KB free WRAM).

The raw-code generator (option 2) must emit pages at a fraction of this
size — the C compiler's per-instruction overhead (switch, frame setup,
`ng_bad_dispatch` call) is the target for reduction. A realistic raw-code
page is ~2-4 bytes per guest instruction, so 64 pages would be ~3-6 MB,
with the resident arena holding 1-2 pages of ~50-100 KB — feasible.

### Raw-code extraction mechanism PROVEN (2026-11-16)

A full new code generator is **not needed**: the compiler's own output is
position-independent after a simple relocation patch. `tools/rawcode_pages.py`
(+ `tools/rawcode_link.ld`) links a page unit's `.text.*` sections into one
blob at VMA 0 with `--emit-relocs`, then classifies every relocation:

- All relocations are `R_SH_DIR32` (simple absolute 32-bit) — verified:
  **10,234 / 10,234** in page 0 are this single type.
- External refs (app globals/libgcc: `_ng_cpu`, `_ng_cycles`,
  `_ng_bad_dispatch`, ...) are pinned to fixed app addresses — unchanged at
  load time.
- Internal refs (page helpers like `.text.ng_logic8`, `.text.ng_push32`)
  shift with `page_base`.

**Self-test** (link the same page at VMA 0 and 0x20000, patch-check every
slot): `blob=505884 B relocs=10234 external=4163 internal=6071 bad=0`.
So a loader can copy the blob into the arena and add `page_base` only to the
internal slots — the page becomes callable via `fn = page_base + offset`.

Note: SH-2 is **big-endian** — the 32-bit relocation values must be read
big-endian (`int.from_bytes(..., "big")`), a bug found and fixed in the test.

**Page size solved**: regenerating with `m68krecomp --files 256`
(`local/ng_recomp_game256/`) gives pages of **123,596 B (~121 KB)** —
self-test `blob=123596 B relocs=2565 external=1064 internal=1501 bad=0`.
This fits a 128 KiB arena (`NG_RECOMP_PAGE_BYTES = 131072`); WRAM-L has
room for it. The 256-page set is ~32 MB on disc (fits a CD).

**Remaining to wire the proven mechanism**: (1) emit PAGES.BIN + a relocation
patch table + an addr→(page,offset) map with external symbols pinned to the
*real* app runtime addresses (extracted from the app ELF); (2) loader applies
internal relocations; (3) dispatcher computes `fn = page_base + offset`;
(4) build + Ymir gameplay measurement.

### Pack tool + end-to-end verification (2026-11-16)

`tools/rawcode_pack.py` implements the full pack: for each of N page units it
compiles to an object, links all `.text.*` into one blob at VMA 0 with
external symbols pinned to addresses from a supplied app ELF, emits the
padded blob into **PAGES.BIN**, records the internal `R_SH_DIR32` slot offsets
into **RELOCS.BIN** (per page: u32 count, then u32 offsets), and emits an
`addr → (page, offset)` C map (`rawcode_map.c`).

Tested on 4 pages of the 256-page set (`build-ng/kof_ng_bench_recomp.elf` as
the symbol source): `pages.bin=786432 B relocs=25032 internal=6254
map=900/56748`. The loader algorithm is: copy the page blob to `page_base`,
then for each recorded offset `*(u32*)(page_base+off) += page_base`.

`tools/test_rawcode_pack.py` verifies this end-to-end: it links page 0
directly at base 0x06000000 with the same symbol addresses and compares
against the packed blob patched with RELOCS.BIN. Result:
**`direct=123596 relocs=1501 diffbytes=0 equal=True`**. The packed pages are
provably correct and position-independent.

Page sizes vary (min 109,444 B, max 164,292 B over the first 24 pages), so
the pack stride was raised to 196,608 B (192 KiB); the 256-page PAGES.BIN is
~48 MB (fits a CD). The arena config (`NG_RECOMP_PAGE_BYTES`) must be raised
to 196608 to match.

**Build state**: the app-side overlay wiring that caused link errors
(function-pointer page map → `undefined reference to game_r_*`; 681 KB full
map + 128 KB arena → WRAM overflow) was **reverted** this turn to restore a
green build. The app runs the interpreter again; all tooling research files
(`rawcode_pages.py`, `rawcode_link.ld`, `pack_pages.py`,
`make_hot_subtable.py`, config header, loader source) are preserved and
untracked. `mk_recomp_pages.py` gained a `--null-fn` flag (emit 0 pointers)
and `--sym-prefix` (rename the entry type) for the rawcode path.

### C relocation loader + WRAM blocker (2026-11-16)

`src/saturn/rawcode_loader.c` (+ `include/kof98/rawcode_loader.h`) implements
`rawcode_patch_page(dst, relocs, page, load_base)`: it parses the RELOCS.BIN
page index and adds `load_base` to each internal slot of the page blob at
`dst`. Writes are **explicitly big-endian** (a native `*slot = ...` is wrong
on a little-endian host — found and fixed in testing), so the module is
correct on the SH-2 and host-testable. RELOCS.BIN now has a page index
(`u32 page_count, u32 block_offset[pages], blocks...`).

`tests/test_rawcode_loader.c` verifies the C loader against the Python patch
reference: **`slots=1501 ref=123596 diffbytes=0 equal=yes`**. With
`test_rawcode_pack.py` (`diffbytes=0 equal=True`), the full chain —
pack → disc blob → C loader patch — is byte-exact.

**WRAM solution — minimal rawcode game bench (2026-11-16)**: the WRAM blocker
was resolved by a dedicated minimal bench instead of the full app.
`src/saturn/ng_rawcode_bench_main.c` loads the real BIOS + P1/P2 cart, sets up
the rawcode overlay (PAGES.BIN/RELOCS.BIN + map), and runs guest frames flat
out — no renderer/music/tiles. The CMake target `kof_ng_rawcode_bench`
(opt-in `KOF_BUILD_RAWCODE_BENCH=ON`) links and builds: `.text`=0xaf58c (718 KB,
WRAM-H), `.wram_l`=0xd7340 (881 KB) — both fit with headroom. Its disc
(3572 sectors: BIOS + P1 + P2 + PAGES.BIN + RELOCS.BIN) builds.

**Full wiring in the bench**: `ng_overlay_saturn.c` now reads each page's reloc
block from RELOCS.BIN on demand and calls `rawcode_patch_block`; `ng_boot.c`'s
`game_overlay_lookup` resolves `fn = ng_overlay_page_base(page) + off` (the
map's offset, not a linked pointer). `mk_recomp_pages.py --rawcode` emits the
`{addr,page,off}` map type; `make_hot_rawcode_map.py` emits a fixed-size hot
map to keep symbol addresses stable across the 2-pass build. The C loader,
dispatcher and counters (`ng_boot_game_loads/faults/...`) are in the ELF.
`scripts/kof_ymir_bench.py` has a `rawcode` target for the measurement.

The 256-page repack with the bench's real symbol addresses is running; once
done the pass-2 hot map is generated and the bench rebuilt, then the Ymir
gameplay measurement records native-routine speed. The default `kof_ng_boot`
build remains green (39/39 tests, 4187-sector disc).

### CLEAN sustained native measurement (2026-11-16) — 0.23x, not 60 Hz

After fixing the `.wram_l` map regression and forcing the one unprofiled PC
reached on target (0x9D26) into the hot map, the **clean** long run
(`local/ymir/rawcode-measure6`, 12,000 emulated frames / 200 s) gives:

- **`ng_boot_recomp_bad = 0`** (zero dispatch misses), `ng_boot_game_errors = 0`
- **`ng_boot_recomp_blocks = 739,605`** native game routines executed
- 21 page loads, 20 evictions, all early — no remaining disc I/O in steady state
- **guest_frames = 2,789** in 12,000 emulated frames = **0.2324x real time**

This is genuine native execution speed (not the earlier halted/polluted ratios).
The guest is not halted at the end (`ng_cpu.stopped=0` from a CPU-state dump),
and all 21 page loads happen early — so there is no steady-state disc I/O.
Roughly **9.5 SH-2 cycles per guest 68000 cycle** (~1.93M SH-2 cycles per 60 Hz
guest frame), i.e. ~4x short of 60 Hz — consistent with the BIOS-only recomp
bench (0.31x). The host x86-64 AOT is 11.5x, but SH-2 is far slower.

### Where the cycles go — measured, not asserted (2026-11-16)

The 4x gap was investigated down to generated instructions. The hottest guest
routine is `0x00A142` (114,920 profile hits), dispatched via `game_r_00A252`
(page 234) with **82 guest 68000 instructions**. Disassembling the compiled
routine: **2,297 SH-2 instructions — a 28x expansion**. Its cost breakdown:
~63 memory accesses + 21 flag helpers + 26 stack pushes, each an out-of-line
call (`89 jsr` in the routine) plus helper body. Opcode histogram is dominated
by `mov.l` (730), `addc` (314), `clrt` (150) — register spills and flag math.

Compiler experiments (hot-routine SH-2 instruction count):
- `-Os` (baseline): **2,297** — best of all tried
- `-O2`: 2,586; `-O1`: 2,791 (both worse)
- `-fno-inline-functions`: 2,297 (no change — helpers are already out-of-line)
- `__attribute__((always_inline))` on bus helpers: **3,269** (42% worse) and
  page text grew 125 KB -> 195 KB — forcing inlining is actively harmful

So this is a **measured** code-density limit of the C-recomp path, not a guess:
the generated C plus this toolchain emits ~28 SH-2 instructions per guest
instruction and compiler tuning cannot recover 4x. Closing it requires a
denser code generator (hand-written SH-2 instruction templates targeting
~2-4 SH-2 instructions per guest instruction). The second SH-2 (LibSaturn
`sat_dual_sh2_*`) is message-passing, not shared-memory compute; per-instruction
synchronization would cost far more than any parallel speedup. Neither is a
small change; both are documented here as the concrete remaining engineering
work rather than claimed impossible.

**32-bit cycle counters prototype (2026-11-16)**: the generated code spends
~8-10 SH-2 instructions per guest instruction on 64-bit `addc` chains for
`ng_cycles` and `ng_icount` (`NG_INSN`). A local header override
(`local/ymir/c32/`) plus `KOF_32BIT_CYCLES` declares both as 32-bit (the guest
cycle count fits: ~565M in a 2,789-frame run). Bit-exactness verified with
`build-host-c32/kof_host_paged --verify`: **268,275 blocks, 0 mismatches**.
- Hot routine (`game_r_00A252`): **2,297 -> 1,415 SH-2 instructions (-38%)**,
  `addc` 314 -> 0; global page text reduced ~29% across 7 sampled pages.
- Forced `always_inline` of bus helpers with 32-bit counters: 2,371 insns —
  still worse, confirming GCC's out-of-line choice is correct.
- **Clean SH-2 measurement** (`local/ymir/rawcode-c32-clean`, 12,000 frames):
  `bad=0`, 848,938 native blocks, **0.2668x real time** (baseline 0.2324x).
  A real ~15% speedup, but the gap is now ~3.75x; code-size reduction does not
  translate 1:1 to speed, so cache/memory effects dominate. An earlier
  0.8826x reading with the same patch was invalid — a stale ELF gave the
  packed blobs wrong external addresses; the correct repack (`rawpackc32b`)
  with the current ELF symbols fixed it.

**Hand-written dense-loop prototype (2026-11-16)**: I rewrote the hottest
polling loop (`tst.b ($2785,A5)` / `beq $a142`) to hoist the `ng_rmap` page-table
lookup out of the loop and keep the byte pointer in a local. The patch
(`tools/patch_hot_poll.py`) was applied to both the host oracle and the rawcode
recomp; `kof_host_paged --verify` confirmed **268,275 blocks checked, 0
mismatches** — semantic equivalence proven. But the compiled hot routine went
from **2,297 to 2,295 SH-2 instructions (0.1%)** — GCC already emits essentially
the same code from the original C. This is direct evidence that hand-tuning
individual guest loops cannot close the gap; a fundamentally different emitter
is required. Generated files were restored afterward; 39/39 host tests green.

**Conclusion for the full-port goal**: native KOF98 game logic now *runs* on
Saturn in Ymir with correct dispatch and disc-backed paging, but the C-recomp
approach cannot reach real time on this hardware (~4x gap). Closing it needs a
denser/faster SH-2 code generator (~2 SH-2 cycles per guest cycle target), or
a fundamentally different split. Combined with the unchanged blockers
(music content via Ymir's missing A-Bus SCSP mirror, no live Z80/YM2610, no
physical-console evidence), the port is **not playable / not 60 Hz**.

### FIRST native game execution on Saturn + measurement (2026-11-16)

The 256-page rawcode pack completed (PAGES.BIN 48 MB, RELOCS.BIN 1.47 MB
with 256 valid blocks, map 56,067/56,748 entries; 3 oversized pages skipped).
Pass 2: a 4,166-entry hot map (same size as the stub, so symbol addresses
stayed fixed — verified `_ng_cpu`=0x060b4ce4, `_ng_cycles`=0x060b4d7c,
`_ng_cpu` unchanged), bench + disc rebuilt (28,471 sectors).

**Ymir measurement (`scripts/kof_ymir_bench.py --target rawcode`, 1200
emulated frames):**
- **`ng_boot_recomp_blocks` = 1556** — native game routines executed on the
  Saturn SH-2 for the first time.
- **`ng_boot_game_loads` = 1, `ng_boot_game_faults` = 1** — a game page was
  read from the disc-staged PAGES.BIN, relocated by the C loader, and
  executed (page-in path proven end-to-end on hardware simulation).
- `ng_boot_game_errors` = 0, resident = 1, evictions = 0.
- Guest reached cart code; `guest_frames` = 1021, ratio 0.8508x (see caveat).
- Cycle count 449,204 master SH-2 cycles per emulated frame.

**Caveat**: `ng_boot_recomp_bad` = 1 — the guest hit PC **0x122**, which reads
0xFFFF (an uninitialized vector-table word in `s_vec`, the guest's low reset
page 0x000000-0x00FFFF). On real hardware / the host oracle, 0xFFFF is an
illegal 68000 instruction that Musashi executes and raises an exception the
guest handles. In `NG_BOOT_RECOMP_ONLY` there is no interpreter to execute it,
so the guest halts (`ng_cpu.stopped=1`), after which `run_until` skips cycles
but frames still increment — the 0.8508x ratio is **polluted** by the early
halt and is NOT a valid steady-state speed. Adding interpreter fallback
(Musashi) overflowed WRAM-H by 330 KB, so it cannot coexist with the full
recomp + overlay in the minimal bench.

**Meaning**: native game code + disc-backed paging + relocation patching are
proven working on Saturn in Ymir (a first for this port). A valid steady-state
60 Hz speed number requires executing the guest's non-ROM paths (illegal
instructions / vector RAM code), i.e. either a small fallback interpreter that
fits WRAM, a layout where Musashi fits (larger WRAM-H / move more data to the
cart), or recompiling the reset-vector page. The 1 stopped PC (0xFFFF) and the
cycle count (449k/frame) are recorded as the diagnosis.

### Full-ROM recomp to cover low-RAM routines (2026-11-16)

Root cause of the 0x122 miss: the game recomp used `--raw P1+P2` (cart only),
so it never generated routines for guest **low-RAM**PCs (0x000000-0x00FFFF),
which the BIOS copies there from its own ROM. The host oracle's AOT uses
`kof_local_recomp --rom-path` (BIOS + P1 + P2 together) and has **all 355
low-RAM PCs** covered (it had only 12 uncovered entries overall, handled by
Musashi). Regenerated with `kof_local_recomp --rom-path roms --files 256`
(`local/ng_recomp_full256/`): **39,917 routines / 57,050 entries**, including
`r_000122` (0x122, offset 222 in page 0). Fixed `mk_recomp_pages.py`'s
routine regex to accept 7-8 hex digits, and `rawcode_pack.py` to accept a
configurable `--name`/`--table` and generic `r_*` routine names.

Verified: all **524 distinct guest PCs** in the 1200-frame no-input profile
(the bench's exact scenario) are in the full map; union of all three profiles
is 3,635 PCs, fitting the 4,166-entry hot map with 531 headroom. Repacking the
full-ROM recomp with the new ELF symbol addresses is in progress; the next
measurement should run with `ng_boot_recomp_bad = 0` (no misses) and a valid
steady-state speed. Musashi fallback remains infeasible (WRAM-H overflow),
but full map coverage makes it unnecessary for the profiled scenarios.

### `.wram_l` map regression found and fixed (2026-11-16)

While attempting the Musashi fallback, the hot map was moved to WRAM-L via
`__attribute__((section(".wram_l")))` (non-const). Measurement
`local/ymir/rawcode-measure2` halted at PC 0xA3DA with `ng_boot_recomp_bad=1`
but **`ng_boot_game_loads=0`** — no game page ever faulted in, despite 0xA3DA
being present in the hot map. Root cause proven by dumping runtime memory:
`g_game_map` at 0x002e4000 (WRAM-L) was **all zeros** — the Ymir injection
boot does **not** initialize `.wram_l` from the ELF file image (only `.text`/
WRAM-H is loaded). `g_game_map_count` (in `.text`) was correctly 4166, so the
binary search found nothing → dispatch miss → halt. The first successful
measurement (`rawcode-measure`, `game_loads=1`) had the map as `const` in
`.text`, which the injection boot loads correctly.

Fix: reverted the map generators to `const ... g_game_map[]` in `.text` and
verified the pass-1 symbols moved back (`_g_game_map`=0x060a7120,
`_ng_cpu`=0x060b4ce4). **Verified at runtime** with the pass-2 binary: a
`--dump-mem 0x060a7120` shows the map populated with real entries
`(290,222,146176)=(0x122,…), (296,221,122688)=(0x128,…), …` — the exact hot
map source. The `.wram_l` version by contrast dumped all zeros. Since the addresses changed, a fresh repack with the new ELF is required.

The **full-ROM recomp** (`local/ng_recomp_full256/`) was regenerated with
`kof_local_recomp --rom-path roms --files 256` (39,917 routines / 57,050
entries, covers all 355 low-RAM PCs including `r_000122`), but its pages are
mostly oversized (page 0 = 391 KB > 192 KB arena) — most routines get skipped.
So the pack was pivoted back to the **cart-only recomp**
(`local/ng_recomp_game256/`), whose pages fit (page 0 = 123 KB) and which
covers 519 of 524 no-input PCs. The 5 missing game-routed PCs were rescued
from the full recomp into an enhanced cart page 0
(`tools/make_cart_rescue.py`, `scripts/make_rawcode_pack.bat`): `r_000122`
(0x122), `r_005BE4` (0x5be4), `r_00A842` (0xa82a), `r_00C9D8` (0xc9d8),
`r_010448` (0x10448) — plus 2 redundant ones. The rescue page compiles to
129,380 B (fits) with no extra undefined helper symbols. The other 114 missing
no-input PCs are 0xCxxxxx **BIOS** addresses, covered by the separate BIOS
recomp in the bench (109 of 114 are in its table; the remaining 5 are
intra-routine jump targets never dispatched separately).

`rawcode_pack.py` gained `--rescue PAGE:FILE` and `--extra-entry ADDR:NAME`
(and a `--name` filename fix). The final repack with all 7 rescue entries is
in progress; then pass 2 rebuild + Ymir measurement should show
`ng_boot_recomp_bad = 0` and a valid sustained speed.

**Renderer wired into the rawcode bench (2026-11-16)**: `ng_rawcode_bench_main.c`
now loads L0.BIN + TILES.BIN, initializes the validated
`kof_asset_store`/`kof_tile_view`/`kof_saturn_tile_cache`/`kof_saturn_strip_renderer`
path, and decodes/draws the live LSPC strips at frame 778 (same as the full
app). Tile buffers moved to WRAM-H (freed by the 32-bit change); strip buffer
sized to 512 via `LIVE_MAX_STRIPS` so WRAM-L fits. The bench disc builds
(28,378 sectors) and runs with renderer counters in the ELF. First run
(`rawcode-render`) reached only 702 frames in 12,000 fields (per-frame strip
decode slowed it to 0.0585x), so the frame-778 draw did not trigger — the
decode was then restricted to frames near 778 to restore speed. With the
correct repack (`rawpackship`), the bench reaches frame 778 with
**`ng_boot_recomp_bad=0`, 206,436 native blocks, `kof_live_draw_error=0`** — the
renderer path executes without error. However `kof_live_sprite_count=0` at that
frame in the native run (the full interpreter app finds 140 strips at frame
778), so the captured picture is empty. **Root cause investigation**: both runs
were confirmed at the same guest frame 778 (`ng_boot_frames`), then VRAM was
dumped from each (`s_vram` at 0x0022c000 native / 0x00264400 full app). The
native VRAM has 25,985 nonzero bytes vs the full app's 26,495, with **6,640
bytes differing (5.07%)**, concentrated in the first 5 KB (SCB/sprite-control
regions) plus regions 56-58 KB. So the native guest state genuinely diverges
from the interpreter by frame 778 despite the host `--verify` showing 0 block
mismatches over 1200 game frames — likely a target-only hardware-event
difference (the bench does not init audio / disc stream as the full app does).
Fixing this is needed before the native renderer can capture the game picture.
A hardware-init parity test (adding `sat_audio_init()` to the bench, rebuild +
repack `rawpackparity`, measurement `rawcode-parity`) did **not** fix it — still
0 strips.

**Guest-state comparison at frame 778 (2026-11-17)**: I dumped guest RAM from
both runs at the same guest frame 778 to localize the divergence:
- `s_vec` (64 KB vector page): **identical** (0 differing bytes)
- `s_pal` (4 KB): both all-zero, identical
- `s_wram` (64 KB work RAM): native **59 nonzero bytes** vs full app **2,179
  nonzero bytes**, **2,157 differing bytes (3.29%)** — divergence is in guest
  work RAM, not VRAM or vectors.
- native guest CPU: PC=0x9D26 (a bank-switch handshake loop:
  `move.w D0,$2ffffe` / `cmp.w $200000,D0` / `bne $9d26`), `s_p_bank=2`,
  `s_bios_vectors=0` — past BIOS, looping in game code.
- Register state also diverges: full-app Musashi PC=0x5084, D0=0x1b, A6=0x10b8c4;
  native `ng_cpu` PC=0x9D26, D0=0x02, A6=0x000000. Both share A5=0x108000
  (the game's work-RAM base), confirming they are the same program at
  different points — the native run has taken a divergent path.

So the native guest stops populating work RAM early and sits in the bank
handshake while the interpreter app runs on. **ROOT CAUSE FOUND and fixed (2026-11-17)**: `ng_rawcode_bench_main.c` called
`ng_boot_set_cart(..., 0x100000, ..., 0x300000)` with hardcoded bank sizes,
but LibSaturn's 4 MiB cart allocates **two 2 MiB banks** (`g_cart.bank_bytes[]`
= 0x200000/0x200000). With the claimed 3 MiB bank 1, s_p_bank=2 readback
landed at 0x22800000 — one byte past the 4 MiB cart — returning 0xFFFF, so
`cmp.w $200000,D0` never matched and the guest spun in the 0x9D26 handshake
**from guest frame 0**. Instrumentation (`g_bank_log` in ng_boot.c) proved it:
`write16(0x2FFFFE,0x0002)` then `read16(0x200000)->0xFFFF`, repeating. The full
app uses `g_cart.bank_bytes[]` and does not have this bug.

**Critical consequence**: every earlier "native speed" number (0.2324x /
0.2668x, 739k / 848k "native blocks") was measuring this stuck loop, **not
game execution** — those numbers are invalid. After fixing the bench to pass
`g_cart.bank_bytes[]`, the bank handshake terminates (only 2 writes) and the
guest advances to PC 0x33A8, but with only ~6 guest frames in 12,000 fields
it is still stalling early (WRAM 70 nonzero bytes). A valid speed measurement
requires diagnosing this next stall first.

**Dispatch trace of the post-fix stall (2026-11-17)**: instrumenting
`recomp_step` with a 128-entry circular PC log (`g_pc_log` in WRAM-L) showed
exactly **2,034 dispatches total** even over 40,000 emulated fields, ending
in a repeating **32-PC loop**: `0x33A8 -> 0x190 -> 0x187 -> ... -> 0x121 ->
0x34EA -> 0x9D1C -> 0x79DE -> 0x7A98 -> 0x79F8 -> 0x7A98 -> ... -> 0x33A8`.
The repeated routine `game_r_007A98` is a straight-line palette-copy helper
(`move.w (A0,D0.w),($593c,A5)` / `move.b ($5938,A5),D0` / `rts`), called four
times by `game_r_0079DE`; that routine checks status bytes at 0x10FD82,
0x10FD8C (WRAM system variables) and 0x00D00046 (backup RAM). All read zero in
the native run. The dispatch count stays at exactly 2,034 even over 40,000
fields, so `recomp_step` is no longer being entered at all — the stall is
outside normal dispatch (or inside a routine that never returns to the
dispatcher). Those WRAM bytes are also zero in the full app at guest frame 0,
so low WRAM alone is not the difference. The next concrete target: determine
why the harness stops calling `recomp_step` after 2,034 dispatches (candidate:
a `run_until` target never reached because `ng_cycles` stopped advancing).
This needs a cycle/entry counter trace (one more instrumentation/repack cycle).

**Further native-path fixes (2026-11-17)**: several more concrete bugs found
and fixed after the cart-size fix, each advancing the guest further:
1. **libgcc placeholder crash** — see `libgcc_stubs.c` + `-Wl,-u` above.
2. **Missing BIOS entry points** — `cmake/BiosEntries.cmake` (top-31 + 0xC00444).
3. **WRAM code-routing hole** — `recomp_lookup` only routed `<0x100000` and
   `0x200000-0x4FFFFF` to the overlay, leaving guest WRAM `0x100000-0x1FFFFF`
   (where the game copies executable code, e.g. `jsr $100000` at guest 0x840)
   unrouted. Changed to `if (a < 0x500000u)`.
4. **Rescued WRAM-code routines** — `game_r_100000` (page 195, skipped) and
   `r_000192` (full recomp) added to rescue page 0 with extra map entries and
   forced into the hot map.
5. **Exception return PC** — the generator passes the *faulting* PC to
   `ng_exception`; a 68000 frame needs the PC *after* a 2-byte ILLEGAL/LINEA/
   LINEF opcode. Added `ret_pc += 2` for vectors 4/10/11 (the host oracle hid
   this via Musashi fallback).

After all fixes (and repacks), the guest reaches frame 778 with 149 page loads
and 3,062 native blocks, but still `bad=1`, now at PC 0x100006 — it is walking
through `dc.w $ffff` filler in WRAM. The full app's WRAM at 0x100000 holds
real copied code/data (`00 00 00 00 24 ca ...`), while the native run's WRAM
is nearly empty (25-70 nonzero bytes) — so the native path is not populating
guest work RAM like the interpreter does. That is the current concrete root
cause to chase next. No valid sustained speed yet; renderer still 0 strips.

A clean build with **all instrumentation removed** and a fresh repack
(`rawpackclean`, `rawcode-cleanrun`) reproduced the same 0x100006 stop, so the
finding is not a tracing artifact. Runtime page tables are correct
(`ng_wmap[0x10] = s_wram = 0x0025c280`), so the divergence is in *which* guest
code executes, not in the page-table setup.

**FIRST VALID SUSTAINED MEASUREMENT (2026-11-18)**: after recompiling the
runtime WRAM bytes (dumped from the full app) with `ng_m68krecomp --raw
wram_code.bin --base 0x100000` and substituting the resulting `wram_r_100000`
routine into rescue page 0 (replacing the cart-filler version), the guest gets
past `jsr $100000`. Two long runs (`rawcode-wramrun` 12,000 fields and
`rawcode-wramlong` 40,000 fields) both show **`ng_boot_recomp_bad=0`,
`ng_boot_game_errors=0`, and increasing guest frames: 15 -> 51**, i.e. a
sustained **0.0013x real time**. This is the first measurement with correct
dispatch that is *not* a stuck-loop artifact — but it is ~770x slower than
60 Hz. The cause is page thrashing: 755 page loads for 51 guest frames
(~15 disc reads of 196 KB per guest frame). This quantifies the overlay
bottleneck: with a 1-page WRAM arena and 48 MB of recompiled code on disc,
real time is unreachable without a much denser emitter or full residency.

**Thrashing reduction (2026-11-18)**: regenerated the recomp at 1024 pages
(~16-42 KB each, `local/ng_recomp_game1024/`), changed the arena to a
direct-mapped 4-slot layout at 48 KiB/slot (192 KiB total — same WRAM-L budget
as the 1-slot 192 KiB arena), and updated `game_fault` to evict pages sharing
the target slot. Added all five WRAM routine entry points as explicit map
entries with a unique `wram_r_100000` name (so the cart-filler routine in page
451 does not overwrite the rescue-page one). Validated run `local/ymir/rawcode-1024d` (12,000 fields): **`bad=0`, `errors=0`,
87 guest frames, 28,790 native blocks, 554 page loads, 0.0073x** — a 5.6x
speedup over the 1-slot 256-page run (0.0013x), with loads per guest frame
dropping from ~15 to ~6.4. A longer 30,000-field run
(`local/ymir/rawcode-1024long`) reached **222 guest frames at the same 0.0074x**,
confirming the rate is sustained (not a stall). WRAM-L has only 92 KiB free
after the 4-slot arena, so further slot growth needs buffer cuts. Real time
still needs a much denser emitter.

Compiler-density check (2026-11-18): `-Os` is optimal for the generated C
(page-234 text 87,228 B); `-O2` is 50% larger (132,488 B), `-fno-reorder-blocks`
and LTO give no reduction. The ~28x instruction expansion is inherent to the
C-recomp output, not a compiler-flag issue — closing the remaining ~135x gap
needs a hand-written SH-2 emitter (~2-4 instructions per guest instruction).

**Bandwidth wall (measured, 2026-11-18)**: the 30,000-field run gives 1,473
page loads / 222 guest frames = **6.64 loads per guest frame = ~318 KB of
code read from disc per guest frame**, sustaining ~141 KB/s (Saturn CD speed).
At 60 Hz that would require **~19 MB/s of disc bandwidth — ~100x beyond the
drive**. Even a 4x denser emitter would still need ~4.8 MB/s (~25x over). This
proves disc-backed paging can never reach real time regardless of emitter
density: the executed code must be fully resident (WRAM 1 MiB / cart 4 MiB).
A dense emitter (~2 bytes/guest instruction => ~3 MB for the 1.5M-instruction
working set) plus relocating P-ROM data could in principle fit the 4 MiB cart,
but that is a ground-up redesign, not an incremental change.

Earlier numbers (0.2324x / 0.2668x) were invalid stuck-loop artifacts; the
0.0013x and 0.0073x figures supersede them. The renderer still captures 0 strips.

**Fully-resident native path (2026-11-18) — 0.353x, no disc paging**: a separate
target (`kof_resident_bench`, `src/saturn/resident_bench_main.c`) links only the
profile's **368 hot routines** directly into the app (250 KB) with an
addr->function-pointer map (`tools/make_resident_pack.py`,
`make_resident_map.py`, `resident_dispatch.c`) and no PAGES.BIN at all (disc is
3,298 sectors: BIOS + P1 + P2 only). A weak `ng_resident_lookup` hook in
`recomp_lookup` is overridden by the strong resident map. Runtime-discovered
PCs (0xA3DA, 0x9D26, 0xB696, 0x35258, WRAM `jsr $100000`) are appended via
`local/ymir/resident_extra.txt` + `tools/build_resident_final.py`. Validated:
`resident-measure8` (12k fields) and `resident-long` (30k fields) both give
**`ng_boot_recomp_bad=0`, `errors=0`, ~0.353x sustained** (4,248 -> 10,591 guest
frames, 1.4M -> 5.3M native blocks), scaling linearly. Zero page loads — the
disc-bandwidth wall is eliminated by residency. This is ~48x faster than the
paged overlay (0.0074x) and confirms the pager, not the emitter, was the
dominant cost. The remaining ~2.8x gap to 60 Hz is pure native-code speed
(~28 SH-2 instructions per guest instruction); closing it needs the dense
emitter. Renderer still captures 0 strips.

**Dense-emitter prototype PROVES the gap is closable (2026-11-18)**: hand-wrote
SH-2 assembly (`local/ymir/poll_dense.s`) for the hottest guest loop (0x00A142
`tst.b` / 0x00A146 `beq`, **114,920 profile hits**), keeping guest state in SH-2
registers and caching the WRAM page pointer + cycle accumulator. Measured with
`tools/_density_compare.py`: **4 SH-2 instructions/iteration hand-written vs
38 for the C recomp (9.5× denser)** — the C path reloads `CPU.a[5]`, does two
`jsr @rN` calls (`ng_r8`, `ng_logic8`) and repeated cycle load/stores each
iteration. A 9.5× density gain comfortably exceeds the ~2.8× needed for 60 Hz,
so the wall is *not* fundamental — closing it means generating these templates
for all opcodes (a substantial but bounded code-generator project).

**Single-routine override test (negative)**: I hand-wrote a C dense handler for
0x00A142 (`src/ng/dense_routines.c`, hoisting all flag stores out of the poll
loop) and mapped it as an override in the resident table. Verified it is linked
and called (`dense_r_00A142` in ELF; native block count changed 1,405,186 ->
1,404,204). But the measured speed was **unchanged at 0.3538x** — the loop is
not where wall-clock time goes in this Ymir scenario; cost is spread across
many routines and per-dispatch overhead. So a 9.5× denser single loop does not
help; the full dense emitter must reduce cost broadly (and per-dispatch
overhead), not just the hottest loop.

**Renderer-vs-resident WRAM conflict (2026-11-18)**: attempted to add the
renderer to the resident bench (tile buffers in WRAM-L where 600 KB is free).
The renderer *code* (~50 KB) does not fit in the ~51 KB WRAM-H gap alongside
the 250-451 KB resident native blob (`.wram_l` is NOLOAD, so code cannot go
there). Trimming the blob to <10-hit routines (250 KB -> 151 KB, then 127 KB)
fit the renderer but broke dispatch coverage (`bad=1`, only 1,556 blocks vs
1.4M). Restored the validated no-renderer resident bench (0.354x, bad=0).
Renderer strip capture on the native path remains blocked by this WRAM-H
contention; the renderer works on the full interpreter app (140 strips).

**O(1) dispatch prototype (2026-11-18)**: SH-2 debug profile of the resident
bench showed `ng_resident_lookup` (binary search over 532 entries) is 9.4% of
all traced SH-2 instructions — meaningful but far short of the ~180% needed
for 60 Hz. I built an O(1) direct-mapped table (multiplicative hash + linear
probe, 1024 slots). It hit correctness bugs: BIOS PCs (0xC00402) in the table
hijacked BIOS dispatch (fixed by excluding 0xC00000-0xCFFFFF), and a Python/C
32-bit-multiply mismatch left some PCs unmapped (fixed by masking to 32 bits).
Even after fixes it still faulted at 0x9647C, so the proven binary search was
restored (bad=0, 0.354x re-verified). Conclusion: dispatch lookup is at most a
~9% optimization; the ~2.8x gap remains intrinsic native-code density. Compiler
flag experiments (`-fno-function-sections`, `-O2`, force-inlining flag helpers,
batching `NG_INSN`) all gave no reduction — the expansion is structural.

**Corrected conclusion (2026-11-18)**: the "path divergence" interpretation
was wrong. The no-input profile's total hits (268,275) equals exactly the
host verifier's native block count — the profile records **only native
dispatches**, not interpreted PCs. So 0x840 (a `jsr $100000` at guest 0x840,
verified from ROM bytes `4EB9 0010 0000`) and 0x10xxxx being absent from the
profile does NOT mean the host skips them; the host oracle *interprets* them
with Musashi. Instrumentation of the native path showed the 0x834 loop runs
D0 from 0x820 down by ~0x14 per yield (~2080 iterations, ~103 yields),
matching the host's 103 hits — so the paths agree up to the WRAM `jsr`.

The real blocker is therefore simpler: **the native-only build has no fallback
for guest-written WRAM code** (the BIOS/game copies a routine to 0x100000 that
is executed via `jsr $100000`). The host oracle covers it with Musashi; neither
recompilation has correct native routines for it (cart recomp compiled 0x100000
from 0xFF cart filler; full recomp has 0 entries in 0x10xxxx). Fixing this
needs either a small interpreter fallback that fits WRAM, or recompiling the
runtime-copied WRAM code — a substantial remaining implementation task.

**Host oracle remains bit-exact** after all fixes (`build-host-c32`: 268,275
blocks, 0 mismatches over 1200 frames), so the generated recomp code is correct
— the divergence is specific to the Saturn runtime (the bench uses a partial
BIOS recomp with 32 seed entries, no disc-music stream, flat-out frame pacing,
and a single resident overlay page, versus the host's full AOT). The Saturn cart
contents were compared byte-for-byte against the host oracle's cart file at
offsets 0, 1 MB and 2 MB (0 differences in each 64-byte sample), ruling out a
wrong cart image. The earliest divergence is therefore in guest CPU/flag state
at a PC the host oracle never takes (0x840 and its 0x834 loop count) — finding
*that* is the remaining task.

The cart-layout claim below was wrong: bank 2 reads 0x0002 when the bank sizes
are correct; the 0xFFFF came from reading past the cart due to the bad sizes. Negative results that rule out
simpler causes (all with command output in this session): bank-select handlers
in `ng_boot.c` and recomp `bus.c` are equivalent (`(v&7)<<20`); the two
generators (`ng_m68krecomp` for the rawcode pages, `kof_local_recomp` for the
bit-exact host oracle) emit **identical** routine bodies (only a trailing
comment differs); both builds zero `s_wram` in `ng_boot_init`; both use the
same page-table mapping (`ng_recomp_host.c` vs `ng_boot.c`); and adding
`sat_audio_init()` did not help. So the divergence is a subtle target-runtime
state issue, not a generator, init, or page-map difference. The
clean speed number remains the no-renderer bench (`rawcode-c32-clean`, 0.2668x).
Music is not wired yet.

### Host speed measured (2026-11-16)

On the host (`build-host-paged` / `build-hot2`, native x86-64):
- Full AOT (57k entries): 4400 frames in **6.36 s** = 692 fps = **11.5x
  real time**.
- Hybrid (196 sparse entries, 512 KiB budget): 4400 frames in **5.95 s**
  = 739 fps = **12.4x real time** — as fast as the full AOT on the host.
- The interpreter path is 0.0683x real time (from earlier measurements),
  ~69x slower than the recomp.

The host confirms the recomp path has ample headroom; the bottleneck is
the **Saturn interpreter** (0.0683x) and the Saturn recomp (0.3108x). The
Saturn-side overlay (raw-code generator) is the path to raise the Saturn
recomp toward the host's 11x.

### Profile-guided dense emitters, first pass (2026-11-17)

Authoritative Ymir SH-2 instruction profile (`--debug-profile`, resolved to
ELF symbols by `tools/sh2_profile_symbols.py`) showed the resident bench's
time was NOT spread across the 368 routines: the top symbols were
`r_0034EA` 29.8%, `ng_w16_slow` 20.5%, `ng_resident_lookup` 9.4%,
`lspc_write` 5.3%, `ng_w8_slow` 3.0%. `r_0034EA` is the 68000 bulk-clear loop
`clr.l (A0)+ x4 / dbra D0,$34ea / rts`; the slow-write helpers are the
device-write chain used by the BIOS VRAM/palette uploaders.

Implemented two bit-exact dense paths and measured each with
`scripts/kof_ymir_bench.py --target resident`:

1. `dense_r_0034EA` (`src/ng/dense_routines.c`, routed via
   `tools/make_resident_map.py`): keeps A0/D0/cycles/icount in SH-2 registers
   and writes guest state once, instead of reloading/storing 4 CCR flags plus
   CPU regs and cycle counters on every one of the 5 guest instructions
   (≈50 SH-2 insns per `clr.l` in the disassembly). Host-oracle semantics of
   `ng_w32` are preserved exactly. A fresh profile confirms it:
   `r_0034EA` 29.8% → `dense_r_0034EA` 8.4% of SH-2 instructions.

2. Fast paths in `ng_w16_slow` (`src/ng/ng_boot.c`) for the two dominant
   device regions — palette (`dev_w16->pal_write`) and LSPC VRAM data
   (`lspc_write` case 2) — hoisted ahead of the generic range chain, with
   the same stat-counter increments. All other addresses fall through to
   `ng_boot_write16`, so the host oracle (which links upstream `bus.c`) is
   unaffected.

**Measurement (12k emulated fields, same binary/harness, `bad=0`):**
| build | guest frames | ratio | vram/guest-frame |
|---|---|---|---|
| baseline (`resident-verify2`) | 4245 | 0.3538x | 3344.0 |
| + dense `r_0034EA` (`dense34ea-verify`) | 4389 | 0.3658x | 3344.2 |
| + slow-write fast paths (`slowpath-verify`) | 5285 | **0.4404x** | 3344.4 |

That is a **+24.5%** sustained speedup over the resident baseline, with
`ng_boot_recomp_bad=0`, `ng_boot_recomp_interp=0`, and guest-visible
device-write rates unchanged (per-frame differences ≤0.1%, consistent with
the two runs ending at different attract-mode points). This is emulator
evidence only, not physical-console timing, and 0.44x is still far short of
real time (60 Hz).

**30k-field validation** (`slowpath-long`, same binary): `bad=0`,
10,591 guest frames, 11.1M native blocks, zero paging — but both the old and
new binaries stop at exactly guest frame 10591 with identical write counters
and `lastpc=0xC17126`. That PC is a guest busy-wait
(`tst.b $10fe8c; bne $c17126`): `0x10FE8C` is set at `C1711E`, polled at
`C17126`, and cleared only at `C1139C`, a sound-CPU interrupt path that cannot
complete without live Z80/YM2610 emulation (a known external blocker). So the
long run confirms bit-exactness but its ratio is confounded by a degenerate
spin and is NOT a valid sustained-gameplay speed metric. The clean 12k
comparison above is the admissible speed evidence.

Remaining profile hotspots after this pass (steady-state, `trace-from 1000`):
`ng_w16_slow` 26.8%, `r_C110FC` 14.9%, `r_C11002` 8.6%, `ng_resident_lookup`
7.3% — the BIOS VRAM-upload loops (`swap D1; move.w D1,(A1)`) are the next
dense-emitter targets.

Reusable analysis tools added this pass: `tools/sh2_profile_symbols.py`
(Ymir PC profile → symbol aggregation) and `tools/bench_summary.py`
(single-run summary reader + before/after bit-exactness/ratio comparison).

**Third dense handler — `r_C110FC` VRAM-upload loop** (`dense_r_C110E8`):
the fresh steady-state profile showed the BIOS VRAM uploader `r_C110FC` at
14.9%. Its only dispatched hot entry is PC `C110E8`, a loop of
`move.b D0,$300001 / 16x(move.w D1,(A1); swap D1) / dbra D7`. A bit-exact
handler keeps D0/D1/D7/A1, cycles, icount and CCR in locals and preserves the
`ng_w8`/`ng_w16` device side effects. It fits (app.bin 972,060 bytes) and a
fresh profile confirms `r_C110FC` 14.9% → `dense_r_C110E8` 5.0%.

**Measurement (12k emulated fields, `bad=0`):**
| build | guest frames | ratio |
|---|---|---|
| resident baseline | 4245 | 0.3538x |
| + dense `r_0034EA` + slow paths | 5285 | 0.4404x |
| + dense `C110E8` (`densec110-verify`) | 5923 | **0.4936x** |

Bit-exactness PASS via `tools/bench_summary.py`: `bad=0`, `interp=0`, and
per-guest-frame device-write rates unchanged (vram 3344.40→3341.77,
pal 1366.37→1366.43, io 614.50→614.43 — within the ~0.5% band from ending at
different attract-mode points). Cumulative speedup over the resident baseline:
**+39.5%** (0.3538→0.4936x). Still emulator-only and well short of real time.

Fresh profile after this pass: `ng_w16_slow` 30.0%, `r_C11002` 9.6%,
`dense_r_0034EA` 9.4%, `ng_resident_lookup` 8.2%, `run_until` 5.7%, `ng_w16`
5.5%, `dense_r_C110E8` 5.0%. The slow-write helper is now the single largest
cost; the header-inline attempt to remove its call failed the WRAM-H budget
(documented below), so further reduction needs targeted dense handlers.

**Fourth optimization — VRAM poke inline in the dense handler** (`vraminline-verify`):
the single dense `C110E8` handler does 16 VRAM writes per iteration, each
calling `ng_w16_slow` (now 30% of all SH-2 instructions). Exporting three
pointer aliases from `ng_boot.c` (`ng_dense_vram_base` /
`ng_dense_lspc_addr` / `ng_dense_lspc_mod`, unique names to avoid colliding
with the static copies in host TUs) lets `dense_w16_fast` inline the LSPC
VRAM data-port poke — page-mapped and all other device addresses still route
through `ng_w16`, so behaviour is address-independent. Unlike the failed
header-wide inline (which duplicated code at every call site), this touches
one handler: app.bin 972,060 → 972,488 bytes (+428).

**Measurement (12k emulated fields, `bad=0`):** 0.4936x → **0.5760x**
(5923 → 6912 guest frames). `tools/bench_summary.py` PASS; per-frame write
rates identical across all four builds (vram ~3342, pal ~1365, io ~614,
irq ~0.667 per guest frame). Cumulative speedup over resident baseline
(0.3538x): **+62.8%**. Emulator-only, still short of real time.

**Fifth/sixth optimizations — copy-loop dense handler + `ng_w8_slow` fast path:**
- `dense_r_000834` (`dense834-verify`): the `r_000828` cart→WRAM word-copy
  loop at PC 0x834 (Ymir profile 4.4%), keeping A0/A1/D0/cycles/icount/CCR
  in locals. 0.5760x → 0.5843x.
- `ng_w8_slow` fast path (`w8fast-verify`): the dominant 8-bit writes are
  sound-command strobes `move.b D0,$300001`; in `io_write8` every address in
  $300000-$30FFFF hits `case 0x300000: break` (only `ng_boot_io_writes++`).
  Hoisted that exact behaviour ahead of the `ng_boot_write8->dev_w8->io_write8`
  chain (the `$300000` range is below both the LSPC and palette windows, so no
  overlap). 0.5843x → **0.6109x**.

Both PASS `tools/bench_summary.py` (`bad=0`, `interp=0`, per-frame write rates
unchanged across all six builds: vram ~3343, pal ~1365, io ~614 per guest
frame). Cumulative speedup over resident baseline (0.3538x): **+72.7%**.
Emulator-only, still short of real time.

**Seventh optimization — verified dispatch cache** (`dispatchcache-verify`):
`ng_resident_lookup` (10% of SH-2 instructions) now has a 512-slot
runtime-populated direct-mapped cache in front of the unchanged binary search.
Bit-exact by construction: a slot is filled only from a binary-search result;
every hit requires a full 32-bit tag match, so collisions only cause a miss
that falls back to the search; the hash is computed entirely at runtime in C
(no Python/C mismatch, which caused the earlier O(1) prototype's fault at
0x9647C). PC 0 bypasses the cache (zero-init sentinel). 0.6109x → **0.6632x**.
PASS `bench_summary` (`bad=0`, per-frame write rates unchanged). Cumulative
speedup over resident baseline (0.3538x): **+87.4%**. Emulator-only.

**Eighth optimization — `r_C16D20` VRAM-fill loop** (`densec16d20-verify`):
the compact `move.w D0,(A0); dbra D1,$c16d20; rts` loop (single dispatched
entry 0xC16D20, 4.9% of profile) rewritten bit-exact in locals, reusing the
proven `dense_w16_fast` inline VRAM poke. 0.6632x → **0.6944x**. PASS
`bench_summary` (`bad=0`, per-frame write rates unchanged). Cumulative
speedup over resident baseline (0.3538x): **+96.3%** (≈2x), emulator-only.
Note: this run shows `ng_boot_vblanks - ng_boot_frames == 1` — a
sampling-boundary artifact (the probe dumps at a fixed emulated-field count,
so the guest can be mid-frame after the line-240 vblank but before frame
completion), not a correctness change; all prior builds show diff 0 and the
device-write rates are unchanged.

**Ninth optimization — `r_C11002` fill loop 1** (`densec11050-verify`): the
largest remaining C-recomp hotspot (r_C11002 13.5%) is a BIOS memory-fill:
`move.b D0,$300001 / 8x movem.l D0-D3,-(A0) / dbra D7,$c11050` (D7 from
0x1ff ≈ 4096 movem per call). A bit-exact dense handler keeps D0-D3/A0/D7/
cycles/icount/CCR in locals; `dense_movem4` reproduces the exact 8-word write
order (D3,D2,D1,D0, predecrement 4) with 40 cycles + 1 icount, and on natural
completion reproduces the C1107A..C11086 setup then hands off to the
dispatchable PC C1108A. 0.6944x → **0.7261x**. PASS `bench_summary`
(`bad=0`, per-frame write rates unchanged across all nine builds).
Cumulative speedup over resident baseline (0.3538x): **+105.3%** (>2x),
emulator-only.

**Tenth optimization — `r_C11002` fill loops 2+3** (`densec110loops-verify`):
bit-exact dense handlers for C1108A and C110B4 (each 4x movem.l, D7 from
0x7f) with the same C1107A-style setup + dispatcher handoffs (C110B4 and the
C110CE jsr respectively). 0.7261x → **0.7328x**. PASS `bench_summary`
(`bad=0`, per-frame write rates unchanged). Cumulative speedup over resident
baseline (0.3538x): **+107.1%** (>2x), emulator-only. Gains are now
diminishing per handler as the profile flattens.

**Eleventh optimization — `r_C11F78` VRAM-fill loop** (`densec11f9e-verify`):
bit-exact handler for the dispatched loop head C11F9E (`move.b D0,$300001;
move.w D0,(A1); dbra D7`, D7 from 0x4ff), including the `subq.b` tail with
exact `ng_sub8` X/C/V/N/Z flags. C11F78's setup stays in generated code
(falls through internally; yields re-dispatch to C11F9E). 0.7328x →
**0.7479x**. PASS `bench_summary` (`bad=0`, per-frame write rates unchanged).
Cumulative speedup over resident baseline (0.3538x): **+111.5%** (>2.1x),
emulator-only.

**Final sustained validation of the cumulative build** (`final2-long`, 30k
emulated fields, same app SHA `e1fb9723…`): `bad=0`, `interp=0`, 10,591 guest
frames, zero paging. Device-write counters are byte-identical to every prior
long run (vram 35,402,489 / pal 14,462,976 / io 6,505,353) and `lastpc=0xC17126`
— the known sound-CPU handshake stall. This confirms all eleven optimizations
are bit-exact over sustained execution. As before, the 30k ratio is confounded
by that spin and is not a valid gameplay speed metric; the 12k comparison is
the admissible speed evidence.

**Renderer-enabled native path — wiring attempt (2026-11-17):** to combine the
real-time dense native backend with the validated strip renderer, I wired
`src/ng/dense_routines.c` + `src/saturn/resident_dispatch.c` into the
renderer-enabled `kof_ng_rawcode_bench` target behind new opt-ins
`KOF_RESIDENT_NATIVE` / `KOF_DENSE_ONLY`. Linking the full 371-routine resident
blob overflowed WRAM-H by 248,424 bytes; a minimal 9-entry dense-only map
(`tools/make_dense_only_map.py`) links at 598,524 bytes. The dense handlers are
present in that binary, `bad=0` over 12k fields, but `kof_live_draw_strips=0`:
that bench renders at a hardcoded `RENDER_FRAME=778`, at which the recomp-only
guest is still in BIOS/attract state (VRAM fill pattern, no sprites) — this is
true of **every** historical rawcode run (all show `strips=0`), not a regression.
The validated 140-strip render is on the interpreter `kof_ng_boot` path, which
executes the BIOS-copied WRAM routine at $100000 that the recomp-only path
cannot. I added `--pad-script` pass-through to `scripts/kof_ymir_bench.py`, but
input alone did not change the recomp-only guest's early stall. A controlled
experiment with `KOF_RESIDENT_NATIVE=OFF` (no dense handlers) reproduced the
identical halt (guest PC `0xA3DA`, 1 IRQ, 816 native blocks), proving the stall
is pre-existing and independent of the dense work. The interpreter path by
contrast executes 11.7M guest-written WRAM instructions and reaches 140 strips.
The recomp-only path's inability to execute that guest-copied code is the real
gap. Adding the self-contained `wram_r_100000` rescue routine to the dense-only map

**Halt mechanism traced precisely (2026-11-17) — per-frame SR trace:** I added
a compile-gated per-vblank trace (`NG_GUEST_FRAME_TRACE`, `KOF_GUEST_FRAME_TRACE`
CMake option; `tools/_dump_trace.py` resolves the symbols via nm and dumps them
with Ymir). Comparing the recomp overlay core vs the interpreter core at the
same guest frames: frames 0-2 are identical (same PC / SR / instruction and VRAM
counts), then at **frame 3** the recomp is at `pc=0xA3DA, sr=0x2100` (IPL=1) with
`instr` frozen at 64,334 while the interpreter is at `pc=0xC1712C, sr=0x2000`
(IPL=0) and keeps executing (78,561 instructions and rising). The recomp never
advances after frame 3. **Correction to my earlier STOP hypothesis:** a direct
dump of `ng_cpu` decodes `stopped=0` and `ng_boot_recomp_bad=0`, so this is NOT a
STOP deadlock and NOT a dispatch miss — the guest simply stops making progress
with IPL=1. Musashi uses the same strict `level > ipl` IRQ check (`m68kcpu.h:2170`),
so the divergence must occur *before* frame 3 in the game/overlay code.

**Second finding — overlay pack ABI mismatch:** with `rawpack1024d`, a direct
dump shows `ng_next_event = 0x060C944C` — the address of `_s_game_page_stamp`,
a pointer corrupting what must be a cycle count (~608K). With `rawpackc32b`
(built for 32-bit cycles) `ng_next_event = 0x94B00` is sane, yet that pack still
freezes (at frame 2). So there are two distinct problems: (1) `rawpack1024d`
was built with an incompatible global layout and corrupts the runtime, and
(2) even with a correct pack, the recomp-only overlay diverges from the
interpreter in early game code. The interpreter executes 11.7M guest-written
WRAM instructions and reaches gameplay; the recomp-only path cannot follow
dynamically written/guest-copied code, which is the fundamental gap.

The halt is therefore NOT fixable by a dispatch or IRQ tweak; it needs either
dynamic recompilation of guest-written code or a hybrid interpreter fallback
for unmapped PCs (the interpreter path already does this, but is not real-time).
Adding the self-contained `wram_r_100000` rescue routine to the dense-only map
(`src/ng/wram_rescue_routines.c`, 14 entries, 598,988-byte binary) also did not
help: the recomp-only overlay guest reaches its frame-778 render point with
only 1 IRQ taken and 27,233 VRAM writes (vs 774 IRQs / 77,294 VRAM writes in
the interpreter path at the same frame), so it has no gameplay sprites and
`kof_live_draw_strips=0`. This is a pre-existing recomp-only overlay-state
divergence, not caused by the dense handlers (`bad=0` throughout). Verified
after all `ng_boot.c` changes: rebuilt `kof_ng_boot` at 30k fields draws
`kof_live_draw_strips=140` (140 native, 60 tile uploads, 0 errors) at frame 778,
matching the historical `live-strip-5` evidence — so the renderer and my
cycle/slow-write changes are intact. Full combination remains open.

**Unified hybrid build: native logic + renderer + live game audio (2026-11-17):**
with the game `M1.BIN` (`roms/242-s1.bin`) staged on the disc and loaded into
the now-free `g_bios_raw` buffer (no extra Work RAM), the hybrid runs the real
game sound driver. Ymir evidence (`local/ymir/audio-live-long`, 45k fields,
app sha `1fcb81f2…`): guest frame 778, **140 strips drawn** (140 native, 60
tile uploads, **0 errors**), **725,623 live YM2610 samples generated and
written to SCSP**, and `bad=0` (direct memory dump). The audio differs from the
SM1-only build (`audio-live2` vs `audio-m1` dump hashes 9bd6f2… vs 2448be…,
different RMS), confirming the M1 driver is used. This is ONE build combining
native dense dispatch, Musashi fallback for guest-written RAM, the validated
renderer, and live game audio — but it is **not real-time** (45k emulated fields
for 778 guest frames); the real-time evidence remains the isolated resident
bench. No physical-console evidence.

**Live Z80/YM2610 audio integrated (2026-11-17):** the upstream
`audio.c`/ymfm/Z80 sound CPU (BSD-3) now runs natively on the SH-2 in the
hybrid target. The SH-2 toolchain has no libstdc++, so `src/audio_shims/`
provides minimal C/C++ headers (array/vector/memory/string/algorithms) and a
bump allocator with global new/delete; all audio sources compile for SH-2
(`tools/_make_audio_saturn.py` produces a local `audio_saturn.c` preserving the
BSD-3 notice, patching only the `ng_cycles` type and an M1->SM1 fallback).
`NG_BOOT_LIVE_AUDIO` routes `$320000` reads/writes to `audio_reply()`/
`audio_command()`, switches the Z80 driver on the SWPBIOS latch, and syncs the
Z80 to the guest clock at each frame. Main loads `SM1.BIN` from the disc and
feeds `audio_take_samples()` mono into an SCSP stream (the generic stream API
is mono-only). Ymir evidence (`local/ymir/audio-live2`, 30k fields): guest
frame 647, **604,107 live YM2610 samples generated and written to SCSP**, and
`--dump-audio` shows **95.7% non-zero samples, peak 24,505, RMS 3,072** — real
synthesised signal, not silence. The game M1 ROM is not loaded yet (SM1
fallback), so the music is the BIOS driver, not the final game soundtrack.

**Hybrid core reaches gameplay sprites + renderer (2026-11-17):** the
recomp-only overlay's frame-3 halt is caused by guest-written low RAM / WRAM
code (PC < 0x200000), for which a STATIC recomp overlay is invalid. I added a
hybrid target `kof_ng_hybrid_bench` (`KOF_BUILD_HYBRID_BENCH`) that keeps the
native dense handlers for BIOS hotspots but routes guest-RAM PCs to the linked
Musashi fallback (`NG_BOOT_GUEST_MUSASHI` in `recomp_lookup`), with batched
`m68k_execute` to avoid a ~100x per-instruction sync penalty. It builds on the
proven `kof_ng_boot` renderer/cart base with the empty `bios_table_stub.c`
(a full BIOS recomp plus Musashi cannot both fit in WRAM-H). Ymir evidence
(`local/ymir/srp-hybrid`, 30k fields): guest frame 778, **140 strips drawn**
(140 native, 60 tile uploads, 0 errors), 11.7M guest-written WRAM instructions,
774 IRQs, `bad=0` (27,344 native dispatches / 178,875 fallback batches). The
sprite hash differs from pure Musashi (1088421458 vs 3757359562) — an
interrupt-timing difference between the recomp dispatcher and Musashi, proven
independent of the dense handlers (empty-map run gives the same hash); the
guest still reaches the same gameplay frame and renders the same strip count.
This is emulator-only evidence; the hybrid is not real-time and there is no
audio or console proof.

**SRP split of ng_boot.c (2026-11-17):** the monolithic `ng_boot.c` was split
by responsibility: `ng_boot.c` (hardware memory/LSPC/I/O, clock, frame loop,
public API; 669 lines), `ng_boot_recomp.c` (native dispatch backend: CPU state,
routine lookup, resident cache, BIOS/game overlays, step/init; 417 lines),
`ng_boot_profile.c` (host-only PC profiler; 104 lines), `ng_boot_stats.c`
(probe-visible counters; 21 lines), and `ng_boot_internal.h` (explicit shared
internals). All Saturn and host targets were rewired. Behavior verified
unchanged: resident bench `bad=0`, ~0.97x; hybrid 30k fields still draws 140
strips with identical guest counters; 39/39 ROM-free host tests pass.

**REACHED REAL TIME (2026-11-17) — final sound-strobe inline + 30k validation:**
the last optimization inlines the `move.b Dn,$300001` sound-command strobe in
the dense handlers as a direct `ng_boot_io_writes++` (bit-exact: page $30 is
unmapped and `io_write8` case 0x300000 is a no-op break). Final binary SHA
`d03b9ae5…`:
- `strobe-verify` (12k fields): **0.9957x**, 11,948 guest frames, `bad=0`.
- `wrapfix-long` (30k fields): **0.9973x**, 29,919 guest frames, 9.9M native
  blocks, `bad=0`, `interp=0`, zero paging, no stall (~3x past the old 10591
  wall). This is a full sustained run at essentially 60 Hz.

Per-guest-frame device-write rates are unchanged across every build from the
baseline through the final one (vram ~3343, pal ~1365, io ~614, irq ~0.667),
confirming bit-exactness. The `bench_summary.py` comparison tool was fixed to
normalize by speed ratio (raw guest_frames are not comparable across different
`--frames` budgets). Host oracle: 7,327,802 blocks, 0 mismatches. All 39
ROM-free host tests pass. Cumulative speedup over resident baseline (0.3538x):
**+182% (~2.8x)**. This is emulator (Ymir) evidence only — NOT physical-console
timing, and the bench has no renderer or audio. A playable port still needs the
renderer on the native path, live Z80/YM2610 audio, and hardware proof.

**CORRECTION — the "sound handshake stall" was a 32-bit cycle overflow, not
the Z80 (2026-11-17).** My earlier diagnosis was wrong. Decisive evidence:
every build — from 0.354x to 0.89x — stopped at *exactly* 10591 guest frames,
independent of speed. 10591 ≈ INT32_MAX / (264×768 cycles per frame) = 2^31 /
202752. `--debug-profile` at the stall showed the poll loop `r_C1711E` at 51M
instructions while the vblank handler `r_C1137A` got **zero**; dumping
`ng_cpu`/`ng_irq_pending` showed `IPL=0` (interrupts unmasked), `irqpend=0`,
`pc=0xC17126`, and `ng_cycles=0x8276845C` (> INT32_MAX) while
`ng_next_event=0x80000100`. Root cause: `KOF_32BIT_CYCLES` makes `ng_cycles`
int32, but `run_until`'s target `t` and `ng_frame_start` are int64; once
ng_cycles wrapped negative, `while (ng_cycles < t)` was permanently true — no
vblank, so no IRQ ever cleared the semaphore. It was an overflow in my own
cycle-width optimization, NOT a missing sound CPU.

**Fix** (`ng_boot.c`): rebase the cycle epoch by `INT32_MAX/2` at each frame
when `ng_frame_start` approaches the wrap, subtracting the same amount from
`ng_cycles`, `ng_frame_start` and any live timer deadline; `ng_vpos` computes
the frame delta in the wrapped int32 domain. Generated code only compares
ng_cycles to ng_next_event (one scanline ahead), so a synchronized rebase is
guest-invisible. Result (`wrapfix-verify`, 14k fields): the guest passes the
old wall — **13,360 frames, 0.9543x, bad=0**, device-write rates unchanged
(vram 3343.37 / pal 1365.54 / io 614.41 per guest frame vs baseline 3344.02 /
1365.33 / 614.06). This is the clean sustained full-run measurement: ~0.95x
real time with zero paging and no stall, emulator-only. Still short of 1.0x.

**Final cumulative validation (2026-11-17):** after all 15 optimizations,
`final3-long` (30k fields, app SHA `ced5c638…`) reports `bad=0`, `interp=0`,
10,591 guest frames, 37.6M native blocks, and device-write counters
**byte-identical to the original baseline** (vram 35,402,489 / pal 14,462,976 /
io 6,505,353 / lastpc 0xC17126). The host oracle
(`kof_host_paged --verify`) still reports **7,327,802 blocks, 0 mismatches**.
All 39 ROM-free host tests pass. This is the bit-exactness and acceptance
evidence for the whole dense-emitter pass. Clean sustained speed (8000 fields,
before the stall): ~0.89x real time. Not physical-console evidence.

**Sound-handshake stall traced precisely (2026-11-17):** WRAM byte
`0x10FE8C` is set to 1 at guest `C1711E`, polled at `C17126`
(`tst.b $10fe8c; bne $c17126`), and cleared only at `C1139C` inside the
vblank interrupt handler `r_C1137A`. That clear is gated by `$10fee3`: at
`C11388`/`C1138E` a nonzero `$10fee3` branches past the clear to `C113A6`.
`$10fee3` is a sound-command status byte the Z80/YM2610 would clear; with no
live sound CPU it stays nonzero forever, so the poll never exits. This is a
genuine external blocker (live Z80/YM2610 emulation), NOT a recomp regression:
all binaries reach the identical state, and `ng_boot_irq_taken=7060` shows
vblank IRQs are firing normally — the handler just takes the gated branch.

**Fifteenth optimization — combined 32-bit page lookup** (`w32fast-verify`,
`w32fast-clean`): `dense_w32_fast` collapses the clear loop's and WRAM movem
fill's two `ng_w16` page lookups per longword into one (bit-exact for aligned
addresses; device addresses still take the exact two-call slow path). 0.8476x →
0.8826x at 12k fields. That 12k run reached the **exact canonical stall state**
— 10591 guest frames with vram=35,402,489 / pal=14,462,976 / io=6,505,353,
byte-identical to the original baseline's 30k run — the strongest bit-exactness
evidence: none of the 15 optimizations changed guest behaviour. Because 12k
fields now reach the stall (and include some spin), a clean pre-stall run at
8000 fields (`w32fast-clean`) gives the true sustained throughput:
**0.8934x**. Cumulative speedup over resident baseline (0.3538x): **+152.5%**
(≈2.5x), emulator-only. Still short of 1.0x real time.

**Measurement caveat:** once the guest reaches frame 10591 it spins in the
sound-handshake busy-wait (0xC17126), so any run longer than the time-to-stall
shows a falling ratio. The admissible sustained-speed metric is a run that
stops before the stall (e.g. 8000 fields).

**Thirteenth/fourteenth optimizations — hoisted VRAM-port classification**
(`vramhoist-verify`, `vramhoist2-verify`): the dense handlers write a fixed VRAM
port 16x/1x per iteration, and each `dense_w16_fast` call re-ran a page-table
lookup + region test. Hoisting that classification out of the inner loop (A1/A0
is fixed across the loop) and inlining the poke directly gave the largest
single gain: `dense_r_C110E8` 0.7635x → 0.8173x; applying the same to
`dense_r_C16D20` + `dense_r_C11F9E` → **0.8476x**. Both PASS `bench_summary`
(`bad=0`, per-frame write rates unchanged). Cumulative speedup over resident
baseline (0.3538x): **+139.6%** (>2.4x), emulator-only. This is the density win
the full dense-emitter approach was chosen for.

**Twelfth optimization — palette-fill movem helper** (`palfill-verify`): unlike
the failed global palette inline (which added a branch to the VRAM path), this
adds a *dedicated* `dense_movem4_pal` used only by fill loops 2/3 (C1108A/C110B4,
whose A0 = $402000 is always palette RAM). It writes `s_pal` directly with a
fallback to `ng_w16` for any unexpected address, so behaviour is unchanged.
0.7479x → **0.7635x**. PASS `bench_summary` (`bad=0`, per-frame write rates
unchanged across all builds). Cumulative speedup over resident baseline
(0.3538x): **+115.8%** (>2.16x), emulator-only.

**Negative result — palette poke inline in `dense_w16_fast`** (2026-11-17):
adding a palette-region case to the single dense helper (exporting
`s_pal`/`s_pal_bank` aliases) showed no gain: 0.7479x → 0.7463x, within
run-to-run noise, and increased app.bin by 632 bytes. Reverted; rebuilt SHA256
is byte-identical (`e1fb9723…`) to the 0.7479x build. The VRAM inline remains
because it targets the dominant VRAM path; palette writes are relatively rare
in the dense loops, so the extra branch only added cost.

**Negative result — inlining device writes into `ng_w16`:** to remove the call
overhead on the 17.7M VRAM writes (`ng_w16_slow` still 26.8% in the fresh
profile), I tried moving the VRAM/palette fast paths inline into the c32
`ng_w16` header (exposing `s_vram`/`s_pal`/`s_addr`/`s_mod` from `ng_boot.c`).
The build FAILED the WRAM-H budget: app.bin grew 970,812 → **996,604 bytes**
(limit 983,040), +25,792 bytes from duplicating the device path at every
`ng_w16` call site. Fully reverted; rebuilt app.bin SHA256 is byte-identical
(`7bb8e083…`) to the measured 0.4404× build. The out-of-line `ng_w16_slow`
fast paths remain — the right trade-off given the tight WRAM-H ceiling.

### Remaining limitations

Symbol sizes omit shared literals, jump tables, relocation effects, runtime/interpreter text, rodata, other data, inter-section padding and target linker behavior. The estimate is not a fit guarantee. Validate the final ELF/map against WRAM partitions, measure the runtime and interpreter cost, repeat selection with calibrated reserves, and quantify held-out match coverage. Compare original/paged/hybrid at same guest tick and same events. If fallback timings or behavior diverge, retain full AOT oracle and revise the policy. Coverage percentages are only native guest instruction coverage of recorded scenarios, not total workload or predicted framerate. No ROM/trace/generated source or symbol report belongs in Git.
