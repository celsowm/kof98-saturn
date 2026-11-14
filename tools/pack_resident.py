#!/usr/bin/env python3
"""Repack a validated clip sequence into a single resident replay with a persistent VDP1 atlas."""
import argparse
from collections import OrderedDict
from difflib import SequenceMatcher
import json
from pathlib import Path
import re
import struct
import zlib

CART=4*1024*1024
PAT_SLOTS=2304
LUT_SLOTS=128
ATLAS=PAT_SLOTS*128+LUT_SLOTS*32
DRAW=struct.Struct(">8H")
LITERAL=struct.Struct(">IHH")
U16=struct.Struct(">H")
UPDATE=struct.Struct(">HH")

class Cache:
    def __init__(self,slots):
        self.slots=slots
        self.live=OrderedDict()
        self.data={}
    def assign(self,ids):
        needed=set(ids)
        if len(needed)>self.slots:raise ValueError("frame exceeds cache slots")
        updates=[]
        # Touch the whole live set used by this frame before evicting anything.
        for key in dict.fromkeys(ids):
            if key in self.live:self.live.move_to_end(key)
        for key in dict.fromkeys(ids):
            if key in self.live:continue
            if len(self.live)<self.slots:
                slot=len(self.live)
            else:
                victim=next(k for k in self.live if k not in needed)
                slot=self.live.pop(victim)
            self.live[key]=slot
            self.data[slot]=key
            updates.append((slot,key))
        return updates

def delta(old,new):
    """Copy exact command runs from previous frame, or emit compact literals."""
    out=bytearray();pos=0
    for a,b,n in SequenceMatcher(None,old,new,autojunk=False).get_matching_blocks():
        if b>pos:
            out+=U16.pack(b-pos)
            for row in new[pos:b]:out+=LITERAL.pack(*row)
        if n:
            out+=struct.pack(">HH",0x8000|n,a)
        pos=b+n
    return out

def decode_delta(data,old,count):
    out=[];p=0
    while p<len(data):
        token,=U16.unpack_from(data,p);p+=2
        n=token&0x7fff
        if not n or len(out)+n>count:raise ValueError("bad delta run")
        if token&0x8000:
            start,=U16.unpack_from(data,p);p+=2
            if start+n>len(old):raise ValueError("bad delta reference")
            out.extend(old[start:start+n])
        else:
            for _ in range(n):
                out.append(LITERAL.unpack_from(data,p));p+=8
    if len(out)!=count:raise ValueError("bad delta count")
    return out

def source_rows(directory):
    manifest=(directory/"sequence_manifest.h").read_text()
    if "SEQ_RESIDENT" in manifest:raise ValueError("use original clip pack as input")
    raw=(directory/"SEQUENCE.BIN").read_bytes()
    def rows(name,n):
        m=re.search(r"\b"+name+r"\[\]\s*=\s*\{(.*?)\};",manifest,re.S)
        if not m:raise ValueError("missing "+name)
        result=[]
        for row in re.findall(r"\{([^{}]+)\}",m[1]):
            vals=tuple(int(v.strip().rstrip("uU"),0) for v in row.split(","))
            if len(vals)!=n:raise ValueError("invalid "+name)
            result.append(vals)
        return result
    clips=rows("seq_clips",6);frames=rows("seq_frames",3)
    if not 1<=len(frames)<=600:raise ValueError("invalid frame count")
    next_frame=0
    for off,size,crc,atlas,first,count in clips:
        payload=raw[off:off+size]
        if len(payload)!=size or zlib.crc32(payload)&0xffffffff!=crc:raise ValueError("source CRC")
        if first!=next_frame or first+count>len(frames) or atlas>size:raise ValueError("source clips")
        for pos,n,bg in frames[first:first+count]:
            if not 0<n<=2048 or pos<atlas or pos+n*16>size:raise ValueError("source frames")
            draw=[]
            for j in range(n):
                x,y,w,h,t,l,f,r=DRAW.unpack_from(payload,pos+j*16)
                t*=8;l*=8
                if not (x<320 and y+h<=224 and w in (8,16) and 1<=h<=16 and not f&~0x60 and not r and t+w*h//2<=atlas and l+32<=atlas):raise ValueError("source draw")
                draw.append((x,y,w,h,f,payload[t:t+w*h//2],payload[l:l+32]))
            yield bg,draw
        next_frame+=count
    if next_frame!=len(frames):raise ValueError("source frame coverage")

def build(source,out):
    if source.resolve()==out.resolve():raise ValueError("separate output directory required")
    out.mkdir(parents=True,exist_ok=True)
    for name in ("SEQUENCE.BIN","sequence_manifest.h","sequence_report.json"):
        (out/name).unlink(missing_ok=True)
    pats={};luts={};pattern_data=[];lut_data=[]
    pc=Cache(PAT_SLOTS);lc=Cache(LUT_SLOTS)
    old=[];packets=bytearray();frames=[];uploads=[];delta_bytes=0
    def intern(table,items,data):
        if data not in table:
            if len(items)>=65536:raise ValueError("dictionary exceeds 16-bit indices")
            table[data]=len(items);items.append(data)
        return table[data]
    for i,(bg,draw) in enumerate(source_rows(source)):
        ids=[(intern(pats,pattern_data,p.ljust(128,b"\0")),intern(luts,lut_data,l)) for x,y,w,h,f,p,l in draw]
        updates_p=pc.assign([p for p,l in ids]);updates_l=lc.assign([l for p,l in ids])
        updates=sorted(updates_p)+[(0x8000|slot,key) for slot,key in sorted(updates_l)]
        new=[]
        for (x,y,w,h,f,p,l),(pid,lid) in zip(draw,ids):
            slot=pc.live[pid];ls=lc.live[lid]
            xy=x|(y<<9)|((w==16)<<17)|((h-1)<<18)|((f>>5)<<22)
            new.append((xy,slot,ls))
            if pattern_data[pc.data[slot]][:len(p)]!=p or lut_data[lc.data[ls]]!=l:raise ValueError("cache verification failed")
        commands=delta(old,new)
        if decode_delta(commands,old,len(new))!=new:raise ValueError("delta verification failed")
        packet=U16.pack(len(updates))+b"".join(UPDATE.pack(*u) for u in updates)+commands
        if len(packet)>128*1024:raise ValueError("packet exceeds WRAM scratch")
        frames.append((len(packets),len(packet),len(new),bg))
        packets+=packet;old=new
        uploads.append(len(updates_p)*128+len(updates_l)*32);delta_bytes+=len(commands)
        if i%60==0:print(f"resident frame {i+1}: {len(updates)} uploads, {len(packet)} packet bytes",flush=True)
    if not frames:raise ValueError("empty replay")
    patterns=b"".join(pattern_data);palettes=b"".join(lut_data)
    prefix=patterns+palettes;payload=prefix+packets
    report=dict(frames=len(frames),resident=True,disc_payload_bytes=len(payload),
                cart_budget=CART,atlas_bytes=ATLAS,pattern_slots=PAT_SLOTS,lut_slots=LUT_SLOTS,
                unique_padded_patterns=len(pattern_data),unique_palettes=len(lut_data),
                dictionary_bytes=len(prefix),packet_bytes=len(packets),delta_bytes=delta_bytes,
                max_packet_bytes=max(f[1] for f in frames),first_upload_bytes=uploads[0],
                mean_upload_bytes_after_first=sum(uploads[1:])/max(1,len(uploads)-1),
                peak_upload_bytes=max(uploads),max_steady_upload_bytes=max(uploads[1:],default=0),
                source_command_resource_mismatches=0)
    (out/"sequence_report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))
    if len(payload)>CART:raise ValueError("resident pack exceeds 4 MiB; no buildable output")
    header="#ifndef KOF_SEQUENCE_MANIFEST_H\n#define KOF_SEQUENCE_MANIFEST_H\n#define SEQ_RESIDENT 1\n"
    for key,val in dict(SEQ_MAX_ATLAS=ATLAS,SEQ_RESIDENT_BYTES=len(payload),SEQ_RESIDENT_CRC=zlib.crc32(payload)&0xffffffff,
                        SEQ_PAT_SLOTS=PAT_SLOTS,SEQ_LUT_SLOTS=LUT_SLOTS,
                        SEQ_PATTERN_COUNT=len(pattern_data),SEQ_PALETTE_COUNT=len(lut_data),
                        SEQ_PALETTE_OFFSET=len(patterns),SEQ_PACKET_OFFSET=len(prefix)).items():
        header+=f"#define {key} {val}u\n"
    header+="static const seq_resident_frame seq_resident_frames[] = {\n"
    for off,size,count,bg in frames:header+=f"  {{{len(prefix)+off}u,{size}u,{count}u,{bg}u}},\n"
    header+="};\n#endif\n"
    (out/"SEQUENCE.BIN").write_bytes(payload)
    (out/"sequence_manifest.h").write_text(header)

if __name__=="__main__":
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument("source",type=Path);a.add_argument("output",type=Path)
    p=a.parse_args()
    try:build(p.source,p.output)
    except (ValueError,OSError,struct.error) as e:a.error(str(e))
