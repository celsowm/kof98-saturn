#!/usr/bin/env python3
"""Prototype: replace the hot tst.b/beq polling loop with a dense variant that
hoists the ng_rmap page-table lookup out of the loop. Semantics are preserved:
same cycle accounting, same ng_icount, same flag result, same yield point.
"""
import sys
from pathlib import Path

OLD = """L_00A142: /* 00A142: tst.b   ($2785,A5) */
    { NG_INSN(12); uint32_t ad = CPU.a[5] + (uint32_t)(10117); uint8_t d = ng_r8(ad); ng_logic8(d); }
L_00A146: /* 00A146: beq     $a142 */
    { NG_INSN(10); if (CC_EQ) { if (ng_cycles >= ng_next_event) { CPU.pc = 0x00A142u; return; } goto L_00A142; } ng_cycles += -2; }"""

NEW = """L_00A142: /* 00A142: tst.b ($2785,A5) + 00A146: beq $a142 (dense) */
    {
        uint32_t ad = CPU.a[5] + (uint32_t)(10117);
        uint8_t *pg = ng_rmap[(ad >> 16) & 0xFFu];
        for (;;) {
            uint8_t d = pg ? pg[ad & 0xFFFFu] : ng_r8_slow(ad);
            ng_icount += 2;
            ng_cycles += 12;
            ng_logic8(d);
            if (d == 0u) {
                ng_cycles += 10;
                if (ng_cycles >= ng_next_event) { CPU.pc = 0x00A142u; return; }
                continue;
            }
            ng_cycles += 8;   /* 10 - 2 from the not-taken beq */
            break;
        }
    }"""


def main():
    for path in sys.argv[1:]:
        p = Path(path)
        s = p.read_text(errors="replace")
        if OLD in s:
            s = s.replace(OLD, NEW)
            p.write_text(s)
            print("[patch] patched", path)
        elif NEW in s:
            print("[patch] already patched", path)
        else:
            print("[patch] PATTERN NOT FOUND in", path)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
