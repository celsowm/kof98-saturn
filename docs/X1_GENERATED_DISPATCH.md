# Offline dispatch packing

The pinned upstream generator emits declarations, ng_recomp_table entries and ng_recomp_count in recomp_table.c. Its region traversal order need not be numeric key order. tools/pack_dispatch.py consumes the exact unprefixed KOF98 format, validates declarations, distinct canonical keys and count, sorts the entries, and emits immutable uint16 directory and offset arrays plus native function pointers. No runtime allocation or dense table is emitted by this tool.

Run locally after upstream recompilation:

    python3 tools/pack_dispatch.py /path/to/upstream/build/generated/recomp_table.c build/generated/kof_paged_table.c

Compile the new translation unit instead of the original recomp_table.c only in a runtime that consumes the const kof_paged_dispatch kof_generated_dispatch symbol. Keep all generated code in ignored build directories. The host oracle can continue to use the original table and binary independently. Do not compile both tables into the target.

The current upstream exec.c uses slot_in/slot both for dense lookup and for the miss recorder's address-to-bit calculation. The integration must decouple these responsibilities. Use canonical address/region arithmetic independently of function pointer storage; do not compute offsets by subtracting unrelated array pointers. Preserve raw tagged miss addresses as required by the pinned upstream generator. A desktop verification build may retain the full miss bitmap, while a size-constrained target must use a bounded miss log with explicit overflow/failure. Do not equate a missing entry with an invalid address or silently skip fallback. ng_main currently selects interpreter-only when count is zero; a paged integration must pass the actual count or redesign that contract explicitly.

## Validation and limits

Five local synthetic Python tests passed before the final linear parsing improvement (valid unsorted input, region boundaries, malformed rows, duplicates/undeclared symbols/wrong count/invalid keys, 65,536-entry rejection). The updated tests run via CTest when Python is available and via the host CI. The parser rejects tables exceeding 65,535 entries; if the complete game exceeds the limit, add a wider directory or segmented table. No actual ROM-generated file or SH-2 execution has been verified. The generated C output is not yet wired into upstream exec.c or linked into a Saturn application.

Next: implement a host differential adapter without dense allocation, exercise original scripts and misses, then separate the desktop-only diagnostic and interpreter dependencies for target compilation.
