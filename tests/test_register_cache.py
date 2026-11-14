import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from register_cache import optimize


def block(pc, body):
    return f'L_{pc:06X}: /* synthetic */\n    {{ NG_INSN(4); {body} }}\n'

A = 'uint32_t s = (uint32_t)CPU.d[0]; uint32_t d = (uint32_t)CPU.d[1]; uint32_t r = ng_add32(s, d); CPU.d[1] = r;'
M = 'uint32_t v = (uint32_t)CPU.d[1]; ng_logic32(v); CPU.d[0] = v;'
Q = 'CPU.d[1] = ng_logic32(0xFFFFFFFFu);'
T = 'uint32_t d = (uint32_t)CPU.d[1]; ng_logic32(d);'

class TestCache(unittest.TestCase):
    def test_dirty_and_readonly(self):
        raw = [block(i*2,A) for i in range(3)]
        out,n,s=optimize(raw,{0})
        self.assertEqual((n,s),(1,6))
        self.assertIn('uint32_t kof_cached_d0 = CPU.d[0];',out[0])
        self.assertIn('CPU.d[1] = kof_cached_d1;',out[-1])
        self.assertNotIn('CPU.d[0] = kof_cached_d0;',out[-1])
    def test_boundaries(self):
        raw = [block(i*2,A) for i in range(6)] + [block(12,'goto L_000008;')]
        out,n,s=optimize(raw,{0,4})
        self.assertEqual(n,3)
        for i in (0,2,4): self.assertIn('uint32_t kof_cached_d1',out[i])
        for i in (1,3,5): self.assertIn('CPU.d[1] = kof_cached_d1;',out[i])
    def test_barriers(self):
        for body in ['ng_w32(0,CPU.d[1]);','ng_exception(5,0); return;','ng_set_sr(0);','if (ng_cycles >= ng_next_event) return;','CPU.d[0] = ng_addx32(1,CPU.d[0]);','CPU.d[1] = ng_r32(0);']:
            raw=[block(0,A),block(2,A),block(4,body),block(6,A),block(8,A)]
            out,n,_=optimize(raw,{0}); self.assertEqual(n,2); self.assertEqual(out[2],raw[2]); self.assertIn('CPU.d[1] = kof_cached_d1;',out[1])
    def test_limits_and_no_gain(self):
        self.assertEqual(optimize([block(0,Q),block(2,T)],{0})[1],0)
        out,n,s=optimize([block(i*2,A) for i in range(5)],{0},max_run=2,max_regs=1)
        self.assertEqual(n,2); self.assertNotIn('uint32_t kof_cached_d0', ''.join(out)); self.assertIn('uint32_t kof_cached_d1',''.join(out))
    def test_invalid(self):
        for raw,entries in [([block(0,A)],{2}),([block(2,A),block(0,A)],{0}),([block(0,'goto L_000002;')],{0})]:
            with self.assertRaises(ValueError): optimize(raw,entries)
    def test_compiled(self):
        cc=shutil.which('cc')
        if not cc: self.skipTest('no C compiler')
        raw=[block(0,A),block(2,M),block(4,A),block(6,'observe(); if (CPU.d[0]&1u) goto L_00000A;'),block(8,Q),block(10,A),block(12,M),block(14,A),block(16,'observe(); if (ng_cycles < ng_next_event) goto L_00000A; CPU.pc=0x100u; return;')]
        entries={0,4,10,16}
        opt,n,s=optimize(raw,entries); self.assertEqual(n,2); self.assertGreater(s,0)
        header='''#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
struct cpu {uint32_t d[8],pc; unsigned x,n,z,v,c;} CPU;
int64_t ng_cycles, ng_next_event; uint64_t ng_icount,digest;
#define NG_INSN(n) (ng_cycles+=(n),ng_icount++)
static uint32_t ng_logic32(uint32_t r) { CPU.n=r>>31; CPU.z=r==0; CPU.v=CPU.c=0; return r; }
static uint32_t ng_add32(uint32_t s,uint32_t d) { uint32_t r=d+s; CPU.x=CPU.c=r<d; CPU.v=((s^r)&(d^r))>>31; CPU.n=r>>31; CPU.z=r==0; return r; }
static void observe(void) { digest=digest*31u+CPU.d[0]+CPU.d[1]+(uint64_t)ng_cycles+ng_icount; }
'''
        harness='''int main(void) { uint32_t rng=42; unsigned entries[]={0,4,10,16}; for(unsigned i=0;i<1024;i++) for(unsigned j=0;j<4;j++) { rng=rng*1664525u+1013904223u; CPU.d[0]=rng; CPU.d[1]=~rng; CPU.pc=entries[j]; CPU.x=i&1; CPU.n=(i>>1)&1; CPU.z=(i>>2)&1; CPU.v=(i>>3)&1; CPU.c=(i>>4)&1; ng_cycles=0x100000000LL; ng_next_event=ng_cycles+24+(i&15); ng_icount=0xfffffffeULL; digest=0; run(); printf("%u %u %u %u %u %u %u %u %lld %llu %llu\\n",CPU.d[0],CPU.d[1],CPU.pc,CPU.x,CPU.n,CPU.z,CPU.v,CPU.c,(long long)ng_cycles,(unsigned long long)ng_icount,(unsigned long long)digest); } }
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
