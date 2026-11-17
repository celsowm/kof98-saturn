#!/usr/bin/env python3
"""ROM-free regression for tools/mk_recomp_pages.py.

Feeds the generator a synthetic m68krecomp output (two page files plus a table
with a routine that owns an extra entry point) and checks the emitted PC -> page
map: sorted, unique, every table entry covered, page numbers taken from the file
that defines the routine, and per-page costs. No ROM or generated game code is
involved.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "mk_recomp_pages.py"

PAGE0 = """\
/* routine $C00010: 3 instructions */
void bios_r_C00010(void) {
    switch (CPU.pc) {
    case 0xC00012u: goto L_C00012;
    default: ng_bad_dispatch(CPU.pc); return;
    }
L_C00010:
L_C00012:
L_C00014:
}

/* routine $C00100: 5 instructions */
void bios_r_C00100(void) {
}
"""

PAGE1 = """\
/* routine $C00200: 4 instructions */
void bios_r_C00200(void) {
}
"""

TABLE = """\
const ng_func_entry_t bios_ng_recomp_table[] = {
    {0xC00012u, bios_r_C00010},
    {0xC00010u, bios_r_C00010},
    {0xC00100u, bios_r_C00100},
    {0xC00200u, bios_r_C00200},
};
const size_t bios_ng_recomp_count = 4;
"""

ENTRY_RE = re.compile(r"^\s*\{0x([0-9A-F]{6})u, ([A-Za-z0-9_]+), (\d+)u\},$")


def run_tool(directory: Path, out_name: str = "bios_recomp_pages.h"):
    out = directory / out_name
    proc = subprocess.run(
        [sys.executable, str(SCRIPT), "--dir", str(directory), "--prefix", "bios_",
         "--out", str(out)],
        capture_output=True, text=True)
    return proc, out


def write_fixture(directory: Path, table: str = TABLE):
    (directory / "bios_recomp_00.c").write_text(PAGE0)
    (directory / "bios_recomp_01.c").write_text(PAGE1)
    (directory / "bios_recomp_table.c").write_text(table)


def parse_defs(text: str):
    entries = []
    info = []
    for line in text.splitlines():
        m = ENTRY_RE.match(line)
        if m:
            entries.append((int(m.group(1), 16), m.group(2), int(m.group(3))))
        if re.match(r"^\s*\{\d+u, \d+u\},$", line):
            m2 = re.match(r"^\s*\{(\d+)u, (\d+)u\},$", line)
            info.append((int(m2.group(1)), int(m2.group(2))))
    return entries, info


class MkRecompPagesTest(unittest.TestCase):
    def test_map_covers_every_entry_point(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            write_fixture(d)
            proc, out = run_tool(d)
            self.assertEqual(proc.returncode, 0, proc.stderr)

            header = out.read_text()
            self.assertIn("#define NG_RECOMP_PAGES 2u", header)
            # ng_func_t must be declared: the header is included from C files that
            # do not pull in the recomp runtime first.
            self.assertIn("#include <neogeorecomp/neogeo.h>", header)

            entries, info = parse_defs((d / "bios_recomp_pages.c").read_text())
            # The table has 4 entry points (one routine owns a second one), and
            # all of them must be in the map, sorted and unique.
            self.assertEqual(entries, [
                (0xC00010, "bios_r_C00010", 0),
                (0xC00012, "bios_r_C00010", 0),
                (0xC00100, "bios_r_C00100", 0),
                (0xC00200, "bios_r_C00200", 1),
            ])
            self.assertEqual(info, [(8, 2), (4, 1)])

    def test_rejects_duplicate_address(self):
        table = TABLE.replace("{0xC00100u,", "{0xC00010u,")
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            write_fixture(d, table)
            proc, _ = run_tool(d)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("duplicate", (proc.stderr + proc.stdout).lower())

    def test_rejects_unknown_routine(self):
        table = TABLE.replace("{0xC00200u, bios_r_C00200},", "{0xC00200u, bios_r_C0FFFF},")
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            write_fixture(d, table)
            proc, _ = run_tool(d)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("not in any page", proc.stderr + proc.stdout)


if __name__ == "__main__":
    unittest.main()
