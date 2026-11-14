"""Batch fixed cycle and instruction counts in pure emitted C sequences."""
import re
from flag_liveness import classify

MAX_RUN = 16
FRAME = re.compile(r'(L_([0-9A-F]+): /\*[^\n]*\*/\n    \{ )NG_INSN\((\d+)\); (.*?)\}\n')
LABEL = re.compile(r'^L_([0-9A-F]+): ')
TARGET = re.compile(r'\bgoto L_([0-9A-F]+);')


def optimize(blocks, entries, max_run=MAX_RUN):
    """Return (fragments, batch count, eliminated counter-update pairs).

    Only straight-line register-only fragments with constant cycle cost.
    All switch entries and explicit goto destinations start new batches.
    Precharge a batch at its first instruction; no allowed body observes
    the counters or calls external code. Both counters are preserved.
    """
    if not 2 <= max_run <= MAX_RUN:
        raise ValueError('invalid batch length')
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
        raise ValueError('missing entry or jump destination')
    run = []
    batches = saved = 0

    def flush():
        nonlocal batches, saved
        if len(run) >= 2:
            total = sum(int(m[3]) for _, m in run)
            for j, (index, m) in enumerate(run):
                counts = f'ng_cycles += {total}; ng_icount += {len(run)}u; ' if j == 0 else ''
                out[index] = m[1] + counts + m[4] + '}\n'
            batches += 1
            saved += len(run) - 1
        run.clear()

    for i, block in enumerate(blocks):
        if pcs[i] in boundaries or len(run) == max_run:
            flush()
        m = FRAME.fullmatch(block)
        # Exact whitelist: no unknown calls, variable cycle adjustments,
        # exception, bus, status-register, or scheduling effects.
        if not m or not 1 <= int(m[3]) <= 1000 or classify(m[4].strip()) is None:
            flush()
            continue
        run.append((i, m))
    flush()
    return tuple(out), batches, saved
