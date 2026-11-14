#!/usr/bin/env python3
"""Local KOF98 AES tile pack: native LUT4 pixels, sorted BE tile index."""
import argparse
import json
import struct
import zlib
from contextlib import ExitStack
from pathlib import Path

C_SIZE = 8 * 1024 * 1024
CART_BYTES = 4 * 1024 * 1024
FIX_SIZE = 128 * 1024
TILE_BYTES = 128


def sprite_tile(a, b):
    """64 bytes from each C ROM; 128 bytes row-major, high nibble first."""
    if len(a) != 64 or len(b) != 64:
        raise ValueError('invalid C tile length')
    out = bytearray()
    for y in range(16):
        for base in (32, 0):
            j = base + 2*y
            p0, p1, p2, p3 = a[j], a[j+1], b[j], b[j+1]
            def pixel(x):
                return ((p0 >> x)&1) | (((p1 >> x)&1)<<1) | (((p2 >> x)&1)<<2) | (((p3 >> x)&1)<<3)
            for x in range(0, 8, 2):
                out.append((pixel(x)<<4) | pixel(x+1))
    return bytes(out)


def fix_tiles(raw):
    """Neo Geo FIX 8x8 tiles to row-major packed pixels; not VDP2 map data."""
    if not raw or len(raw)%32:
        raise ValueError('invalid FIX length')
    return bytes(((v&15)<<4)|(v>>4) for t in range(0,len(raw),32)
                 for y in range(8) for col in (16,24,0,8) for v in (raw[t+col+y],))


def layout(ids, capacity, fix_count=0):
    if not isinstance(ids,list) or not ids or any(type(x) is not int or x<0 or x>=524288 for x in ids):
        raise ValueError('expected nonempty list of valid KOF98 C tile IDs')
    if not 1 <= capacity <= CART_BYTES or not 0 <= fix_count <= 2:
        raise ValueError('invalid capacity or FIX count')
    ids=sorted(set(ids))
    sizes=[len(ids)*4,len(ids)*TILE_BYTES]+[FIX_SIZE]*fix_count
    offsets=[]; end=0
    for size in sizes:
        end=(end+31)&~31
        offsets.append(end); end+=size
    if end>capacity:
        raise ValueError(f'phase needs {end} bytes; budget {capacity}')
    return ids, offsets, end


def read_selected(pairs, ids):
    """Pairs are equal-sized, concatenated in order; no whole-ROM allocation."""
    with ExitStack() as stack:
        regions=[]; end=0
        for a,b in pairs:
            sa,sb=a.stat().st_size,b.stat().st_size
            if not sa or sa!=sb or sa%64:
                raise ValueError('invalid C ROM pair size')
            fa,fb=stack.enter_context(a.open('rb')),stack.enter_context(b.open('rb'))
            regions.append((end,end+sa//64,fa,fb)); end+=sa//64
        out=bytearray(); region=0
        for tile in ids:
            if type(tile) is not int or tile<0 or tile>=end:
                raise ValueError('tile outside C ROM')
            while tile>=regions[region][1]: region+=1
            start,_,fa,fb=regions[region]
            if tile<start: raise ValueError('tile IDs must be sorted')
            for f in (fa,fb): f.seek((tile-start)*64)
            out.extend(sprite_tile(fa.read(64),fb.read(64)))
        return bytes(out)


def build(rom_dir, ids, output, capacity, cart_fix=False, bios_fix=None):
    ids, offsets, used=layout(ids,capacity,int(cart_fix)+int(bios_fix is not None))
    pairs=[(rom_dir/f'242-c{i}.bin',rom_dir/f'242-c{i+1}.bin') for i in (1,3,5,7)]
    if any(p.stat().st_size!=C_SIZE for pair in pairs for p in pair):
        raise ValueError('KOF98 requires eight 8 MiB C ROM files')
    assets=[(1,'sprite_index',b''.join(struct.pack('>I',i) for i in ids)),(2,'sprite_lut4',read_selected(pairs,ids))]
    for ident,name,path in [(3,'cart_fix',rom_dir/'242-s1.bin' if cart_fix else None),(4,'bios_fix',bios_fix)]:
        if path is not None:
            if path.stat().st_size!=FIX_SIZE: raise ValueError('expected 128 KiB FIX ROM')
            data=fix_tiles(path.read_bytes())
            if len(data)!=FIX_SIZE: raise ValueError('invalid FIX read')
            assets.append((ident,name,data))
    pack=bytearray(); records=[]
    for (ident,name,data),off in zip(assets,offsets):
        pack.extend(b'\0'*(off-len(pack))); pack.extend(data)
        records.append(dict(id=ident,name=name,source_offset=off,bytes=len(data),crc32=zlib.crc32(data)&0xffffffff))
    if len(pack)!=used: raise ValueError('pack length mismatch')
    # Do not overwrite an existing phase directory.
    output.mkdir(parents=True,exist_ok=False)
    (output/'TILES.BIN').write_bytes(pack)
    report={'format':'kof98-tile-pack-v1','tile_count':len(ids),'capacity':capacity,'bytes':used,'assets':records}
    (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    header='#ifndef KOF_LOCAL_TILE_MANIFEST_H\n#define KOF_LOCAL_TILE_MANIFEST_H\n#include "kof98/asset_store.h"\n'
    header+=f'#define KOF_LOCAL_TILE_COUNT {len(ids)}u\nstatic const kof_asset_desc kof_local_tile_assets[] = {{\n'
    for r in records: header+='    {%du, %du, %du, 0x%08Xu},\n'%(r['id'],r['source_offset'],r['bytes'],r['crc32'])
    header+='};\n#define KOF_LOCAL_TILE_ASSET_COUNT (sizeof(kof_local_tile_assets)/sizeof(kof_local_tile_assets[0]))\n#endif\n'
    (output/'tile_manifest.h').write_text(header)
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom_dir',type=Path); p.add_argument('selection',type=Path); p.add_argument('output',type=Path)
    p.add_argument('--capacity',type=int,required=True); p.add_argument('--cart-fix',action='store_true'); p.add_argument('--bios-fix',type=Path)
    a=p.parse_args()
    try: print(json.dumps(build(a.rom_dir,json.loads(a.selection.read_text()),a.output,a.capacity,a.cart_fix,a.bios_fix),indent=2))
    except (OSError,ValueError) as e: p.error(str(e))

if __name__=='__main__': main()
