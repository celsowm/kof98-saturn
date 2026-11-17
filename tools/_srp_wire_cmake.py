#!/usr/bin/env python3
"""One-shot CMake wiring for the SRP split (local tool)."""
from pathlib import Path

p = Path("CMakeLists.txt")
s = p.read_text()

# Each key is a unique source line inside a target that defines NG_BOOT_RECOMP.
patches = [
    # host recomp, host recomp-only (identical source lines: patch both)
    ("src/host/ng_boot_host.c src/host/ng_input_script.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_live_strips.c",
     "src/host/ng_boot_host.c src/host/ng_input_script.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c src/ng/ng_live_strips.c", 2),
    # host overlay
    ("src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_live_strips.c\n    src/lspc_ir.c src/strip.c src/strip_emit.c\n    \"${mus}/m68kcpu.c\" \"${mus}/m68kdasm.c\" \"${hgen}/m68kops.c\"",
     "src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c src/ng/ng_live_strips.c\n    src/lspc_ir.c src/strip.c src/strip_emit.c\n    \"${mus}/m68kcpu.c\" \"${mus}/m68kdasm.c\" \"${hgen}/m68kops.c\"", 1),
    # saturn recomp bench
    ("src/saturn/ng_bench_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c \"${recomp_c}\" \"${recomp_tab}\"",
     "src/saturn/ng_bench_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c \"${recomp_c}\" \"${recomp_tab}\"", 1),
    # saturn rawcode bench
    ("src/saturn/ng_rawcode_bench_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c",
     "src/saturn/ng_rawcode_bench_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c", 1),
    # saturn hybrid bench
    ("src/saturn/ng_boot_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c",
     "src/saturn/ng_boot_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c", 1),
    # saturn resident bench
    ("src/saturn/resident_bench_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/libgcc_stubs.c",
     "src/saturn/resident_bench_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c src/ng/libgcc_stubs.c", 1),
]

for old, new, expected in patches:
    n = s.count(old)
    if n != expected:
        raise SystemExit(f"expected {expected} matches, found {n} for: {old[:70]}")
    s = s.replace(old, new)

p.write_text(s)
print("patched", sum(x[2] for x in patches), "recomp targets")
