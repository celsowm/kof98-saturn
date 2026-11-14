import shutil
import subprocess
import sys
import tempfile
import unittest
from dataclasses import dataclass, field
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from cfg_merge import explicit_blocks, merge_cfg

@dataclass
class Routine:
    name: str
    cases: set
    blocks: tuple
    context: tuple = ('bank',0)
    extra: set = field(default_factory=set)


def block(pc, code):
    return f'L_{pc:06X}: /* synthetic */\n    {{ {code} }}\n'


def fixture():
    # Neither routine is a suffix of the other. Both share a conditional
    # path with the same lexical successor and different incoming edges.
    shared=block(0x10,'cycles += 2; if (d & 1) goto L_000014;')
    next_=block(0x12,'cycles += 3; d += 7;')
    end=block(0x14,'cycles += 4; pc = 0x80; return;')
    a=Routine('a',{0,0x10},(block(0,'cycles++; d += 1; goto L_000010;'),shared,next_,end))
    b=Routine('b',{2,0x12},(block(2,'cycles++; d *= 2; goto L_000010;'),shared,next_,end,block(0x40,'cycles++; return;')))
    # The added tail changes explicit lexical fallthrough even after return.
    # Remove it so the shared end is normalized identically; use distinct
    # middle labels instead, both before shared region.
    b.blocks=(b.blocks[0],block(4,'d += 9; goto L_000010;'),shared,next_,end)
    b.cases.add(4)
    return [a,b]


def render(r):
    cases=''.join(f'case {pc}: goto L_{pc:06X};' for pc in sorted(r.cases|r.extra))
    return f'void {r.name}(void) {{ switch(pc) {{ {cases} default: abort(); }}\n'+''.join(r.blocks)+'}\n'


class TestCFG(unittest.TestCase):
    def test_merge(self):
        rr=fixture(); aliases=merge_cfg(rr)
        self.assertEqual(aliases,{'b':'b','a':'b'})
        self.assertEqual(len(rr[1].blocks),6)
        self.assertEqual(rr[1].cases|rr[1].extra,{0,2,4,0x10,0x12})
    def test_conflict(self):
        rr=fixture(); rr[0].blocks=tuple(x.replace('d += 7','d += 8') for x in rr[0].blocks)
        self.assertEqual(merge_cfg(rr),{'a':'a','b':'b'})
    def test_fallthrough_conflict(self):
        rr=fixture(); rr[0].blocks=rr[0].blocks[:2]+(block(0x11,'d += 1;'),)+rr[0].blocks[2:]
        self.assertEqual(merge_cfg(rr),{'a':'a','b':'b'})
    def test_context_and_limits(self):
        rr=fixture(); rr[0].context=('bank',1)
        self.assertEqual(merge_cfg(rr),{'a':'a','b':'b'})
        self.assertEqual(merge_cfg(fixture(),max_blocks=5),{'a':'a','b':'b'})
        self.assertEqual(merge_cfg(fixture(),max_cases=4),{'a':'a','b':'b'})
    def test_reject_bad_labels(self):
        with self.assertRaises(ValueError): explicit_blocks(('bad',))
        with self.assertRaises(ValueError): explicit_blocks((block(2,'return;'),block(0,'return;')))
    def test_compiled(self):
        cc=shutil.which('cc')
        if not cc: self.skipTest('host compiler absent')
        entries=[(0,'a'),(2,'b'),(4,'b'),(0x10,'a'),(0x12,'b')]
        results=[]
        with tempfile.TemporaryDirectory() as td:
            for merged in (False,True):
                rr=fixture(); aliases=merge_cfg(rr) if merged else {r.name:r.name for r in rr}
                source='#include <stdio.h>\n#include <stdlib.h>\nunsigned pc, d, cycles;\n'+''.join(render(r) for r in rr if aliases[r.name]==r.name)
                source+='int main(void) { for (unsigned i=0;i<256;i++) {\n'
                for addr,name in entries:
                    source+=f'pc={addr}; d=i; cycles=0; {aliases[name]}(); printf("%u %u %u\\n",pc,d,cycles);\n'
                source+='} return 0; }\n'
                path=Path(td)/'fixture.c'; exe=Path(td)/'fixture'; path.write_text(source)
                subprocess.run([cc,'-std=c11','-O2',str(path),'-o',str(exe)],check=True)
                results.append(subprocess.check_output([str(exe)]))
        self.assertEqual(results[0],results[1])
        self.assertEqual(len(results[0].splitlines()),1280)

if __name__=='__main__': unittest.main()
