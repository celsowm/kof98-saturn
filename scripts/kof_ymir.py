#!/usr/bin/env python3
"""External LibSaturn/Ymir harness integration; no emulator linked into the game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
SYMBOLS=("kof_sequence_stage","kof_sequence_frames","kof_sequence_clip",
         "kof_sequence_index","kof_sequence_commands","kof_sequence_vram_free",
         "kof_sequence_vram_requested")
STAGES={0:"not started",1:"CD initialization",2:"CD to cart",
        3:"VDP1 atlas upload",4:"playback"}

def executable(name):
    return shutil.which(name)

def run_checked(cmd,**kw):
    print("+",subprocess.list2cmdline([str(x) for x in cmd]),flush=True)
    subprocess.run([str(x) for x in cmd],check=True,**kw)

def resolve_probe(a,sdk):
    if a.probe:
        p=a.probe.resolve()
        if not p.is_file():raise ValueError(f"probe missing: {p}")
        return p
    if os.getenv("LIBSATURN_YMIR_PROBE"):
        p=Path(os.environ["LIBSATURN_YMIR_PROBE"]).resolve()
        if not p.is_file():raise ValueError(f"probe missing: {p}")
        return p
    suffix=".exe" if os.name=="nt" else ""
    dest=ROOT/"build-ymir"
    for folder in (dest,sdk/"harness/build",sdk/"harness/build-macos"):
        p=folder/("probe"+suffix)
        if p.is_file():return p
    cmd=["cmake","-S",sdk/"harness","-B",dest,"-G","Ninja","-DCMAKE_BUILD_TYPE=Release"]
    env=os.environ.copy()
    if os.name=="nt":
        msys=Path(os.getenv("LIBSATURN_MSYS2_ROOT",r"C:\msys64"))
        native=msys/"ucrt64/bin"
        cxx=native/"g++.exe"
        if not cxx.is_file():raise ValueError("UCRT64 g++ missing; use --probe")
        env["PATH"]=os.pathsep.join(map(str,[native,msys/"usr/bin",env.get("PATH","")]))
        cmd.append("-DCMAKE_CXX_COMPILER="+str(cxx).replace("\\","/"))
    run_checked(cmd,env=env)
    run_checked(["cmake","--build",dest,"--target","probe","-j",str(a.jobs)],env=env)
    p=dest/("probe"+suffix)
    if not p.is_file():raise ValueError(f"probe build did not produce {p}")
    return p

def resolve_symbols(elf,build):
    nm=executable("sh2eb-elf-nm")
    if not nm and os.name=="nt":
        p=Path.home() # toolchain may be installed in MSYS2 home
        msys=Path(os.getenv("LIBSATURN_MSYS2_ROOT",r"C:\msys64"))
        candidate=msys/"home"/p.name/"saturn-tools/bin/sh2eb-elf-nm.exe"
        if candidate.is_file():nm=str(candidate)
    found={}
    if nm:
        result=subprocess.run([nm,"-S","--defined-only",str(elf)],text=True,capture_output=True)
        if result.returncode==0:
            for line in result.stdout.splitlines():
                m=re.match(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\w\s+_?(kof_sequence_\w+)$",line)
                if m and int(m[2],16)==4:found[m[3]]=int(m[1],16)
    for p in (elf.with_suffix(".elf.map"),elf.with_suffix(".map"),build/"kof_sequence_replay.map"):
        if not p.is_file():continue
        for line in p.read_text(errors="replace").splitlines():
            m=re.match(r"^\s*(0x[0-9a-fA-F]+)\s+_?(kof_sequence_\w+)\s*$",line)
            if m:found.setdefault(m[2],int(m[1],16))
    missing=set(SYMBOLS)-found.keys()
    if missing:raise ValueError(f"missing ELF/map symbols: {sorted(missing)}")
    return {k:found[k] for k in SYMBOLS}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--libsaturn",type=Path,default=Path(os.getenv("LIBSATURN_ROOT",str(ROOT.parent/"libsaturn"))))
    p.add_argument("--probe",type=Path)
    p.add_argument("--bios",type=Path)
    p.add_argument("--build",type=Path,default=ROOT/"build-saturn")
    p.add_argument("--out",type=Path)
    p.add_argument("--frames",type=int,default=2400,help="emulated fields, NOT gameplay frames")
    p.add_argument("--jobs",type=int,default=6)
    p.add_argument("--timeout",type=int,default=1200)
    p.add_argument("--pc",action="store_true",help="optional PC profile (slower)")
    a=p.parse_args()
    if min(a.frames,a.jobs,a.timeout)<1:p.error("positive limits required")
    sdk=a.libsaturn.resolve();build=a.build.resolve()
    bios=(a.bios or Path(os.getenv("LIBSATURN_BIOS",str(sdk/"bios/saturn_bios_jp.bin")))).resolve()
    elf=build/"kof_sequence_replay.elf"
    app=build/"kof_sequence_replay.app.bin"
    disc=build/"disc/kof_sequence_replay.cue"
    for f in (elf,app,disc,bios):
        if not f.is_file():p.error(f"missing: {f}")
    if bios.stat().st_size!=524288:p.error("expected 512 KiB Saturn IPL")
    out=(a.out or ROOT/"local/ymir"/time.strftime("%Y%m%d-%H%M%S")).resolve()
    out.mkdir(parents=True,exist_ok=False)
    try:
        addresses=resolve_symbols(elf,build)
        probe=resolve_probe(a,sdk)
        # All output paths in compound arguments are relative to cwd: avoids Windows drive colon ambiguity.
        cmd=[probe,"--boot-mode","injection","--iso",disc,"--bin",app,"--bios",bios,
             "--boot-frames","90","--frames",str(a.frames),"--sh2-cache","on","--ram-cart","4m",
             "--out","probe.json","--video-observations","video.csv",
             "--profile-instructions","instructions.csv","--profile-transfers","transfers.csv",
             "--misaligned-report","misaligned.txt"]
        if a.pc:cmd+=["--profile-pc","pc.csv"]
        for key,address in addresses.items():
            cmd+=["--dump-mem",f"0x{address:08x}:4:{key}.bin"]
        for count in (1,60,145,300,600):
            cmd+=["--screenshot-when",f"0x{addresses['kof_sequence_frames']:08x}:{count}+2:submitted-{count:04d}.png"]
        for field in sorted({0,min(119,a.frames-1),a.frames-1}):
            cmd+=["--screenshot",f"{field}:field-{field:05d}.png"]
        with (out/"probe.log").open("w",encoding="utf-8") as log:
            run_checked(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT,timeout=a.timeout)
        state={k:struct.unpack(">I",(out/(k+".bin")).read_bytes())[0] for k in addresses}
        report=json.loads((out/"probe.json").read_text())
        boot=report.get("boot",{})
        state["phase"]=STAGES.get(state["kof_sequence_stage"],"unknown")
        state["emulated_fields"]=report.get("frames_run",a.frames)
        state["cache"]="on"
        state["boot"]=boot
        state["app_sha256"]=hashlib.sha256(app.read_bytes()).hexdigest()
        state["probe_sha256"]=hashlib.sha256(probe.read_bytes()).hexdigest()
        state["submitted_per_nominal_second_including_loads"]=60*state["kof_sequence_frames"]/state["emulated_fields"]
        state["full_sequence_submitted"]=state["kof_sequence_frames"]>=600
        state["note"]="Submissions include repeated loops; not pixel-distinct FPS or physical console timing. Injection is not optical boot."
        (out/"summary.json").write_text(json.dumps(state,indent=2)+"\n")
        print(json.dumps(state,indent=2))
        print(f"Evidence: {out}")
        if state["kof_sequence_frames"]==0:raise ValueError("no gameplay submissions: inspect phase, screenshots and probe.log")
    except (OSError,ValueError,subprocess.SubprocessError) as e:
        p.exit(1,f"{e}\nEvidence retained: {out}\n")

if __name__=="__main__":main()
