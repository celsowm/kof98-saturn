# X4 — staged VDP1 sprite emission

This is the first sprite-only command emitter using the pinned LibSaturn API. It is not a playable Saturn application. The caller starts a 320x224 frame, supplies a completed strip builder and a live resident tile view, then calls kof_saturn_strip_draw. The function appends native VDP1 normal sprites, not scaled sprites. It does not submit, swap or draw FIX/backdrop/VDP2.

## Rendering contract

Check the complete plan before any writes: count/capacity, all linked indices unique and reachable, slot tail, increasing nonoverlapping Y within each column, valid masks/rows, tile ID, remaining SDK command budget (including END and HUD reserve), maximum 2,048 strips and 256 non-native variants. A 256-byte local bitset validates the chain. These bounds are experimental; no measured KOF worst-case guarantee. Failure leaves an incomplete frame unsubmitted.

Native full-size strips use the existing texture/LUT cache and hardware H/V flip flags. All other strips read the original packed tile from the cart, materialize exact selected pixels with transparent padding, upload a fixed 128-byte slot, and draw 1:1 at padded width and actual height. A palette-only cache API avoids occupying native pattern slots for transformed strips. Same palette words can share one LUT regardless of pattern. Colors convert to RGB555; pen zero stays zero. Verified pinned SDK compose_sprite_pmod uses 0x0088 for LUT4 (end-code disable, lookup-table mode) and sets transparent-pixel disable only for OPAQUE, which is not passed. Pen 15 therefore remains an ordinary color according to the SDK source. Actual hardware clipping, framebuffer output and transparency still need a probe. Coordinates convert top-left screen to SDK centered local coordinates, using signed multiplication rather than shifting negative values.

Reserve 32 KiB once for variants (256 x 128 B), in addition to the existing 66 KiB native cache and up to 24 bytes alignment overhead. Total 98 KiB payload; the SDK allocator owns the rest of VRAM. First implementation reuploads each transformed strip every prepared list, no cross-frame reuse or within-frame variant dedup. Reuse only after the caller ends the current cache transaction upon confirmed submission or actual discard. Each overwrite uses sat_vdp1_vram_write (SDK waits for previous draw, then DMA copies). This is a conservative potentially stalling path, not a measured framerate gain. No CD accesses.

## Integration and failure

Initialize the native tile cache and strip renderer once after sat_init. Use one master-owned staged command list, no other user of this cache, no concurrent SDK command batch. Caller must handle frame submission and cache_end explicitly. On success, pins remain held. On error, SDK checkpoint rollback removes all appended commands; VRAM transfers cannot roll back but their cache state remains coherent. If rollback succeeds, release pins. If rollback fails, retain pins and DO NOT submit until a real discard/recovery. Use ON_SUBMIT and verify fencing at actual draw completion on hardware; no assumption that VBlank alone is enough. Do not reinitialize/reset the cartridge, VRAM or SDK while consumers survive.

The portable preflight reports invalid plans and budget violations through one failure code, mapped to SAT_ERR_CAPACITY in the adapter; precise diagnostics can be refined later.

## Validation

Local GCC C11 integration test with -O2 -Wall -Wextra -Wpedantic -Werror passed; GCC AddressSanitizer and UndefinedBehaviorSanitizer passed. The new emitter, preflight and palette-only function ran with the existing local portable strip/store/tile-cache sources and a mock SDK interface. Tests cover a full-size flipped strip, a non-native shrink strip, shared LUT, exact uploaded sample bytes, centered coordinates, command budget rejection without writes, malformed chain, successful rollback and failed rollback keeping pins. This is not validation of actual SDK DMA, hardware command words or timing. Mock headers are scoped to the host test target only. CMake registration added; full remote suite not run here. Pinned SDK source was inspected, not compiled for SH-2.

## Next gates

Before new speculative optimizations, make a minimal installed-SDK Saturn disc probe for the complete synthetic cart -> tile -> strip -> VDP1 path, including 0/F pens, cropped patterns, flips, palette sharing, frame submission, misses and fenced overwrite. Capture framebuffer and link map in Ymir, then test physical hardware. Integrate actual LSPC traces with real per-line palettes to quantify strip, variant, LUT and cart working-set pressure. Add FIX and backdrop/VDP2 paths separately. No local game ROM, SH-2 compiler or emulator validation here.
