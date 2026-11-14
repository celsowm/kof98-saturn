# X2 — bounded CFG region sharing

Experimental candidate. Not enabled by default. The previous exact suffix pass is independently selectable for A/B comparison.

## Algorithm

The pinned generator emits instruction labels, an entry switch and inline C control flow. Overlapping routines need not be suffixes. This pass normalizes lexical fallthrough: append an explicit goto to the original next label (or return at the end) after each original instruction fragment. This may add unreachable edges, which the C optimizer can remove; it deliberately does not guess which fragments terminate.

Merge two regions only if they share a guest address and ALL overlapping instruction fragments, including the explicit continuation, are exactly equal. Keep the original bank context; never merge across banks. Union the unique labels and original entry cases and redirect the existing dispatch keys to the retained root. No change to emitted cycle arithmetic, IRQ yielding, guest PC writes, exceptions, or original call/return boundaries. Nonmerged routines remain byte-for-byte at fragment level.

This is bounded region sharing, not a globally minimal CFG, not arbitrary semantic equivalence, and not cross-bank deduplication. The default limits (512 instruction labels, 128 entry cases, 8 indexed roots per signature, 32 candidate roots per routine) are conservative heuristic choices to constrain compilation and lookup cost. Oversized routines and conflicts remain unchanged. The pass may miss legal sharing opportunities when syntactically different fallthroughs are unreachable.

## Reproduce

Use a separate host build tree with KOF_BUILD_HOST_ORACLE=ON and KOF_DEDUP_CFG=ON. KOF_DEDUP_SUFFIX and KOF_DEDUP_CFG are mutually exclusive. The transform runs on local ROM-derived generated C, then packs the new dispatch table. Neither original nor transformed game code is committed.

Manual: python3 tools/dedup_routines.py <raw-generated-dir> <new-dir> --mode cfg

The local report records original/retained routines, instruction label counts and preserved dispatch count. It does not measure compiled text size.

## Evidence

Six unit tests actually executed with Python and a local C compiler (-std=c11 -O2): all passed. Synthetic non-suffix overlap: 2 routines / 9 labels became 1 routine / 6 labels, preserving 5 entry cases. Compared 1,280 execution results (256 input values x 5 entries), including both sides of a conditional branch, PC, accumulator and cycles, with zero differences. Rejects conflicting instruction bodies, conflicting lexical continuations, different bank contexts and configured size limits. Also checks malformed/noncanonical labels. The tests exercise the standalone CFG algorithm, not the actual ROM-generated parser/build integration.

## Promotion and next work

Use the same original/paged/suffix/CFG host differential scripts and --verify with local AES dumps. Require exact dispatch key coverage and compare same-tick logic/misses, especially loop resumes, interrupts and bank switching. Cross-compile identical inputs under -Os/-O2/-O3, collect per-unit text/rodata and link map, report switch costs and timing on SH-2; select a candidate only with evidence. Do not infer 33% smaller game binaries from the toy fixture. The current environment cannot resolve github.com for cloning, has no sh2eb-elf compiler or local ROM inputs; full builds and real workload equivalence remain open.

Next: profile real instruction mix and code residency. Before implementing flag liveness or cycle aggregation, establish the observable flag/event dependencies and verification gates. If generated code still cannot fit internal WRAM, evaluate hot native code plus cold interpreter/overlays instead of prematurely assuming full AOT fits.
