import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from dedup_routines import read_unit, merge, transform

HEADER='/* generated fixture */\n/* example, part 1 */\n#include "recomp_common.h"\n'

def routine(seed, cases, blocks):
    return ('\n/* routine $'+f'{seed:06X}: {len(blocks)} instructions */\nvoid r_{seed:06X}(void) {{\n    switch (CPU.pc) {{\n'+
            ''.join(f'    case 0x{pc:06X}u: goto L_{pc:06X};\n' for pc in cases)+
            '    default: ng_bad_dispatch(CPU.pc); return;\n    }\n'+''.join(blocks)+'}\n')

def block(pc, code):
    return f'L_{pc:06X}: /* synthetic */\n    {{ {code} }}\n'

A=block(0x200000,'NG_INSN(4); CPU.d += 3;')
B=block(0x200002,'NG_INSN(6); CPU.d *= 2;')
C=block(0x200004,'NG_INSN(10); CPU.pc = 0x300000u; return;')
ROOT=routine(0x200000,[0x200000],[A,B,C])
TAIL=routine(0x200002,[0x200002],[B,C])
OTHER_BANK=routine(0x1200002,[0x200002],[B,C])

class TestDedup(unittest.TestCase):
    def test_suffix_and_bank(self):
        _,r=read_unit(HEADER+ROOT+TAIL+OTHER_BANK,0)
        aliases=merge(r)
        self.assertEqual(aliases['r_200002'],'r_200000')
        self.assertEqual(aliases['r_1200002'],'r_1200002')
        self.assertEqual(r[0].extra,{0x200002})
    def test_changed_body_and_not_suffix(self):
        changed=routine(0x200002,[0x200002],[B.replace('6','8'),C])
        _,r=read_unit(HEADER+ROOT+changed,0)
        self.assertEqual(merge(r)['r_200002'],'r_200002')
        _,r=read_unit(HEADER+ROOT+routine(0x200002,[0x200002],[B]),0)
        self.assertEqual(merge(r)['r_200002'],'r_200002')
    def test_reject_drift(self):
        for bad in (ROOT.replace('3 instructions','4 instructions'),ROOT.replace('goto L_200000','goto L_200006'),ROOT+'unexpected\n'):
            with self.assertRaises(ValueError): read_unit(HEADER+bad,0)
    def test_compiled_equivalence(self):
        cc=shutil.which('cc')
        if not cc: self.skipTest('host C compiler missing')
        with tempfile.TemporaryDirectory() as td:
            base=Path(td); src=base/'raw'; dst=base/'out'; src.mkdir()
            (src/'recomp_00.c').write_text(HEADER+ROOT+OTHER_BANK)
            (src/'recomp_01.c').write_text(HEADER+TAIL)
            (src/'recomp_common.h').write_text('#include <stdint.h>\nextern struct cpu {uint32_t pc,d;} CPU;\nextern unsigned cycles;\n#define NG_INSN(n) (cycles += (n))\nvoid ng_bad_dispatch(uint32_t pc);\n')
            (src/'recomp_table.c').write_text('''#include <neogeorecomp/neogeo.h>
void r_200000(void); void r_200002(void); void r_1200002(void);
const ng_func_entry_t ng_recomp_table[] = {
    {0x200000u, r_200000},
    {0x1200002u, r_1200002},
    {0x200002u, r_200002},
};
const size_t ng_recomp_count = 3;
''')
            report=transform(src,dst,2)
            self.assertEqual((report['routines_after'],report['instruction_labels_after'],report['dispatch_entries']),(2,5,3))
            inc=base/'neogeorecomp'; inc.mkdir()
            (inc/'neogeo.h').write_text('#include <stdint.h>\n#include <stddef.h>\ntypedef struct {uint32_t addr; void (*fn)(void);} ng_func_entry_t;\n')
            harness=base/'harness.c'; harness.write_text('''#include <stdio.h>
#include <stdlib.h>
#include "recomp_common.h"
#include <neogeorecomp/neogeo.h>
struct cpu CPU; unsigned cycles;
void ng_bad_dispatch(uint32_t pc) { (void)pc; abort(); }
extern const ng_func_entry_t ng_recomp_table[];
extern const size_t ng_recomp_count;
int main(void) {
 for (unsigned key=0; key<3; ++key) {
  uint32_t pc = key==0 ? 0x200000u : key==1 ? 0x200002u : 0x1200002u;
  for(size_t i=0; i<ng_recomp_count; ++i) if (ng_recomp_table[i].addr==pc) {
   CPU.pc=pc&0xffffff; CPU.d=7; cycles=0; ng_recomp_table[i].fn();
   printf("%x %u %u\\n", CPU.pc, CPU.d, cycles);
  }
 }
 return 0;
}
''')
            results=[]
            for folder in (src,dst):
                exe=folder/'test'; subprocess.run([cc,'-std=c11','-O2','-I',str(base),'-I',str(folder),str(harness),str(folder/'recomp_00.c'),str(folder/'recomp_01.c'),str(folder/'recomp_table.c'),'-o',str(exe)],check=True)
                results.append(subprocess.check_output([str(exe)]))
            self.assertEqual(results[0],results[1])
            self.assertEqual(results[1],b'300000 20 20\n300000 14 16\n300000 14 16\n')

if __name__=='__main__': unittest.main()
