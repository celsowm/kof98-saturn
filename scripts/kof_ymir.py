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
         "kof_sequence_vram_requested","kof_sequence_cd_loads",
         "kof_sequence_upload_bytes","kof_sequence_upload_runs")
STAGES={0:"not started",1:"CD initialization",2:"CD to cart",
        3:"VDP1 atlas upload",4:"playback",5:"live LSPC decode"}

def executable(name):
    return shutil.which(name)

def run_checked(cmd,**kw):
    print("+",subprocess.list2cmdline([str(x) for x in cmd]),flush=True)
    subprocess.run([str(x) for x in cmd],check=True,**kw)

REQUIRED_PROBE_OPTIONS=("--sh2-cache","--video-observations","--profile-instructions",
                        "--profile-transfers","--ram-cart","--dump-mem","--screenshot-when")

def missing_probe_options(probe):
    # Older probes exit 2 for --help but print their supported CLI usage.
    r=subprocess.run([str(probe),"--help"],capture_output=True,text=True,
                     errors="replace",timeout=15)
    usage=r.stdout+r.stderr
    return [opt for opt in REQUIRED_PROBE_OPTIONS if opt not in usage]

def verify_probe(probe):
    if not probe.is_file():raise ValueError(f"probe missing: {probe}")
    missing=missing_probe_options(probe)
    if missing:raise ValueError(f"outdated probe {probe}: missing {', '.join(missing)}")
    return probe

def resolve_probe(a,sdk):
    if a.probe:
        return verify_probe(a.probe.resolve())
    if os.getenv("LIBSATURN_YMIR_PROBE"):
        return verify_probe(Path(os.environ["LIBSATURN_YMIR_PROBE"]).resolve())
    suffix=".exe" if os.name=="nt" else ""
    dest=ROOT/"build-ymir"
    for folder in (dest,sdk/"harness/build",sdk/"harness/build-macos"):
        p=folder/("probe"+suffix)
        if p.is_file():
            missing=missing_probe_options(p)
            if not missing:
                print(f"Using compatible Ymir probe: {p}",flush=True)
                return p
            print(f"Skipping outdated probe {p}: {', '.join(missing)}",flush=True)
    source=sdk/"harness/src/probe_main.cpp"
    if not source.is_file():raise ValueError(f"missing harness source: {source}")
    missing=[opt for opt in REQUIRED_PROBE_OPTIONS if opt not in source.read_text(encoding="utf-8")]
    if missing:
        raise ValueError(f"LibSaturn harness source is outdated ({', '.join(missing)}). "
                         f"Update {sdk} with git pull --ff-only, preserving local changes.")
    cmd=["cmake","-S",sdk/"harness","-B",dest,"-G","Ninja","-DCMAKE_BUILD_TYPE=Release"]
    env=os.environ.copy()
    if os.name=="nt":
        msys=Path(os.getenv("LIBSATURN_MSYS2_ROOT",r"C:\msys64"))
        native=msys/"ucrt64/bin"
        cxx=native/"g++.exe"
        if not cxx.is_file():raise ValueError("UCRT64 g++ missing; use --probe")
        git=Path(os.environ.get("ProgramFiles",r"C:\Program Files"))/"Git/cmd"
        paths=[native,msys/"usr/bin"]
        if (git/"git.exe").is_file():
            paths.insert(0,git)
            cmd.append("-DGIT_EXECUTABLE="+str(git/"git.exe").replace("\\","/"))
        env["PATH"]=os.pathsep.join(map(str,[*paths,env.get("PATH","")]))
        cmd.append("-DCMAKE_CXX_COMPILER="+str(cxx).replace("\\","/"))
    print(f"Building compatible native Ymir probe in {dest}",flush=True)
    run_checked(cmd,env=env)
    run_checked(["cmake","--build",dest,"--target","probe","-j",str(a.jobs)],env=env)
    return verify_probe(dest/("probe"+suffix))

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
    optional=('kof_sequence_fix_updates','kof_sequence_fix_swaps','kof_sequence_fix_errors',
              'kof_sequence_stage_updates','kof_sequence_stage_swaps','kof_sequence_stage_max_updates',
              'kof_sequence_live_verified','kof_sequence_live_hash_errors','kof_sequence_live_cache_hits',
              'kof_sequence_live_cache_misses','kof_sequence_live_lut_hits','kof_sequence_live_lut_misses',
              'kof_sequence_live_trace_fields','kof_sequence_live_lspc_fields','kof_sequence_live_draw_fields',
              'kof_sequence_live_frame_count','kof_sequence_live_scheduled','kof_sequence_live_schedule_builds',
              'kof_sequence_live_eval_slots','kof_sequence_live_timer_hz','kof_sequence_live_schedule_ticks',
              'kof_sequence_live_line_ticks','kof_sequence_live_strip_ticks','kof_sequence_live_hash_ticks',
              'kof_sequence_live_cache_ticks','kof_sequence_live_read_ticks','kof_sequence_live_timed_frames',
              'kof_sequence_live_versioned_strips','kof_sequence_live_palette_changes',
              'kof_sequence_live_epochs','kof_sequence_live_epoch_count')
    return {k:found[k] for k in (*SYMBOLS,*optional) if k in found}

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
        for count in (1,2,8,16,17,60,145,300,599,600,601,900,1200):
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
        state["sequence_length"]=state.get("kof_sequence_live_frame_count",600)
        state["full_sequence_submitted"]=state["kof_sequence_frames"]>=state["sequence_length"]>0
        state["note"]="Submissions include repeated loops; not pixel-distinct FPS or physical console timing. Injection is not optical boot."
        (out/"summary.json").write_text(json.dumps(state,indent=2)+"\n")
        print(json.dumps(state,indent=2))
        print(f"Evidence: {out}")
        if state["kof_sequence_frames"]==0:raise ValueError("no gameplay submissions: inspect phase, screenshots and probe.log")
    except (OSError,ValueError,subprocess.SubprocessError) as e:
        p.exit(1,f"{e}\nEvidence retained: {out}\n")

if __name__=="__main__":main()
