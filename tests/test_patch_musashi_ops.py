#!/usr/bin/env python3
"""ROM-free regression for tools/patch_musashi_ops.py.

The patcher must rewrite the single NUM_CPU_TYPES define in a synthetic
generated m68kops.c, be idempotent, and refuse anything it cannot recognize -
a wrong binary search of Musashi tables would silently corrupt the boot core.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "patch_musashi_ops.py"

OPS = """\
#define NUM_CPU_TYPES 5
unsigned char m68ki_cycles[NUM_CPU_TYPES][0x10000]; /* Cycles used by CPU type */
"""


def run(path: Path):
    return subprocess.run([sys.executable, str(SCRIPT), str(path)],
                          capture_output=True, text=True)


class PatchMusashiOpsTest(unittest.TestCase):
    def test_patches_once_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = Path(tmp) / "m68kops.c"
            ops.write_text(OPS)
            first = run(ops)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertIn("patched", first.stdout)
            self.assertIn("#define NUM_CPU_TYPES 1", ops.read_text())

            second = run(ops)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertIn("already patched", second.stdout)
            self.assertEqual(ops.read_text().count("NUM_CPU_TYPES 1"), 1)

    def test_rejects_unrecognized_generated_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            ops = Path(tmp) / "m68kops.c"
            ops.write_text("#define NUM_CPU_TYPES 7\n")
            proc = run(ops)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("found 0", proc.stderr)

            ops2 = Path(tmp) / "missing.c"
            proc = run(ops2)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("missing file", proc.stderr)


if __name__ == "__main__":
    unittest.main()
