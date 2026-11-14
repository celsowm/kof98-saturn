#!/usr/bin/env python3
"""Validate consecutive captures, measure churn, and pack bounded replay phases."""
import argparse
import csv
import json
import struct
import zlib
from pathlib import Path
from pack_frame import TileSource, compile_frame, ppm

CART=4*1024*1024
# Pinned LibSaturn uses 2*(64 KiB commands + 16 KiB Gouraud) + 32 B root.
# Leave ~32 KiB for SDK allocations and atlas alignment.
VRAM=320*1024
MAX_DRAWS=2048
CMD=struct.Struct(">8H")

def align(n,a): return (n+a-1)&-a

def resources(atlas,draws):
    patterns={atlas[t:t+w*h//2] for x,y,w,h,t,l,f in draws}
    palettes={atlas[l:l+32] for x,y,w,h,t,l,f in draws}
    return patterns,palettes

class Clip:
    def __init__(self):
        self.atlas=bytearray()
        self.locations={}
        self.commands=bytearray()
        self.frames=[]

    def append(self,atlas,draws,bg):
        # Trial allocation, committed only if both budgets fit.
        new=bytearray(self.atlas)
        loc=self.locations.copy()
        def alloc(data,a):
            key=(data,a)
            if key not in loc:
                new.extend(bytes((-len(new))%a))
                loc[key]=len(new)
                new.extend(data)
            return loc[key]
        commands=bytearray()
        for x,y,w,h,t,l,f in draws:
            p=atlas[t:t+w*h//2]; lut=atlas[l:l+32]
            if len(p)!=w*h//2 or len(lut)!=32: raise ValueError("bad atlas offsets")
            lt=alloc(lut,32);tx=alloc(p,8)
            if align(len(new),32)>VRAM: return False
            commands.extend(CMD.pack(x,y,w,h,tx//8,lt//8,f,0))
        if align(len(new),32)+len(self.commands)+len(commands)>CART: return False
        self.atlas=new;self.locations=loc
        self.frames.append((len(self.commands),len(draws),bg))
        self.commands.extend(commands)
        return True

    def finish(self):
        if not self.frames: raise ValueError("empty clip")
        size=align(len(self.atlas),32)
        payload=bytes(self.atlas)+bytes(size-len(self.atlas))+bytes(self.commands)
        return payload,size,[(size+o,n,bg) for o,n,bg in self.frames]

def build(captures,roms,out,start,count):
    out.mkdir(parents=True,exist_ok=True)
    for name in ("SEQUENCE.BIN","sequence_manifest.h","sequence_report.json","frames.csv","SEQUENCE.tmp"):
        (out/name).unlink(missing_ok=True)
    paths=[captures/f"frame-{i:08d}.kfr" for i in range(start,start+count)]
    if any(not p.is_file() for p in paths):
        raise ValueError("missing consecutive capture files")
    src=TileSource(roms)
    clip=Clip();clips=[];frames=[];metrics=[]
    prev_p=set();prev_l=set()
    all_p=set();all_l=set()
    total=0
    tmp=out/"SEQUENCE.tmp"
    def flush(f):
        nonlocal clip,total
        payload,atlas_size,rows=clip.finish()
        clips.append((total,len(payload),zlib.crc32(payload)&0xffffffff,atlas_size,len(frames),len(rows)))
        frames.extend(rows);f.write(payload);total+=len(payload);clip=Clip()
    try:
        with tmp.open("wb") as f:
            for i,p in enumerate(paths,start):
                bg,ref,final,atlas,draws,r=compile_frame(p,src)
                if r["rgb555_mismatches"] or len(set(bg))!=1 or not 0<len(draws)<=MAX_DRAWS:
                    ppm(out/"failed-reference.ppm",ref);ppm(out/"failed-replay.ppm",final)
                    raise ValueError(f"frame {i} rejected: {r}")
                pats,luts=resources(atlas,draws)
                new_p=pats-prev_p;new_l=luts-prev_l
                r.update(frame=i,new_patterns=len(new_p),new_palettes=len(new_l),
                         ideal_upload_bytes=sum(map(len,new_p))+sum(map(len,new_l)),
                         pattern_reuse=len(pats&prev_p))
                metrics.append(r)
                all_p.update(pats);all_l.update(luts);prev_p=pats;prev_l=luts
                if not clip.append(atlas,draws,bg[0]):
                    if not clip.frames: raise ValueError(f"frame {i} exceeds replay budget")
                    flush(f)
                    if not clip.append(atlas,draws,bg[0]): raise ValueError(f"frame {i} exceeds replay budget")
                if (i-start)%30==0 or i==start+count-1: print(f"validated {i-start+1}/{count}: {len(draws)} commands",flush=True)
            flush(f)
        if len(frames)!=count: raise ValueError("frame count mismatch")
        with (out/"frames.csv").open("w",newline="") as f:
            w=csv.DictWriter(f,fieldnames=list(metrics[0]));w.writeheader();w.writerows(metrics)
        report=dict(frames=count,first_frame=start,last_frame=start+count-1,clips=len(clips),
                    disc_payload_bytes=total,max_clip_bytes=max(c[1] for c in clips),
                    max_clip_atlas_bytes=max(c[3] for c in clips),atlas_budget_bytes=VRAM,peak_commands=max(r["commands"] for r in metrics),
                    mean_commands=sum(r["commands"] for r in metrics)/count,
                    peak_fix_commands=max(r["fix_commands"] for r in metrics),
                    unique_patterns=len(all_p),unique_palettes=len(all_l),
                    peak_ideal_upload_bytes=max(r["ideal_upload_bytes"] for r in metrics),
                    mean_ideal_upload_bytes_after_first=(sum(r["ideal_upload_bytes"] for r in metrics[1:])/(count-1) if count>1 else None),
                    rgb555_mismatches=sum(r["rgb555_mismatches"] for r in metrics),
                    clip_frame_counts=[c[5] for c in clips])
        header="#ifndef KOF_SEQUENCE_MANIFEST_H\n#define KOF_SEQUENCE_MANIFEST_H\n"
        header+=f"#define SEQ_MAX_ATLAS {report['max_clip_atlas_bytes']}u\n"
        header+="static const seq_clip seq_clips[] = {\n"
        for c in clips: header+="  {%du,%du,0x%08xu,%du,%du,%du},\n"%c
        header+="};\nstatic const seq_frame seq_frames[] = {\n"
        for row in frames: header+="  {%du,%du,0x%04xu},\n"%row
        header+="};\n#endif\n"
        tmp.replace(out/"SEQUENCE.BIN")
        (out/"sequence_manifest.h").write_text(header)
        (out/"sequence_report.json").write_text(json.dumps(report,indent=2)+"\n")
        print(json.dumps(report,indent=2))
    except BaseException:
        tmp.unlink(missing_ok=True)
        for name in ("SEQUENCE.BIN","sequence_manifest.h"):
            (out/name).unlink(missing_ok=True)
        raise

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("captures","roms","output"): p.add_argument(name,type=Path)
    p.add_argument("--start",type=int,required=True)
    p.add_argument("--count",type=int,default=600)
    a=p.parse_args()
    if a.start<0 or not 1<=a.count<=600: p.error("invalid frame range")
    try: build(a.captures,a.roms,a.output,a.start,a.count)
    except (ValueError,OSError,struct.error) as e: p.error(str(e))
