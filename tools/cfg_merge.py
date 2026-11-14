"""Conservative union of overlapping emitted control-flow regions."""
import re
from dataclasses import dataclass

LABEL = re.compile(r'^L_([0-9A-F]+): ')
MAX_BLOCKS = 512
MAX_CASES = 128
MAX_ROOTS_PER_SIGNATURE = 8
MAX_CANDIDATES = 32


def explicit_blocks(blocks):
    """Preserve original lexical fallthrough even after reordering labels.

    The extra edge is unreachable after a terminating instruction. Do not
    guess which generated C fragments terminate or which conditions are true.
    """
    pcs = []
    for body in blocks:
        match = LABEL.match(body)
        if not match:
            raise ValueError('missing instruction label')
        pcs.append(int(match[1], 16))
    if pcs != sorted(set(pcs)):
        raise ValueError('noncanonical instruction order')
    return {pc: body + (f'    goto L_{pcs[i+1]:06X};\n' if i+1 < len(pcs) else '    return;\n')
            for i, (pc, body) in enumerate(zip(pcs, blocks))}


@dataclass
class Group:
    root: object
    blocks: dict
    cases: set
    changed: bool = False


def merge_cfg(routines, max_blocks=MAX_BLOCKS, max_cases=MAX_CASES):
    """Mutate retained roots; preserve every original dispatch entry.

    Only union regions with exactly equal normalized bodies at *all* shared
    guest addresses, including original fallthrough. Keep bank context in
    index. Bounds limit switch/region growth and candidate-search cost.
    """
    if len({r.name for r in routines}) != len(routines):
        raise ValueError('duplicate routine definition')
    if max_blocks < 1 or max_cases < 1:
        raise ValueError('invalid CFG limits')
    index = {}
    groups = {}
    aliases = {}

    def register(group, blocks):
        for pc, body in blocks.items():
            key = (group.root.context, pc, body)
            roots = index.setdefault(key, [])
            if len(roots) < MAX_ROOTS_PER_SIGNATURE:
                roots.append(group.root.name)

    for r in sorted(routines, key=lambda x: (-len(x.blocks), x.name)):
        r.extra.clear()
        aliases[r.name] = r.name
        if not r.blocks or len(r.blocks)>max_blocks or len(r.cases)>max_cases:
            continue
        blocks = explicit_blocks(r.blocks)
        if not r.cases.issubset(blocks):
            raise ValueError('entry label absent')
        candidates = set()
        for pc, body in blocks.items():
            for name in index.get((r.context, pc, body), ()):
                candidates.add(name)
                if len(candidates) >= MAX_CANDIDATES:
                    break
            if len(candidates) >= MAX_CANDIDATES:
                break
        best = None
        score = 0
        for name in sorted(candidates):
            g = groups[name]
            shared = g.blocks.keys() & blocks.keys()
            if not shared or len(g.blocks)+len(blocks)-len(shared)>max_blocks or len(g.cases | r.cases)>max_cases:
                continue
            if any(g.blocks[pc] != blocks[pc] for pc in shared):
                continue
            if len(shared)>score:
                best,score=g,len(shared)
        if best is None:
            g=Group(r,blocks,set(r.cases))
            groups[r.name]=g
            register(g,blocks)
        else:
            new={pc:body for pc,body in blocks.items() if pc not in best.blocks}
            best.blocks.update(new)
            best.cases.update(r.cases)
            best.changed=True
            aliases[r.name]=best.root.name
            register(best,new)
    for g in groups.values():
        if g.changed:
            g.root.blocks=tuple(g.blocks[pc] for pc in sorted(g.blocks))
            g.root.extra.update(g.cases-g.root.cases)
    return aliases
