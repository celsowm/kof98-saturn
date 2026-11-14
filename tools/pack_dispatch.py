#!/usr/bin/env python3
"""Convert pinned m68krecomp C table to local-only paged C, without ROM data in git."""
import argparse
import hashlib
import re
from pathlib import Path

PAGES = 1312
LIMIT = 65535
IDENT = r'[A-Za-z_][A-Za-z0-9_]*'

def page(key):
    bank, pc = key >> 24, key & 0xffffff
    if key < 0 or key > 0xffffffff or pc & 1:
        raise ValueError(f'invalid address {key:x}')
    if bank == 0 and pc < 0x100000:
        return pc >> 12
    if bank < 4 and 0x200000 <= pc < 0x300000:
        return (256 if bank == 0 else 544 + (bank-1)*256) + ((pc-0x200000)>>12)
    if bank == 0 and 0xc00000 <= pc < 0xc20000:
        return 512 + ((pc-0xc00000)>>12)
    raise ValueError(f'noncanonical address {key:x}')

def parse(source):
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    table = re.search(r'\bconst\s+ng_func_entry_t\s+ng_recomp_table\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;', source, re.S)
    count = re.search(r'\bconst\s+size_t\s+ng_recomp_count\s*=\s*(\d+)\s*;', source)
    if not table or not count:
        raise ValueError('expected unprefixed KOF98 upstream table and count')
    declarations = set(re.findall(r'\bvoid\s+('+IDENT+r')\s*\(\s*void\s*\)\s*;', source))
    row = re.compile(r'\s*\{\s*0[xX]([0-9a-fA-F]+)[uU]?\s*,\s*('+IDENT+r')\s*\}\s*,')
    body = table.group(1).rstrip()
    entries=[]; pos=0; seen=set()
    while pos < len(body):
        match = row.match(body,pos)
        if not match:
            raise ValueError(f'unrecognized table row at offset {pos}')
        key, fn = int(match[1],16), match[2]
        page(key)
        if key in seen or fn not in declarations:
            raise ValueError(f'duplicate key or undeclared symbol: {key:x} {fn}')
        seen.add(key); entries.append((key,fn)); pos=match.end()
    if len(entries) != int(count[1]) or not entries or len(entries)>LIMIT:
        raise ValueError('entry count mismatch, empty table, or 16-bit capacity exceeded')
    return sorted(entries)

def emit(entries):
    directory=[]; offsets=[]; funcs=[]; i=0
    for p in range(PAGES):
        directory.append(i)
        while i<len(entries) and page(entries[i][0])==p:
            key, fn=entries[i]; offsets.append(key&0xfff); funcs.append(fn); i+=1
    directory.append(i)
    if i!=len(entries):
        raise ValueError('page order mismatch')
    def array(kind,name,values):
        return f'static const {kind} {name}[] = {{\n' + ''.join('    '+', '.join(map(str,values[j:j+12]))+',\n' for j in range(0,len(values),12))+'};\n'
    return ('/* ROM-derived: local build only; never commit or distribute. */\n#include "kof98/paged_dispatch.h"\n'+
            ''.join(f'extern void {fn}(void);\n' for fn in sorted(set(funcs)))+
            array('uint16_t','directory',directory)+array('uint16_t','offsets',offsets)+
            array('kof_dispatch_fn','functions',funcs)+
            f'const kof_paged_dispatch kof_generated_dispatch = {{directory, offsets, functions, {i}u}};\n')

def fingerprint(table):
    """Identify the exact generated C set, independent of its directory."""
    h=hashlib.sha256()
    inputs=[table,table.parent/'recomp_common.h']+sorted(table.parent.glob('recomp_[0-9][0-9].c'))
    for path in inputs:
        data=path.read_bytes()
        h.update(path.name.encode('utf-8')+b'\0')
        h.update(len(data).to_bytes(8,'big'))
        h.update(data)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path); p.add_argument('output',type=Path)
    a=p.parse_args()
    if a.source.resolve()==a.output.resolve(): p.error('input and output must differ')
    try:
        result=emit(parse(a.source.read_text(encoding='utf-8')))
        result+=f'const char kof_generated_dispatch_id[] = "{fingerprint(a.source)}";\n'
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(result,encoding='utf-8')
    except (ValueError,OSError) as e: p.error(str(e))

if __name__=='__main__': main()
