import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from cycle_batch import optimize


def block(pc, cycles, body=''):
    return f'L_{pc:06X}: /* synthetic */\n    {{ NG_INSN({cycles}); {body}{" " if body else ""}}}\n'


Q = 'CPU.d[0] = ng_logic32(0xFFFFFFFFu);'
M = 'uint32_t v = (uint32_t)CPU.d[0]; ng_logic32(v); CPU.d[1] = v;'
A = 'uint32_t s = (uint32_t)CPU.d[0]; uint32_t d = (uint32_t)CPU.d[1]; uint32_t r = ng_add32(s, d); CPU.d[1] = r;'


class TestBatch(unittest.TestCase):
    def test_batch(self):
        out, b, s = optimize([block(0,4,Q),block(2,6),block(4,8,M)], {0})
        self.assertEqual((b,s),(1,2))
        self.assertIn('ng_cycles += 18; ng_icount += 3u;',out[0])
        self.assertNotIn('NG_INSN',out[1]+out[2])
        self.assertIn(M,out[2])

    def test_entries_and_gotos(self):
        raw=[block(0,4,Q),block(2,4,M),block(4,4,Q),block(6,4,M),block(8,4,'goto L_000006;')]
        out,b,s=optimize(raw,{0,2})
        self.assertEqual((b,s),(1,1))
        self.assertEqual(out[0],raw[0]); self.assertEqual(out[3],raw[3])
        self.assertIn('ng_icount += 2u;',out[1])

    def test_barriers(self):
        for body in ['ng_cycles += 6;', 'ng_w16(0, 0);','ng_exception(4, 0); return;', 'ng_set_sr(0);','CPU.d[0] = ng_r32(0);','if (ng_cycles >= ng_next_event) return;', 'CPU.d[0] = ng_addx32(1, CPU.d[0]);']:
            raw=[block(0,4,Q),block(2,4,body),block(4,4,M)]
            self.assertEqual(optimize(raw,{0}),(tuple(raw),0,0))

    def test_bounded(self):
        out,b,s=optimize([block(i*2,4) for i in range(35)],{0})
        self.assertEqual((b,s),(3,32))
        self.assertIn('ng_icount += 16u;',out[0]); self.assertIn('ng_icount += 3u;',out[32])

    def test_invalid(self):
        for raw, entries in [([block(0,4)],{2}),([block(2,4),block(0,4)],{0}),([block(0,4,'goto L_000002;')],{0})]:
            with self.assertRaises(ValueError): optimize(raw,entries)

    def test_compiled(self):
        cc=shutil.which('cc')
        if not cc: self.skipTest('no C compiler')
        raw=[block(0,4,Q),block(2,6,M),block(4,8,A),block(6,4,'observe(); if (CPU.d[0]&1u) goto L_00000A;'),block(8,4,Q),block(10,6,M),block(12,4,A),block(14,8,'observe(); if (ng_cycles < ng_next_event) goto L_00000A; CPU.pc = 0x100u; return;')]
        entries={0,4,10,14}
        opt,b,s=optimize(raw,entries)
        self.assertEqual((b,s),(2,2))
        header='''#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
struct cpu {uint32_t d[8],pc; unsigned x,n,z,v,c;} CPU;
int64_t ng_cycles, ng_next_event; uint64_t ng_icount, digest;
#define NG_INSN(n) (ng_cycles+=(n),ng_icount++)
static uint32_t ng_logic32(uint32_t r) { CPU.n=r>>31; CPU.z=r==0; CPU.v=CPU.c=0; return r; }
static uint32_t ng_add32(uint32_t s,uint32_t d) { uint32_t r=d+s; CPU.x=CPU.c=r<d; CPU.v=((s^r)&(d^r))>>31; CPU.n=r>>31; CPU.z=r==0; return r; }
static void observe(void) { digest=digest*31u+(uint64_t)ng_cycles+ng_icount*7u+CPU.d[1]; }
'''
        harness='''int main(void) { uint32_t rng=42; unsigned entries[]={0,4,10,14}; for(unsigned i=0;i<1024;i++) for(unsigned j=0;j<4;j++) { rng=rng*1664525u+1013904223u; CPU.d[0]=rng; CPU.d[1]=~rng; CPU.pc=entries[j]; CPU.x=i&1; CPU.n=(i>>1)&1; CPU.z=(i>>2)&1; CPU.v=(i>>3)&1; CPU.c=(i>>4)&1; ng_cycles=0x100000000LL+(i&31); ng_next_event=ng_cycles+24+(i&15); ng_icount=0xfffffffeULL; digest=0; run(); printf("%u %u %u %u %u %u %u %u %lld %llu %llu\\n",CPU.d[0],CPU.d[1],CPU.pc,CPU.x,CPU.n,CPU.z,CPU.v,CPU.c,(long long)ng_cycles,(unsigned long long)ng_icount,(unsigned long long)digest); } }
'''
        results=[]
        with tempfile.TemporaryDirectory() as td:
            for name,blocks in [('raw',raw),('opt',opt)]:
                src=Path(td)/f'{name}.c'; exe=Path(td)/name
                cases=''.join(f'case {pc}: goto L_{pc:06X};' for pc in sorted(entries))
                src.write_text(header+'static void run(void) { switch(CPU.pc) {'+cases+'default: abort();}\n'+''.join(blocks)+'}\n'+harness)
                subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Wno-unused-label','-Werror',str(src),'-o',str(exe)],check=True)
                results.append(subprocess.check_output([str(exe)]))
        self.assertEqual(results[0],results[1]); self.assertEqual(len(results[0].splitlines()),4096)

if __name__=='__main__': unittest.main()
