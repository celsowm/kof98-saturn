#!/usr/bin/env python3
"""Generate a runnable C runtime from a cleaned Ghidra translation unit.

Produces, under local/decomp/ (ignored):

  <name>_runtime.c  - the cleaned C with register locals removed and a dispatch
                      table mapping every guest routine address to its function;
  <name>_runtime.h  - bindings: address-named globals -> flat guest memory,
                      register locals -> ng_cpu register file, function prototypes.

The generated code is ordinary C11: compile it together with tools/pure_c_main.c
to execute guest routines directly (no 68000 interpreter needed for the
translated entries). Game-derived, local only.
"""
import argparse
import re
from pathlib import Path

REG_DECL = re.compile(
    r"^\s*(?P<ty>\w+)(?P<star>\s*\*+\s*|\s+)"
    r"(?P<name>(?:unaff|extraout|in)_[A-Za-z0-9_]+)\s*;\s*$")


def reg_expr(name: str) -> str:
    m = re.match(r"(?:unaff|extraout|in)_([AD])([0-7])([bw]?)", name)
    if m:
        bank = "a" if m.group(1) == "A" else "d"
        e = f"ng_cpu.{bank}[{m.group(2)}]"
        if m.group(3) == "b":
            return f"(uint8_t)((uint32_t){e} & 0xFF)"
        if m.group(3) == "w":
            return f"(uint16_t)((uint32_t){e} & 0xFFFF)"
        return f"((uint32_t){e})"
    if name.startswith("in_stack_"):
        off = int(name.rsplit("_", 1)[1], 16)
        return f"((uint32_t)(ng_cpu.a[7] + 0x{off:X}))"
    return f"((uint32_t)ng_cpu.d[0])"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("src", type=Path)
    ap.add_argument("header", type=Path, help="original cleaned-C declarations header")
    ap.add_argument("out", type=Path)
    ap.add_argument("--prefix", required=True)
    a = ap.parse_args()

    clean_src = a.src.read_text(errors="replace")
    lines = clean_src.splitlines()
    regs = {}
    kept = []
    func_regs = []
    for line in lines:
        if re.match(r"^\w+(?:\s+\w+)*\s+\w+_\w+\s*\(", line):
            func_regs = []
        m = REG_DECL.match(line)
        if m and m.group("ty") not in ("return", "goto", "case", "if", "while"):
            name = m.group("name")
            ty = m.group("ty")
            star = m.group("star").strip()
            decl_ty = ty + (" " + star if star else "")
            is_ptr = bool(star)
            mm = re.match(r"(?:unaff|extraout|in)_D[0-7]", name)
            deref = bool(re.search(r"\*\s*" + re.escape(name) + r"\b", clean_src))
            if mm or not deref:
                is_ptr = False
            indent = line[:len(line) - len(line.lstrip())]
            # Keep the original per-function type, initialized from the
            # shared slot. A single global macro type is unsound: the same
            # register is declared with different types in different routines.
            kept.append(f"{indent}{decl_ty} {name} = ({decl_ty})(uintptr_t)purec_slot_{name};")
            func_regs.append((name, decl_ty))
            regs.setdefault(name, (ty, is_ptr))
            continue
        rm = re.match(r"^(\s*)return\s*(.*);\s*$", line)
        if rm and func_regs:
            for n, _ in func_regs:
                kept.append(f"{rm.group(1)}purec_slot_{n} = (uintptr_t){n};")
        kept.append(line)
    body = "\n".join(kept)
    # Ghidra assumes a 16-bit int, so it emits `(int32_t)ptr + offset` where
    # the original 68000 simply adds a byte displacement. On a 64-bit host the
    # int32_t cast truncates the guest-memory pointer; preserve it instead.
    for name, (_, name_is_ptr) in regs.items():
        if not name_is_ptr:
            continue
        body = re.sub(rf"\(\(\s*(?:int|uint)(?:8|16|32)?_t\s*\)\s*({re.escape(name)})\s*\+",
                      lambda m: f"((uintptr_t){m.group(1)} +", body)
    # Ghidra `LAB_` symbols are both goto labels and data addresses. Keep the
    # macro for data uses, but give control-flow labels their own C names.
    body = re.sub(rf"\bgoto\s+{a.prefix}_lbl_([0-9a-fA-F]+)",
                  lambda m: f"goto guest_lbl_{m.group(1)}", body)
    body = re.sub(rf"^(\s*){a.prefix}_lbl_([0-9a-fA-F]+)\s*:",
                  lambda m: f"{m.group(1)}guest_lbl_{m.group(2)}:", body, flags=re.M)
    # Ghidra sometimes reuses a global symbol name for a jump label; rename
    # those labels (and their gotos) so they do not collide with data macros.
    label_names = {name for _, name in re.findall(r"^(\s*)(\w+):\s*$", body, re.M)}
    macroish = {n for n in label_names
                if re.match(rf"{a.prefix}_(?:mem|ptr_mem|ptr_lbl|lbl|fn|thunk|case)_", n)}
    for n in macroish:
        body = re.sub(rf"^(\s*){re.escape(n)}\s*:", r"\1guest_label_" + n + ":",
                      body, flags=re.M)
        body = re.sub(rf"\bgoto\s+{re.escape(n)}\b", "goto guest_label_" + n, body)
        body = re.sub(rf"\bgoto\s+guest_lbl_{re.escape(n)}\b",
                      "goto guest_label_" + n, body)
    # Ghidra register locals are cast between pointer and integer types in
    # different functions; make arithmetic uses explicit.
    for name, (_, is_ptr) in regs.items():
        if is_ptr:
            continue  # casting a host pointer to uint32_t would truncate it
        body = re.sub(rf"(?<!\*)\b{re.escape(name)}\b(?=\s*(?:<<|>>|[+\-&|^]))",
                      f"((uint32_t)(uintptr_t){name})", body)
        body = re.sub(rf"(?<=[+\-/&|^])(?P<sp>\s*)\b{re.escape(name)}\b(?!\s*[=([])",
                      lambda m: m.group("sp") + f"((uint32_t)(uintptr_t){name})", body)
    body = re.sub(r'#include "[^"]*_clean\.h"',
                  f'#include "{a.out.with_suffix(".h").name}"', body)

    # Address -> function map from section comments.
    sections = re.split(r"(?=^/\* [0-9A-F]+ : )", body, flags=re.M)
    entries = []
    for sec in sections:
        h = re.match(r"/\* ([0-9A-F]+) : \w+_fn_([0-9a-fA-F]+) \*/", sec)
        fn = re.search(rf"^\w+\s+({a.prefix}_fn_[0-9a-fA-F]+)\s*\(", sec, re.M)
        if h and fn:
            entries.append((int(h.group(1), 16), fn.group(1)))
    entries = sorted(set(entries))

    # Prototypes and C macros from the original cleaned-C header. Join any
    # multi-line prototypes before filtering.
    raw = a.header.read_text(errors="replace")
    joined, pending = [], ""
    for line in raw.splitlines():
        pending = (pending + " " + line.strip()) if pending else line
        if pending.count("(") == pending.count(")"):
            joined.append(pending)
            pending = ""
    if pending:
        joined.append(pending)
    proto = []
    for line in joined:
        if (line.startswith("#define CONCAT") or line.startswith("typedef ")
                or (re.match(r"^((?:\w+[ \t]+)*\w+(?:[ \t]*\*+)?)[ \t]+\w+\s*\(", line)
                    and not re.match(r"^extern\s", line))):
            proto.append(line)
    proto = sorted(set(proto))

    hdr = ["/* Generated runtime bindings. Game-derived, local only. */",
           "#pragma once", '#include "pure_c_state.h"', ""]
    for name in sorted(regs):
        hdr.append(f"extern uintptr_t purec_slot_{name};")
    hdr += ["", "extern uint8_t g_mem[0x1000000];", ""]
    # When guest code stores the address of a global into guest memory, the
    # value must be the 24-bit guest address, not a truncated host pointer.
    def guest_addr(m):
        return str(int(m.group(1).rsplit("_", 1)[1], 16))
    body = re.sub(rf"\(uint(?:32|64)_t\)\(uintptr_t\)&({a.prefix}_mem_[0-9a-fA-F]+)",
                  guest_addr, body)
    body = re.sub(rf"\(uint(?:32|64)_t\)&({a.prefix}_mem_[0-9a-fA-F]+)",
                  guest_addr, body)
    # Address-named globals -> views onto the flat guest memory.
    mems = sorted(set(re.findall(rf"\b{a.prefix}_mem_([0-9a-fA-F]+)(?:_[bw])?\b", body)))
    for m in mems:
        addr = int(m, 16)
        hdr += [f"#define {a.prefix}_mem_{m} (*(uint32_t *)(g_mem + 0x{addr:X}))",
                f"#define {a.prefix}_mem_{m}_b (g_mem + 0x{addr:X})",
                f"#define {a.prefix}_mem_{m}_w ((uint16_t *)(g_mem + 0x{addr:X}))"]
    for p in sorted(set(re.findall(rf"\b{a.prefix}_ptr_(?:mem|lbl)_([0-9a-fA-F]+)\b", body))):
        hdr.append(f"#define {a.prefix}_ptr_mem_{p} (*(void **)(g_mem + 0x{int(p,16):X}))")
    for p in sorted(set(re.findall(rf"\b{a.prefix}_ptr_lbl_([0-9a-fA-F]+)\b", body))):
        hdr.append(f"#define {a.prefix}_ptr_lbl_{p} (*(void **)(g_mem + 0x{int(p,16):X}))")
    for l in sorted(set(re.findall(rf"\b{a.prefix}_lbl_([0-9a-fA-F]+)\b", body))):
        hdr.append(f"#define {a.prefix}_lbl_{l} (*(uint8_t *)(g_mem + 0x{int(l,16):X}))")
    hdr.append("")
    hdr.append("void caseD_0();")
    for p in sorted(set(re.findall(rf"\b{a.prefix}_ptrfn_([0-9a-fA-F]+)\b", body))):
        hdr.append(f"extern uintptr_t {a.prefix}_ptrfn_{p};")
    hdr.append("")
    for p in proto:
        hdr.append(p)
    hdr += ["", f"/* {len(entries)} guest routine entries */",
            f"extern int {a.prefix}_runtime_dispatch(uint32_t pc);",
            f"#define GUEST_RUNTIME_DISPATCH {a.prefix}_runtime_dispatch", ""]
    a.out.with_suffix(".h").write_text("\n".join(hdr) + "\n", encoding="utf-8")
    (a.out.parent / "pure_c_runtime_binding.h").write_text(
        f"#pragma once\nint {a.prefix}_runtime_dispatch(uint32_t pc);\n"
        f"#define GUEST_RUNTIME_DISPATCH {a.prefix}_runtime_dispatch\n",
        encoding="utf-8")

    # Backing slots for register locals, then a dispatcher that syncs them
    # with ng_cpu at each guest routine entry/exit.
    tail = ["\n/* Backing storage for Ghidra register locals. */"]
    for name in sorted(regs):
        tail.append(f"uintptr_t purec_slot_{name};")
    tail += [f"\n/* Generated dispatch table ({len(entries)} entries). */",
             f"int {a.prefix}_runtime_dispatch(uint32_t pc) {{",
             "    int hit = 1;"]
    sync_in, sync_out = [], []
    for name in sorted(regs):
        m = re.match(r"(?:unaff|extraout|in)_([AD])([0-7])([bw]?)", name)
        if not m:
            continue
        reg = f"ng_cpu.{'a' if m.group(1) == 'A' else 'd'}[{m.group(2)}]"
        is_ptr = regs[name][1]
        if is_ptr:
            tail.append(f"    purec_slot_{name} = (uintptr_t)(g_mem + ((uint32_t){reg} & 0xFFFFFF));")
            sync_out.append(f"    {reg} = (uint32_t)(purec_slot_{name} - (uintptr_t)g_mem);")
        else:
            tail.append(f"    purec_slot_{name} = (uintptr_t)(uint32_t){reg};")
            sync_out.append(f"    {reg} = (uint32_t)purec_slot_{name};")
    tail += ["    switch (pc) {"]
    for addr, fn in entries:
        # Ghidra recovers some stack arguments as C parameters; the dispatcher
        # supplies guest state through register slots instead, so call through
        # a generic function pointer.
        tail += [f"    case 0x{addr:06X}u: ((void (*)(void)){fn})(); break;"]
    tail += ["    default: hit = 0; break;", "    }"]
    tail += sync_out + ["    return hit;", "}", ""]
    a.out.write_text(body + "\n".join(tail) + "\n", encoding="utf-8")
    print(f"wrote {a.out} ({len(entries)} dispatch entries, {len(regs)} register locals)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
