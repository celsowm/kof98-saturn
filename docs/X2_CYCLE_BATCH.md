# X2 — fixed cycle/instruction counter batching

Experimental, off by default. The pinned runtime updates signed 64-bit ng_cycles and unsigned 64-bit ng_icount on every emitted instruction via NG_INSN. SH-2 is a 32-bit CPU; repeated wide arithmetic and global loads/stores may cost time and code. The compiler may already coalesce these operations. This pass is not a demonstrated target speedup.

## Contract

Recognize only exact register-only instruction forms from flag_liveness.classify and NOP. Each candidate has a fixed positive base cycle charge and no external calls except the audited inline pure register ALU helpers. The helpers do not read/write clocks, invoke the bus, or exit. No variable cycle instructions, memory operations (including ROM peeks), SR changes, exceptions, conditional branches, scheduler checks or unknown fragments are batched. Split at every switch entry (including merged suffix cases) and every explicit goto destination. Preserve original lexical fallthrough. Do not introduce new entries or change labels.

Precharge the sum of base cycles and the number of instructions at the first label in a batch; remove subsequent NG_INSN updates but keep instruction bodies/flags. Under the pinned runtime contract, no observer can see these counters inside the pure sequence. All observations at bus/exception/scheduler boundaries retain the original counts. Signed cycle overflow is outside the original runtime contract; the unsigned instruction counter retains modulo-64-bit behavior. Do not use this pass if debugging or asynchronous instrumentation observes each instruction. This is not a claim of more precise hardware interrupts than upstream.

Maximum 16 instructions per batch, a conservative experiment limit. Separate from flags and CFG experiments until proven together; may combine with suffix sharing, whose new entry cases are included in the boundary set. Generated reports count static batches and removed update pairs, not retired SH-2 instructions. All output remains local.

## Reproduce

Configure a separate host build using HOST_PAGED_ORACLE.md and -DKOF_OPT_CYCLES=ON. Or run python3 tools/dedup_routines.py RAW OUT --mode none --cycles. Do not enable KOF_OPT_FLAGS or KOF_DEDUP_CFG in this first cycle candidate.

## Evidence

Six tests passed in the local Linux container with Python and cc -std=c11 -O2 -Wall -Wextra -Wno-unused-label -Werror. The compiled fixture compares baseline and batched code across 1,024 input sets and four entry points (4,096 executions). Compared D0, D1, PC, XNZVC, both 64-bit counters and an observation digest at conditional/scheduler boundaries, including loop resumption and instruction-count carry across 32 bits. Zero differences. In the fixture two batches each combine two instructions, eliminating two static pairs of counter updates; the independent 35-NOP test forms three batches, eliminating 32 pairs. Other tests cover invalid entries, branches, barriers and maximum length. The fixture uses representative C helpers and is not the real game or Musashi.

## Promotion gates

Run the same local ROM inputs and profile list for original, paged baseline, suffix, flags and cycle candidates. Measure compiled SH-2 text/rodata and timing with -Os/-O2/-O3, compare same-tick state, IRQ/yield deadlines, bus event timestamps, --verify counts and fallback, including long waits and bank changes. Verify wide counter correctness with large initial values. Keep this option off if actual results do not improve the critical path. No local ROMs or SH-2 toolchain are available; the optional integrated generation/CMake build and actual target run are still open.

Next: establish a real hot-code profile and use code-size-aware residency. Register caching must prove all guest register/flag flushes before observable callbacks, rather than blindly hoisting globals.
