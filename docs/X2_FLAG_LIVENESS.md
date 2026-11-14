# X2 — local dead condition-code writes

The pinned NeoGeoRecomp CPU helpers store X/N/Z/V/C individually. The upstream C compiler already eliminates some dead stores, so explicit elimination is an experiment, not guaranteed SH-2 speed or size improvement.

## Conservative contract

The offline pass classifies only whole, exact emitted register-only C forms: MOVEQ, MOVE.L Dn,Dn, TST and CMP register forms (8/16/32), ADD.L and SUB.L Dn,Dn. All are independent of incoming CCR. Logic/compare overwrite NZVC but preserve X; add/sub overwrite all five. Backward analysis starts with every flag live at a routine exit or unknown fragment. An instruction's flag-producing helper is replaced only when ALL flags it writes are dead. Do not omit the actual register result or base cycle/instruction count. No partial-mask substitution yet. The comparison/TST may become only NG_INSN, because the only operand reads are nonvolatile CPU registers.

Memory accesses, immediate ROM reads, SR/CCR operations, ADDX/SUBX, sticky-Z BCD operations, branches, loop scheduler yields, exceptions, unknown code and any unmatched formatting are barriers. Fallthrough edges within the exact recognized instruction sequence are the only paths analyzed. A later instruction may be a separate switch entry; the earlier dead write remains irrelevant because paths that start at the later entry never execute it. The pass relies on the pinned runtime's scheduler boundary semantics (no external observation of flags within a pure register-only straight-line sequence); it is not a claim of cycle-exact asynchronous hardware interrupt timing beyond that existing contract.

Select a separate local host build with KOF_OPT_FLAGS=ON. This can be combined with suffix sharing but not CFG sharing in the first implementation: CFG normalization adds explicit edges that this lexical analysis deliberately does not model. The flags pass runs before rendering emitted C. Default is OFF. No game-derived generated files are committed.

## Validation

Six ROM-free local Python tests passed. A synthetic compiled C differential fixture ran 1,024 sets of inputs across six switch entries (6,144 comparisons), with all five incoming and outgoing flags, D0/D1, PC, cycle and instruction counters compared. Zero differences. Three of six synthetic instruction fragments lose their flag-producing helper; the final writes and the X dependency remain preserved. The tests use representative helper equations rather than compiling the actual upstream cpu.h. Additional cases cover barriers, register compare, unknown syntax, and arithmetic followed by a logic instruction that cannot kill X. The exact core/test algorithm was run locally; remote CI and complete parser/CMake integration have not yet been run.

## Promotion

Run actual local game generation; count removed flag producers, not merely stores or instructions. Compare dense upstream, paged baseline and flag-optimized candidates with --verify and deterministic full-state and bus traces, boot/BIOS, exceptions, boss and interrupt/resume coverage. Compile identical generated inputs under SH-2 -Os/-O2/-O3, compare text and rodata plus critical-path timing. Do not promote based on synthetic counts. No ROM, upstream checkout via git, or SH-2 compiler available in this environment. Actual code size and performance remain unknown.

Next: extend pure-fragment analysis to batch cycle/instruction counters with explicit split at any possible switch entry or branch target; keep all scheduler/bus/exception observation points. Measure both optimizations independently before combining.
