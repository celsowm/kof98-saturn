#!/usr/bin/env python3
"""Emit a fixed-size, zeroed game map for pass-1 of the rawcode 2-pass build."""
import sys
from pathlib import Path

n = int(sys.argv[1]) if len(sys.argv) > 1 else 4166
out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("local/ng_recomp_game256/stub_map.c")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(
    "/* Stub game map for pass-1 build; real map is loaded from disc at boot. */\n"
    '#include "game_recomp_pages.h"\n'
    "const ng_gamemap_page_entry_t g_game_map[%d];\n" % n
    + "const unsigned g_game_map_count = %d;\n" % n
)
print("[stub_map] %d entries x 12 B = %d B -> %s" % (n, n * 12, out))
