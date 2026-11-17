#!/usr/bin/env python3
"""Compile captured sprite strips to a deduplicated, static VDP1 frame atlas."""
import argparse
from pathlib import Path
import struct
import zlib
import json
from pack_tiles import read_selected, C_SIZE, fix_tiles

W,H=320,224
RECORD=struct.Struct(">I6H16H16B")

def rgb(w):
    return 0x8000 | ((w>>7)&30) | ((w>>14)&1) | ((((w>>3)&30)|((w>>13)&1))<<5) | ((((w<<1)&30)|((w>>12)&1))<<10)

def read_capture(path):
    data=path.read_bytes()
    if len(data)<8 or data[:4] not in (b"KFR1",b"KFR2"): raise ValueError("bad capture magic")
    count=struct.unpack_from(">I",data,4)[0]
    start=8+2*H+2*W*H
    if not 1<=count<=224*(96+40) or len(data)!=start+count*RECORD.size: raise ValueError("bad capture size")
    bg=struct.unpack_from(f">{H}H",data,8)
    ref=struct.unpack_from(f">{W*H}H",data,8+2*H)
    records=list(RECORD.iter_unpack(data[start:]))
    if data[:4]==b'KFR1' and any(r[0]>=524288 for r in records): raise ValueError('invalid KFR1 tile')
    return bg,ref,records

def pixels(rec, raw):
    tile,x,y,mask,h,w,flip=rec[:7]
    rows=rec[23:23+h]
    cols=[i for i in range(16) if mask&(1<<i)]
    if not (0<=tile<532480 and 1<=h<=16 and 1<=w<=16 and len(cols)==w and x+w<=W and y+h<=H and flip in (0,1) and all(r<16 for r in rows)):
        raise ValueError("invalid strip")
    padded=(w+7)&~7
    out=bytearray(h*padded//2)
    for j,r in enumerate(rows):
        for k,c in enumerate(cols):
            c=15-c if flip else c
            p=(raw[r*8+c//2]>>(0 if c&1 else 4))&15
            out[j*padded//2+k//2]|=p<<(0 if k&1 else 4)
    return padded,bytes(out)

def ppm(path, image):
    def channel(v): return (v<<3)|(v>>2)
    path.write_bytes(b"P6\n320 224\n255\n"+bytes(channel((v>>s)&31) for v in image for s in (0,5,10)))

class TileSource:
    def __init__(self,roms):
        self.pairs=[(roms/f"242-c{i}.bin",roms/f"242-c{i+1}.bin") for i in (1,3,5,7)]
        if any(p.stat().st_size!=C_SIZE for pair in self.pairs for p in pair): raise ValueError("incorrect C ROM sizes")
        self.roms=roms
        self.cache={}
        self.fix=None

    def get(self,ids):
        missing=sorted(i for i in ids if i<524288 and i not in self.cache)
        if missing:
            raw=read_selected(self.pairs,missing)
            self.cache.update({i:raw[k*128:(k+1)*128] for k,i in enumerate(missing)})
        fix=[i for i in ids if i>=524288 and i not in self.cache]
        if fix:
            if self.fix is None:
                self.fix=[fix_tiles((self.roms/name).read_bytes()) for name in ("242-s1.bin","sfix.sfix")]
                if any(len(b)!=131072 for b in self.fix): raise ValueError("invalid FIX ROM size")
            for i in fix:
                bank,code=divmod(i-524288,4096)
                if not 0<=bank<2: raise ValueError("invalid FIX tile")
                source=self.fix[bank][code*32:code*32+32]
                # Embed 8x8 in the left half of a 16x16 source tile.
                self.cache[i]=b"".join(source[y*4:y*4+4]+bytes(4) for y in range(8))+bytes(64)
        return self.cache

def compile_frame(capture,source):
    bg,ref,records=read_capture(capture)
    ids=sorted({r[0] for r in records})
    tiles=source.get(ids)
    image=[v for v in bg for _ in range(W)]
    atlas=bytearray(); locations={}; draws=[]; palettes=set(); patterns=set()
    def alloc(data,alignment):
        key=(data,alignment)
        if key not in locations:
            atlas.extend(bytes((-len(atlas))%alignment))
            locations[key]=len(atlas);atlas.extend(data)
        return locations[key]
    skipped=0; native=0; fix_commands=0
    for r in records:
        tile,x,y,mask,h,w,flip=r[:7]
        padded,pattern=pixels(r,tiles[tile])
        if not any(pattern): skipped+=1;continue
        lut=(0,)+tuple(rgb(v) for v in r[8:23])
        for j in range(h):
            for k in range(w):
                pen=(pattern[j*padded//2+k//2]>>(0 if k&1 else 4))&15
                if pen:image[(y+j)*W+x+k]=lut[pen]
        rows=r[23:23+h]
        normal=mask==65535 and h==16 and (rows==tuple(range(16)) or rows==tuple(range(15,-1,-1)))
        flags=0
        if tile>=524288: fix_commands+=1
        if normal:
            pattern=tiles[tile];flags=(0x20 if flip else 0)|(0x40 if rows[0]==15 else 0);native+=1
        lutdata=struct.pack(">16H",*lut)
        lutoff=alloc(lutdata,32);texoff=alloc(pattern,8)
        palettes.add(lutoff);patterns.add(texoff)
        draws.append((x,y,padded,h,texoff,lutoff,flags))
    # Independently replay the FINAL atlas commands (including native HW flips).
    final=[v for v in bg for _ in range(W)]
    for x,y,w,h,tex,lut,flags in draws:
        for j in range(h):
            sy=h-1-j if flags&0x40 else j
            for k in range(w):
                if x+k>=W: continue
                sx=w-1-k if flags&0x20 else k
                pen=(atlas[tex+sy*(w//2)+sx//2]>>(0 if sx&1 else 4))&15
                if pen: final[(y+j)*W+x+k]=struct.unpack_from(">H",atlas,lut+pen*2)[0]
    if final!=image: raise ValueError("atlas command replay differs from strip raster")
    mismatches=sum(a!=b for a,b in zip(final,ref))
    report=dict(strips=len(records),fix_commands=fix_commands,transparent_strips=skipped,commands=len(draws),native=native,variants=len(draws)-native,unique_source_tiles=len(ids),unique_patterns=len(patterns),unique_palettes=len(palettes),vram_bytes=len(atlas),rgb555_mismatches=mismatches,backdrop_colors=len(set(bg)))
    return bg,ref,final,bytes(atlas),draws,report

def build(capture,roms,out):
    out.mkdir(parents=True,exist_ok=True)
    for name in ("FRAME.BIN","frame_manifest.h"):
        (out/name).unlink(missing_ok=True)
    bg,ref,image,atlas,draws,report=compile_frame(capture,TileSource(roms))
    atlas=bytearray(atlas)
    mismatches=report["rgb555_mismatches"]
    ppm(out/"reference.ppm",ref);ppm(out/"replay.ppm",image)
    (out/"report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))
    if mismatches: raise ValueError("oracle mismatch; inspect PPM files")
    if len(set(bg))!=1: raise ValueError("raster backdrop unsupported for this first replay")
    if not draws or len(draws)>2048 or ((len(atlas)+31)&~31)>320*1024: raise ValueError("static frame exceeds initial replay budget")
    atlas.extend(bytes((-len(atlas))%32))
    (out/"FRAME.BIN").write_bytes(atlas)
    header='#ifndef KOF_FRAME_MANIFEST_H\n#define KOF_FRAME_MANIFEST_H\n'
    header+=f'#define FRAME_BG 0x{bg[0]:04x}u\n#define FRAME_BYTES {len(atlas)}u\n#define FRAME_CRC 0x{zlib.crc32(atlas):08x}u\n'
    header+='static const frame_draw frame_draws[] = {\n'
    for d in draws: header+='  {%du,%du,%du,%du,%du,%du,%du},\n'%d
    header+='};\n#endif\n'
    (out/"frame_manifest.h").write_text(header)

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("capture",type=Path);p.add_argument("roms",type=Path);p.add_argument("output",type=Path)
    a=p.parse_args()
    try: build(a.capture,a.roms,a.output)
    except (ValueError,OSError,struct.error) as e: p.error(str(e))
