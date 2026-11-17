#!/usr/bin/env python3
"""One-shot SRP extraction of the native recomp backend from ng_boot.c (local).

Keeps the device slow-write helpers in ng_boot.c (they poke LSPC/palette state)
and the hardware->page-table wiring there too (it sees the hardware arrays).
Everything else in the NG_BOOT_RECOMP block becomes ng_boot_recomp.c.
"""
from pathlib import Path

p = Path("src/ng/ng_boot.c")
s = p.read_text()

blk_start = s.index("#ifdef NG_BOOT_RECOMP\n/* ---- native recomp dispatch (X2) ----")
blk_end = s.index("#endif /* NG_BOOT_RECOMP */\n", blk_start) + len("#endif /* NG_BOOT_RECOMP */\n")
block = s[blk_start:blk_end]

# --- carve out the device slow-write helpers (keep in ng_boot.c) ---
ws = block.index("/* Ymir profile: ng_w8_slow")
we = block.index("\n\n", block.index("void ng_w16_slow")) + 2
slow_writes = block[ws:we]
block = block[:ws] + block[we:]

# --- carve out recomp_map_pages (keep in ng_boot.c, renamed) ---
ms = block.index("static void recomp_map_pages(void) {")
me = block.index("\n}\n", ms) + 3
map_fn = block[ms:me]
block = block[:ms] + block[me:]
map_body = map_fn.split("{", 1)[1].rsplit("}", 1)[0]

# recomp_init must call the ng_boot.c-owned wiring helper instead.
block = block.replace("    recomp_map_pages();", "    ng_boot_setup_recomp_maps();")

# Build the recomp backend module.
module = (
    "/* ng_boot_recomp.c - native 68000 dispatch backend.\n"
    " * Extracted from ng_boot.c (SRP): CPU state, routine lookup (resident cache,\n"
    " * BIOS table, overlay pages), exception entry and the step/init drivers are\n"
    " * the native-backend responsibility. The bus/device side stays in ng_boot.c.\n"
    " */\n"
    '#include "ng_boot_internal.h"\n'
    "#include <neogeorecomp/cpu.h>\n"
    "#include <neogeorecomp/bus.h>\n"
    "#include <neogeorecomp/neogeo.h>\n"
    "#ifndef NG_BOOT_RECOMP_ONLY\n"
    '#include "m68k.h"\n'
    "#endif\n\n"
    + block.rstrip() + "\n"
)
Path("src/ng/ng_boot_recomp.c").write_text(module)

# ng_boot.c: replace the extracted block with just the device writers + wiring.
keep = (
    "#ifdef NG_BOOT_RECOMP\n"
    + slow_writes.rstrip() + "\n\n"
    + "/* Wire the emulated hardware into the recomp backend's page tables.\n"
    " * Defined here (not in ng_boot_recomp.c) because it needs the hardware\n"
    " * arrays; ng_rmap/ng_wmap are owned by the recomp backend. */\n"
    + "void ng_boot_setup_recomp_maps(void) {" + map_body + "}\n"
    + "#endif\n"
)
s = s[:blk_start] + keep + s[blk_end:]
p.write_text(s)

print("recomp module bytes:", len(module))
print("ng_boot.c bytes:", len(s))
