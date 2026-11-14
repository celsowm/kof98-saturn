# X3 — local native tile pack

## Source and format

Verified against pinned neogeorecomp a47ee3fe09dc46d1f2b86c36ec972b43b35ebf62 src/romload.c and src/video.c, KOF98 Recomp game.c at 44f873a3cca3827a48fca30fa8091da746311af0, and LibSaturn dbc679a3ac213cbb6a975030212c96b21bd684ad include/saturn/vdp1.h.

Eight 8 MiB C ROMs form four ordered pairs. Each 16x16 tile reads 64 bytes from each file, right half stored before left, bit x sampled LSB first. Interleaving a0,b0,a1,b1 gives pixel weights 1,4,2,8 respectively. The output is row-major LUT4 pattern, left pixel in the high nibble: 128 bytes/tile, versus desktop oracle's expanded 256 bytes/tile. All 16 indices survive including 0 and 15. Palette selection, RGB555 quantization including Neo Geo dark-bit approximation, transparent pen 0, disabling VDP1 end-code interpretation of pen 15, flips, zoom, priority and scanline limits are renderer responsibilities. No texture or palette upload yet. FIX graphics optionally repacked to 32 bytes per 8x8 tile using source column offsets 16,24,0,8; these pixels are not a VDP2 tilemap and have not been uploaded on target.

The C ROM contains 524,288 tiles, 64 MiB packed, so never preload the full ROM. Only selected absolute tile IDs are converted. The converter needs local legally obtained extracted AES ROMs; it validates exact lengths but does not authenticate ROM revisions/checksums. No derived data committed.

## Local invocation

Supply a JSON array of integer tile IDs (example synthetic selection only: [0,1,12345]) under local/. Later obtain actual phase selections from LSPC oracle traces, not guesses.

    python3 tools/pack_tiles.py roms local/phase_tiles.json build/phase01 --capacity 3145728 --cart-fix

Optional --bios-fix /local/path/sfix.sfix includes another 128 KiB FIX set. The capacity is the phase arena's budget, NOT a mandated partition; account separately for guest ROM pages and other consumers. Output folder must not exist. Store all local outputs under ignored build/ or local/. A failed output write may leave an incomplete directory; discard it and retry.

Outputs:
- TILES.BIN: 32-byte-aligned asset payloads, no embedded header. Asset 1: sorted unique tile IDs as u32 big-endian; asset 2: same-order 128-byte LUT4 tiles; optional 3 and 4: cart/BIOS FIX patterns.
- manifest.json: exact offsets, sizes, CRC32 and phase byte count.
- tile_manifest.h: local C descriptors compatible with kof_asset_desc and KOF_LOCAL_TILE_COUNT. Call kof_asset_begin with descriptors after opening this file through the SDK adapter. Runtime tile lookup and manifest-to-disc integration are still pending.

The store can reproduce this layout because both sides align every record to 32 bytes. No extra full-ROM temporary buffer is needed. Index and pixels together cost 132 bytes per selected tile plus up to 31 alignment bytes; 4 MiB can store 31,775 tiles if there are no other assets or reserved bytes. A phase with 10,000 selected tiles uses 1,320,000 bytes before optional FIX. This is capacity arithmetic, not a measured KOF working set. No deduplication of equal pixel patterns across distinct IDs yet.

## Evidence and next gates

Five Python unittest cases executed locally (0.309 seconds reported by unittest). Exhaustive single-bit checks at all 256 positions and four planes (1,024 patterns), 1,024 deterministic random tiles against a separately implemented interleaving/reference expression, all-15 tile, FIX nibble order, sorted/deduplicated selection, sparse synthetic 8 MiB ROM files across pairs 0,1,3, aligned pack/CRC, capacity rejection and output overwrite protection. The exact converter and test sources were exercised locally; CTest registration and remote CI have not been run. Actual KOF graphics and SH-2/VDP1 output remain untested.

Next: portable tile-index lookup (validated against declared asset lengths, BE parsing, generation handles), per-phase LSPC tracing of actual visible tile IDs and palette use, then bounded VDP1 texture/LUT cache with draw-completion fences. If measured phase residency exceeds the budget, implement a trace-driven prefetch policy with explicit missing-asset diagnostics. Do not silently substitute missing tiles. The cartridge is data-only; CD reads must not occur synchronously during combat. No actual ROM dumps, target SDK installation, SH-2 compiler or emulator available in this environment.
