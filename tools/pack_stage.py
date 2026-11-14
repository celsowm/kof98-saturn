"""Conservative VDP2 underlay: move only an ordered prefix of VDP1 sprites.

This is a replay experiment, not semantic stage segmentation. Never move a
later sprite under an earlier retained sprite. If the 4bpp encoding cannot
represent a prefix exactly, try a shorter prefix or retain all draws.
"""
from copy import deepcopy
from pack_fix import Fix, LayerCapacity

STAGE_TILE_LIMIT=4095 # B1, leave its final 32 bytes for SDK backdrop
STAGE_PALETTE_LIMIT=48 # indices 64..111, disjoint from FIX and font
STAGE_CELL_LIMIT=256 # at most 512 changed nonblank entries across a transition

class Stage:
    def __init__(self,max_prefix=256):
        if not 1<=max_prefix<=2047:raise ValueError("invalid stage prefix cap")
        self.max_prefix=max_prefix
        self.layer=Fix(STAGE_TILE_LIMIT,STAGE_PALETTE_LIMIT,STAGE_CELL_LIMIT)
        self.removed=[]
        self.fallback=0
    def encode(self,draw):
        cap=min(self.max_prefix,max(0,len(draw)-1))
        # Keep at least one VDP1 command and the complete ordered suffix.
        candidates=sorted({cap,cap*3//4,cap//2,cap//4,cap//8,cap//16,0},reverse=True)
        for count in candidates:
            trial=deepcopy(self.layer)
            try:packet=trial.encode(draw[:count])
            except LayerCapacity:continue
            self.layer=trial
            self.removed.append(count)
            if count<cap:self.fallback+=1
            return count,packet
        raise AssertionError("transparent empty layer must fit")
