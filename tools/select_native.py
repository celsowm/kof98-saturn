"""Select entire native routines within an estimated byte budget."""
import argparse
import csv
import json
from fractions import Fraction
from pathlib import Path
from collections import Counter
from pack_dispatch import parse, fingerprint
from dedup_routines import read_unit, render

DIRECTORY_BYTES = 2626
ENTRY_BYTES = 6


def read_profile(path, identity, keys):
    with path.open(newline='') as f:
        if f.readline().strip() != '# kof-native-profile-v1 ' + identity:
            raise ValueError(f'profile identity mismatch: {path}')
        reader = csv.DictReader(f)
        if reader.fieldnames != ['key', 'hits', 'instructions', 'cycles']:
            raise ValueError('invalid profile columns')
        seen = set()
        for row in reader:
            key = int(row['key'], 16)
            h, i, c = (int(row[x]) for x in ('hits', 'instructions', 'cycles'))
            if key not in keys or key in seen or h <= 0 or min(i,c) < 0:
                raise ValueError('invalid, duplicate or unknown profile entry')
            seen.add(key)
            yield key, h, i, c


def read_sizes(path, names):
    """Read target nm -S --defined-only output for routine objects. Sizes may
    be decimal (test fixtures, --radix=d) or hex (sh2eb-elf-nm prints hex
    regardless of --radix=d), and the C symbol has a leading underscore the
    table name does not."""
    sizes = {}
    for line in path.read_text().splitlines():
        fields = line.split()
        if len(fields) != 4:
            continue
        _, size, kind, name = fields
        if name.startswith('_'):
            name = name[1:]
        if name not in names:
            continue
        try:
            if len(size) == 8 and all(c in '0123456789abcdef' for c in size):
                size = int(size, 16)  # nm's zero-padded 8-digit hex
            else:
                size = int(size)      # decimal fixture or short hex without letters
        except ValueError:
            size = int(size, 16)
        if kind not in ('T','t') or name in sizes or size <= 0:
            raise ValueError(f'invalid or duplicate text symbol: {name}')
        sizes[name] = size
    if set(sizes) != names:
        raise ValueError('target size report missing routine symbols')
    return sizes


def choose(entries, sizes, weights, budget, reserve=0):
    """Greedy guest-instruction density, not optimal knapsack or SH-2 timing."""
    counts = Counter(name for _,name in entries)
    if set(sizes) != set(counts) or min(sizes.values(), default=0) <= 0:
        raise ValueError('size coverage mismatch')
    if reserve < 0 or budget <= reserve + DIRECTORY_BYTES:
        raise ValueError('budget cannot hold fixed reserve and directory')
    costs = {n: ((sizes[n]+15)//16)*16+ENTRY_BYTES*counts[n] for n in counts}
    ranked = sorted((n for n in counts if weights[n]>0), key=lambda n:(-Fraction(weights[n],costs[n]),n))
    used = reserve+DIRECTORY_BYTES
    kept=set()
    for name in ranked:
        if used+costs[name] <= budget:
            kept.add(name); used+=costs[name]
    if not kept:
        raise ValueError('no observed routine fits budget')
    return kept, used


def transform(src, dst, profiles, sizes_path, budget, reserve, parts=16):
    if src.resolve()==dst.resolve(): raise ValueError('source and output must differ')
    table=src/'recomp_table.c'
    entries=parse(table.read_text()); keys=dict(entries)
    identity=fingerprint(table)
    weights=Counter(); hits=Counter(); cycles=Counter()
    for path in profiles:
        for key,h,i,c in read_profile(path,identity,keys):
            name=keys[key]; weights[name]+=i; hits[name]+=h; cycles[name]+=c
    names=set(keys.values()); sizes=read_sizes(sizes_path,names)
    # The size-report producer attaches the same generated-code identity.
    if sizes_path.read_text().splitlines()[0] != '# kof-native-sizes-v1 '+identity:
        raise ValueError('size report identity mismatch')
    kept,used=choose(entries,sizes,weights,budget,reserve)
    units=[]; definitions=set()
    for part in range(parts):
        prefix,rr=read_unit((src/f'recomp_{part:02d}.c').read_text(),part)
        for r in rr:
            if r.name in definitions: raise ValueError('duplicate definition')
            definitions.add(r.name)
        units.append((prefix,rr))
    cases={r.name:r.cases for _,rr in units for r in rr}
    if any(n not in cases or k&0xffffff not in cases[n] for k,n in entries):
        raise ValueError('table/switch mismatch')
    retained=[(k,n) for k,n in entries if n in kept]
    dst.mkdir(parents=True,exist_ok=True)
    (dst/'recomp_common.h').write_bytes((src/'recomp_common.h').read_bytes())
    for i,(prefix,rr) in enumerate(units):
        (dst/f'recomp_{i:02d}.c').write_text(prefix+''.join(render(r) for r in rr if r.name in kept))
    out='/* ROM-derived: local build only. */\n#include <neogeorecomp/neogeo.h>\n'
    out+=''.join(f'void {n}(void);\n' for n in sorted(kept))
    out+='const ng_func_entry_t ng_recomp_table[] = {\n'+''.join(f'    {{0x{k:06X}u, {n}}},\n' for k,n in retained)+'};\n'
    out+=f'const size_t ng_recomp_count = {len(retained)};\n'
    (dst/'recomp_table.c').write_text(out)
    report={'budget_bytes':budget,'reserve_bytes':reserve,'estimated_used_bytes':used,'routines_total':len(names),'routines_retained':len(kept),'entries_total':len(entries),'entries_retained':len(retained)}
    for label,m in [('hits',hits),('guest_instructions',weights),('guest_cycles',cycles)]:
        report[label+'_total']=sum(m.values()); report[label+'_retained']=sum(m[n] for n in kept)
    (dst/'native_selection.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path); p.add_argument('output',type=Path)
    p.add_argument('--profile',type=Path,action='append',required=True)
    p.add_argument('--sizes',type=Path,required=True); p.add_argument('--budget',type=int,required=True)
    p.add_argument('--reserve',type=int,default=0); p.add_argument('--parts',type=int,default=16)
    a=p.parse_args()
    try:
        if not 1<=a.parts<=100: raise ValueError('invalid parts')
        print(json.dumps(transform(a.source,a.output,a.profile,a.sizes,a.budget,a.reserve,a.parts),indent=2))
    except (OSError,ValueError,TypeError) as e: p.error(str(e))

if __name__=='__main__': main()
