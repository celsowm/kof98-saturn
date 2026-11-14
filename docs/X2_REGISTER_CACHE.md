# X2 — bounded local data register caching

Experimental off-by-default source transformation. SH-2 has a small physical register file; repeatedly accessing the shared 68000 register structure could increase memory traffic. Local C variables permit the compiler to allocate guest values to host registers but do not force physical allocation. The compiler may already achieve the same optimization or spill the locals. This is not a measured Saturn speedup.

## Scope and safety

Use the exact pure register-only whitelist already audited for flag liveness. Cache D0–D7 only, maximum three selected registers and 16 instructions per lexical region. Split at every entry case (including suffix-merged entries), goto destination, unknown instruction, bus operation, status change, exception or scheduling check. Initialize local uint32 values before the first instruction in the region, preserve original bodies, cycle counters and flag helpers, write back only modified registers before leaving the scope. No edge can enter the middle of the scope because all original explicit targets and switch entries are split. Do not run this pass on CFG-normalized or other transformed bodies.

Select candidates where textual register references exceed one load plus a store if dirty. This is a static heuristic, not physical loads/stores measured on any CPU. The helpers only access flags, not guest data registers or external observers. As with existing generated-code semantics, no concurrent guest CPU observer or per-instruction debugging hook is allowed inside a pure region. A7/address register mappings, live flags in physical registers and caching across calls or bus operations are deliberately out of scope.

## Validation

Six local Python tests ran successfully (including cc -std=c11 -O2 -Wall -Wextra -Wno-unused-label -Werror). A compiled synthetic comparison exercised 1,024 inputs at four distinct entry points: 4,096 outcomes matched for D0/D1, PC, XNZVC, both wide counters and observation digest at conditional/scheduler boundaries. Tests cover dirty vs read-only locals, no-benefit regions, entry/branch targets, bounds and barriers. Three synthetic ADD.L operations have nine textual D-register references vs three explicit global accesses after caching, six fewer. This is not a native memory-access measurement. No actual KOF ROM execution or compilation of the complete integrated host CMake configuration was performed.

## Use and promotion

Configure a separate host oracle build with -DKOF_OPT_REGCACHE=ON. Manual: python3 tools/dedup_routines.py RAW OUT --mode none --regcache. May use suffix sharing; flags, cycle batching and CFG are independent experiments and cannot be combined with this pass yet. Report cached regions and estimated textual references removed in the local dedup_report.json. Keep generated artifacts local.

Compare actual ROM inputs against dense and paged baseline with --verify and same-tick state/bus traces. Inspect SH-2 assembly for register allocation, spills and prologue/epilogue, and compare text/rodata and cycles. Reject if no gain. Real ROMs and target compiler are unavailable here. Next gate: collect real dispatch profiles and size-aware native code selection; stop adding speculative transformations before obtaining actual code-size and hot-path evidence.
