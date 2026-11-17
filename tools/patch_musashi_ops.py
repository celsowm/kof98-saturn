#!/usr/bin/env python3
"""Shrink the generated Musashi opcode tables to the one CPU type we run.

`m68kmake` emits `m68ki_cycles[NUM_CPU_TYPES][0x10000]` with NUM_CPU_TYPES 5
into the generated `m68kops.c`. The KOF98 boot config (`ng_musashi_conf_boot.h`)
disables every CPU type except the plain 68000, and `ng_boot.c` only ever calls
`m68k_set_cpu_type(M68K_CPU_TYPE_68000)`, so rows 1-4 (256 KB on the SH-2, where
WRAM-H is the binding constraint) are dead weight.

This tool rewrites exactly that one `#define` in the *generated* file inside the
build tree; it never touches the pinned upstream sources.

On a 68000-only build `m68ki_cycles` becomes 65,536 B and the runtime table
filler writes only row 0, which is the only row `CYC_INSTRUCTION` ever points
at for `M68K_CPU_TYPE_68000`.

Usage: python tools/patch_musashi_ops.py <generated m68kops.c>
"""
import argparse
import sys
from pathlib import Path

OLD = "#define NUM_CPU_TYPES 5"
NEW = "#define NUM_CPU_TYPES 1"


def patch(path: Path) -> str:
    text = path.read_text(encoding="utf-8", errors="strict")
    if NEW in text and OLD not in text:
        return "already patched"
    count = text.count(OLD)
    if count != 1:
        raise ValueError(f"expected exactly one '{OLD}', found {count}")
    path.write_text(text.replace(OLD, NEW, 1), encoding="utf-8")
    return "patched"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("ops", type=Path, help="generated m68kops.c in the build tree")
    a = p.parse_args()
    try:
        if not a.ops.is_file():
            raise ValueError(f"missing file: {a.ops}")
        print(f"m68kops: {patch(a.ops)}")
        return 0
    except (OSError, ValueError, UnicodeError) as e:
        p.error(str(e))


if __name__ == "__main__":
    raise SystemExit(main())
