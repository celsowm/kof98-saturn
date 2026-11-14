# Host integration of paged dispatch (X0/X1)

Status: source-level integration, **not yet compiled against upstream ROM-generated code**. This is a host oracle comparison only. It preserves upstream Musashi, CPU, IRQ, bus, LSPC/video, audio and verification modules. Only lookup and miss-address indexing are changed. A host-only 328 KiB miss bitmap and 256 KiB miss list are intentionally retained. The target runtime must not link them. A miss-log overflow is explicitly reported and the dump is refused; upstream main currently ignores the return code of exec_dump_misses, so treat the diagnostic as failure.

## Local build

Use a clean, pinned checkout of KOF98 Recomp plus initialized submodules, and your legally obtained extracted ROM directory. Do not put derived output in the repository. Configure separate build trees for the dense upstream oracle and this candidate.

```sh
git clone --recurse-submodules https://github.com/danilopaulinodasilva/kof98recomp deps/kof98recomp
git -C deps/kof98recomp checkout 44f873a3cca3827a48fca30fa8091da746311af0
git -C deps/kof98recomp submodule update --init --recursive
cmake -S . -B build-host-paged -DCMAKE_BUILD_TYPE=Release \
  -DKOF_BUILD_HOST_ORACLE=ON \
  -DKOF_UPSTREAM_DIR="$PWD/deps/kof98recomp" -DKOF_ROM_DIR="$PWD/roms"
cmake --build build-host-paged --target kof_host_paged --parallel
./build-host-paged/kof_host_paged --rom-path "$PWD/roms" --headless --verify \
  --frames 4400 --input deps/kof98recomp/tests/start_fight.txt
```

Pass -DKOF_ENTRIES=/absolute/path/to/profiled/entries.txt to both candidate and original configure (upstream uses the same variable name). The adapter generates exec_paged.c in the build only, with strict anchor checks; no upstream source tree is edited. The unmodified ng_main passes a NULL legacy table plus the real count to the patched exec_init; that private contract is explicitly validated along with bank count and paged arrays. The original table is an intermediate generator output, not a source of the candidate executable. Both legacy and paged data exist only on the host filesystem; no two tables are linked.

Acceptance before promoting:
1. Verify original dense host and candidate build at identical pins and -D options. Save local logs, binary hashes and size/map. Verify native and interpreted block counts and no mismatch/overflow.
2. Compare original and candidate with identical inputs, at least start_fight, play_long, all mode scripts and boss coverage. Retain original guest clock and 4:3 mode; capture same-tick frames and state. Compare --dump-misses outputs (raw tagged addressing and BIOS mirror behavior). Test --interp control separately.
3. Inspect symbols to ensure ng_recomp_table and former dense arrays are absent from candidate and that no legacy allocations remain; validate actual generated counts <= 65,535.
4. Do not infer SH-2 speed or size from these runs. Next work: bounded target diagnostics, compact interpreter, cycle accounting and ROM data placement.

No upstream clone or cross-compiler is available in the current execution environment, and no local ROMs were supplied. The actual integration build and oracle runs remain open. ROM-free tests cover source transformation and table parsing but cannot establish game equivalence. Do not commit generated C, captures, ROM data or traces.
