#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import pack_resident as p
from pack_fix import Fix
import struct

class Resident(unittest.TestCase):
    def test_delta(self):
        old=[(i,i,0) for i in range(200)]
        new=old[:42]+[(800,7,3),(900,8,4)]+old[45:]+old[:10]
        self.assertEqual(p.decode_delta(p.delta(old,new),old,len(new)),new)
        self.assertEqual(p.decode_delta(p.delta([],old),[],len(old)),old)
        with self.assertRaises(ValueError):p.decode_delta(b"\x80\x01\x00\x01",[],1)

    def test_pinned_eviction(self):
        c=p.Cache(3)
        self.assertEqual(c.assign([1,2,3]),[(0,1),(1,2),(2,3)])
        self.assertEqual(c.assign([4,1,3]),[(1,4)])
        self.assertEqual(c.live[1],0)
        self.assertEqual(c.live[3],2)
        self.assertEqual(c.data[1],4)
        with self.assertRaises(ValueError):c.assign([1,2,3,4])
    def test_fix_raster_and_wrap(self):
        f=Fix()
        pal=struct.pack(">16H",0,0x801f,*([0]*14))
        pal2=struct.pack(">16H",0,0x83e0,*([0]*14))
        draws=[(0,0,8,4,0,bytes([0x11])*16,pal),
               (0,4,8,4,0,bytes([0x11])*16,pal2)]
        packet=f.encode(draws)
        self.assertEqual(struct.unpack_from(">H",packet)[0],1120)
        t,l=f.previous[0]
        lut=struct.unpack(">16H",f.palettes[l])
        colors=[lut[(f.tiles[t][i//2]>>(0 if i&1 else 4))&15] for i in range(64)]
        self.assertEqual(colors,[0x801f]*32+[0x83e0]*32)
        self.assertEqual(f.encode(draws),b"\x00\x00")
        self.assertEqual(struct.unpack(">4H",f.encode([])),(1,0,0,0))
        self.assertEqual(f.previous[0],(0,0))
if __name__=="__main__":unittest.main()
