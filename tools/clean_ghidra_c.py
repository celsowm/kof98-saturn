#!/usr/bin/env python3
"""Mechanically clean Ghidra C pseudocode into readable C.

Not a semantic decompiler: this normalises Ghidra's auto-names and types so a
human (or agent) can work on real logic. All identifiers keep their addresses,
so provenance is never lost. Output is a local build artifact, never committed.
"""
import argparse
import re
from pathlib import Path

TYPE_MAP = {
    "undefined": "uint8_t",
    "undefined1": "uint8_t",
    "undefined2": "uint16_t",
    "undefined3": "uint32_t",
    "undefined4": "uint32_t",
    "undefined8": "uint64_t",
    "byte": "uint8_t",
    "ushort": "uint16_t",
    "uint": "uint32_t",
    "int": "int32_t",
    "short": "int16_t",
    "long": "int64_t",
    "ulong": "uint64_t",
    "bool": "bool",
    "char": "int8_t",
    "int3": "int32_t",
    "uint3": "uint32_t",
}


def clean(text: str, prefix: str) -> str:
    # Drop Ghidra's noise comments.
    text = re.sub(r"/\* WARNING:.*?\*/\n?", "", text, flags=re.S)
    text = re.sub(r"/\* WARNING:.*?\*/", "", text)
    text = re.sub(r"/\* Bad instruction.*?\*/", "", text)
    # Section separators -> comments; deduplicate repeated function bodies.
    text = re.sub(r"^==== ([0-9A-Fa-f]+) : (.*?) ====$",
                  lambda m: f"/* {m.group(1)} : {m.group(2)} */", text, flags=re.M)
    seen = set()
    kept = []
    for block in re.split(r"(?=/\* [0-9A-Fa-f]+ : )", text):
        m = re.search(r"\b(?:kof|FUN)_[0-9a-fA-F]+\s*\(", block)
        if m:
            key = m.group(0)
            if key in seen:
                continue
            seen.add(key)
        kept.append(block)
    text = "".join(kept)
    text = re.sub(r"\b(?:kof|FUN)_([0-9a-fA-F]+)\b",
                  lambda m: f"{prefix}_fn_{m.group(1)}", text)
    # Stable, address-bearing names.
    text = re.sub(r"\bFUN_([0-9a-fA-F]+)\b", lambda m: f"{prefix}_fn_{m.group(1)}", text)
    text = re.sub(r"\bDAT_([0-9a-fA-F]+)(?:_([0-9a-fA-F]+)_([0-9a-fA-F]+))?\b",
                  lambda m: f"{prefix}_mem_{m.group(1)}", text)
    text = re.sub(r"\bLAB_([0-9a-fA-F]+)\b", lambda m: f"{prefix}_lbl_{m.group(1)}", text)
    text = re.sub(r"\bPTR_LAB_([0-9a-fA-F]+)\b", lambda m: f"{prefix}_ptr_lbl_{m.group(1)}", text)
    text = re.sub(r"\bPTR_DAT_([0-9a-fA-F]+)\b", lambda m: f"{prefix}_ptr_mem_{m.group(1)}", text)
    text = re.sub(r"\bsub_([0-9a-fA-F]+)\b", lambda m: f"{prefix}_fn_{m.group(1)}", text)
    # Normalise away leading zeros so a function has one canonical name.
    text = re.sub(rf"\b{prefix}_fn_0+([0-9a-fA-F]+)\b",
                  lambda m: f"{prefix}_fn_{m.group(1)}", text)
    text = re.sub(r"\bfunc_0x0*([0-9a-fA-F]+)\b",
                  lambda m: f"{prefix}_fn_{m.group(1)}", text)
    # Ghidra's other address-named globals -> one canonical namespace.
    for pat in (r"\b_DAT_([0-9a-fA-F]+)\b", r"\bDAT_([0-9a-fA-F]+)(?:_[0-9a-fA-F]+)*\b",
                r"\b[cu]Ram([0-9a-fA-F]+)\b"):
        text = re.sub(pat, lambda m: f"{prefix}_mem_{m.group(1)}", text)
    # Ghidra assigns void calls in some decompilations; drop the assignment.
    text = re.sub(rf"^(\s*)\w+\s*=\s*({prefix}_fn_[0-9a-fA-F]+)\(\);",
                  lambda m: f"{m.group(1)}{m.group(2)}();", text, flags=re.M)
    # Ghidra byte/word union accessors -> typed byte/word reads.
    def acc(m):
        base, off, width = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
        return f"{base}_b[{off}]" if width == 1 else f"{base}_w[{off // 2}]"
    text = re.sub(r"\b([A-Za-z_][A-Za-z0-9]*_mem_[0-9a-fA-F]+)\._([0-9a-fA-F]+)_([0-9a-fA-F]+)_\b", acc, text)
    text = re.sub(r"\b([A-Za-z_][A-Za-z0-9]*_ptr_(?:mem|lbl)_[0-9a-fA-F]+)\._([0-9a-fA-F]+)_([0-9a-fA-F]+)_\b",
                  lambda m: f"((uint{8 * int(m.group(3), 16)}_t *){m.group(1)})[{int(m.group(2), 16) // int(m.group(3), 16)}]",
                  text)
    # Standard fixed-width types (longest first to avoid partial matches).
    for old in sorted(TYPE_MAP, key=len, reverse=True):
        text = re.sub(rf"\b{old}\b", TYPE_MAP[old], text)
    # Normalise blank lines.
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("src", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--prefix", default="guest")
    a = ap.parse_args()
    header = (
        "/* Machine-cleaned Ghidra pseudocode. Game-derived, local only. */\n"
        "#include <stdint.h>\n#include <stdbool.h>\n"
        f'#include "{a.out.with_suffix(".h").name}"\n\n'
    )
    body = clean(a.src.read_text(errors="replace"), a.prefix)
    a.out.write_text(header + body)
    # Companion header: declare every address-named global and function so the
    # cleaned C is syntactically complete and can be compiled as a translation unit.
    mems = sorted(set(re.findall(rf"\b{a.prefix}_mem_([0-9a-fA-F]+)(?:_[bw])?\b", body)))
    ptrs = sorted(set(re.findall(rf"\b{a.prefix}_ptr_(?:mem|lbl)_([0-9a-fA-F]+)\b", body)))
    fns = sorted(set(re.findall(rf"\b{a.prefix}_fn_([0-9a-fA-F]+)\b", body)))
    hdr = [f"/* Address-named declarations for {a.out.name}. Local build artifact. */",
           "#include <stdint.h>", "#ifndef KOF_DECOMP_DECLS_H", "#define KOF_DECOMP_DECLS_H", "",
           "#define CONCAT11(a,b) ((uint16_t)(((uint16_t)(uint8_t)(a) << 8) | (uint8_t)(b)))",
           "#define CONCAT13(a,b) ((uint32_t)(((uint32_t)(a) << 16) | (uint8_t)(b)))",
           "#define CONCAT12(a,b) ((uint16_t)(((uint16_t)(uint8_t)(a) << 8) | (uint16_t)(b)))",
           "#define CONCAT21(a,b) ((uint32_t)(((uint32_t)(uint16_t)(a) << 16) | (uint8_t)(b)))",
           "#define CONCAT22(a,b) ((uint32_t)(((uint32_t)(uint16_t)(a) << 16) | (uint16_t)(b)))",
           "#define CONCAT31(a,b) ((uint32_t)(((uint32_t)(a) << 24) | (uint8_t)(b)))",
           "#define CONCAT32(a,b) ((uint32_t)(((uint32_t)(a) << 24) | (uint16_t)(b)))",
           "#define CONCAT44(a,b) ((uint64_t)(((uint64_t)(uint32_t)(a) << 32) | (uint32_t)(b)))",
           "void halt_baddata(void);",
           "typedef void (*code)(void);", "void __m68k_trap(int);", ""]
    for m in mems:
        hdr += [f"extern uint32_t {a.prefix}_mem_{m};",
                f"extern uint8_t {a.prefix}_mem_{m}_b[16];",
                f"extern uint16_t {a.prefix}_mem_{m}_w[8];"]
    for p in ptrs:
        hdr.append(f"extern void *{a.prefix}_ptr_mem_{p};")
    for p in sorted(set(re.findall(rf"\b{a.prefix}_ptr_lbl_([0-9a-fA-F]+)\b", body))):
        hdr.append(f"extern void *{a.prefix}_ptr_lbl_{p};")
    for lbl in sorted(set(re.findall(rf"\b{a.prefix}_lbl_([0-9a-fA-F]+)\b", body))):
        hdr.append(f"extern uint8_t {a.prefix}_lbl_{lbl};")
    sigs = {h: (r, p) for r, h, p in re.findall(
        rf"^(\w+)\s+{a.prefix}_fn_([0-9a-fA-F]+)\s*\(([^)]*)\)", body, re.M)}
    for f in fns:
        r, p = sigs.get(f, ("void", ""))
        hdr.append(f"{r} {a.prefix}_fn_{f}({p or 'void'});")
    hdr += ["#endif", ""]
    hdr_path = a.out.with_suffix(".h")
    hdr_path.write_text("\n".join(hdr))
    print(f"wrote {a.out} and {hdr_path} ({len(mems)} globals, {len(fns)} functions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
