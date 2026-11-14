import unittest
import tempfile
import subprocess
import shutil
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from flag_liveness import optimize


def block(pc, body):
    return f'L_{pc:06X}: /* synthetic */\n    {{ NG_INSN(4); {body} }}\n'

Q = 'CPU.d[0] = ng_logic32(0xFFFFFFFFu);'
T = 'uint32_t d = (uint32_t)CPU.d[0]; ng_logic32(d);'
M = 'uint32_t v = (uint32_t)CPU.d[0]; ng_logic32(v); CPU.d[1] = v;'
A = 'uint32_t s = (uint32_t)CPU.d[0]; uint32_t d = (uint32_t)CPU.d[1]; uint32_t r = ng_add32(s, d); CPU.d[1] = r;'
S = A.replace('ng_add32','ng_sub32')
EXIT = 'CPU.pc = 0x100u; return;'

class TestFlags(unittest.TestCase):
    def test_logic(self):
        out,n=optimize([block(0,Q),block(2,T),block(4,M),block(6,EXIT)])
        self.assertEqual(n,2); self.assertNotIn('ng_logic32',out[0]); self.assertNotIn('ng_logic32',out[1]); self.assertIn('ng_logic32',out[2])
    def test_x_live(self):
        out,n=optimize([block(0,A),block(2,Q),block(4,EXIT)])
        self.assertEqual(n,0); self.assertIn('ng_add32',out[0])
    def test_arith_overwritten(self):
        out,n=optimize([block(0,A),block(2,S),block(4,EXIT)])
        self.assertEqual(n,1); self.assertNotIn('ng_add32',out[0])
    def test_barriers(self):
        for barrier in ['ng_exception(5, 0); return;', 'if (CC_EQ) goto L_000004;', 'ng_w32(0x100u, CPU.d[0]);', 'ng_set_sr(0);', 'CPU.d[0] = ng_addx32(1, CPU.d[0]);', 'CPU.d[0] = ng_r32(0);', 'if (ng_cycles >= ng_next_event) { CPU.pc = 0; return; }']:
            with self.subTest(barrier=barrier):
                self.assertEqual(optimize([block(0,Q),block(2,barrier),block(4,M)])[1],0)
    def test_compare_and_partial(self):
        cmp='uint16_t s = (uint16_t)CPU.d[0]; ng_cmp16(s, (uint16_t)CPU.d[1]);'
        self.assertEqual(optimize([block(0,cmp),block(2,Q)])[1],1)
        self.assertEqual(optimize([block(0,cmp.replace('uint16_t','uint8_t',1)),block(2,Q)])[1],0)
    def test_compiled(self):
        cc=shutil.which('cc')
        if not cc: self.skipTest('no C compiler')
        raw=tuple(block(i*2,b) for i,b in enumerate([A,S,Q,T,M,EXIT]))
        opt,n=optimize(raw); self.assertEqual(n,3)
        header='''#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
struct cpu {uint32_t d[8],pc; unsigned x,n,z,v,c;} CPU;
unsigned cycles,icount;
#define NG_INSN(n) (cycles+=(n),icount++)
static uint32_t ng_logic32(uint32_t r) { CPU.n=r>>31; CPU.z=r==0; CPU.v=CPU.c=0; return r; }
static uint32_t ng_add32(uint32_t s,uint32_t d) { uint32_t r=d+s; CPU.x=CPU.c=r<d; CPU.v=((s^r)&(d^r))>>31; CPU.n=r>>31; CPU.z=r==0; return r; }
static uint32_t ng_sub32(uint32_t s,uint32_t d) { uint32_t r=d-s; CPU.x=CPU.c=s>d; CPU.v=((s^d)&(r^d))>>31; CPU.n=r>>31; CPU.z=r==0; return r; }
'''
        h='''int main(void) { uint32_t rng=42; for(unsigned i=0;i<1024;i++) for(unsigned pc=0;pc<12;pc+=2) { rng=rng*1664525u+1013904223u; CPU.d[0]=rng; CPU.d[1]=~rng; CPU.pc=pc; CPU.x=i&1; CPU.n=(i>>1)&1; CPU.z=(i>>2)&1; CPU.v=(i>>3)&1; CPU.c=(i>>4)&1; cycles=icount=0; run(); printf("%u %u %u %u %u %u %u %u %u %u\\n",CPU.d[0],CPU.d[1],CPU.pc,CPU.x,CPU.n,CPU.z,CPU.v,CPU.c,cycles,icount); } }
'''
        results=[]
        with tempfile.TemporaryDirectory() as td:
            for name,blocks in [('raw',raw),('opt',opt)]:
                src=Path(td)/f'{name}.c'; exe=Path(td)/name
                cases=''.join(f'case {i*2}: goto L_{i*2:06X};' for i in range(6))
                src.write_text(header+'static void run(void) { switch(CPU.pc) {'+cases+'default: abort();}\n'+''.join(blocks)+'}\n'+h)
                subprocess.run([cc,'-std=c11','-O2',str(src),'-o',str(exe)],check=True)
                results.append(subprocess.check_output([str(exe)]))
        self.assertEqual(results[0],results[1]); self.assertEqual(len(results[0].splitlines()),6144)

if __name__=='__main__': unittest.main()
