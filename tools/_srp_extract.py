#!/usr/bin/env python3
"""One-shot SRP extraction of the host profiler out of ng_boot.c (local tool)."""
from pathlib import Path

p = Path("src/ng/ng_boot.c")
s = p.read_text()

start_marker = "/* Called for every instruction by m68ki_instr_hook (compile-time hook). */\n#ifdef NG_BOOT_PC_PROFILE\n"
end_marker = "#endif /* NG_BOOT_PC_PROFILE */\n"
i = s.index(start_marker)
j = s.index(end_marker, i) + len(end_marker)
block = s[i:j]

# Build the profiler module: reuse the exact block, rename prof_record -> the
# module-visible ng_profile_record, and add the required includes/prototype.
module = (
    "/* ng_boot_profile.c - host-only guest PC profiler.\n"
    " * Extracted from ng_boot.c (SRP): the emulated machine and its scheduler\n"
    " * do not depend on profiling. Only built for NG_BOOT_PC_PROFILE hosts.\n"
    " */\n"
    "#include \"ng_boot_internal.h\"\n\n"
    + block.replace("static void prof_record(uint32_t pc) {", "void ng_profile_record(uint32_t pc) {")
)
Path("src/ng/ng_boot_profile.c").write_text(module)

# ng_boot.c: drop the block, redirect the hook, include the internal header.
s = s[:i] + s[j:]
s = s.replace("    prof_record(pc);", "    ng_profile_record(pc);")
s = s.replace('#include "ng_boot.h"\n', '#include "ng_boot.h"\n#include "ng_boot_internal.h"\n', 1)
p.write_text(s)

print("extracted profiler block:", len(block), "bytes")
print("ng_boot.c now:", len(s), "bytes")
