#!/usr/bin/env python3
"""ROM-free regression for tools/profile_guest.py.

Builds two synthetic X1 profiles (addresses and counts only, no ROM data) and
checks the aggregation, unit grouping, coverage and budget arithmetic, plus the
rejections that keep a broken profile from silently producing a size estimate.
"""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "profile_guest.py"

PROFILE = """\
# kof-guest-profile-v1
instructions=300
distinct_words=6
distinct_pages=2
page 001000 hits 100 words 2
line 001000 hits 60
line 001040 hits 40
page 002000 hits 200 words 4
line 002000 hits 100
line 002040 hits 60
line 002080 hits 40
"""

WORDS = """\
# kof-guest-words-v1
001000-001002
002000-002006
"""


def write(directory: Path, profile: str = PROFILE, words: str = WORDS,
          suffix: str = "") -> tuple[Path, Path]:
    p = directory / f"run{suffix}.txt"
    w = directory / f"run{suffix}.words"
    p.write_text(profile)
    w.write_text(words)
    return p, w


def run(args):
    return subprocess.run([sys.executable, str(SCRIPT), *args],
                          capture_output=True, text=True)


class ProfileGuestTest(unittest.TestCase):
    def test_single_run_units_and_budget(self):
        with tempfile.TemporaryDirectory() as tmp:
            p, w = write(Path(tmp))
            proc = run([str(p), "--words", str(w), "--json",
                        "--budget", "400000"])
            self.assertEqual(proc.returncode, 0, proc.stderr)
            data = json.loads(proc.stdout)

            self.assertEqual(data["instructions"], 300)
            self.assertEqual(data["distinct_pages"], 2)
            self.assertEqual(data["distinct_words"], 6)
            self.assertEqual(data["units"], 1)          # both pages in unit 0
            self.assertEqual(data["regions"]["p1"]["hits"], 300)
            self.assertEqual(data["regions"]["p1"]["words"], 6)

            top = data["top_units"][0]
            self.assertEqual(top["unit"], "000000")
            self.assertEqual(top["hits"], 300)
            self.assertEqual(top["words"], 6)
            self.assertEqual(top["pages"], 2)

            # One 16 KiB unit at the measured -Os size covers everything.
            for row in data["coverage"]:
                self.assertEqual(row["units"], 1)
                self.assertEqual(row["share"], 1.0)
                self.assertEqual(row["-Os"], 334724)

            sel = {s["opt"]: s for s in data["budget"]}
            self.assertEqual(sel["-Os"]["units"], 1)
            self.assertEqual(sel["-Os"]["bytes"], 334724)
            self.assertEqual(sel["-Os"]["share"], 1.0)

    def test_two_runs_merge_hits_and_words(self):
        with tempfile.TemporaryDirectory() as tmp:
            p1, w1 = write(Path(tmp), suffix="a")
            p2, w2 = write(Path(tmp), suffix="b")
            proc = run([str(p1), str(p2), "--words", str(w1), "--words", str(w2),
                        "--json"])
            self.assertEqual(proc.returncode, 0, proc.stderr)
            data = json.loads(proc.stdout)
            self.assertEqual(data["instructions"], 600)
            self.assertEqual(data["distinct_words"], 6)   # union, not 12
            self.assertEqual(data["top_pages"][0]["runs"], 2)
            self.assertEqual(sum(r["share"] for r in data["top_pages"]), 1.0)

    def test_rejects_bad_header_and_hit_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            p, w = write(d, profile=PROFILE.replace("# kof-guest-profile-v1", "# other"))
            proc = run([str(p), "--words", str(w)])
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("not a", proc.stderr)

            p, w = write(d)
            proc = run([str(p), "--words", str(w), "--coverage", "0"])
            self.assertNotEqual(proc.returncode, 0)

            broken = PROFILE.replace("line 001040 hits 40", "line 001040 hits 39")
            p, w = write(d, profile=broken)
            proc = run([str(p), "--words", str(w)])
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("hit sum does not match", proc.stderr)

    def test_rejects_word_count_mismatch_and_overlap(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            p, w = write(d, words="# kof-guest-words-v1\n001000-001000\n002000-002008\n")
            proc = run([str(p), "--words", str(w)])
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("does not match", proc.stderr)

            p, w = write(d, words="# kof-guest-words-v1\n002000-002006\n001000-001002\n")
            proc = run([str(p), "--words", str(w)])
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("ordered", proc.stderr)


if __name__ == "__main__":
    unittest.main()
