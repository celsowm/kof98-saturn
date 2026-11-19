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

Until items 1-6 are closed, the static recomp remains the executable backend;
the pure-C runtime is a proven, compilable and runnable alternative path.
