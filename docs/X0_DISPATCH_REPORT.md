# X0: compact dispatch experiments

Status: ROM-free host proof; target execution/size gate is open.
The 4 MiB cartridge remains mandatory and is reserved for data, not executable code.

## Design

Keep baseline sorted sparse entries for comparison. The new page directory contains 1,313 uint16 indices (1,312 guest pages + terminal sentinel), followed by separate arrays of uint16 page offsets and native function pointers. Page order: fixed, bank 0, BIOS, banks 1–3. BIOS mirrors and odd-address normalization match existing dispatch policy; bus exception semantics remain separate. Search is limited to one page; no heap. Build helper is for preparation/host tests. The target must consume generated arrays directly, not retain both representations or allocate the baseline table at boot. Validate table once before use; keep immutable over lifetime. A table bigger than 65,535 entries is explicitly rejected; actual boss/expanded coverage may require wider indices or multiple groups. Overlay generation invalidation is not yet implemented.

Estimated SH-2 tables only, with 57,972 entries:

| Scheme | Bytes | KiB |
|---|---:|---:|
| Dense | 10,747,904 | 10,496 |
| Sparse | 463,776 | 452.91 |
| Paged | 350,458 | 342.24 |

Paged saves 113,318 B vs sparse. This excludes executable code, data, stack, array alignment and view descriptor. Real entry count/distribution comes from local game dumps, not this test.

## Host experiment

Synthetic uniform deterministic keys spanning all guest regions, alternating two function pointers. Exhaustively compared 2,686,976 canonical word addresses against sorted sparse lookup, 57,972 hits, all equal. Also checked BIOS mirror, bank and odd-address semantics, invalid ranges, capacity and malformed table. No SNK input used.

Local Linux C compiler, C11, -O2 -Wall -Wextra -Wpedantic -Werror. Three runs of 2,000,000 identical synthetic pseudorandom queries per representation:

| Run | Sparse CPU seconds | Paged CPU seconds |
|---|---:|---:|
| 1 | 0.198715 | 0.083537 |
| 2 | 0.193468 | 0.079294 |
| 3 | 0.187865 | 0.079447 |

All checksums 64744. These host times include key generation; fixed ordering and synthetic distribution limit conclusions. They are not SH-2 timings or real hit frequency. No actual lookup workload measured.

## Reproduction

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/test_paged_dispatch
python3 tools/dispatch_budget.py

Run the isolated target size/disassembly probe with a working SH-2 compiler:
sh tools/sh2_size_probe.sh

The container used for this experiment has no sh2eb-elf compiler, local ROMs, or network access for cloning. GitHub updates use the repository connector. The local tests used the same algorithm as the submitted files; repository CI is configured but has not been observed. Target size/cycles, installed SDK linkage, actual ROM-generated tables, and integrated oracle remain unverified. Next: automate local upstream generation and transform actual sorted table to paged arrays offline; benchmark real recorded hits/misses; build LibSaturn-linked FRT microprobe and qualify on Ymir and physical hardware.
