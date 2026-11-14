# X2 — profile-driven hot native / cold interpreter experiment

The full AOT program has not been shown to fit the Saturn. This candidate selects whole generated functions using measured SH-2 symbol sizes and desktop guest-instruction coverage. No executable code is placed in the mandatory 4 MiB data cartridge. The host still uses Musashi for unselected entry points and RAM code; the target's interpreter size/speed remains an independent gate.

## Collect local evidence

Use the unoptimized full paged host baseline in HOST_PAGED_ORACLE.md, without dedup/flags/cycles/regcache. The host-only profiler is opt-in via KOF_NATIVE_PROFILE (output CSV filename). Run independent boot/menu, attract, team, practice, boss and varied fight scenarios, each with a unique path. Use the original headless input scripts and verified local AES dumps.

    mkdir -p local/profiles
    KOF_NATIVE_PROFILE=local/profiles/fight.csv ./build-host-paged/kof_host_paged --rom-path "$PWD/roms" --headless --verify --frames 4400 --input deps/kof98recomp/tests/start_fight.txt

The CSV logs canonical tagged entry PC, calls, executed guest instructions and virtual cycles aggregated at native block return. It is not wall time or target cycles. RAM/interpreted work is excluded; retain original interpreter/native totals and verify output alongside it. A SHA-256 identity binds profiles and size reports to generated table, common header and routine units. It does not authenticate toolchains, ROM data not represented in those outputs, or runtime configuration; record those separately. Counters fail loudly on overflow. The hash table is desktop-only (typically 4 MiB allocated only when profiling).

    python3 tools/measure_native.py build-host-paged/generated deps/kof98recomp/neogeorecomp build-sh2-sizes --opt Os
    python3 tools/select_native.py build-host-paged/generated build-hot/generated --profile local/profiles/fight.csv --sizes build-sh2-sizes/sizes.txt --budget 524288 --reserve 65536

Add repeat --profile for independent scenarios. The budget and reserve above are example arguments, NOT the established WRAM allocation. Set them from a target link budget, including all runtime code/data, stacks and guards. Selection greedily ranks observed routines by guest instructions per estimated byte. Charge 16-byte-rounded symbol size, 6 bytes per retained dispatch entry, 2,626 bytes directory and explicit reserve. All entries belonging to a retained routine are kept. Retain nothing from unobserved routines in this first experiment; missing native entries intentionally use the interpreter. Do not feed hybrid miss dumps into generator coverage because deliberate omissions are not discovery bugs. No overlay implementation yet.

Build the hybrid host by configuring a separate tree with KOF_BUILD_HOST_ORACLE=ON plus KOF_NATIVE_PROFILES (semicolon-separated absolute CSV paths), KOF_NATIVE_SIZES (absolute sizes.txt), KOF_NATIVE_BUDGET and KOF_NATIVE_RESERVE. Exact generated-code identity must match. The command selects from the raw generator output; optimization flags are mutually exclusive for this experiment.

## Evidence and limitations

Five ROM-free local tests actually ran successfully. Compiled host C profiler with strict warnings; verified repeated key accumulation, distinct banks, BIOS mirror canonicalization and disabled profiling. Python tests checked deterministic budget selection, missing sizes, malformed/duplicate profile keys and invalid budgets. Local Python tests used stubs for unused full-generation parser/render functions and the C test used an equivalent local dispatch-key implementation. Full transformed real source/CMake integration, fingerprint-binding integration, size measurement on SH-2 and actual gameplay remain unexecuted. The new size-report identity guard was added after the isolated test run.

Symbol sizes omit shared literals, jump tables, relocation effects, runtime/interpreter text, rodata, other data, inter-section padding and target linker behavior. The estimate is not a fit guarantee. Validate the final ELF/map against WRAM partitions, measure the runtime and interpreter cost, repeat selection with calibrated reserves, and quantify held-out match coverage. Compare original/paged/hybrid at same guest tick and same events. If fallback timings or behavior diverge, retain full AOT oracle and revise the policy. Coverage percentages are only native guest instruction coverage of recorded scenarios, not total workload or predicted framerate. No ROM/trace/generated source or symbol report belongs in Git.
