import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from pack_dispatch import parse, emit, page

def table(rows,count=None):
    return 'void a(void); void b(void);\nconst ng_func_entry_t ng_recomp_table[] = {\n'+''.join(f'{{0x{k:x}u, {f}}},\n' for k,f in rows)+'};\nconst size_t ng_recomp_count = '+str(len(rows) if count is None else count)+';'

class Tests(unittest.TestCase):
    def test_sort_and_emit(self):
        rows=[(0x3200000,'a'),(0xc00000,'b'),(0,'a'),(0x1200000,'b'),(0x200000,'a')]
        e=parse(table(rows)); self.assertEqual(e,sorted(rows))
        output=emit(e); self.assertIn('const kof_paged_dispatch kof_generated_dispatch',output); self.assertNotIn('ng_recomp_table',output)
    def test_pages(self):
        self.assertEqual([page(k) for k in [0,0xffffe,0x200000,0x2ffffe,0xc00000,0xc1fffe,0x1200000,0x32ffffe]],[0,255,256,511,512,543,544,1311])
    def test_invalid(self):
        for rows,count in [([(1,'a')],None), ([(0,'a'),(0,'b')],None), ([(0,'missing')],None), ([(0x4200000,'a')],None), ([(0xc20000,'a')],None), ([(0,'a')],2),([],None)]:
            with self.subTest(rows=rows),self.assertRaises(ValueError): parse(table(rows,count))
    def test_malformed(self):
        with self.assertRaises(ValueError): parse(table([(0,'a')]).replace('0x0u','0x0u + 2'))
    def test_capacity(self):
        with self.assertRaises(ValueError): parse(table([(i*2,'a') for i in range(65536)]))

if __name__=='__main__': unittest.main()
