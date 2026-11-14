#!/usr/bin/env python3
"""Conservative dead CCR write elimination for pinned emitted C fragments."""
import re

ALL = 0x1f
NZVC = 0x0f
# The entire instruction body must match. No memory / bus / SR / control flow.
FRAME = re.compile(r'(L_[0-9A-F]+: /\*[^\n]*\*/\n    \{ NG_INSN\(\d+\); )(.*?)( \}\n)')
REG = r'[0-7]'
MOVEQ = re.compile(r'CPU\.d\[('+REG+r')\] = ng_logic32\((0x[0-9A-F]+u)\);')
MOVEL = re.compile(r'uint32_t v = \(uint32_t\)CPU\.d\[('+REG+r')\]; ng_logic32\(v\); CPU\.d\[('+REG+r')\] = v;')
TST = re.compile(r'uint(8|16|32)_t d = \(uint\1_t\)CPU\.d\['+REG+r'\]; ng_logic\1\(d\);')
CMP = re.compile(r'uint(8|16|32)_t s = \(uint\1_t\)CPU\.d\['+REG+r'\]; ng_cmp\1\(s, \(uint\1_t\)CPU\.d\['+REG+r'\]\);')
ARITH = re.compile(r'uint32_t s = \(uint32_t\)CPU\.d\[('+REG+r')\]; uint32_t d = \(uint32_t\)CPU\.d\[('+REG+r')\]; uint32_t r = ng_(add|sub)32\(s, d\); CPU\.d\[\2\] = r;')


def classify(body):
    """Return guaranteed flag writes and equivalent flag-free body, or barrier."""
    if body == '':
        return 0, body
    m = MOVEQ.fullmatch(body)
    if m:
        return NZVC, f'CPU.d[{m[1]}] = {m[2]};'
    m = MOVEL.fullmatch(body)
    if m:
        return NZVC, f'CPU.d[{m[2]}] = CPU.d[{m[1]}];'
    if TST.fullmatch(body) or CMP.fullmatch(body):
        return NZVC, ''
    m = ARITH.fullmatch(body)
    if m:
        op = '+' if m[3] == 'add' else '-'
        return ALL, f'CPU.d[{m[2]}] = CPU.d[{m[2]}] {op} CPU.d[{m[1]}];'
    return None


def optimize(blocks):
    """Local straight-line backward liveness; every unknown fragment is a fence.

    All flags live at routine exit. A recognized fragment has no external
    calls or control flow and reads no CCR bits. Never rewrite partial masks.
    """
    out = list(blocks)
    live = ALL
    changed = 0
    for i in range(len(out)-1, -1, -1):
        m = FRAME.fullmatch(out[i])
        # Upstream leaves a trailing space before closing brace. NOP leaves
        # exactly one space after NG_INSN; it is conservatively a barrier.
        effect = classify(m[2]) if m else None
        if effect is None:
            live = ALL
            continue
        writes, replacement = effect
        if writes and not (live & writes):
            out[i] = m[1] + replacement + m[3]
            changed += 1
        live &= ~writes
    return tuple(out), changed
