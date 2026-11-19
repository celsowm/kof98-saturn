#!/usr/bin/env python3
"""ROM-free test that the generated pure-C runtime is buildable and runnable."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

SNIPPET_C = """#include <stdint.h>
#include "snippet_clean.h"
/* 001000 : guest_fn_1000 */
void guest_fn_1000()
{
  uint32_t *unaff_A4;
  guest_mem_2000 = 0x1234;
  *unaff_A4 = guest_mem_2000;
  return;
}
"""

SNIPPET_H = """#pragma once
#include <stdint.h>
#define CONCAT11(a,b) ((uint16_t)(((uint16_t)(uint8_t)(a) << 8) | (uint8_t)(b)))
typedef void (*code)(void);
void halt_baddata(void);
uint32_t bcdAdjust(int);
void pushInvalidateCaches(int);
void halt_unimplemented(void);
void __m68k_trap(int);
int32_t SBORROW1(int32_t,int32_t);
int32_t SUB42(void *,int32_t);
uintptr_t guest_ptrfn_1000;
void guest_fn_1000();
extern int guest_runtime_dispatch(uint32_t pc);
"""


@unittest.skipUnless(shutil.which("gcc"), "gcc not available")
class TestPureCRuntime(unittest.TestCase):
    def test_generate_build_and_run(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            src, hdr = d / "snippet.c", d / "snippet.h"
            src.write_text(SNIPPET_C, encoding="utf-8")
            hdr.write_text(SNIPPET_H, encoding="utf-8")
            gen = d / "snippet_runtime.c"
            r = subprocess.run(
                [sys.executable, str(TOOLS / "make_pure_c_runtime.py"),
                 str(src), str(hdr), str(gen), "--prefix", "guest"],
                capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("1 dispatch entries", r.stdout)
            shutil.copy(TOOLS / "pure_c_state.h", d / "pure_c_state.h")
            shutil.copy(TOOLS / "pure_c_helpers.c", d / "pure_c_helpers.c")
            shutil.copy(TOOLS / "pure_c_main.c", d / "pure_c_main.c")
            exe = d / "run.exe"
            r = subprocess.run(
                ["gcc", "-std=c11", "-Wno-error=incompatible-pointer-types",
                 "-Wno-error=int-conversion", "-w",
                 "-I", str(TOOLS / "shim"),
                 "-I", str(ROOT / "deps" / "kof98recomp" / "neogeorecomp" / "include"),
                 "-I", str(d),
                 "-o", str(exe), str(gen), str(d / "pure_c_helpers.c"),
                 str(d / "pure_c_main.c")], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[-1000:])
            r = subprocess.run([str(exe), "0x1000", "1"], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("hits=1", r.stdout)
            self.assertIn("wram_hash=", r.stdout)


if __name__ == "__main__":
    unittest.main()
