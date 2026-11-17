#!/usr/bin/env python3
"""ROM-free raster packet roundtrip and boundary regression tests."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import pack_live as p

def capture(path,initial,events):
    data=bytearray(b"KST1"+p.STATE.pack(*initial))
    for y in range(p.H):
        changes=events.get(y,[])
        data+=p.LINE.pack(y&7,y&1,len(changes))
        for idx,val in changes:data+=p.CHANGE.pack(idx,val)
    path.write_bytes(data)

def apply(initial,packet):
    state=initial[:];pos=0;lines=[]
    for y in range(p.H):
        anim,bios,n=p.LINE.unpack_from(packet,pos);pos+=p.LINE.size
        for _ in range(n):
            i,v=p.CHANGE.unpack_from(packet,pos);pos+=p.CHANGE.size;state[i]=v
        lines.append((anim,bios))
    if pos!=len(packet):raise AssertionError("trailing bytes")
    return state,lines

class Live(unittest.TestCase):
    def test_frame_and_loop_reset(self):
        with tempfile.TemporaryDirectory() as tmp:
            a=Path(tmp)/"a.kst";b=Path(tmp)/"b.kst"
            initial=[0]*p.WORDS;initial[0]=3
            capture(a,initial,{7:[(0,4),(p.WORDS-1,0x803f)]})
            first,packet,final=p.packet(a,None)
            self.assertEqual(list(p.STATE.unpack(first)),initial)
            got,flags=apply(initial,packet)
            self.assertEqual(got,final)
            self.assertEqual(flags,[(y&7,y&1) for y in range(224)])
            second=initial[:];second[1]=42
            capture(b,second,{223:[(0,9)]})
            prefix,pkt,end=p.packet(b,final)
            self.assertIsNone(prefix)
            self.assertEqual(apply(final,pkt)[0],end)
            self.assertEqual(end[0],9)
            self.assertEqual(end[1],42)
            self.assertEqual(end[-1],0)
            self.assertEqual(apply(initial,packet)[0],final)
    def test_bad_trace(self):
        with tempfile.TemporaryDirectory() as tmp:
            a=Path(tmp)/"a.kst"
            capture(a,[0]*p.WORDS,{1:[(1,4),(1,5)]})
            with self.assertRaises(ValueError):p.packet(a,None)
    def test_zoom_reference_loader(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory=Path(tmp)
            path=directory/"000-lo.lo"
            first=bytes(range(256))*256
            path.write_bytes(first)
            self.assertEqual(p.load_zoom(directory),(first,65536))
            path.write_bytes(first+bytes([0xA5])*65536)
            self.assertEqual(p.load_zoom(directory),(first,131072))
            path.write_bytes(first[:-1])
            with self.assertRaisesRegex(ValueError,"65535 bytes"):
                p.load_zoom(directory)
            path.rename(directory/"000-lo.bin")
            (directory/"000-lo.bin").write_bytes(first)
            self.assertEqual(p.load_zoom(directory),(first,65536))
    def test_hash(self):
        self.assertEqual(p.fnv(b"hello"),0x4f9f2cab)
if __name__=="__main__":unittest.main()
