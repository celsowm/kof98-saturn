# Runnable pure-C guest runtime (local-only build)

The decompiled C is no longer only compile-checked: it can be compiled into an
executable that **dispatches and executes guest 68000 routines directly**, with
no 68000 interpreter in the loop for the translated entries.

## How it works

`tools/make_pure_c_runtime.py` takes a cleaned Ghidra translation unit and
emits (under ignored `local/decomp/`):

- bindings mapping every address-named global to a flat 24-bit guest memory
  array (`g_mem`), and every Ghidra implicit register local (`unaff_A4`,
  `extraout_D1w`, ...) to a backing slot that is synced with `ng_cpu` at each
  dispatch call;
- a `switch (pc)` dispatch table mapping every guest routine address to the
  corresponding translated C function;
- the cleaned C itself with control-flow labels separated from data labels.

`tools/build_pure_c_runtime.py` drives the whole pipeline: generate, compile,
auto-stub called-only routines that are outside the translated set, and run a
smoke test that checks a leaf routine executes and mutates guest RAM.

`tools/pure_c_state.h`, `pure_c_helpers.c`, `pure_c_main.c` are the small,
game-independent runtime scaffold (committed tooling; all generated code stays
under ignored `local/`).

## Measured result (2026-11-19)

| Set | Dispatch entries | Build | Smoke |
|---|---|---|---|
| 92 WRAM routines | 65 | OK | `0x100130` runs, `hits=1`, guest RAM hash changes |
| All profiled routines | 2,096 | OK (137 called-only stubs) | `0x10074A` runs, `hits=1` |

`tests/test_pure_c_runtime.py` is a ROM-free regression that generates, builds
and runs a tiny synthetic unit; registered as ctest `pure_c_runtime`.
**41/41 host tests pass.**

## What is NOT yet playable

This is a real execution milestone, not a playable port. Missing before it can
replace the static recomp in a running game:

1. **Called-only routines**: 137 guest routines referenced by the translated
   code have no translated body in the profiled set and are currently stubbed.
   Full coverage requires decompiling the remaining routines / dispatch entries.
2. **Hardware bus**: guest code writes to LSPC/palette/IO addresses in `g_mem`;
   there is no device model translating those into frames or sound yet.
3. **Boot state**: the smoke test starts from zeroed RAM/registers. A real run
   needs a checkpointed boot state (the host oracle already has the machinery).
4. **Frame presentation and input**: no pad polling, VBlank timing or VDP
   output; the existing Saturn/SDL renderers still consume the interpreter's
   LSPC model, not the pure-C `g_mem` image.
5. **Equivalence**: `tools/build_pure_c_equiv.py` runs the generated C and the
   machine-generated recomp C from identical CPU/RAM state and compares guest
   RAM. Four real harness defects were found and fixed this round:
   - `ng_next_event` was left at 0, so recomp loops yielded after one instruction;
   - the recomp returns at every `bsr`, so a single call never executed callees
     (the driver now loops through all recomp resume labels until the initial
     `rts`);
   - the 0x3C0000 register region was not restored between the two runs;
   - a 32-bit binding store to `$3c0002` clobbered `$3c0004` (width is now
     inferred from the decompiled assignments).
   Thunks were also renamed by target address while call sites use the thunk's
   own address; the cleaner now emits address aliases, and de-duplication no
   longer drops distinct thunks that share a callee.

   Measured on the 33 WRAM routines both implementations cover:
   `15 MATCH, 0 MISMATCH, 17 crash, 1 timeout`. The 17 crashes and the `$1008A6`
   timeout were **not** hidden. `--include-stack` (compare ABI stack-spill bytes
   too) gives `13 MATCH, 2 MISMATCH` — the two differences are `move.w Dn,-(A7)`
   spills that the decompiled C abstracts away; the default excludes that
   window explicitly via `EXCLUDE_STACK`.

6. **Not-comparable routines / real checkpoint state**: the synthetic
   `set_state()` vector fills WRAM with PRNG data, so pointer fields become
   invalid. A `--state safe` variant (zeroed RAM, registers in WRAM) was
   measured and is **worse** (`12 MATCH`), so it was not adopted. No real
   emulator checkpoint exists under `local/` (no `NGSTATE1` save; the `.kst`
   files hold LSPC raster state, not CPU/WRAM). GDB on a representative crash
   (`0x100138`, access violation) shows invalid *host* addresses produced by
   generated-C pointer arithmetic, so the 17 crashes are not solved by state
   alone: the generator still needs to distinguish guest-address arithmetic
   from host-pointer arithmetic. This remains open and is reported as such.

## Host integration (pure C inside the playable engine)

`tools/purec-host/` builds a variant of the upstream SDL player that hands a
pure-C dispatch table to the real `ng_main()` engine. The pinned reference port
is built from source but never edited; only game-derived adapter/runtime files
are generated under ignored `local/purec-host/`.

```
python tools/purec-host/build_purec_host.py
./local/build-purec/kof98_purec.exe --frames 30 --headless --rom-path roms
```

Measured with the 70-entry WRAM runtime (2026-11-19):

```
[neogeorecomp] kof98: 70 recompiled entry points
[exec] native blocks 27, interpreted instructions 182435, 53 uncovered entry points
[purec] dispatches=27 hits=27 cycles=6912
```

So generated C really executes inside the playable engine; the remaining
entry points still use the Musashi interpreter fallback. This is a prototype
bridge: mapped pages are copied word-swapped into the flat image around each
dispatch, the guest continuation is read from the state word at A4, cycles are
charged provisionally (256 per dispatch). LSPC/palette writes are now
forwarded: the bridge snapshots the 0x3C0000/0x400000 windows around each
dispatch and forwards changed words to `lspc_write()` / `pal_write()`.
Measured on the same run: uncovered entry points dropped 53 -> 51 and
interpreted instructions 182,435 -> 182,421, so device state feeds back into
the engine. This is evidence of integration, **not** a fidelity or playability
claim.

### Full-profile run (2,001 usable entries)

The 2,112-entry all-profile runtime can also drive the engine. Routines whose
Ghidra body contains an empty `do {} while(true)` (111 of them) are excluded
from the pure-C table and left to the interpreter, because they would hang.
Measured headless, 10 frames:

```
[neogeorecomp] kof98: 2001 recompiled entry points
[exec] native blocks 7920, interpreted instructions 7, 2 uncovered entry points
[purec] dispatches=7920 hits=7920 cycles=2027520
```

A 600-frame run completed with 475,200 pure-C dispatches and 7 interpreted
instructions. **This is guest execution almost entirely in generated C inside
the real player** — the closest result so far to "playable pure C" on host.

It is still not visually correct: frame 30 from the pure-C build is uniformly
(4,4,4), while the interpreter-only baseline frame 30 has 66,306 pixels of
(246,246,246). The likely cause is the prototype memory bridge: word-swapping
makes 16-bit guest reads agree but breaks the many 8-bit WRAM accesses, and
device state is only partially mirrored. Fixing that needs big-endian-neutral
accessors in the generated C, not another bridge tweak.

### Memory-bridge experiments (rejected, with evidence)

A whole-engine native-endian bus shim was attempted: `tools/shim/neogeorecomp/bus.h`
was extended to make `ng_r8/16/32` and `ng_w8/16/32` read/write flat `g_mem`,
the engine targets were given that include path first, and the adapter's page
copying was removed. Results:

- raw page copies into `g_mem`: `native blocks 0, interpreted instructions
  219191`, `dispatches=0`, and frame 30 still uniformly `(4,4,4)` — boot no
  longer reached any pure-C entry;
- word-swapped copies during page sync: immediate access violation before
  frame 30.

Cause: the engine's recompiled/interpreter code compares big-endian guest word
values against embedded numeric constants, while byte-wide guest accesses use
the opposite byte of a word after swapping. A single untyped `g_mem` cannot
satisfy both; the fix has to be explicit endianness-aware accessors at every
generated memory access, not a bridge-level copy. The experiment was reverted.

Charging 8 cycles per dispatch instead of 256 hit the 500,000-dispatch cap
before frame 30, confirming the provisional 256 charge is needed for frame
progression.

### Root cause of the dark frame (measured, 2026-11-20)

Dispatch/PC tracing in the host adapter found the actual blocker:

```
[pcseq] 1 C116AA ... [pcseq] 40 C116AA     (first 40 dispatches, identical)
[bridge] next=F3000010 valid=0             (A4 state word is garbage for this BIOS routine)
[ret] a7=10F2FC ret=102600C1               (guest stack has no valid return address)
```

Ghidra's C abstracts the 68000 register ABI: a `rts` becomes a plain C
`return`, and only the WRAM state-machine routines store their continuation in
the A4 state word. BIOS/leaf routines do neither, so the generated C never
communicates a next PC to the engine. The bridge's "7920 native blocks in 10
frames" are repeated dispatches of the same entry, not forward progress — the
guest is stuck before the boot flash at frame 15. This also explains why the
flat image's WRAM hash is constant across frame counts while the interpreter
build's changes every frame.

Consequently the frame divergence is **not** primarily a byte-order problem:
the required work is to model control-flow exits (`rts`/`jmp`/branch targets
and flags) in the generated C so the dispatcher can advance, before or together
with any endianness accessor conversion.

An automated bounded experiment excluded stuck entries one at a time so the
interpreter would handle them:

```
iter=0 stuck C116AA   native 12672
iter=1 stuck C116D0   native 12671
iter=2 stuck C16CEC   native 11800
iter=3 stuck C16D20   native 11799
iter=4 crash (access violation), no rendered frame
```

The four exclusions are BIOS thunks/leaf routines that do not propagate a PC.
Excluding them does not converge: more such routines appear and the run
eventually crashes. This confirms that a per-routine exclusion list is not a
fix; control-flow exits must be modelled in the translation itself.

A partial big-endian accessor conversion was also attempted: `G8/G16/G32`
loads and `S8/S16/S32` stores for explicit-cast derefs, plus a big-endian bus
shim for the equivalence harness. It produced only `7 MATCH, 16 MISMATCH`,
because the generated C still contained 57 raw cast derefs, 193 typed register
derefs and 50 typed-pointer subscripts that kept native-endian semantics. A
mixed model is invalid; the conversion must cover **every** guest access
(binding macros, typed pointer locals, subscripts and pointer arithmetic) before
it can be measured. The partial pass was reverted.

The validated word-swapped bridge was restored: 7,920 dispatches in 10 frames,
equivalence `18 MATCH, 1 MISMATCH, 13 crash, 1 timeout`, 41/41 host tests,
purity gate PASS.

Until items 1-6 and the memory-model gap are closed, the static recomp remains
the executable backend; the pure-C runtime is a proven, compilable and runnable
alternative path.
