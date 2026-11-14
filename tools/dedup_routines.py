#!/usr/bin/env python3
"""Conservative suffix sharing for the pinned m68krecomp C emitter."""
import argparse
import hashlib
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from pack_dispatch import parse
from cfg_merge import merge_cfg
from flag_liveness import optimize as optimize_flags
from cycle_batch import optimize as optimize_cycles
from register_cache import optimize as optimize_registers

ROUTINE = re.compile(r'^/\* routine \$([0-9A-F]+): (\d+) instructions \*/\nvoid (r_[0-9A-F]+)\(void\) \{\n    switch \(CPU.pc\) \{\n(.*?)^    default: ng_bad_dispatch\(CPU.pc\); return;\n    \}\n(.*?)^\}\n', re.M | re.S)
CASE = re.compile(r'    case 0x([0-9A-F]+)u: goto L_([0-9A-F]+);\n')
LABEL = re.compile(r'^L_([0-9A-F]+): ', re.M)

@dataclass
class Routine:
    name: str
    seed: int
    file: int
    cases: set
    blocks: tuple
    extra: set = field(default_factory=set)

    @property
    def context(self):
        # Never merge across bank contexts; fixed and BIOS are separate too.
        pc = self.seed & 0xffffff
        return ('bank', self.seed >> 24) if 0x200000 <= pc < 0x300000 else ('bios' if pc >= 0xc00000 else 'fixed', 0)


def read_unit(source, part):
    found = list(ROUTINE.finditer(source))
    prefix = source[:found[0].start()] if found else source
    # Only the fixed banner/include may precede the routine bodies.
    if not re.fullmatch(r'/\*.*?\*/\s*/\*[^\n]*\*/\s*#include "recomp_common\.h"\s*', prefix, re.S):
        raise ValueError('unexpected generated unit header')
    out = []
    pos = len(prefix)
    for m in found:
        if source[pos:m.start()].strip():
            raise ValueError('unparsed generated code between routines')
        seed, count, name = int(m[1], 16), int(m[2]), m[3]
        if int(name[2:], 16) != seed:
            raise ValueError('routine symbol/seed mismatch')
        cases = set()
        for c in m[4].splitlines(keepends=True):
            match = CASE.fullmatch(c)
            if not match or match[1] != match[2] or int(match[1],16) in cases:
                raise ValueError('unexpected switch case')
            cases.add(int(match[1],16))
        body = m[5]
        labels = list(LABEL.finditer(body))
        pcs = [int(x[1],16) for x in labels]
        if len(labels) != count or pcs != sorted(set(pcs)) or not cases.issubset(pcs):
            raise ValueError('unexpected instruction labels or count')
        if (labels and labels[0].start() != 0) or (not labels and body.strip()):
            raise ValueError('unexpected instruction body')
        blocks = tuple(body[x.start():labels[i+1].start() if i+1<len(labels) else len(body)] for i,x in enumerate(labels))
        if re.search(r'\br_[0-9A-F]+\s*\(', body):
            raise ValueError('direct routine calls are not supported')
        if not set(int(x,16) for x in re.findall(r'\bgoto L_([0-9A-F]+);',body)).issubset(pcs):
            raise ValueError('goto to missing instruction label')
        out.append(Routine(name,seed,part,cases,blocks))
        pos = m.end()
    if source[pos:].strip():
        raise ValueError('unparsed generated code at end of unit')
    return prefix, out


def suffixes(blocks):
    digest = b''
    for i in range(len(blocks)-1,-1,-1):
        digest = hashlib.sha256(blocks[i].encode('utf-8') + digest).digest()
        yield i,digest


def merge(routines):
    by_name = {r.name:r for r in routines}
    if len(by_name)!=len(routines):
        raise ValueError('duplicate routine definition')
    candidates = {}
    for r in routines:
        r.extra.clear()
        if not r.blocks:
            continue
        digest = next(d for i,d in suffixes(r.blocks) if i==0)
        candidates.setdefault((r.context,len(r.blocks),digest),[]).append(r)
    aliases = {}
    for root in sorted(routines,key=lambda r:(-len(r.blocks),r.name)):
        if root.name in aliases:
            continue
        aliases[root.name]=root.name
        for i,digest in suffixes(root.blocks):
            for small in candidates.get((root.context,len(root.blocks)-i,digest),()):
                if small.name in aliases:
                    continue
                # A digest is an index only, never proof of equality.
                if root.blocks[i:] == small.blocks:
                    aliases[small.name]=root.name
                    root.extra.update(small.cases)
    return aliases


def render(r):
    cases = ''.join(f'    case 0x{pc:06X}u: goto L_{pc:06X};\n' for pc in sorted(r.cases | r.extra))
    return ('\n/* routine $'+f'{r.seed:06X}: {len(r.blocks)} instructions */\nvoid {r.name}(void) {{\n    switch (CPU.pc) {{\n'+cases+
            '    default: ng_bad_dispatch(CPU.pc); return;\n    }\n'+''.join(r.blocks)+'}\n')


def transform(src, dst, parts=16, mode='suffix', flags=False, cycles=False, regcache=False):
    if src.resolve()==dst.resolve():
        raise ValueError('source and destination must differ')
    prefixes=[]; routines=[]
    for part in range(parts):
        prefix, rr=read_unit((src/f'recomp_{part:02d}.c').read_text(encoding='utf-8'),part)
        prefixes.append(prefix); routines.extend(rr)
    entries=parse((src/'recomp_table.c').read_text(encoding='utf-8'))
    by_name={r.name:r for r in routines}
    for key,fn in entries:
        if fn not in by_name or key & 0xffffff not in by_name[fn].cases:
            raise ValueError(f'table entry not in original switch: {key:x} {fn}')
    labels_before=sum(len(r.blocks) for r in routines)
    if mode not in ('none','suffix','cfg'):
        raise ValueError('unknown dedup mode')
    if flags and mode=='cfg':
        raise ValueError('flags and CFG sharing must be measured separately')
    if cycles and (flags or mode=='cfg'):
        raise ValueError('cycle batching must be measured separately from flags and CFG')
    if regcache and (flags or cycles or mode=='cfg'):
        raise ValueError('register caching requires separate builds from flags, cycles and CFG')
    aliases=merge_cfg(routines) if mode=='cfg' else merge(routines) if mode=='suffix' else {r.name:r.name for r in routines}
    kept=[r for r in routines if aliases[r.name]==r.name]
    flag_writes_removed=0
    if flags:
        for r in kept:
            r.blocks, removed=optimize_flags(r.blocks)
            flag_writes_removed+=removed
    cycle_batches=counter_pairs_removed=0
    if cycles:
        for r in kept:
            r.blocks, batches, removed=optimize_cycles(r.blocks,r.cases|r.extra)
            cycle_batches+=batches
            counter_pairs_removed+=removed
    cache_regions=register_refs_removed=0
    if regcache:
        for r in kept:
            r.blocks, regions, removed=optimize_registers(r.blocks,r.cases|r.extra)
            cache_regions+=regions
            register_refs_removed+=removed
    # Switch ownership must be unchanged, even when an entry points into a tail.
    for key,fn in entries:
        root=by_name[aliases[fn]]
        if key & 0xffffff not in root.cases | root.extra:
            raise ValueError('merged switch lost entry')
    dst.mkdir(parents=True,exist_ok=True)
    (dst/'recomp_common.h').write_bytes((src/'recomp_common.h').read_bytes())
    for part,prefix in enumerate(prefixes):
        (dst/f'recomp_{part:02d}.c').write_text(prefix+''.join(render(r) for r in kept if r.file==part),encoding='utf-8')
    table='/* ROM-derived: local build only. */\n#include <neogeorecomp/neogeo.h>\n'
    table+=''.join(f'void {name}(void);\n' for name in sorted({aliases[fn] for _,fn in entries}))
    table+='const ng_func_entry_t ng_recomp_table[] = {\n'+''.join(f'    {{0x{key:06X}u, {aliases[fn]}}},\n' for key,fn in entries)+'};\n'
    table+=f'const size_t ng_recomp_count = {len(entries)};\n'
    (dst/'recomp_table.c').write_text(table,encoding='utf-8')
    report={'routines_before':len(routines),'routines_after':len(kept),'mode':mode,'flag_producers_removed':flag_writes_removed,'cycle_batches':cycle_batches,'counter_update_pairs_removed':counter_pairs_removed,'register_cache_regions':cache_regions,'estimated_register_refs_removed':register_refs_removed,'instruction_labels_before':labels_before,'instruction_labels_after':sum(len(r.blocks) for r in kept),'dispatch_entries':len(entries)}
    (dst/'dedup_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path); p.add_argument('output',type=Path); p.add_argument('--parts',type=int,default=16)
    p.add_argument('--mode',choices=('none','suffix','cfg'),default='suffix')
    p.add_argument('--flags',action='store_true')
    p.add_argument('--cycles',action='store_true')
    p.add_argument('--regcache',action='store_true')
    a=p.parse_args()
    try:
        if not 1<=a.parts<=100: raise ValueError('invalid part count')
        print(json.dumps(transform(a.source,a.output,a.parts,a.mode,a.flags,a.cycles,a.regcache),indent=2))
    except (OSError,ValueError) as exc: p.error(str(exc))

if __name__=='__main__': main()
