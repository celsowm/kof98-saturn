#!/usr/bin/env python3
"""Derive guest bsr return addresses from machine-generated recomp C.

Each recompiled `bsr` is emitted as:
    { NG_INSN(18); ng_push32(0xRETURNu); CPU.pc = 0xTARGETu; return; }
so the return address is explicit. The generated manifest maps
(caller routine, callee target) -> return PC, which is exactly what a
control-flow-exact pure-C translation needs to push before a guest call
and to pop on rts.

Output is game-derived and stays under ignored local/purec-host/.
"""
import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT = ROOT / "local" / "ng_recomp_full256"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", default=str(DEFAULT))
    ap.add_argument("--out", default=str(ROOT / "local" / "purec-host" / "exit_manifest.c"))
    a = ap.parse_args()

    entries = []
    for path in sorted(Path(a.src).glob("recomp_*.c")):
        text = path.read_text(errors="replace")
        for m in re.finditer(r"ng_push32\(0x([0-9A-Fa-f]+)u\);\s*CPU\.pc = 0x([0-9A-Fa-f]+)u",
                             text):
            ret, target = int(m.group(1), 16), int(m.group(2), 16)
            entries.append((target, ret))

    # Key by callee target so a generated call site can find its return PC.
    # Ambiguous targets (multiple call sites) are reported, not guessed.
    from collections import defaultdict
    by_target = defaultdict(set)
    for target, ret in entries:
        by_target[target].add(ret)
    unique = sorted((t, next(iter(rets))) for t, rets in by_target.items() if len(rets) == 1)
    ambiguous = {t: rets for t, rets in by_target.items() if len(rets) > 1}

    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "/* Generated guest bsr return manifest. Game-derived, local only. */",
        '#include <stdint.h>',
        "",
        "typedef struct { uint32_t callee; uint32_t ret; } purec_bsr_t;",
        "const purec_bsr_t purec_bsr[] = {",
    ]
    for target, ret in unique:
        lines.append(f"    {{ 0x{target:06X}u, 0x{ret:06X}u }},")
    lines += ["};",
              f"const unsigned purec_bsr_count = {len(unique)};", ""]
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"bsr sites: {len(entries)}, unique callees: {len(by_target)}, "
          f"unambiguous: {len(unique)}, ambiguous: {len(ambiguous)}")

    # Fall-through successor for each resume label: the next label emitted in
    # the same recompiled routine. This is the guest PC a decompiled fragment
    # should install when its C body returns without an explicit state update.
    succ = {}
    for path in sorted(Path(a.src).glob("recomp_*.c")):
        text = path.read_text(errors="replace")
        for block in re.split(r"(?=void r_[0-9A-Fa-f]+\(void\))", text):
            labels = re.findall(r"^L_([0-9A-Fa-f]{6}):", block, re.M)
            for i, lab in enumerate(labels[:-1]):
                succ.setdefault(int(lab, 16), int(labels[i + 1], 16))
    hdr = Path(a.out).with_suffix(".h")
    h = [
        "/* Generated guest PC fall-through map. Game-derived, local only. */",
        "#pragma once",
        "#include <stdint.h>",
        "typedef struct { uint32_t pc, next; } purec_succ_t;",
        "extern const purec_succ_t purec_succ[];",
        f"extern const unsigned purec_succ_count;",
        "",
    ]
    hdr.write_text("\n".join(h), encoding="utf-8")
    out2 = Path(a.out).with_name("successors.c")
    sl = ["#include \"exit_manifest.h\"", "const purec_succ_t purec_succ[] = {"]
    for pc, nxt in sorted(succ.items()):
        sl.append(f"    {{ 0x{pc:06X}u, 0x{nxt:06X}u }},")
    sl += ["};", f"const unsigned purec_succ_count = {len(succ)};", ""]
    out2.write_text("\n".join(sl), encoding="utf-8")
    print(f"fall-through successors: {len(succ)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
