import sys,tempfile,unittest,subprocess,shutil
from pathlib import Path
from collections import Counter
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from select_native import choose, read_profile, read_sizes
ID='0123456789abcdef'*4
class Tests(unittest.TestCase):
 def test_budget(self):
  entries=[(0,'a'),(2,'a'),(4,'b'),(6,'c')]
  sizes={'a':64,'b':32,'c':16}; w=Counter(a=100,b=60,c=0)
  self.assertEqual(choose(entries,sizes,w,2702),({'b'},2664))
  self.assertEqual(choose(entries,sizes,w,2740),({'a','b'},2740))
 def test_reject(self):
  for sizes,budget,reserve in [({'a':0},5000,0),({'a':16},2626,0),({'a':16},5000,5000),({'a':16},5000,-1)]:
   with self.assertRaises(ValueError): choose([(0,'a')],sizes,Counter(a=2),budget,reserve)
 def test_sizes(self):
  with tempfile.TemporaryDirectory() as td:
   p=Path(td)/'nm'; p.write_text('0000 64 T a\n0000 32 T b\n')
   self.assertEqual(read_sizes(p,{'a','b'}),{'a':64,'b':32})
   with self.assertRaises(ValueError):read_sizes(p,{'a','b','c'})
 def test_profile(self):
  with tempfile.TemporaryDirectory() as td:
   p=Path(td)/'profile'; prefix=f'# kof-native-profile-v1 {ID}\nkey,hits,instructions,cycles\n';p.write_text(prefix+'00000000,2,10,20\n')
   self.assertEqual(list(read_profile(p,ID,{0})),[(0,2,10,20)])
   for bad in ['00000002,2,10,20\n','00000000,0,10,20\n','00000000,2,-1,20\n','00000000,2,10,20\n00000000,2,10,20\n']:
    p.write_text(prefix+bad)
    with self.assertRaises(ValueError): list(read_profile(p,ID,{0}))
 def test_compiled_profile(self):
  cc=shutil.which('cc')
  if not cc:self.skipTest('compiler missing')
  root=Path(__file__).resolve().parents[1]
  with tempfile.TemporaryDirectory() as td:
   exe=Path(td)/'profile'; p=Path(td)/'out.csv'
   subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'include'),str(root/'tests/test_native_profile.c'),str(root/'src/host/native_profile.c'),str(root/'src/dispatch.c'),'-o',str(exe)],check=True)
   subprocess.run([str(exe),str(p)],check=True)
   self.assertEqual(sorted(read_profile(p,ID,{0x200000,0x1200000,0xc01234})),[(0x200000,2,10,40),(0xc01234,2,17,90),(0x1200000,1,5,20)])
if __name__=='__main__': unittest.main()
