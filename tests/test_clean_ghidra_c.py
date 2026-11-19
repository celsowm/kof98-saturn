#!/usr/bin/env python3
"""ROM-free regression for tools/clean_ghidra_c.py.

Feeds a small synthetic Ghidra snippet through the cleaner and checks that the
result is stable, address-named and - most importantly - compiles as ordinary
C11 with no error-suppression flags.
"""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from clean_ghidra_c import clean  # noqa: E402

SNIPPET = """==== 000100 : FUN_000100 ====

void FUN_000100(void)

{
  undefined4 *puVar1;
  uint bVar2;

  puVar1 = (undefined4 *)&DAT_00100000;
  *puVar1 = 0x1234;
  bVar2 = CONCAT11(1,2);
  DAT_00100004 = &DAT_00100008;
  FUN_000200();
}

==== 000200 : FUN_000200 ====

void FUN_000200(void)

{
  return;
}
"""


class TestCleanGhidraC(unittest.TestCase):
    def test_normalises_names_and_types(self):
        out = clean(SNIPPET, "t")
        self.assertIn("t_fn_100()", out)
        self.assertIn("t_mem_100000", out)
        self.assertIn("uint32_t", out)
        self.assertNotIn("undefined4", out)
        self.assertNotIn("FUN_", out)

    def test_loose_assignment_gets_explicit_cast(self):
        out = clean(SNIPPET, "t")
        self.assertIn("t_mem_100004 = (uint32_t)(uintptr_t)&t_mem_100008;", out)

    def test_deduplicates_identical_bodies(self):
        out = clean(SNIPPET + SNIPPET, "t")
        self.assertEqual(out.count("t_fn_100()"), 1)

    @unittest.skipUnless(shutil.which("gcc"), "gcc not available")
    def test_cleaned_output_compiles_without_suppression(self):
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / "snippet.c.txt"
            out = Path(td) / "snippet_clean.c"
            src.write_text(SNIPPET, encoding="utf-8")
            r = subprocess.run(
                [sys.executable, str(ROOT / "tools" / "clean_ghidra_c.py"),
                 str(src), str(out), "--prefix", "t"],
                capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run(
                ["gcc", "-std=c11", "-Wall", "-Werror=implicit-function-declaration",
                 "-Werror=incompatible-pointer-types", "-Werror=int-conversion",
                 "-I", str(out.parent), "-fsyntax-only", str(out)],
                capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == "__main__":
    unittest.main()
