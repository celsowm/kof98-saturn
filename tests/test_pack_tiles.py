import random
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from pack_tiles import sprite_tile,fix_tiles,layout,read_selected,build,C_SIZE,FIX_SIZE


def reference(a,b):
    inter=bytes(x for pair in zip(a,b) for x in pair)
    pixels=[]
    for y in range(16):
        for x in range(16):
            j=(64 if x<8 else 0)+y*4
            pixels.append(sum(((inter[j+plane]>>(x%8))&1)<<weight for weight,plane in enumerate((0,2,1,3))))
    return bytes((pixels[i]<<4)|pixels[i+1] for i in range(0,256,2))


class Tests(unittest.TestCase):
    def test_planes_orientation(self):
        for plane in range(4):
            for y in range(16):
                for x in range(16):
                    a,b=bytearray(64),bytearray(64)
                    (a if plane<2 else b)[(32 if x<8 else 0)+y*2+plane%2]=1<<(x%8)
                    expected=bytearray(128); expected[y*8+x//2]=(1<<plane)<<(4 if x%2==0 else 0)
                    self.assertEqual(sprite_tile(a,b),expected)
    def test_random(self):
        rng=random.Random(42)
        for _ in range(1024):
            a,b=rng.randbytes(64),rng.randbytes(64)
            self.assertEqual(sprite_tile(a,b),reference(a,b))
        self.assertEqual(sprite_tile(bytes([255])*64,bytes([255])*64),bytes([255])*128)
        with self.assertRaises(ValueError): sprite_tile(b'',bytes(64))
    def test_fix(self):
        raw=bytearray(32)
        for j,off in enumerate((16,24,0,8)): raw[off]=((2*j+1)<<4)|(2*j)
        self.assertEqual(fix_tiles(raw)[:4],bytes([0x01,0x23,0x45,0x67]))
        with self.assertRaises(ValueError): fix_tiles(b'x')
    def test_layout(self):
        self.assertEqual(layout([9,2,9],1024),([2,9],[0,32],288))
        for ids,cap in [([],100),([True],100),([-1],100),([524288],100),([1],100),([1],4194305)]:
            with self.assertRaises(ValueError): layout(ids,cap)
    def test_selection_and_pack(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); pairs=[]
            for i in range(4):
                a,b=root/f'242-c{2*i+1}.bin',root/f'242-c{2*i+2}.bin'
                for p,byte in [(a,1<<i),(b,255-i)]:
                    with p.open('wb') as f: f.write(bytes([byte])*64); f.truncate(C_SIZE)
                pairs.append((a,b))
            ids=[0,C_SIZE//64,3*C_SIZE//64]
            expect=b''.join(sprite_tile(bytes([1<<i])*64,bytes([255-i])*64) for i in (0,1,3))
            self.assertEqual(read_selected(pairs,ids),expect)
            (root/'242-s1.bin').write_bytes(bytes([0x12])*FIX_SIZE)
            out=root/'phase'; report=build(root,ids[::-1]+ids,out,200000,True)
            pack=(out/'TILES.BIN').read_bytes()
            self.assertEqual(len(pack),report['bytes'])
            self.assertEqual(struct.unpack('>III',pack[:12]),tuple(ids))
            self.assertEqual(pack[32:32+len(expect)],expect)
            for r in report['assets']:
                offset=r['source_offset']; self.assertEqual(offset%32,0)
                self.assertEqual(zlib.crc32(pack[offset:offset+r['bytes']])&0xffffffff,r['crc32'])
            self.assertIn('KOF_LOCAL_TILE_COUNT 3u',(out/'tile_manifest.h').read_text())
            with self.assertRaises(FileExistsError): build(root,ids,out,200000)
            with self.assertRaises(ValueError): build(root,ids,root/'too-small',100)
            self.assertFalse((root/'too-small').exists())

if __name__=='__main__': unittest.main()
