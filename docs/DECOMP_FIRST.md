# Decompilation-first track (2026-11-19)

Goal (user-directed): turn the KOF98 machine-generated recomp into clean,
readable C **before** further Saturn work, using REA + Ghidra.

## Tooling installed locally (not committed)

- Node.js 26.12.1 and `rea-agents` 6.9.0 CLI.
- Ghidra 12.2 (extracted to a local temp dir) with the native decompiler.
- Temurin JDK 25 (REA requires JDK 25+; Ghidra 12.x).

REA's Windows Ghidra provider refuses raw `m68k` binaries (`target_format_unsupported`:
"Windows Ghidra P0 accepts native PE, DOS MZ, and explicitly selected raw 6502
targets only"), even though REA's general spec lists m68k. So the decompilation
runs through Ghidra headless directly; REA remains available for other targets.

## Local flow

1. `tools/make_flat68k_image.py` builds `local/decomp/kof98_flat.bin`: P1 at
   `$000000`, P2 banks at `$200000`, BIOS (byte-swapped) at `$C00000`, image base 0.
2. `tools/routine_addresses.py` converts the 94-name pruned working set into
   guest addresses.
3. `tools/run_ghidra_decomp.ps1` + `tools/GhidraDecompile.java` import the raw
   image as `68000:BE:32:default`, seed functions at requested addresses, and
   export C-like pseudocode.

All ROM-derived inputs and outputs live under ignored `local/`.

## Measured result

- BIOS routine `$C11002` (the BIOS block that copies the 68000 routine into
  work RAM) decompiles cleanly: a memcpy-style loop with named temporaries.
- The 94-routine boot working set: 91/94 decompiled, 135 KB of pseudocode. Three
  addresses gave no function (`000800`, `C00402`, `100000` — the last is WRAM,
  not present in the flat ROM image).
- Many game routines truncate with `/* WARNING: Bad instruction - Truncating
  control flow here */`. Cause: the flat image has no WRAM/I/O/banked-RAM
  regions, and the guest code jumps through those; Ghidra cannot follow them.
  The existing machine-generated recomp C remains the more complete translation
  of the code.

## Conclusion so far

The decomp-first pipeline works and is reproducible, but Ghidra pseudocode from
this memory layout is only partial. Making it "pure C" at game scale would need
mapped RAM/IO regions and extensive manual typing/naming of 56k+ addresses — a
separate long-running project. No Saturn changes were made in this track.
