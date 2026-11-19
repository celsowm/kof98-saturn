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
    "ulonglong": "uint64_t",
    "longlong": "int64_t",
    "undefined6": "uint64_t",
}


def clean(text: str, prefix: str) -> str:
    # Drop Ghidra's noise comments.
    text = re.sub(r"/\* WARNING:.*?\*/\n?", "", text, flags=re.S)
    text = re.sub(r"/\* WARNING:.*?\*/", "", text)
    text = re.sub(r"/\* Bad instruction.*?\*/", "", text)
    # Drop whole sections that failed decompilation (garbage text would leak
    # into the C otherwise).
    sections = re.split(r"(?=^==== [0-9A-Fa-f]+ : )", text, flags=re.M)
    text = "".join(s for s in sections
                   if not re.search(r"NO FUNCTION|DECOMPILE FAILED", s))
    # Ghidra thunks and leading-underscore aliases -> one function namespace.
    text = re.sub(r"\bthunk_FUN_([0-9a-fA-F]+)\b",
                  lambda m: f"{prefix}_thunk_{m.group(1)}", text)
    text = re.sub(r"\b_FUN_([0-9a-fA-F]+)\b",
                  lambda m: f"{prefix}_fn_{m.group(1)}", text)
    text = re.sub(r"\b_LAB_([0-9a-fA-F]+)\b",
                  lambda m: f"{prefix}_lbl_{m.group(1)}", text)
    text = re.sub(r"\bunique0x([0-9a-fA-F]+)\b",
                  lambda m: f"{prefix}_mem_{m.group(1)}", text)
    # Ghidra switch tables: both `switchD_x::caseD_n` definitions and
    # `switchD_x_caseD_n` goto labels become one valid C identifier.
    text = re.sub(r"\bswitchD_([0-9a-fA-F]+)\s*::\s*caseD_(\w+)",
                  lambda m: f"{prefix}_case_{m.group(1)}_{m.group(2)}", text)
    text = re.sub(r"\bswitchD_([0-9a-fA-F]+)_caseD_(\w+)",
                  lambda m: f"{prefix}_case_{m.group(1)}_{m.group(2)}", text)
    # Switch-table pointer globals -> address-named globals.
    text = re.sub(r"\bPTR_caseD_[a-z]_([0-9a-fA-F]+)\b",
                  lambda m: f"{prefix}_mem_{m.group(1)}", text)
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
    for kind in ("mem", "ptr_mem", "ptr_lbl", "lbl"):
        text = re.sub(rf"\b{prefix}_{kind}_0+([0-9a-fA-F]+)((?:_[bw])?)\b",
                      lambda m, k=kind: f"{prefix}_{k}_{m.group(1)}{m.group(2)}", text)
    text = re.sub(r"\bfunc_0x0*([0-9a-fA-F]+)\b",
                  lambda m: f"{prefix}_fn_{m.group(1)}", text)
    # Ghidra's other address-named globals -> one canonical namespace.
    for pat in (r"\b_DAT_([0-9a-fA-F]+)\b", r"\bDAT_([0-9a-fA-F]+)(?:_[0-9a-fA-F]+)*\b",
                r"\b[a-z]+Ram([0-9a-fA-F]+)\b",
                r"\bram0x([0-9a-fA-F]+)\b",
                r"\bPTR_FUN_([0-9a-fA-F]+)\b",
                r"\bPTR_PTR_([0-9a-fA-F]+)\b",
                r"\bregister0x([0-9a-fA-F]+)\b",
                r"\bPTR_\w+?_([0-9a-fA-F]{6,8})\b",
                r"\bs_\w+_([0-9a-fA-F]{6,8})\b"):
        text = re.sub(pat, lambda m: f"{prefix}_mem_{m.group(1)}", text)
    # Ghidra assigns void calls in some decompilations; drop the assignment.
    text = re.sub(rf"^(\s*)\w+\s*=\s*({prefix}_fn_[0-9a-fA-F]+)\(\);",
                  lambda m: f"{m.group(1)}{m.group(2)}();", text, flags=re.M)
    # A Ghidra function symbol is sometimes reused as a data pointer; those
    # uses get their own namespace so calls and data accesses do not collide.
    text = re.sub(rf"\b{prefix}_fn_([0-9a-fA-F]+)(?=\s*(?:\[|=[^=]))",
                  lambda m: f"{prefix}_ptrfn_{m.group(1)}", text)
    # Calls through a global held as a scalar -> explicit code-pointer cast.
    text = re.sub(rf"\(\*({prefix}_mem_[0-9a-fA-F]+)\)\(\)",
                  lambda m: f"((code){m.group(1)})()", text)
    text = re.sub(rf"\(\*\({prefix}_ptrfn_([0-9a-fA-F]+)\)\)\(\)",
                  lambda m: f"((code){prefix}_ptrfn_{m.group(1)})()", text)
    # A reused function symbol subscripted as data -> cast the scalar to a
    # code pointer before indexing.
    text = re.sub(rf"\b{prefix}_ptrfn_([0-9a-fA-F]+)\s*(?=\[)",
                  lambda m: f"((code *){prefix}_ptrfn_{m.group(1)})", text)
    # Dereferencing a scalar global as a byte pointer.
    text = re.sub(rf"\(\*({prefix}_mem_[0-9a-fA-F]+)\b",
                  lambda m: f"(*(uint8_t *){m.group(1)}", text)
    # Functions called with arguments but declared void(void): use an
    # unprototyped definition so the call type-checks (Ghidra loses args on
    # some 68000 register-ABI functions).
    called_args = set(re.findall(rf"\b{prefix}_fn_([0-9a-fA-F]+)\s*\([^)]*\w[^)]*\)", text))
    for h in called_args:
        text = re.sub(rf"\bvoid(\s+{prefix}_fn_{h}\s*)\(void\)", r"void\1()", text)
    # Ghidra's `(void)` prototypes reject calls that lost arguments in the
    # register ABI; unprototyped `()` keeps those calls valid C11.
    text = text.replace("(void)", "()")
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
    # A few Ghidra functions are typed void but used as values in callers. Give
    # those a uint32_t return so the C types agree (the value is unused by the
    # callers - we only need a compilable translation unit).
    text = fix_void_value_calls(text, prefix)
    # Ghidra treats the 68000 as having 16-bit `int`; its pseudocode therefore
    # freely mixes pointer and integer types. Make each such assignment explicit
    # with a cast (no error suppression needed at compile time).
    text = add_type_casts(text, prefix)
    # Normalise blank lines.
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text


def fix_void_value_calls(text: str, prefix: str) -> str:
    """Promote void functions that are used in value contexts to uint32_t."""
    kinds = ("fn", "thunk")
    used = set()
    for k in kinds:
        used |= set(re.findall(rf"\b{prefix}_{k}_([0-9a-fA-F]+)\s*\([^)]*\)\s*[<+\-*/&|)]",
                               text)) | set(re.findall(
            rf"[=(]\s*{prefix}_{k}_([0-9a-fA-F]+)\s*\([^)]*\)", text))
    void_defs = set()
    for k in kinds:
        void_defs |= set(re.findall(rf"^void\s+{prefix}_{k}_([0-9a-fA-F]+)\s*\(",
                                    text, re.M))
    promote = used & void_defs
    if not promote:
        return text
    blocks = re.split(r"(?=^(?:void|uint32_t)\s+\w+_(?:fn|thunk)_[0-9a-fA-F]+\s*\()", text, flags=re.M)
    out = []
    for b in blocks:
        m = re.match(r"^(?:void|uint32_t)\s+\w+_(fn|thunk)_([0-9a-fA-F]+)\s*\(", b)
        if m and m.group(2) in promote:
            b = re.sub(r"^void(\s+\w+_(?:fn|thunk)_)", r"uint32_t\1", b, flags=re.M)
            b = re.sub(r"(?m)^(\s*)return;\s*$", r"\1return 0;", b)
            if "return 0;" not in b:
                b = re.sub(r"\n}\s*$", "\n  return 0;\n}\n", b)
        out.append(b)
    return "".join(out)


def add_type_casts(text: str, prefix: str) -> str:
    """Insert explicit casts for Ghidra's loose pointer/integer assignments.

    The decompiler declares local variables with concrete C types, so we can
    use those declarations to choose the right cast at each assignment site.
    Globals are treated as integers and functions as code pointers.
    """
    lines = text.splitlines()
    types: dict[str, str] = {}
    out = []

    def decl(line: str) -> None:
        if re.match(r"\s*(?:return|goto|case|if|while|for|switch|else|break|continue)\b",
                    line):
            return
        m = re.match(r"\s*(?:extern\s+)?([\w]+)\s*(\**)\s*(\w+)\s*;", line)
        if m:
            types[m.group(3)] = m.group(1) + m.group(2)

    for line in lines:
        # Cast pointer-typed stores through `*(TYPE *)(addr) = &global`.
        m = re.search(r"(\*\(([^)]+)\)[^=,;]*=\s*)(&?[A-Za-z_]\w*)", line)
        if m:
            pre, ty, rhs = m.groups()
            if (rhs.startswith("&") or "_fn_" in rhs or "_case_" in rhs
                    or "_mem_" in rhs):
                # `*(T **)(addr) = ...` stores a T*, not a T**.
                cast_ty = ty.strip()
                if cast_ty.endswith("*"):
                    cast_ty = cast_ty[:-1].rstrip()
                line = line.replace(pre + rhs, pre + f"({cast_ty}){rhs}", 1)
        # Cast pointer stores into integer globals held in dereferenced locals.
        if re.match(r"^\s*\*\w+\s*=\s*(?:&|in_|\w+_fn_)", line):
            line = re.sub(r"^(\s*)(\*\w+\s*=\s*)", r"\1\2(uint32_t)(uintptr_t)", line)
        if re.match(r"^\w+\s+\w+_fn_[0-9a-fA-F]+\s*\(", line):
            types.clear()  # locals are function-scoped
        m = re.match(r"^(\s*)(\*?\w+(?:\[[^\]]*\])?)(\s*)=(\s*)(.+?)(\s*);(\s*)$", line)
        if not m:
            out.append(line)
            decl(line)
            continue
        indent, lhs, _, _, rhs, _, tail = m.groups()
        if "[" in lhs:
            base = types.get(lhs.split("[")[0].lstrip("*"), "uint32_t")
            ltype = base[:-1] if base.endswith("*") else base
        elif lhs.startswith("*"):
            base = types.get(lhs[1:], "uint32_t")
            ltype = base[:-1] if base.endswith("*") else base
        elif re.fullmatch(r"\w+_ptr_(?:mem|lbl)_[0-9a-fA-F]+", lhs):
            ltype = "void *"
        elif re.fullmatch(r"\w+_mem_[0-9a-fA-F]+", lhs):
            ltype = "uint32_t"
        else:
            ltype = types.get(lhs)
        if ltype is None:
            out.append(line)
            decl(line)
            continue
        pointer = "*" in ltype
        need_cast = False
        if (re.fullmatch(r"&[A-Za-z_]\w*", rhs)
                or re.fullmatch(r"\w*_(?:fn|case|lbl|ptr_mem|ptr_lbl)_[0-9a-fA-F]+(?:_\w+)?", rhs)
                or re.fullmatch(r"\w+_fn_[0-9a-fA-F]+", rhs)):
            need_cast = True
        elif (re.fullmatch(r"\w*_mem_[0-9a-fA-F]+(?:_[bw])?(?:\[[^\]]*\])?(?:\s*[+\-]\s*\w+)?", rhs)
              or re.fullmatch(r"PTR_(?:FUN|PTR)_[0-9a-fA-F]+", rhs)
              or re.fullmatch(r"ram0x[0-9a-fA-F]+", rhs)):
            need_cast = pointer
        elif re.fullmatch(r"\([A-Za-z_]\w*\s*\*\)\s*\w+", rhs):
            need_cast = not pointer
        elif rhs.startswith("&") and pointer and re.fullmatch(r"&\w+_mem_[0-9a-fA-F]+(?:\s*[+]\s*.*)?", rhs):
            need_cast = True
        elif re.fullmatch(r"\*[A-Za-z_]\w*|[A-Za-z_]\w*\[[^\]]*\]", rhs) and not pointer:
            need_cast = True
        elif re.fullmatch(r"[A-Za-z_]\w*", rhs):
            # A pointer-typed local (or a Ghidra `in_*` register parameter)
            # stored into an integer global needs an explicit narrowing cast.
            need_cast = (not pointer) and (rhs.startswith("in_")
                                           or "*" in types.get(rhs, ""))
        if need_cast:
            cast = ltype if pointer else f"{ltype})(uintptr_t"
            line = f"{indent}{lhs} = ({cast}){rhs};{tail}"
        out.append(line)
        decl(line)
    return "\n".join(out)


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
           "typedef uint32_t (*code)(void);", "void __m68k_trap(int);",
           "uint32_t bcdAdjust(int);", "void pushInvalidateCaches(int);",
           "void caseD_0();",
           "void halt_unimplemented(void);", "typedef void BADSPACEBASE;",
           "int32_t SBORROW1(int32_t,int32_t);", "int32_t SBORROW2(int32_t,int32_t);",
           "int32_t SCARRY1(int32_t,int32_t);", "int32_t SCARRY4(int32_t,int32_t);",
           "int32_t CARRY1(int32_t,int32_t);", "int32_t CARRY2(int32_t,int32_t);",
           "int32_t SUB42(void *,int32_t);",
           "#define CONCAT14(a,b) ((uint32_t)(((uint32_t)(a) << 8) | (uint8_t)(b)))",
           "#define CONCAT24(a,b) ((uint32_t)(((uint32_t)(a) << 16) | (uint16_t)(b)))",
           ""]
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
    for p in sorted(set(re.findall(rf"\b{a.prefix}_ptrfn_([0-9a-fA-F]+)\b", body))):
        hdr.append(f"extern uintptr_t {a.prefix}_ptrfn_{p};")
    thunk_defs = {h: ret for ret, h in re.findall(
        rf"^(\w+)\s+{a.prefix}_thunk_([0-9a-fA-F]+)\s*\(", body, re.M)}
    value_thunks = set(re.findall(rf"[=(]\s*{a.prefix}_thunk_([0-9a-fA-F]+)\s*\(", body))
    for t in sorted(set(re.findall(rf"\b{a.prefix}_thunk_([0-9a-fA-F]+)\b", body))):
        ret = "uint32_t" if t in value_thunks else thunk_defs.get(t, "void")
        hdr.append(f"{ret} {a.prefix}_thunk_{t}();")
    for c in sorted(set(re.findall(rf"\b{a.prefix}_case_([0-9a-fA-F]+_\w+)\b", body))):
        hdr.append(f"void {a.prefix}_case_{c}(void);")
    sigs = {h: (r, p) for r, h, p in re.findall(
        rf"^((?:\w+[ \t]+)*\w+(?:[ \t]*\*+)?)\s+{a.prefix}_fn_([0-9a-fA-F]+)\s*\(([^)]*)\)",
        body, re.M)}
    defs = set(re.findall(rf"^\w+\s+{a.prefix}_fn_([0-9a-fA-F]+)\s*\(", body, re.M))
    for f in fns:
        # Undefined (called-only) functions may be used in expressions; a
        # uint32_t default keeps those valid. Locally defined void functions
        # keep their real signature.
        r, p = sigs.get(f, ("uint32_t" if f not in defs else "void", ""))
        hdr.append(f"{r} {a.prefix}_fn_{f}({p});")
    hdr += ["#endif", ""]
    hdr_path = a.out.with_suffix(".h")
    hdr_path.write_text("\n".join(hdr))
    print(f"wrote {a.out} and {hdr_path} ({len(mems)} globals, {len(fns)} functions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
