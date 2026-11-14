#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import pack_resident as p

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
if __name__=="__main__":unittest.main()
