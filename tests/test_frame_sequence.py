#!/usr/bin/env python3
"""ROM-free KFR1/KFR2 and clip-repacking regression."""
import sys
import tempfile
import unittest
import struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import pack_frame as pf
import pack_sequence as ps

class Source:
    def get(self,ids):
        return {0:bytes([0x11])*128,524288:bytes([0x22,0x22,0x22,0x22,0,0,0,0])*8+bytes(64)}

def record(tile,w,h,mask,pal):
    return pf.RECORD.pack(tile,0,0,mask,h,w,0,*pal,*range(16))

class TestReplay(unittest.TestCase):
    def test_fix_over_sprite(self):
        p=[0]*16;p[1]=0x4f00;p[2]=0x20f0
        bg=pf.rgb(0);red=pf.rgb(p[1]);green=pf.rgb(p[2])
        ref=[bg]*(320*224)
        for y in range(16):
            for x in range(16):ref[y*320+x]=green if y<8 and x<8 else red
        data=b"KFR2"+struct.pack(">I",2)+struct.pack(">224H",*([bg]*224))+struct.pack(">71680H",*ref)
        data+=record(0,16,16,65535,p)+record(524288,8,8,255,p)
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/"f.kfr";path.write_bytes(data)
            _,_,final,atlas,draws,r=pf.compile_frame(path,Source())
            self.assertEqual(final,ref)
            self.assertEqual(r["rgb555_mismatches"],0)
            self.assertEqual(r["fix_commands"],1)
            self.assertEqual(r["native"],1)
            clip=ps.Clip()
            self.assertTrue(clip.append(atlas,draws,bg))
            self.assertTrue(clip.append(atlas,draws,bg))
            payload,size,frames=clip.finish()
            self.assertEqual(frames,[(size,2,bg),(size+32,2,bg)])
            for offset,count,_ in frames:
                for j in range(count):
                    x,y,w,h,t,l,flags,reserved=ps.CMD.unpack_from(payload,offset+j*16)
                    a=draws[j]
                    self.assertEqual((x,y,w,h,flags,reserved),(*a[:4],a[-1],0))
                    self.assertEqual(payload[t*8:t*8+w*h//2],atlas[a[4]:a[4]+w*h//2])
                    self.assertEqual(payload[l*8:l*8+32],atlas[a[5]:a[5]+32])
            self.assertLessEqual(ps.VRAM+24,512*1024-2*(64+16)*1024-32)
            original=ps.VRAM
            try:
                ps.VRAM=1
                self.assertFalse(clip.append(bytes([0x33])*128+bytes(32),[(0,0,16,16,0,128,0)],bg))
                self.assertEqual(clip.finish(),(payload,size,frames))
            finally:ps.VRAM=original
            path.write_bytes(b"KFR1"+data[4:])
            with self.assertRaises(ValueError):pf.read_capture(path)

if __name__=="__main__": unittest.main()
