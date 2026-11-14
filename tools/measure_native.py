"""Compile local generated routine units and collect target symbol sizes."""
import argparse
import os
import subprocess
from pathlib import Path
from pack_dispatch import fingerprint

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path); p.add_argument('upstream_ng',type=Path); p.add_argument('out',type=Path)
    p.add_argument('--opt',choices=('Os','O2','O3'),default='Os')
    p.add_argument('--cc',default=os.environ.get('SH2_CC','sh2eb-elf-gcc'))
    p.add_argument('--nm',default=os.environ.get('SH2_NM','sh2eb-elf-nm'))
    a=p.parse_args()
    try:
        identity=fingerprint(a.source/'recomp_table.c')
        a.out.mkdir(parents=True,exist_ok=True)
        rows=['# kof-native-sizes-v1 '+identity]
        version=subprocess.check_output([a.cc,'--version'],text=True)
        inputs=sorted(a.source.glob('recomp_[0-9][0-9].c'))
        if not inputs: raise ValueError('missing generated units')
        commands=[]
        for src in inputs:
            obj=a.out/(src.stem+'.o')
            cmd=[a.cc,'-m2','-mb','-'+a.opt,'-std=c11','-ffreestanding','-fno-lto','-ffunction-sections','-fdata-sections','-I'+str(a.upstream_ng/'include'),'-I'+str(a.source),'-c',str(src),'-o',str(obj)]
            commands.append(repr(cmd))
            subprocess.run(cmd,check=True)
            rows.append(subprocess.check_output([a.nm,'-S','--radix=d','--defined-only',str(obj)],text=True))
        (a.out/'sizes.txt').write_text('\n'.join(rows)+'\n')
        (a.out/'compiler.txt').write_text(version+'\n'+'\n'.join(commands)+'\n')
    except (OSError,ValueError,subprocess.CalledProcessError) as e: p.error(str(e))

if __name__=='__main__': main()
