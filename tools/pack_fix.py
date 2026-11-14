"""Exact cell reconstruction of the final FIX overlay, independent of VDP1."""
import struct

CELLS=40*28
TILE_LIMIT=4096 # 128 KiB in VDP2 B0
PALETTE_BASE=16 # reserve CRAM banks 0 and 7 (loading font)
PALETTE_LIMIT=96
class LayerCapacity(ValueError):
    """A layer cannot represent the proposed draw set within its budget."""

class Fix:
    def __init__(self,tile_limit=TILE_LIMIT,palette_limit=PALETTE_LIMIT,cell_limit=CELLS):
        self.tile_limit=tile_limit
        self.palette_limit=palette_limit
        self.cell_limit=cell_limit
        self.tiles=[bytes(32)];self.tile_ids={self.tiles[0]:0}
        self.palettes=[bytes(32)];self.palette_ids={self.palettes[0]:0}
        self.previous=None
        self.max_updates=0
        self.total_updates=0
    def intern(self,items,ids,data,limit):
        if data not in ids:
            if len(items)>=limit:raise LayerCapacity("cell layer dictionary capacity")
            ids[data]=len(items);items.append(data)
        return ids[data]
    def encode(self,draw):
        cells={}
        for x,y,w,h,f,p,l in draw:
            lut=struct.unpack(">16H",l)
            for j in range(h):
                sy=h-1-j if f&0x40 else j
                for k in range(w):
                    if x+k>=320:continue
                    sx=w-1-k if f&0x20 else k
                    pen=(p[sy*(w//2)+sx//2]>>(0 if sx&1 else 4))&15
                    if not pen:continue
                    cx,cy=(x+k)//8,(y+j)//8
                    cell=cy*40+cx
                    pixel=((y+j)&7)*8+((x+k)&7)
                    colors,candidates=cells.setdefault(cell,([0]*64,set()))
                    colors[pixel]=lut[pen];candidates.add(l)
        if len(cells)>self.cell_limit:raise LayerCapacity('cell layer active cell budget')
        names=[(0,0)]*CELLS
        for cell,(colors,candidates) in cells.items():
            used=set(colors)-{0}
            if len(used)>15:raise LayerCapacity(f"cell {cell}: more than 15 opaque colors")
            # Prefer a source palette, even for split/raster-updated strips.
            available=[p for p in sorted(candidates) if used<=set(struct.unpack(">16H",p)[1:])]
            pal=available[0] if available else struct.pack(">16H",0,*sorted(used),*([0]*(15-len(used))))
            lut=struct.unpack(">16H",pal)
            pens={v:i for i,v in reversed(list(enumerate(lut))) if i}
            pens[0]=0
            pat=bytes((pens[colors[i]]<<4)|pens[colors[i+1]] for i in range(0,64,2))
            if [lut[(pat[i//2]>>(0 if i&1 else 4))&15] if colors[i] else 0 for i in range(64)]!=colors:raise ValueError("FIX reconstruction mismatch")
            if any(((pat[i//2]>>(0 if i&1 else 4))&15)!=0 for i in range(64) if colors[i]==0):raise ValueError("FIX transparency mismatch")
            tile=self.intern(self.tiles,self.tile_ids,pat,self.tile_limit)
            palette=self.intern(self.palettes,self.palette_ids,pal,self.palette_limit)
            names[cell]=(tile,palette)
        changes=[(i,t,p) for i,(t,p) in enumerate(names) if self.previous is None or (t,p)!=self.previous[i]]
        self.previous=names
        self.max_updates=max(self.max_updates,len(changes));self.total_updates+=len(changes)
        return struct.pack(">H",len(changes))+b"".join(struct.pack(">3H",*u) for u in changes)
