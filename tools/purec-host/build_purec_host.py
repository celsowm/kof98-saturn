#!/usr/bin/env python3
"""Build a host KOF98 player whose guest routines run as generated pure C.

The pinned upstream engine (deps/kof98recomp) is built from source but never
modified. This script only emits a game-derived adapter + runtime under the
ignored local/purec-host/ directory, then configures/builds a variant of the
player that hands a pure-C dispatch table to ng_main().

The adapter is a prototype bridge, not a correctness claim:
  * WRAM is copied to/from the engine's big-endian WRAM around each dispatch;
  * 0x3C0000 device writes stay in the flat image and are not yet forwarded
    to LSPC/palette/IO;
  * the guest continuation is read from the state word at A4 after a dispatch;
  * cycles are charged provisionally (PUREC_CYCLES_PER_DISPATCH).
"""
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
GEN = ROOT / "local" / "purec-host"
NG = ROOT / "deps" / "kof98recomp" / "neogeorecomp"

MAIN = r'''
/* Host entry point: the real upstream engine with a pure-C guest table. */
#include <neogeorecomp/neogeo.h>
#include <stdio.h>

extern const ng_func_entry_t purec_table[];
extern const size_t purec_count;
uint64_t purec_hits, purec_dispatches, purec_cycles;

extern const ng_game_t k_kof98;

int main(int argc, char **argv) {
    int rc = ng_main(argc, argv, &k_kof98, purec_table, purec_count);
    fprintf(stderr, "[purec] dispatches=%llu hits=%llu cycles=%llu\n",
            (unsigned long long)purec_dispatches, (unsigned long long)purec_hits,
            (unsigned long long)purec_cycles);
    return rc;
}
'''


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runtime", default=str(ROOT / "local" / "decomp" / "wram_runtime.c"))
    ap.add_argument("--build-dir", default=str(ROOT / "local" / "build-purec"))
    ap.add_argument("--no-build", action="store_true")
    a = ap.parse_args()

    runtime = Path(a.runtime)
    GEN.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(runtime, GEN / "purec_runtime.c")
    # The generated runtime includes its own <name>_runtime.h header.
    for hdr in runtime.parent.glob(runtime.stem + "*.h"):
        shutil.copyfile(hdr, GEN / hdr.name)
    for stub in runtime.parent.glob(runtime.stem.replace("_runtime", "") + "*_stubs.c"):
        shutil.copyfile(stub, GEN / stub.name)
    (GEN / "main.c").write_text(MAIN, encoding="utf-8")

    txt = runtime.read_text(errors="replace")
    dm = re.search(r"\bint\s+(\w+_runtime_dispatch)\s*\(", txt)
    if not dm:
        print("no runtime dispatch symbol found", file=sys.stderr)
        return 2
    dispatch_fn = dm.group(1)
    addrs = sorted(set(int(x, 16) for x in re.findall(r"case 0x([0-9A-Fa-f]+)u:", txt)))
    if not addrs:
        print("no dispatch entries in runtime", file=sys.stderr)
        return 2

    lines = [
        "/* Generated host adapter: pure-C dispatch as ng_func_t entries. Local only. */",
        "#include <neogeorecomp/neogeo.h>",
        "#include <neogeorecomp/bus.h>",
        "#include <stdint.h>",
        "#include <string.h>",
        "",
        "uint8_t g_mem[0x1000000];",
        f"int {dispatch_fn}(uint32_t pc);",
        "extern uint64_t purec_hits, purec_dispatches, purec_cycles;",
        "",
        "/* Copy every mapped engine page into the flat native-endian image the",
        " * generated C uses. Word-swap so a 16-bit guest word reads back with",
        " * the same numeric value (8-bit accesses are a known limitation). */",
        "static void mapped_to_gmem(void) {",
        "    for (unsigned p = 0; p < 256; p++) {",
        "        uint8_t *src = ng_rmap[p];",
        "        if (!src) continue;",
        "        uint8_t *dst = g_mem + (p << 16);",
        "        for (unsigned i = 0; i < 0x10000; i += 2) {",
        "            dst[i] = src[i + 1]; dst[i + 1] = src[i];",
        "        }",
        "    }",
        "}",
        "static void gmem_to_mapped(void) {",
        "    for (unsigned p = 0; p < 256; p++) {",
        "        uint8_t *dst = ng_wmap[p];",
        "        if (!dst) continue;",
        "        uint8_t *src = g_mem + (p << 16);",
        "        for (unsigned i = 0; i < 0x10000; i += 2) {",
        "            dst[i + 1] = src[i]; dst[i] = src[i + 1];",
        "        }",
        "    }",
        "}",
        "",
        "#define PUREC_CYCLES_PER_DISPATCH 256",
        "",
    ]
    for addr in addrs:
        lines += [
            f"static void purec_{addr:06X}(void) {{",
            "    purec_dispatches++;",
            "    mapped_to_gmem();",
            f"    int hit = {dispatch_fn}(0x{addr:06X}u);",
            "    if (hit) purec_hits++;",
            "    /* The decompiled C writes the guest continuation into the",
            "     * state word at A4; feed it back to the engine dispatcher. */",
            "    if (hit) CPU.pc = *(uint32_t *)(g_mem + (CPU.a[4] & 0xFFFFFF));",
            "    gmem_to_mapped();",
            "    ng_cycles += PUREC_CYCLES_PER_DISPATCH;",
            "    purec_cycles += PUREC_CYCLES_PER_DISPATCH;",
            "}",
            "",
        ]
    lines += ["const ng_func_entry_t purec_table[] = {"]
    for addr in addrs:
        lines.append(f"    {{ 0x{addr:06X}u, purec_{addr:06X} }},")
    lines += ["};", "const size_t purec_count = sizeof(purec_table) / sizeof(purec_table[0]);", ""]
    (GEN / "purec_adapter.c").write_text("\n".join(lines), encoding="utf-8")
    print(f"generated adapter for {len(addrs)} pure-C dispatch entries")

    if a.no_build:
        return 0
    r = subprocess.run(["cmake", "-S", str(TOOLS / "purec-host"), "-B", a.build_dir,
                        "-DCMAKE_BUILD_TYPE=Release"], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-2000:], file=sys.stderr)
        return r.returncode
    r = subprocess.run(["cmake", "--build", a.build_dir, "-j", "4"],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-3000:], file=sys.stderr)
        return r.returncode
    print("host pure-C player built at", Path(a.build_dir) / "kof98_purec.exe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
