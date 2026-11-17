#!/usr/bin/env python3
"""Generate a profile-pruned native guest core from already-generated routines.

This is a *selection* tool: it never recompiles 68000 bytes. It copies the
bit-exact routine bodies emitted by m68krecomp for exactly the routines in the
supplied set and rebuilds the two dispatch tables (resident + BIOS) so only
those routines are reachable. Dense handlers and the WRAM rescue routine are
linked from their hand-written sources by CMake and only their map entries are
emitted here.

All inputs/outputs are local build artifacts under ``local/``; nothing here is
committed. See docs/ARCHITECTURE_REDESIGN.md (P2).
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

DEFAULT_GAME_SRC = Path("local/ymir/resident_routines.c")
DEFAULT_GAME_MAP = Path("local/ymir/resident_map.c")
DEFAULT_DENSE = Path("src/ng/dense_routines.c")
DEFAULT_WRAM = Path("src/ng/wram_rescue_routines.c")

FUNC = re.compile(r"^void ((?:game_|bios_|wram_)?r_[0-9A-Fa-f]+|dense_r_[0-9A-Fa-f]+)\(void\)\s*\{", re.M)
ENTRY = re.compile(r"\{\s*0x([0-9A-Fa-f]+)u?\s*,\s*((?:game_|bios_|wram_|dense_r_)?[rR]?_[0-9A-Fa-f]+|dense_r_[0-9A-Fa-f]+)\s*\}")

def extract_body(text: str, name: str) -> str | None:
    # locate the exact function definition
    marker = re.search(r"void %s\(void\)\s*\{" % re.escape(name), text)
    if not marker:
        return None
    b = text.find("{", marker.start())
    depth = 0
    for k in range(b, len(text)):
        c = text[k]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[marker.start():k + 1]
    return None


def all_routines(text: str):
    return [m.group(1) for m in FUNC.finditer(text)]


def parse_entries(text: str):
    return [(int(a, 16), fn) for a, fn in ENTRY.findall(text)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--routines", required=True, type=Path, help="newline-separated kept routine names")
    ap.add_argument("--game-src", type=Path, default=DEFAULT_GAME_SRC)
    ap.add_argument("--game-map", type=Path, default=DEFAULT_GAME_MAP)
    ap.add_argument("--dense-src", type=Path, default=DEFAULT_DENSE)
    ap.add_argument("--wram-src", type=Path, default=DEFAULT_WRAM)
    ap.add_argument("--bios-src", required=True, type=Path)
    ap.add_argument("--bios-map", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()

    kept = set(l.strip() for l in a.routines.read_text().splitlines() if l.strip())
    dense_names = set(all_routines(a.dense_src.read_text())) if a.dense_src.exists() else set()
    wram_names = set(all_routines(a.wram_src.read_text())) if a.wram_src.exists() else set()

    game_text = a.game_src.read_text(errors="replace")
    bios_text = a.bios_src.read_text(errors="replace")
    sources = [("game", game_text), ("bios", bios_text)]

    # Verify every kept routine is extractable.
    bodies = {}
    missing = []
    for name in sorted(kept):
        if name in dense_names or name in wram_names:
            continue  # linked from hand-written source, do not copy
        for tag, text in sources:
            body = extract_body(text, name)
            if body:
                bodies[name] = body
                break
        else:
            missing.append(name)
    if missing:
        print("ERROR: %d kept routines not found in generated sources:" % len(missing))
        print("  " + ", ".join(missing[:20]))
        return 2

    # Map entries: keep every PC whose target routine is in the kept set.
    # Dense handlers and the WRAM rescue routine are always kept (tiny, and
    # they override generated routines at their PCs - dropping an entry could
    # turn a handled PC into a fail-loud dispatch miss).
    all_res = parse_entries(a.game_map.read_text(errors="replace"))
    resident_entries = [(pc, fn) for pc, fn in all_res
                        if fn in kept or fn in dense_names or fn in wram_names]
    resident_entries = sorted(set(resident_entries))

    bios_entries = sorted(set(
        (pc, fn) for pc, fn in parse_entries(a.bios_map.read_text(errors="replace"))
        if fn in kept
    ))

    a.out.mkdir(parents=True, exist_ok=True)

    native_lines = [
        "/* Profile-pruned native guest core. Local build artifact; do not commit. */",
        '/* Selection of m68krecomp output only - no instruction semantics changed. */',
        '#include "game_recomp_common.h"',
        '#include "bios_recomp_common.h"',
        "",
    ]
    for name in sorted(bodies):
        native_lines += [bodies[name], ""]
    (a.out / "pruned_native.c").write_text("\n".join(native_lines))

    res_lines = [
        "/* Pruned resident dispatch map (binary search). Local build only. */",
        '#include "game_recomp_pages.h"',
        "#include <neogeorecomp/neogeo.h>",
    ]
    for _, fn in resident_entries:
        res_lines.append("void %s(void);" % fn)
    res_lines += [
        "struct resident_entry { uint32_t addr; ng_func_t fn; };",
        "extern const struct resident_entry g_resident_map[];",
        "const struct resident_entry g_resident_map[] = {",
    ]
    for pc, fn in resident_entries:
        res_lines.append("    {0x%06X, %s}," % (pc, fn))
    res_lines.append("};")
    res_lines.append("const unsigned g_resident_map_count = %d;" % len(resident_entries))
    (a.out / "pruned_resident_map.c").write_text("\n".join(res_lines) + "\n")

    bios_lines = [
        "/* Pruned BIOS recomp dispatch table (binary search). Local build only. */",
        "#include <neogeorecomp/neogeo.h>",
    ]
    for _, fn in bios_entries:
        bios_lines.append("void %s(void);" % fn)
    bios_lines.append("const ng_func_entry_t bios_ng_recomp_table[] = {")
    for pc, fn in bios_entries:
        bios_lines.append("    {0x%06Xu, %s}," % (pc, fn))
    bios_lines.append("};")
    bios_lines.append("const size_t bios_ng_recomp_count = %d;" % len(bios_entries))
    (a.out / "pruned_bios_table.c").write_text("\n".join(bios_lines) + "\n")

    # Compile locally for a size report (does not affect the target build).
    import subprocess
    CC = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-gcc.exe"
    SZ = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-size.exe"
    inc = ["-Ilocal/ng_recomp_game256", "-Ilocal/ymir/c32",
           f"-I{a.bios_src.parent}", "-Ideps/kof98recomp/neogeorecomp/include"]
    obj = a.out / "pruned_native.o"
    r = subprocess.run([CC, "-m2", "-mb", "-Os", "-std=c11", "-ffreestanding", "-fno-lto",
                        *inc, "-c", str(a.out / "pruned_native.c"), "-o", str(obj)],
                       capture_output=True, text=True)
    text_size = None
    if r.returncode == 0:
        last = subprocess.run([SZ, str(obj)], capture_output=True, text=True).stdout.splitlines()[-1].split()
        text_size = int(last[0])
    else:
        print("compile WARN:", r.stderr[:400])

    manifest = {
        "kept_routines": len(kept),
        "copied_bodies": len(bodies),
        "dense_linked": sorted(dense_names),
        "wram_linked": sorted(wram_names),
        "resident_entries": len(resident_entries),
        "bios_entries": len(bios_entries),
        "native_text_size": text_size,
        "sha256": {
            p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (a.out / "pruned_native.c", a.out / "pruned_resident_map.c",
                      a.out / "pruned_bios_table.c")
        },
    }
    (a.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
