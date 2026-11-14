#!/usr/bin/env python3
"""Bounded raw LSPC raster trace and original tile bank for Saturn runtime replay."""
import argparse
import json
from pathlib import Path
import struct
import zlib
from pack_frame import TileSource,read_capture,RECORD

WORDS=0x8580+4096
LIMIT=4*1024*1024
H=224
WORD=struct.Struct(">H")
STATE=struct.Struct(f">{WORDS}H")
LINE=struct.Struct(">3H")
CHANGE=struct.Struct(">2H")

def fnv(data):
    h=2166136261
    for v in data:h=((h^v)*16777619)&0xffffffff
    return h

def lines(path):
    data=path.read_bytes()
    if data[:4]!=b"KST1" or len(data)<4+STATE.size:raise ValueError("bad KST1")
    initial=list(STATE.unpack_from(data,4))
    pos=4+STATE.size
    for y in range(H):
        anim,bios,n=LINE.unpack_from(data,pos);pos+=LINE.size
        if anim>7 or bios>1 or n>WORDS or (y==0 and n):raise ValueError("bad raster header")
        changes=[];last=-1
        for _ in range(n):
            idx,val=CHANGE.unpack_from(data,pos);pos+=CHANGE.size
            if not last<idx<WORDS:raise ValueError("bad raster index")
            last=idx;changes.append((idx,val))
        yield initial if y==0 else None,anim,bios,changes
    if pos!=len(data):raise ValueError("trailing state bytes")

def packet(path,previous):
    out=bytearray()
    state=previous
    first=None
    for initial,anim,bios,changes in lines(path):
        if initial is not None:
            if state is None:
                first=STATE.pack(*initial);changes=[]
            else:
                changes=[(i,v) for i,v in enumerate(initial) if v!=state[i]]
            state=initial[:]
        out+=LINE.pack(anim,bios,len(changes))
        for i,v in changes:
            out+=CHANGE.pack(i,v);state[i]=v
    if len(out)>128*1024:raise ValueError("raster packet exceeds scratch budget")
    return first,bytes(out),state

def load_zoom(roms):
    # The pinned NeoGeoRecomp video_load() copies exactly sizeof(s_zoom_rom)
    # (0x10000 bytes) from the beginning of 000-lo.lo. Match that behavior
    # for larger dumps, but never pad a truncated ROM.
    path=roms/"000-lo.lo"
    if not path.is_file():path=roms/"000-lo.bin"
    raw=path.read_bytes()
    if len(raw)<65536:
        raise ValueError(f"L0 ROM {path} has {len(raw)} bytes; need at least 65536")
    return raw[:65536],len(raw)

def build(captures,roms,out,start,count):
    if not 1<=count<=60 or start<0:raise ValueError("select 1..60 frames")
    out.mkdir(parents=True,exist_ok=True)
    for name in ("SEQUENCE.BIN","sequence_manifest.h","sequence_report.json"):
        (out/name).unlink(missing_ok=True)
    ids=set();rows=[];packets=bytearray();prev=None;initial=None
    max_sprite=max_fix=0
    for i in range(start,start+count):
        stem=captures/f"frame-{i:08d}"
        bg,_,records=read_capture(stem.with_suffix(".kfr"))
        if len(set(bg))!=1:raise ValueError("raster backdrop not supported yet")
        sp=sum(r[0]<524288 for r in records);fx=len(records)-sp
        if not sp or not fx or sp+fx>2045:raise ValueError("strip budget")
        if any(r[0]<524288 for r in records[sp:]):raise ValueError("FIX suffix")
        max_sprite=max(max_sprite,sp);max_fix=max(max_fix,fx)
        ids.update(r[0] for r in records)
        first,p,prev=packet(stem.with_suffix(".kst"),prev)
        if first is not None:initial=first
        expected=fnv(b"".join(RECORD.pack(*r) for r in records))
        rows.append((len(packets),len(p),sp,fx,bg[0],expected))
        packets+=p
        print(f"state {i}: {sp}+{fx} strips, {len(p)} raster bytes",flush=True)
    if initial is None:raise ValueError("missing initial state")
    ids=sorted(ids);tiles=TileSource(roms).get(ids)
    zoom,zoom_source_bytes=load_zoom(roms)
    print(f"L0 ROM: using {len(zoom)} of {zoom_source_bytes} bytes (reference loader)",flush=True)
    # The first packet (frame zero) has no line-zero deltas. At each loop
    # restore the original snapshot; all other frames are cross-frame deltas.
    prefix=initial+zoom+b"".join(struct.pack(">I",i) for i in ids)
    tiles_offset=len(prefix);prefix+=b"".join(tiles[i] for i in ids)
    payload=prefix+packets
    report=dict(frames=count,first_frame=start,live_lspc=True,cart_bytes=len(payload),
                cart_budget=LIMIT,raw_tiles=len(ids),trace_bytes=len(packets),
                zoom_source_bytes=zoom_source_bytes,zoom_used_bytes=len(zoom),
                zoom_crc32=zlib.crc32(zoom)&0xffffffff,
                max_packet=max(r[1] for r in rows),max_sprite_strips=max_sprite,max_fix_strips=max_fix)
    (out/"sequence_report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))
    if len(payload)>LIMIT:raise ValueError("raw state pack exceeds cart capacity")
    vals=dict(LIVE_BYTES=len(payload),LIVE_CRC=zlib.crc32(payload)&0xffffffff,
              LIVE_ZOOM_OFFSET=len(initial),LIVE_INDEX_OFFSET=len(initial)+len(zoom),
              LIVE_TILES_OFFSET=tiles_offset,LIVE_TILE_COUNT=len(ids),
              LIVE_MAX_SPRITES=max_sprite,LIVE_MAX_FIX=max_fix)
    header="#ifndef KOF_SEQUENCE_MANIFEST_H\n#define KOF_SEQUENCE_MANIFEST_H\n#define SEQ_LIVE 1\n"
    for k,v in vals.items():header+=f"#define {k} {v}u\n"
    header+="static const seq_live_frame seq_live_frames[]={\n"
    for off,size,sp,fx,bg,hash_ in rows:header+=f"  {{{len(prefix)+off}u,{size}u,{sp}u,{fx}u,{bg}u,{hash_}u}},\n"
    header+="};\n#endif\n"
    (out/"SEQUENCE.BIN").write_bytes(payload)
    (out/"sequence_manifest.h").write_text(header)

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("captures","roms","output"):p.add_argument(name,type=Path)
    p.add_argument("--start",type=int,default=4350);p.add_argument("--count",type=int,default=16)
    a=p.parse_args()
    try:build(a.captures,a.roms,a.output,a.start,a.count)
    except (ValueError,OSError,struct.error) as e:p.error(str(e))
