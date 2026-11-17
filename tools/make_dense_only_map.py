#!/usr/bin/env python3
"""Emit a minimal resident map containing ONLY the dense handlers.

For the renderer-enabled rawcode bench, the full 371-routine resident blob
(~451 KiB) cannot coexist with the validated strip renderer in 1 MiB WRAM-H.
The dense handlers are self-contained (they call ng_w16/ng_w8/ng_w32 and the
exported device-state aliases from ng_boot.c) and cover the measured hotspots
from the Ymir SH-2 profile. This map routes just those PCs so the renderer
path still benefits from the real-time native work without the full blob.

Usage: python3 tools/make_dense_only_map.py
"""
from pathlib import Path

# (guest PC, handler) — must match the overrides in tools/make_resident_map.py
DENSE = [
    (0x000834, "dense_r_000834"),
    (0x0034EA, "dense_r_0034EA"),
    (0x00A142, "dense_r_00A142"),
    # Guest-copied WRAM routine (jsr $100000): without these entries the
    # recomp-only guest cannot execute BIOS-copied code and stalls before
    # gameplay, so the renderer never sees sprites. Small + self-contained.
    (0x100000, "wram_r_100000"),
    (0x100004, "wram_r_100000"),
    (0x100006, "wram_r_100000"),
    (0x10000A, "wram_r_100000"),
    (0x10000E, "wram_r_100000"),
    (0x00C11050, "dense_r_C11050"),
    (0x00C1108A, "dense_r_C1108A"),
    (0x00C110B4, "dense_r_C110B4"),
    (0x00C110E8, "dense_r_C110E8"),
    (0x00C11F9E, "dense_r_C11F9E"),
    (0x00C16D20, "dense_r_C16D20"),
]


def main() -> None:
    DENSE.sort()
    lines = ["/* Dense-handler-only resident map (renderer-enabled bench). Local build only. */",
             '#include "game_recomp_pages.h"',
             '#include <neogeorecomp/neogeo.h>']
    for _, fn in DENSE:
        lines.append(f"void {fn}(void);")
    lines += ["struct resident_entry { uint32_t addr; ng_func_t fn; };",
              "extern const struct resident_entry g_resident_map[];",
              "extern const unsigned g_resident_map_count;",
              "const struct resident_entry g_resident_map[] = {"]
    for pc, fn in DENSE:
        lines.append(f"    {{0x{pc:06X}, {fn}}},")
    lines.append("};")
    lines.append(f"const unsigned g_resident_map_count = {len(DENSE)};")
    Path("local/ymir/dense_only_map.c").write_text("\n".join(lines) + "\n")
    print(f"dense-only map entries: {len(DENSE)}")


if __name__ == "__main__":
    main()
