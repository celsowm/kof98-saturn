# Cross-port engineering evidence and implementation decisions

This document is normative for the initial KOF98 Saturn feasibility work. It complements PORTING_PLAN.md rather than replacing the original Neo Geo semantics with either reference game's gameplay engine.

## Reviewed baselines

- Tekken3 Saturn: [6e8f16f](https://github.com/celsowm/tekken3-saturn/tree/6e8f16fb40ecfb506a480ca59f52f669759e6ed8).
- Ikemen Saturn: [a9dbd0f](https://github.com/celsowm/ikemen-saturn/tree/a9dbd0ff8a11a52040d8fb9decd800d2d632ae5e).
- KOF98 upstream and SDK pins remain in PORTING_PLAN.md. Before implementing a reused API, verify it exists at the SDK pin and update the pin with a reproducible comparison if necessary. No code imported from either project in this documentation change.

## 1. Reference findings and limitations

| Reference | Evidence | KOF adoption |
|---|---|---|
| [Ikemen asset store](https://github.com/celsowm/ikemen-saturn/blob/a9dbd0ff8a11a52040d8fb9decd800d2d632ae5e/src/ikemen_asset_store.c) | Requires SAT_RAM_CART_4MB, CDFS mount, chunks loaded through WRAM, logical bank-crossing buffers and bounded reads; single bank only for raw map | Typed asset store, two-bank allocator, verified sizes and immutable slot handles |
| [Ikemen main](https://github.com/celsowm/ikemen-saturn/blob/a9dbd0ff8a11a52040d8fb9decd800d2d632ae5e/src/main.c) | 32-entry texture cache, owner/sprite key, epoch pinning, separate effect quota of 3, six animation-frame lookahead; memoizes positive and negative prefetch decision | Use identity and epoch design, not arbitrary cache size; predict KOF tiles from guest state/traces |
| [Ikemen architecture](https://github.com/celsowm/ikemen-saturn/blob/a9dbd0ff8a11a52040d8fb9decd800d2d632ae5e/docs/ARCHITECTURE.md) | 4 MiB cart holds P1/P2/FIGHTFX blobs; WRAM scratch; VDP2 NBG0 stage | Adapt to active fighter, next fighter, stage and ROM-data competition |
| [Tekken memory lifetimes](https://github.com/celsowm/tekken3-saturn/blob/6e8f16fb40ecfb506a480ca59f52f669759e6ed8/docs/MEMORY_LIFETIMES.md) | 78,668 B WRAM-H reduction in specific paired builds after removing command/Gouraud staging; loader-held pointers keep assets live in combat | Phase-aware executable/data budgets, direct VRAM list baseline |
| [Tekken VDP1 bank validation](https://github.com/celsowm/tekken3-saturn/blob/6e8f16fb40ecfb506a480ca59f52f669759e6ed8/docs/VDP1_BANK_VALIDATION.md) | 441/441 pairs in specific 1500-frame attract smoke; max 809/2048 commands; no draw timeout, event overflow or face rejection; three pairs each had one late swap | Exhaustive roster smoke plus independent dynamic visual/logic coverage; do not transfer command counts to KOF |
| [Tekken overlap](https://github.com/celsowm/tekken3-saturn/blob/6e8f16fb40ecfb506a480ca59f52f669759e6ed8/docs/FRAME_PREPARATION_OVERLAP.md) | 17.95% submission gain in historical Mednafen control; 16,334 original/host/Saturn ticks matched in diagnostic tests; removing wait without replacing clock broke simulation | Separate clocks and overlap CPU work with prior VDP1 draw from day one |
| [Tekken CPU budget](https://github.com/celsowm/tekken3-saturn/blob/6e8f16fb40ecfb506a480ca59f52f669759e6ed8/docs/NATIVE_CPU_MEMORY_BUDGET.md) | Cache-on Master and actual Slave verification; different timing in injected Ymir versus CD-boot Mednafen | Test both boot paths; do not mistake cache-enabled emulation for hardware proof |
| [Tekken post-VRAM plan](https://github.com/celsowm/tekken3-saturn/blob/6e8f16fb40ecfb506a480ca59f52f669759e6ed8/docs/POST_VRAM_FRAME_RATE_PLAN.md) | Matched A/B builds, scalar rather than raw struct comparisons, worst segment, no discarded ticks | Performance and parity contract |
| [Tekken SFX residency](https://github.com/celsowm/tekken3-saturn/blob/6e8f16fb40ecfb506a480ca59f52f669759e6ed8/docs/SFX_RESIDENCY_STUDY.md) | Paul/Law 400,596 B resident SFX in historical baseline; 15,288 B exact duplicates; music and SFX compete for Sound RAM | Deduplicate and phase-pack audio; joint budget, never remove effects merely because one script didn't request them |
| [Tekken real hardware](https://github.com/celsowm/tekken3-saturn/blob/6e8f16fb40ecfb506a480ca59f52f669759e6ed8/docs/REAL_HARDWARE.md) | Loader-dependent reports not reproduced in emulators | CD/ODE/loader-specific diagnostics and physical acceptance |
| [Ikemen oracle](https://github.com/celsowm/ikemen-saturn/blob/a9dbd0ff8a11a52040d8fb9decd800d2d632ae5e/tools/ikemen_oracle/README.md) | Deterministic authored input, per-frame traces, first-divergence and RNG auditing; README scopes 66 classic KFM scenarios, not all engine modes or AI | Instrument existing KOF oracle; preserve original RNG and AI, no rewrite |

Ikemen documentation describes lookahead up to 32 animation ticks, while source bounds traversal to six animation frames. Treat the source as the current contract; do not equate ticks and animation frames. Metrics above are historical evidence for specific builds and workloads, not a claim about current hardware FPS or KOF performance.

## 2. Memory system: budgets by phase, not fixed 2+2 split

4 MiB expansion required, no stock/1 MiB fallback. Use adjustable two-bank pools with a measured placement plan. Begin with a synthetic partition hypothesis (2 MiB ROM data/cache, 2 MiB visual assets), then derive partitions from simultaneous working sets and bank-crossing costs. Do not execute guest translated code from the cart. Do not assume one linear 4 MiB address range. A 2 MiB bank may contain multiple asset slots; larger logical blobs must use bank-aware read APIs.

Phases: boot/BIOS, title/menu/select, round-load, fight, round transition, ending. Each resource declares owner, size, alignment, memory region, generation, phase of first/last use, dependency edges and whether pinned across transitions. Loader-only references do not prove that a buffer is dead. Generate a liveness/placement report from ELF/map, authored ownership graph and runtime high-water marks. Distinguish permanent hot state, temporary scratch, immutable cart data, overlay executable and guest ROM pages. Assert boot image <= 983040 B only if the pinned LibSaturn packaging contract requires it; additionally enforce actual WRAM linker and stack guards. Do not count a reduced initialized image as total RAM usage.

Measure hot ROM reads and classify fixed/banked data before sizing graphics slots. Compare dense page residency vs deduplicated data blocks; no synchronous CD miss during combat. If a required set exceeds capacity, stop and report the exact deficit; replan data layout or hybrid translation rather than omit content.

Cart slots: active P1/P2, next P1/P2 (opportunistic), stage, common FX/HUD, guest program data, audio staging where feasible. Implement phase-resettable arena with generation-checked handles, borrow regions only after retire fences, transactional loads or explicit rollback. Measure direct cart-to-VDP1/VDP2 SCU DMA for aligned packed data vs cart-to-WRAM-to-VRAM; SCU cannot write to cart. For many 128-byte tiles compare coalescing and CPU transfer versus DMA setup; no tiny-DMA assumption.

## 3. Native renderer: two caches, frame epoch pinning

Capture LSPC register/VRAM writes and palette/raster events, build portable renderer IR with visible column runs, tile ID, palette ID, flips, shrink, clip and order. Replay the same trace with software oracle and Saturn backend before guest CPU integration. Separate tile pixels from palette-dependent bindings; keys include source/pack generation and exact transform variant only when pixel layout differs. Use 4bpp native patterns wherever possible; no global 8bpp expansion.

Cache tiers:
- Cart asset store: phase-resident immutable payloads grouped by character/scene and tile IDs; dedup exact pixels and index by original tile ID.
- VDP1 texture residency: bounded byte allocator and stable handles, O(1) or paged lookup preferred over linear full-cache scan at Neo Geo scale. Track fragmentation, unique requested bytes, uploads, evictions, pin failures and misses. Separate quota/reservation for transient effects; measure based on actual bursts.
- Palette/CLUT cache: separate lifetime and generation, preserve palette writes, shadow/dark-bit conversions, index-zero transparency and VDP2 priority semantics. Evaluate native 4-bit bank mode against LUT4 for CRAM capacity and VDP2 stage pressure. Avoid tying each texture to a fresh palette copy.

Two-phase render: enumerate and pin all required texture/palette generations before emitting; after submission keep pins until the associated draw completes, not merely until CPU advances epoch. A new frame may allocate only from unpinned space or update texture storage after a safe fence. Double command banks do not double texture storage or make texture updates automatically safe. Use the installed SDK's direct-to-inactive-VRAM list mode where available; verify both END/CEF and framebuffer swap ownership, no forbidden SCU IST writes; test allocator and fence behavior with synthetic timeouts and held draw.

Start with tile-level baseline; compare contiguous vertical strip signatures and exact shrink/flip variants. Coalesce compatible visible tiles only if ordering and raster semantics are unchanged. Bounded descriptor cache for common strips, offline packed variants when justified by trace. Measure list command max, bytes and VDP1 work; do not inherit Tekken's 2048/809 command assumptions. Try game-specific VDP2 extraction for static/scrolling backgrounds and fix HUD, retaining generic VDP1 fallback and raster handling. Compare same-tick full composited image and sprite coverage; include flashing palettes, super freeze, transparent layers, large projectiles, stage edge and palette swaps.

Predictive prefetch: memoize by immutable animation/column signature and guest state when proven; otherwise use trace-derived tile transition graph, conservative likely next sets and current LSPC writes. Prefetch negative results too to avoid repeated scanning. Measure accuracy, wasted bytes, late misses and CPU overhead. Never use a prediction as proof that an unpredictable move cannot request an asset.

## 4. CPU and pipeline: no serialized VBlank dependency

Master owns authoritative guest CPU, bus, interrupt and bank state. Keep original 12 MHz clock and ~59.19 Hz guest video timeline; separate hardware display fields, simulation frames, draw completion and audio clock. At NTSC presentation nominal 16.7 ms, set an initial (unproven) CPU critical-path allocation: guest execution 8 ms, draw preparation/submit 4 ms, assets/audio service 2 ms, margin 2.7 ms. Slave work may overlap; measure both processors, shared-bus contention and critical path, not sum of parallel spans. Revise allocations after measurement, not guest timing.

First try canonical blocks and compact page-sliced dispatch with hot direct cache (tag includes guest bank and overlay generation). Measure sorted sparse baseline and code size. Evaluate 32-bit relative guest-cycle deadlines with wrap-safe comparisons and 64-bit absolute epoch only if needed; remove per-instruction verification counters in release iff no semantic dependency. Inspect disassembly for libgcc 64-bit shifts/divides, register spills and repeated global CPU loads. Apply safe SH-2 DIVU hardware only for proven operand range and ownership, preserve guest DIV flags, exceptions and reentrancy. Compare GCC configurations with the same compiler/toolchain as reference plus alternatives; no assumed faster newer compiler. Place hot tables and loops by measured WRAM-H/L and cart latency. Precompute constant transforms offline rather than in frame loop.

Slave may classify immutable render snapshots, construct strip plans, convert textures or stage audio blocks using bounded jobs. First compare Master-only vs Slave candidate including handoff costs. Publish output and invalidate only aligned spans actually written, with SDK cache ownership contract. Do not execute two guest instructions concurrently or simulate next guest frame on Slave until a proof addresses shared state and deterministic IRQ ordering. Overlap next guest/CPU preparation with preceding draw but serialize conflicting palette/texture writes and protect all frame-resource lifetimes.

Preserve elapsed time: no hidden dropping of guest ticks, no changed input semantics. Sample pad at defined guest boundary; use rational clock accumulator and explicit overload report. Track guest backlog separately from presentation; a backlog is a failed full-speed gate, not an excuse to slow or skip gameplay. Present ideal NTSC cadence without inventing 60 Hz guest simulation.

## 5. Sound and disc

Use existing LibSaturn music/audio APIs and a single disc scheduler. No independent loader that can steal CD Block from ADX. Instrument guest command/reply and YM timer events before replacing synthesis. Pre-render music with exact sample-domain loop/cue metadata, convert voices/effects, dedup exact waveform identities with aliases and phase-specific residency. Compare original and decoded output with timestamp alignment; handle overlapping cues and arbitrary simultaneous requests. Define SCSP budget including driver, PCM sample bank, ring, hardware transfer scratch and fragmentation. Test ring size and underruns under combined worst asset load; don't copy Tekken's ring size without measurement. Use music-disabled A/B only as diagnosis, not release optimization. No silently dropped commands.

## 6. Oracle and test infrastructure

Reproduce Ikemen's first-divergence tooling and Tekken's multi-level gates using upstream KOF as reference:
- Host oracle against Musashi: guest register, flags, bank, PC, bus writes, IRQ, timing, RAM-routine bootstrap; trace only selected events in target performance build.
- Host portable runtime vs Saturn: versioned canonical scalar serialization with explicit endianness, no pointer/padding/host addresses, per-tick hashes plus full state checkpoints and first divergent block/field. Use original input tape and exact guest tick and display-field mapping. Do not compare frames just by requested emulator fields.
- Renderer replay: independently test VDP1 command fields (ignore only known physical addresses via normalization), textures, palettes, VDP2 maps/line tables, final pixels, order and raster changes.
- Audio oracle: command/reply timestamps, cue coverage, latency, steals, underruns; separately test loop seam.
- Matrix: derive roster and stage count from validated game data; all ordered legal fighter pairs, including mirrors, alternate palettes, hidden/boss characters when supported, every stage and stress effects. For team compositions use all individual pairs plus rotation/transition equivalence classes and edge cases, not infeasible Cartesian product. Report untested combinations explicitly. Every mode, boss, timeout, draw, KO, super, throw, counter, simultaneous hit, team switch, demo, intro, pause and reset.
- Emulator: Ymir cache-on injection for precise trace; Mednafen actual BIOS CD boot with isolated profiles and extram4, then physical console and supported loader/ODE profiles. Synthetic boot-cart tests cover absent/1 MiB/incompatible cartridge with explicit rejection.
- Pin app, SDK, upstream, compiler, BIOS identity, emulator, cart type, input tape, ELF/disc hashes and flags. Store sensitive game-derived reports locally only. Compare candidate/control at identical guest time and video mode. Record rejected changes and why.

Release diagnostics: cartridge ID/free bytes; both CCRs and verified Slave job; IRQ/VBlank/FRT, guest ticks/debt, submitted/drawn/swapped/changed images, late swap/timeouts, VDP1 command and texture high-water, cache miss and DMA bytes, CD request deadlines/read errors, sound ring min fill and underruns, stack high-water, first error. Keep HUD optional (measure cost); export bounded binary traces for long tests.

Acceptance: zero logical divergence for covered cases, zero discarded guest ticks, zero unhandled dispatch miss, zero missing required assets, zero command overflow/timeout and zero unexpected audio underruns. Report p50/p95/p99/worst critical spans, sustained game cadence and presentation cadence separately. Reject speed changes that alter fidelity or worsen worst-case beyond measured noise. Never claim physical 60 FPS from emulated list submissions.

## 7. Implementable work packages in priority order

| ID | Scope | Dependency | Exit evidence |
|---|---|---|---|
| X0 | ROM-free dispatcher unit/benchmark, pinned installed SDK, compile size and link budget automation | current repo | host test logs and target ELF/map; no ROM claim |
| X1 | Local oracle profiler incl. bank/page access, LSPC tile/raster and sound command traces | legal dumps | versioned trace and repeatable first-divergence |
| X2 | Deduplicated blocks, compact dispatch, RAM fallback, ROM-data residency | X1 | deterministic guest fight and actual critical-path budget |
| X3 | Cart store, lifetime allocator, manifest and transfer benchmark | X0; X1 to tune | rejection of wrong cart, bank boundary tests, measured transfers |
| X4 | Native LSPC IR replay, 4bpp cache, palette/fence ownership, VDP2 fix | X1, X3 | same-tick image diff and high-water metrics |
| X5 | Stage plane extraction, strip coalescing, async preparation | X4 | paired comparisons across stages, stress effects, no visual regressions |
| X6 | Single versus integration and boot/CD loading | X2, X4 | reproducible local disc boot, original timing |
| X7 | Audio handshake/SCSP/ADX, team residency and transitions | X3, X6 | zero underflow/missing cue, bounded loads |
| X8 | All-mode/roster matrix, real hardware and ODE | X7 | explicit coverage report and hardware evidence |

Parallelize host trace and synthetic renderer/card tests, but do not declare a playable milestone until guest execution passes. No changes to Ikemen or Tekken repositories. Shared generic SDK features should be isolated in LibSaturn changes with focused tests and reviewed independently.
