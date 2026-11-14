# KOF98 Saturn — Engineering plan

Status: proposed architecture and feasibility gates, not a playable port.

## 1. Goal and upstream baseline

Preserve the original AES program semantics, not a rewritten fighting engine. Develop a native SH-2 execution backend and convert Neo Geo hardware output into Saturn hardware operations through the installed LibSaturn SDK. Target NTSC 320×224 with a mandatory 4 MiB expansion cartridge. Stock RAM and 1 MiB cartridges are explicitly out of scope. PAL and widescreen are separate experiments. The 4 MiB cartridge is a minimum requirement, not proof that the total memory budget is sufficient.

Pinned source baselines:
- [KOF98 Recomp](https://github.com/danilopaulinodasilva/kof98recomp/tree/44f873a3cca3827a48fca30fa8091da746311af0)
- [neogeorecomp submodule](https://github.com/danilopaulinodasilva/neogeorecomp/tree/a47ee3fe09dc46d1f2b86c36ec972b43b35ebf62)
- [LibSaturn](https://github.com/celsowm/libsaturn/tree/dbc679a3ac213cbb6a975030212c96b21bd684ad)

Use local legally obtained AES kof98h and neo-po.bin dumps. Do not distribute ROMs, generated C/assembly, derived manifests, screenshots, traces, captures, disc images or BIOS files. Preserve original MIT notices when incorporating upstream source. Keep upstream patches reviewable and independent of game-derived files.

## 2. Rationale and observed blockers

Upstream game-notes reports 40,801 routines and 57,972 dispatch entries after profiling; baseline generation emits 1,555,625 instructions including duplicate coverage. The desktop architecture uses a global CPU register file, flag helpers, cycle accounting, virtual bus, and indirect dispatcher. Its dense function lookup allocates five 1 MiB address regions at four bytes per word plus a 128 KiB BIOS region: 10.25 MiB on SH-2, excluding miss and profiling tables. The software video path expands 64 MiB of 4bpp tile ROMs to 128 MiB of 8bpp pixels. Both designs are unsuitable as-is.

Game program ROM is 5 MiB (1 fixed + four banked MiB); data reads still matter after recompilation. The original sound ROMs total 16 MiB, and the fix layer is 128 KiB. Do not assume program data can be fetched from CD synchronously in combat.

The boot path executes roughly 11.7 million interpreted instructions in work RAM. Boss coverage is incomplete upstream. A ROM-only native build is not enough to boot or cover all modes. Retain a bounded fallback or translate the RAM routine with validation, and explicitly test boss paths.

## 3. Architecture and ownership

Host pipeline (independent CLI tools; no dependencies on Saturn SDK at generation time):
- romcheck: verify local ROM lengths and CRC, normalize byte order.
- trace/oracle: run upstream headless with deterministic inputs; record CPU, bus, LSPC, frame and audio outputs locally.
- recomp: upstream decoder/discovery plus SH-2-specific emission, compact dispatch and placement metadata.
- program-pack: classify code/data, produce bank and overlay manifests; preserve every required data read.
- sprite-pack: planar tile conversion to packed 4bpp, deduplication with explicit flip metadata, residency groups and sector-aligned index.
- stage-pack: identify scroll planes and preserve ordering, animated tiles and raster effects.
- audio-pack: decode samples and render music, preserve loop/cue timing and command mappings.
- disc-pack: allocate extents and build local BIN/CUE through LibSaturn.

Runtime components:
- core: CPU state, virtual cycle scheduler, dispatch, exceptions, compact 68000 interpreter fallback, virtual bus and bank switch.
- video: LSPC register/VRAM and palette state, raster event journal, draw plan, native VDP1/VDP2 backends.
- audio: original command-latch semantics, timer/reply model, SCSP and ADX playback.
- assets: program-data cache, per-match tiles, stage and sound residency, CD prefetch.
- platform: LibSaturn startup, pads, VBlank, DMA, cache coherence, diagnostics and disc I/O.

KOF-specific code stays in this repo. Reusable hardware support belongs in LibSaturn only after an isolated test proves the need. Keep the Neo Geo runtime portable to host for differential tests before targeting SH-2. No parallel execution of the stateful guest CPU without a proof of independence.

## 4. Phase A — measure before optimizing

1. Build upstream native oracle in host-only build tree, using the pinned commits. Verify CRCs, record tool versions, baseline 4:3 scripts and profile boss paths. Preserve guest cycle timestamps and original bank IDs. Collect executed PCs, basic blocks, frequency, instruction types, data pages, bank-switch frequency, IRQs and per-frame CPU cost.
2. Generate full C and representative high-frequency blocks; build with sh2eb-elf GCC at -Os, -O2 and -O3, capture ELF/map, text, rodata, data, bss, stack reserves, literal pools, relocation counts, per-bank code and instruction counts. Separate code size from input ROM size. Use host-built generator then cross-built runtime; do not execute a SH-2 generator on the host.
3. Replace dense dispatch with sorted sparse keys (bank in high byte, even 24-bit PC), initially binary search. The seed implementation here uses a function pointer plus key (8 bytes/entry on 32-bit target, ~453 KiB with 57,972 entries, excluding code). Compare per-page directory of sorted slices, compressed offsets and hot cache; validate exact entry equivalence, including aliases, bank zero, BIOS mirrors, odd PC policy and misses.
4. Build minimal SH-2 microbenchmarks for bus accesses, ALU helpers, dispatch, register access and guest cycle updates. Time isolated hot paths and end-to-end frames with LibSaturn FRT and Ymir. Compare interpreted fallback baseline as a control.
5. Report actual peak memory and CPU cycles/frame with median, p95, p99 and worst; separate boot from combat. No gameplay milestone can be marked done based only on host benchmarks.

Gate A: reproducible full-size/link report (or documented link failure), representative hot trace and SH-2 timings. If code cannot fit, evaluate the following alternatives in order before committing to a renderer.

## 5. Phase B — reduce code and memory

- Canonical basic blocks keyed by region, PC and bank. Deduplicate overlapping emitted routines. Preserve computed entry and resume points; retain guest stack semantics rather than mapping all JSR/RTS to C calls.
- Dataflow flag liveness across blocks only where interrupt, exception, fallback and externally observed state are preserved. Keep exact carry/X/BCD behavior. Aggregate cycle bookkeeping only across sections that cannot observe it; check event deadlines and side-effect ordering.
- Specialize bus accesses for aligned big-endian SH-2 RAM where valid, retaining byte and word semantics, mirroring and MMIO ordering. Keep ROM data reads available independently of emitted instructions.
- Try local register caching and direct transfers with typed exit stubs. Consider a native SH-2 emitter only if optimized C still fails the budget.
- Compare hybrid native hot code + compact interpreter for cold routines. Record coverage, worst-case timing, self-modifying/RAM code handling and miss behavior.
- Partition overlays by proven lifetimes (boot, menu, fight, ending) rather than arbitrary ROM bank boundaries. Load only into reserved executable internal WRAM, validate relocations, invalidate instruction cache as required, and make dispatch generation-aware. Never execute code from the RAM cart as a baseline: LibSaturn's RAM expansion contract is data-only. Cart has two separate 2 MiB banks; use logical slices. SCU DMA can read but not write cart memory according to the SDK contract.
- Measure fixed bank and banked data working sets. Demand-load only at safe points or prefetch with proven bounds; do not claim that 5 MiB of P ROM can fit alongside code and graphics. Consider extracting data-only reachable ranges with trace plus static bounds; retain correctness fallback.

Memory accounting: WRAM-H 1 MiB and WRAM-L 1 MiB, not one contiguous free arena. Account for SDK/linker reservations, stacks, 64 KiB guest RAM, 128 KiB LSPC VRAM model, palette RAM, data caches, queues, overlays and DMA buffers. Cart: mandatory 4 MiB data in two banks. Start with an adjustable budget of 2 MiB for character/stage tiles and 2 MiB for guest program data/cache, then rebalance from measured working sets. Both allocations are hypotheses, not validated residency sizes. Detect the 4 MiB cartridge before allocating game resources and provide a readable startup diagnostic if absent or incompatible. VDP1 VRAM 512 KiB (commands, textures, LUTs); VDP2 VRAM 512 KiB (patterns/maps/scroll tables), CRAM mode-dependent up to 4 KiB, Sound RAM 512 KiB for driver, samples and ring. Each memory space gets separate peak and guard reports. No borrowing framebuffer RAM or cart execution in baseline.

Gate B: deterministic boot and scripted fight logic within actual link/stack limits, and p99 guest CPU cost with adequate measured rendering/audio headroom. Target roughly 60 Hz presentation, not a claim.

## 6. Phase C — minimal system and deterministic timing

Implement virtual bus map as documented upstream: fixed ROM, mirrored work RAM, bank window, input and sound ports, LSPC, palette, AES BIOS vectors. Preserve bank and vector switches, IRQ priorities and auto-animation. Guest clock is 12 MHz; 264×768 = 202,752 guest cycles/frame (~59.19 Hz). Use guest units, not SH-2 clocks, for game timing. Schedule IRQ1 at the precise original VBlank point and IRQ2 at its deadline. Guest scheduler runs independently from output VBlank, with rational accumulator for long-term cadence, no frame-count-based gameplay acceleration.

Start from a real boot, plus a local captured fight snapshot for rapid iteration. Compare CPU registers, flags, signed/unsigned operations, bus write order, guest RAM, bank, interrupt state and LSPC state at block/frame checkpoints. First mismatch must stop with diagnostics. Keep oracle configuration and input script hashes. Verify known RAM execution and all dispatch misses.

Gate C: boot, title, single match with identical frame checkpoints against host, including scripted input for both players. No visual or audio claim yet.

## 7. Phase D — native two-dimensional rendering

Reference renderer: upstream software scanline path, max 381 sprite columns/frame and 96/line. Capture LSPC snapshots and mid-frame register/palette changes. First use generic LSPC-to-VDP1 tile/column translation for correctness (including chained columns, wrap, clipping, auto-animation, index zero transparency, flips, and layer order). Convert tiles offline from their planar representation to native packed 4bpp; do not expand to 8bpp resident.

VDP1: measure per-tile draw baseline, then coalesce compatible vertical strips and runs; only combine equivalent palette/flip/zoom/order groups. Implement texture residency with tile-ID mapping, generation tags, pinning and bounded eviction. Compare LUT4 versus native 16-color CRAM bank mode; verify CRAM palette pressure, RGB555 conversion, transparency and sprite priority bits. Neo Geo has 256 sprite palettes, so palette residency and color representation need actual evidence, not a direct 1:1 mapping.

VDP2: first map the 8×8 fix layer to a top-priority tile plane; preserve 40-column HUD, margin/overscan and palette changes. Then classify stage layers via trace and static analysis, using tilemaps, scroll and line-scroll where equivalent. Preserve foreground ordering relative to fighters. Any unclassified layer stays in VDP1. For non-equivalent shrink modes, precompute exact pixel masks/variants from the original zoom table, or use a targeted software fallback. Record raster splits where a single frame snapshot is insufficient.

Use direct list submission and DMA through LibSaturn, double-buffer resource lifetimes, avoid per-frame malloc. Master runs guest; Slave may prepare immutable draw plans or tile conversion with explicit cache ownership. Render replay can be benchmarked using trace-derived local fixtures independently from CPU.

Gate D: pixel comparison of representative frames with documented color quantization differences; exact sprite coverage/order where supported; stable command/texture capacity; sustained presentation target measured in Ymir, Mednafen and physical hardware.

## 8. Phase E — content residency and CD

Build sector-aligned archives and generated (local-only) manifests for character, stage and shared assets. Profile both teams, all animations, projectiles, assists/entrances, UI, stage effects and transitions. Start single versus, then team versus with a bounded policy for active fighters, next fighters, shared effects and stage; no assumption that all six fighter sets fit simultaneously. Prefetch during selection and round transitions, fail explicitly rather than silently drop essential tiles. Record cache hits/misses, seeks, transfers, scene transition length and late requests. Avoid CD seeks during active combat whenever possible. Test cold/warm cache and worst pair/stage combination.

Gate E: no unexpected blocking reads or missing assets during stress combat, defined transition latency and robust recovery from I/O failures.

## 9. Phase F — sound

Trace original Z80 commands, replies, YM2610 writes, timers and music state. The reply latch can affect boot/game behavior; do not replace it with unconditional success. First emulate handshake and compare event log. Generate music ADX offline from the original driver with correct loop points, and convert SFX/voices to suitable PCM assets. Handle simultaneous effects, priorities, voice stealing, stop/fade commands and dynamic tempo/pitch where needed; compare against oracle. For cases requiring live synthesis consider a hybrid driver approach, not a full YM2610 software emulation assumption. Use LibSaturn SCSP driver and cooperative disc scheduling; reserve ring and avoid underruns while loading artwork. Music and SFX fidelity tested separately.

Gate F: no timing deadlock, handshake divergence, unexpected underruns or missing accepted cues.

## 10. Phase G — full game and release validation

Run upstream mode scripts (single/team play and versus, survival/practice), attract, every stage, character/costume, rounds and boss; include two human pads, grabs, supers, KO, timeouts, pauses and reset. Build generated exhaustive matchup matrix and report seeds, results and divergences. Long-run memory high-water and cache stress. Validate CD boot and 4 MiB detection on actual hardware; explicit diagnostic for no/wrong cart. PAL and widescreen are opt-in research milestones only after NTSC baseline. Stock RAM and 1 MiB cartridges are not supported targets. Widescreen patches upstream alter camera/wall behavior; not default. Never publish copyrighted game artifacts from CI or releases.

## 11. Deliverables and priorities

| Milestone | Evidence | State |
|---|---|---|
| M0 repository, architecture, sparse dispatcher seed | host unit test and analytical footprint | started |
| M1 oracle traces + full SH-2 cross-link | ELF/map and frame profiles | pending |
| M2 deterministic guest boot + fight | differential hashes and timings | pending |
| M3 native replay renderer | frame diff and VRAM metrics | pending |
| M4 playable single versus | emulator ROM-local build | pending |
| M5 team play and audio | streaming and cue tests | pending |
| M6 full coverage | mode/boss/hardware gates | pending |

Immediate task: obtain local ROMs, build upstream oracle, run profiling and size the generated C on the actual toolchain. Do not spend time on title screen or graphics before resolving feasibility. If the full native executable cannot fit, document a hybrid/overlay design with measured hot paths instead of hiding the failure.
