# Architecture redesign — one build with real-time guest + renderer + live audio

**Status: P0–P2 executed and passing; P3/P4 unified target built; P5 speed and
render gates NOT met.** See `docs/REDESIGN_EXECUTION_LOG.md` for the measured
results. The architecture redesign solved the memory blocker but the runtime
cost of live YM2610 synthesis prevents a ~1.0× single build with these
techniques. This document does not claim a playable port. All figures below
come from local commands/reports; game-derived generated code stays under
`local/` and is never committed.

## 1. Diagnosis — why the three capabilities are stuck in separate builds

| Build | Guest logic | Renderer | Live audio | Result |
|---|---|---|---|---|
| `kof_resident_bench` | native, RECOMP_ONLY, `bad=0`, **0.9973×** over 30k fields (502M guest instr, 9.9M native blocks) | none | none | real-time but not playable |
| `kof_ng_hybrid_bench` | dense native + Musashi fallback, `bad=0` | 140 strips at frame 778 | 725k YM2610 samples to SCSP | playable frame but ~30k fields for 778 guest frames (≈0.026×) |

Root causes (measured, not guessed):

1. **The resident recomp blob is ~90% dead code for the measured workload.**
   SH-2 symbol profile of the real-time bench (`local/ymir/resprof12k`):
   **94 of 679 native routines execute — 105 KiB of 904 KiB (11.6%)**.
   The executed set is *identical* at 3k and 12k fields (0 new routines), so the
   working set is bounded, not an artifact of a short run. 799 KiB is never
   touched. This is the dominant reason the real-time build cannot coexist with
   renderer + audio: it is carrying ~800 KiB of code it never runs.
2. **Musashi is huge.** Memory audit: Musashi consumes **627,557 B (613 KiB)** in
   WRAM-H (262 KiB jump table + 65 KiB cycle table + 299 KiB text). The hybrid
   has only **364 B** of WRAM-H headroom; the resident bench (no Musashi) has
   **21,720 B**. Removing Musashi is a prerequisite, and the RECOMP_ONLY bench
   proves the guest runs without it at `bad=0`.
3. **Static recomp cannot see runtime-WRAM code.** 7.64M instructions execute
   at `$100000-$13FFFF`; the guest copies bytes there at runtime. Every
   generated recomp table contains only `$100000` (a 5-instruction hand-lowered
   rescue routine), and the actual PCs (`$100078 … $1008b0`) are in none of them
   (108,269 lookups / 1 hit — `docs/X2_NATIVE_RESIDENCY.md`). The interpreter
   fallback is what handles this today.
4. **Audio + renderer additions:** linked audio is 50,284 B code + 80,032 B
   WRAM-H buffers; renderer ~18–58 KiB + buffers. Adding both to the unpruned
   real-time build overflows WRAM-H by **460,020 B**. After pruning + removing
   Musashi this is no longer a constraint (see §2).

## 2. Target architecture — profile-pruned native core, no interpreter

The key change: **replace "full recomp + Musashi fallback" with "profile-pruned
native set + a tiny, deterministic RAM-code handler"**, then add renderer and
audio into the reclaimed WRAM-H.

### 2.1 Normative constraints (from AGENTS.md / PORTING_PLAN / CROSS_PORT_ENGINEERING)
- 4 MiB cart is mandatory and **data-only**; do not execute translated code from
  it (PORTING_PLAN:64/67, CROSS_PORT_ENGINEERING:31). Cart-executable code is
  physically possible (A-Bus `0x02400000-0x027FFFFF`, unified SH-2 address space)
  but is explicitly *not* the baseline and is slow — rejected for this plan.
- Keep WRAM-H (1 MiB, executable) / WRAM-L (1 MiB, data-only) /
  cart / Sound RAM budgets separate. 16 KiB stack reserve must remain.
- No silent dispatch misses or overflows; `bad=0`, increasing guest frames,
  no dropped ticks are acceptance requirements.
- Host tests stay ROM-free; no ROMs/binaries/game-derived code committed.

### 2.2 Memory budget of the redesigned build (projected, WRAM-H)

| Consumer | Bytes |
|---|---|
| Pruned native guest routines (125 routines, 25% headroom over measured 94) | ~140 KiB |
| Dense handlers + dispatch cache + map | ~12 KiB |
| Guest bus/CPU/scheduler/ng_boot (no Musashi) | ~40 KiB |
| Audio code (linked) + buffers | ~130 KiB |
| Renderer code + `g_ir` / tile cache | ~58 KiB |
| SCSP/cd/disc + SDK runtime | ~60 KiB |
| Stack reserve | 16 KiB |
| **Total** | **~456 KiB** (of ~1008 KiB usable) — **~550 KiB headroom** |

WRAM-L (data-only) is already under budget in the resident configuration
(652 KiB used, 396 KiB free), enough for the Neo Geo mirrors plus renderer strip
buffers and the audio ROM ring. The audio arena must be fixed at a real size
(the shipped hybrid ELF currently relies on a stale 8 KiB object while the
source says 48 KiB) — see risk R4.

### 2.3 Native core: profile-pruned resident set
- New local tool `tools/make_pruned_native.py`: take the **union** of executed
  native routines from a comprehensive set of input tapes / modes (§4, P1),
  extract exactly those routine bodies + a minimal sorted addr→fn map from the
  generated page sources, and emit a local `pruned_native.c` / `pruned_map.c`.
  Keep the proven binary-search + 512-slot direct-mapped tag cache dispatch
  unchanged (`src/saturn/resident_dispatch.c`); it is trusted and fast.
- This is a *selection* of already bit-exact generated routines, not new codegen:
  no change to instruction lowering, flags, cycles or bus semantics.
- Keep the BIOS recomp entries actually dispatched; drop the ~800 KiB of
  unreachable routines. Gate: re-run the full host oracle over the same tapes —
  bit-exact, 0 mismatches, and `bad=0` in Ymir over 30k fields.

### 2.4 Guest-WRAM code: small runtime snapshot handler, not a recompiler
The only runtime-written code observed is the BIOS-copied `$100000` routine.
Keep the existing hand-lowered `wram_r_100000` (5 cases) as the authoritative
native entry, produced from a **dumped runtime snapshot** (`wram_code.bin`), not
from the P-ROM image. Add a fail-loud diagnostic for any other WRAM PC: count
and report, never silently dispatch. If new WRAM PCs appear in other modes,
snapshot-and-lower them offline with the same deterministic flow (no online JIT
in the baseline). This preserves `bad=0` without a runtime recompiler.

### 2.5 Renderer integration
- Reuse the validated strip pipeline already in the hybrid. Gate
  `ng_live_sprite_strips()` to the render window (already proven: 45k→30k fields,
  140 strips, 0 errors, same sprite hash).
- `kof_lspc_line` was 32.5% of SH-2 time before gating; after gating it no
  longer dominates. Keep the gate. Do not add new raster work per field.
- Carry over the tile/VDP1 command high-water and pin/fence diagnostics.

### 2.6 Audio integration
- Reuse the working native Z80/YM2610 path (`src/audio_shims/`, `audio_init`,
  `$320000` latches, SWPBIOS driver switch, SCSP mono stream feed). It already
  produced 725k live samples with game M1 at `bad=0`.
- Fix the allocator arena mismatch (R4); right-size the sample ring from
  measured underruns rather than copying a fixed size.

## 3. Work packages (priority order, exit evidence required)

| ID | Scope | Exit evidence |
|---|---|---|
| P0 | Freeze a pinned baseline: commit hashes for app, SDK, upstream, BIOS, Ymir, input tapes; reproduce 0.9973× / 140 strips / 725k samples numbers | Reproducible command log; hashes recorded |
| P1 | Build comprehensive executed-routine profiles across modes (boot, menu, select, fight, boss, ending) with authored input tapes; compute the union set | Versioned CSVs + SHA-256 identity; union list; 0 mismatches vs host oracle on each tape |
| P2 | Implement `make_pruned_native.py`; build a RECOMP_ONLY target with the pruned set + `wram_r_100000`; link within WRAM-H with stack guard | ELF/map showing ≥400 KiB headroom; host oracle bit-exact; Ymir 30k fields `bad=0`, increasing frames |
| P3 | Add renderer (gated) to the pruned native target | 140 strips at frame 778, 0 errors, sprite hash matches hybrid; no new `bad` |
| P4 | Add live game audio to the same target (M1+SM1, SCSP feed) | 700k+ samples written, non-zero dump, no underruns; `bad=0` |
| P5 | **Unified gate:** one build at ~60 Hz guest cadence with P2–P4 evidence simultaneously | `bad=0`, guest_frames increasing at ~1.0×, strips>0 with 0 errors, live audio non-zero, WRAM-L/H high-water within guards |
| P6 | Content residency for all modes (phase overlays from cart *data* into WRAM executable arena at safe points) per PORTING_PLAN Phase E | Full matchup/mode matrix, no blocking reads or missing assets |
| P7 | Physical console timing/capture (user-supplied hardware) | Real-hardware evidence; explicitly blocked until hardware is provided |

## 4. Risks and mitigations

- **R1 — coverage.** A profile from one input path may miss routines needed in
  other modes. Mitigate with P1's multi-tape union, phase overlays (P6), and a
  fail-loud miss counter; never grow the blob speculatively.
- **R2 — guest-cycle timing.** Pruning changes code layout; preserve cycle
  accounting and IRQ boundaries exactly. Re-run the 32-bit-cycle wrap regression
  (historical frame-10591 freeze) and compare guest tick equivalence.
- **R3 — interrupt divergence.** The hybrid showed a deterministic sprite-hash
  difference vs pure Musashi (timing, not correctness). Track it as a known
  signature; do not treat a hash difference alone as a regression, but require
  the same gameplay frame/strip count and documented IRQ timing.
- **R4 — build integrity.** `shim_alloc.c` source (48 KiB arena) is stale vs the
  linked 8 KiB object; rebuilding as-is breaks the link. Resolve before P2.
- **R5 — A-Bus cart execution rejected.** Do not switch to cart-executable code
  unless P2–P5 fail *and* the normative docs are formally amended with measured
  latency evidence.

## 5. Acceptance (no playable claim until all hold)

1. One target binary with native guest logic, renderer, and live game audio.
2. `bad=0`, no dispatch miss silently masked, no guest ticks dropped.
3. Guest frames increase at ~1.0× of emulated fields (currently only the
   renderer-free bench does this); report p50/p95/p99 critical spans.
4. 140 strips drawn at frame 778 with 0 draw errors; 700k+ live YM2610 samples
   to SCSP with no underruns.
5. 39/39 ROM-free host tests pass; host oracle bit-exact on all tapes.
6. WRAM-H/L and Sound RAM high-water within guards; no ROM/binaries staged.
7. Emulator evidence (Ymir cache-on injection) is not presented as physical
   console proof; physical evidence requires user-supplied hardware (P7).
