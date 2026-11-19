#!/usr/bin/env python3
"""Regenerate and verify the complete pure-C decompilation deliverable.

Takes the Ghidra pseudocode dumps under local/decomp/ (generated offline from
local ROM dumps), cleans them into ordinary C11, compiles every translation unit
with NO error suppression, and writes a manifest. All inputs and outputs stay
under ignored local/. This is the single reproducible conclusion gate for the
decomp-first track.
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from clean_ghidra_c import clean  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT / "local" / "decomp"

SETS = [
    ("wram92.c.txt", "wram92_clean.c", "wram"),
    ("hot94b.c.txt", "hot94_clean.c", "kof"),
    ("all_exec.c.txt", "all_exec_clean.c", "kof"),
]


def write_set(src_name: str, out_name: str, prefix: str) -> dict:
    src = DECOMP / src_name
    out = DECOMP / out_name
    if not src.exists():
        return {"source": src_name, "error": "missing (run Ghidra pipeline first)"}
    body = clean(src.read_text(errors="replace"), prefix)
    header = (
        "/* Machine-cleaned Ghidra pseudocode. Game-derived, local only. */\n"
        "#include <stdint.h>\n#include <stdbool.h>\n"
        f'#include "{out.with_suffix(".h").name}"\n\n'
    )
    out.write_text(header + body, encoding="utf-8")

    # Reuse the header generator from the cleaner CLI.
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "clean_ghidra_c.py"),
                        str(src), str(out), "--prefix", prefix],
                       capture_output=True, text=True)
    if r.returncode:
        return {"source": src_name, "error": r.stderr.strip()}

    defs = set(re.findall(rf"^\w+\s+{prefix}_fn_([0-9a-fA-F]+)\s*\(", body, re.M))
    trunc = src.read_text(errors="replace").count("Truncating control flow")
    return {
        "source": src_name, "output": out_name, "bytes": len(body),
        "defined_functions": len(defs),
        "raw_address_headers": src.read_text(errors="replace").count("\n===="),
        "truncated_bodies": trunc,
    }


def main() -> int:
    if not shutil.which("gcc"):
        print("gcc not found: cannot verify", file=sys.stderr)
        return 2
    manifest = {"sets": [], "verification": {}}
    rc = 0
    for src, out, prefix in SETS:
        info = write_set(src, out, prefix)
        manifest["sets"].append(info)
        if "error" in info:
            rc = 1
            continue
        v = subprocess.run([sys.executable, str(ROOT / "tools" / "verify_pure_c.py"),
                            str(DECOMP / out)], capture_output=True, text=True)
        manifest["verification"][out] = "OK" if v.returncode == 0 else v.stderr.strip()
        if v.returncode:
            rc = 1
    (DECOMP / "pure_c_manifest.json").write_text(
        json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest, indent=2))
    print("CONCLUSION:", "PASS" if rc == 0 else "FAIL")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
