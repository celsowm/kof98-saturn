# X2 experiment: exact suffix sharing

## Motivation and scope

The pinned KOF98 generator explores routines independently and therefore can emit the same guest instruction at multiple C labels in different functions. Its profiled output is reported upstream to contain around 1.56 million emitted instructions; the actual number of suffixes suitable for sharing is unknown until local generation. This pass is deliberately narrower than full CFG/basic-block canonicalization. It makes no SH-2 fit or performance claim.

The source transform reads the pinned emitter's unprefixed C files (including all 16 output units) and finds routines whose **entire ordered label/body sequence is an exact suffix of a retained routine in the same bank context**. It indexes by suffix digest, then checks full equality before sharing. A retained function gains all the removed routine's original switch entries; the dispatch table redirects the corresponding original keys. No instruction body, implicit fallthrough, exception, cycle increment, loop yield, or return boundary is rewritten. Different bank contexts are never merged, including identical byte sequences. Fixed and BIOS contexts remain separate. Unexpected output, missing labels/entries, direct calls between generated routines, malformed tables and unsupported table size all fail closed. The original generated files remain untouched.

This is not general C parsing. It supports only the pinned m68krecomp format and assumes generated routine identities are not referenced elsewhere except the table. The pinned emitter uses the dispatcher for guest calls and returns. A future emitter change requires a new audit. Larger switches and changed code layout may cost SH-2 cycles, so enable this only as an A/B candidate.

## Build

Use the configured host oracle from HOST_PAGED_ORACLE.md, but create a separate build directory and add -DKOF_DEDUP_SUFFIX=ON. The default is OFF. It generates the usual local C code, writes an independent dedup subtree, then passes its table to pack_dispatch.py. Only the selected routine files and paged table are linked. The generated dedup_report.json records routine count, label count and unchanged dispatch count; do not commit that game-derived report.

Manual use: python3 tools/dedup_routines.py build-host-paged/generated build-dedup/generated --parts 16

## Evidence from a ROM-free local C/Python fixture

Four standalone Python unittest cases passed in the local Linux environment using cc -O2, including compilation and execution of baseline and transformed C. The test fixture had three routines, seven instruction labels, three dispatch keys, and an identical-looking routine in a different bank. After transformation: two routines, five labels, all three dispatch keys; the other bank remains independent. All three entries produced identical synthetic accumulator value, next PC and cycle count in compiled host execution. Tests also reject changed bodies, non-suffix partial overlaps, malformed labels and output drift. The local run used the same transform and upstream table parser logic as committed; the CI run and optional complete host CMake configure have not been observed.

## Required promotion gates

1. Compare raw and transformed real local generation: routine/label counts, key sets, and actual host and SH-2 object/linked section sizes (-Os/-O2/-O3 with pinned tools). Label-count reduction is not binary-size reduction.
2. Compare original host, paged host and paged+dedup host using identical inputs, --verify and same-tick state; require zero divergence in covered cases and unchanged original dispatch/miss coverage. Include boot fallback, bank changes, loops yielding at IRQ deadlines, and boss/extra-entry profiling.
3. Measure runtime SH-2 hot switches and critical path, not host speed alone. Reject any unsafe or materially slower candidate. Preserve explicit fallback to the unmerged build.
4. Only after this gate consider arbitrary common basic blocks, live flags and cycle aggregation. Those require additional CFG proofs and a semantic oracle, not string equality.

No ROM-derived sources, upstream clone, sh2eb-elf compiler, emulator or physical Saturn available in this execution environment. Actual KOF savings, target timing and integration remain unmeasured.
