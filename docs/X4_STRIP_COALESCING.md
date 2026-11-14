# X4 — exact vertical sprite strip coalescing

## Algorithm and contract

The LSPC scanline IR is consumed sequentially, one visible line at a time. Maintain one open tail per hardware column (381 slots). Append to a strip only when the next visible line is adjacent, tile ID, clipped X, source mask, horizontal flip and all 16 guest palette words are equal, and height is below 16. Record the exact source row per destination row, allowing repeated, skipped and reversed rows. Otherwise start another strip. Palette bank changes are safe only when the caller provides the correct current palette words at each line; identical palette content can share a strip even if the bank/index changes. The entire frame is rejected on invalid IR or capacity exhaustion.

The final traversal is ascending hardware slot, then linked insertion order within each slot. Reordering disjoint scanlines in this way preserves painter order for any pixel: within one line all overlapping columns are still in original increasing slot order. Each column contains at most one span per line. Pen zero remains transparent.

Full 16x16 masks with source rows exactly 0..15 or 15..0 can bind the original cached tile with H/V flips, without materializing a variant. Other strips copy only the selected source nibbles (exact shrink/flip/clip) into a tightly packed 4bpp pattern padded to width 8 or 16 with zero pen. Draw at 1:1 scale. Hardware clipping must hide any padding beyond screen right; padding is transparent even where it overlaps other sprites. Native width/height, byte order, end-code disable and transparent pen must still be tested on VDP1. Source and output tile buffers must not alias. Source tile fetch is the caller's responsibility; use the resident pack, not CD.

Sprite-only: FIX composition, backdrop raster effects, VDP2 extraction and SDK command emission are not implemented. This is a command candidate plan, not actual submitted commands. Different tile IDs never merge, even when adjacent; multi-tile strip assembly is a later candidate. No assumption of equivalent VDP1 scaling. The palette captured is raw Neo Geo color including dark bit, not yet quantized RGB555. The renderer must use the existing conversion and transparent pen handling.

## Budget

The caller owns the strip array; 64 bytes per strip on the tested host, plus 1,552 bytes builder metadata. With 2,048 strips the array takes 128 KiB, which has NOT been allocated in target WRAM. Actual configured capacity needs a link budget and scene measurements. The working set can be reduced using palette interning and compact row maps after measuring real traces. One scanline is streamed; no 224-line IR array is kept. A worst-case raster-heavy frame can still overflow any practical capacity; do not truncate. Record failure and compare with a proven fallback or redesign the rendering partition. The 512-slot tile cache only handles original 16x16 tiles; transformed strips need a separate bounded fenced variant arena, with explicit command and VRAM accounting.

## Validation actually run

GCC and Clang, -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror: pass. GCC AddressSanitizer + UndefinedBehaviorSanitizer: pass. Compiled exact core and test sources locally, including a complete synthetic sprite-only 320x224 replay with overlapping columns, zoom mask, flipped/reversed rows, missing scanline, palette change at line 100, transparent nibbles and edge padding. 447 input spans became 31 candidate strips (12 reusable full-size patterns). All 71,680 output pixel positions matched the scalar span renderer. Capacity refusal and invalid duplicate slot tested. No hardware draw or timing measured. The fixture uses two synthetic columns, not typical or worst-case KOF gameplay.

## Promotion

1. Connect actual oracle scanline trace with palette and FIX/backdrop state. Compare real scene replays and collect unique tile/palette/variant counts and candidate command histograms.
2. Add native/variant VRAM materialization and command emitter using fenced writes and pinned current-list references. Reject incomplete lists; compare actual VDP1 output on synthetic probe and real local trace. Measure upload latency/draw waits.
3. Reduce commands further by multi-tile strip sharing only when order/palette/sampling equivalence is proven. Evaluate VDP2 background/FIX paths separately.
4. Validate memory and frame deadlines on SH-2 and Ymir/physical cart. No actual KOF ROMs, target SDK or SH-2 toolchain available here; full CTest suite and remote CI not observed.
