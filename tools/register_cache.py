"""Bounded lexical cache for emitted register-only instruction sequences."""
import re
from collections import Counter
from cycle_batch import FRAME, LABEL, TARGET, MAX_RUN
from flag_liveness import classify

REG = re.compile(r'CPU\.d\[([0-7])\]')
WRITE = re.compile(r'CPU\.d\[([0-7])\]\s*=')
MAX_REGS = 3


def optimize(blocks, entries, max_run=MAX_RUN, max_regs=MAX_REGS):
    if not 2 <= max_run <= MAX_RUN or not 1 <= max_regs <= MAX_REGS:
        raise ValueError('invalid cache limits')
    out = list(blocks)
    labels = [LABEL.match(b) for b in blocks]
    if any(m is None for m in labels):
        raise ValueError('missing instruction label')
    pcs = [int(m[1], 16) for m in labels]
    if pcs != sorted(set(pcs)):
        raise ValueError('noncanonical instruction labels')
    boundaries = set(entries)
    for block in blocks:
        boundaries.update(int(m, 16) for m in TARGET.findall(block))
    if not boundaries.issubset(pcs):
        raise ValueError('missing entry or jump target')
    run = []
    regions = saved = 0

    def flush():
        nonlocal regions, saved
        if len(run) >= 2:
            refs = Counter(reg for i, m in run for reg in REG.findall(m[4]))
            dirty = {reg for i, m in run for reg in WRITE.findall(m[4])}
            benefit = {reg: count - 1 - int(reg in dirty) for reg, count in refs.items()}
            selected = sorted((r for r in refs if benefit[r] > 0), key=lambda r: (-benefit[r], r))[:max_regs]
            if selected:
                names = {r: f'kof_cached_d{r}' for r in selected}
                first = run[0][0]
                # The first guest label must precede initialization. No edge
                # can enter any interior label, so all locals are initialized.
                for j, (i, m) in enumerate(run):
                    body = REG.sub(lambda x: names.get(x[1], x[0]), m[4])
                    fragment = m[1] + f'NG_INSN({m[3]}); ' + body + '}\n'
                    if j == 0:
                        prefix, rest = fragment.split('\n', 1)
                        fragment = prefix + '\n    {\n' + ''.join(f'    uint32_t {names[r]} = CPU.d[{r}];\n' for r in selected) + rest
                    if j == len(run)-1:
                        fragment += ''.join(f'    CPU.d[{r}] = {names[r]};\n' for r in selected if r in dirty) + '    }\n'
                    out[i] = fragment
                regions += 1
                saved += sum(benefit[r] for r in selected)
        run.clear()

    for i, block in enumerate(blocks):
        if pcs[i] in boundaries or len(run) == max_run:
            flush()
        m = FRAME.fullmatch(block)
        if not m or classify(m[4].strip()) is None:
            flush()
            continue
        run.append((i, m))
    flush()
    return tuple(out), regions, saved
