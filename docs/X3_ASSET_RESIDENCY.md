# X3 — 4 MiB data-cartridge phase residency

## Implemented

A heap-free portable asset-store core and optional LibSaturn adapter at the pinned SDK revision (dbc679a3ac213cbb6a975030212c96b21bd684ad). The adapter requires an actual 4 MiB expansion cart, checks two banks and allocation capacity, reserves a logical buffer once and uses sat_ram_cart_buffer_read_at/write_at across bank boundaries. It accepts a caller-mounted CDFS volume and one packed file, and does not initialize or own the CD controller. The master SH-2 is the sole caller. No raw physical pointers are exposed; the cart is data only.

Phase manifest: up to 64 records {nonzero asset ID, source offset, byte count, IEEE CRC32}, laid out at 32-byte-aligned logical offsets. Caller owns descriptors and an aligned scratch buffer up to 8 KiB. Plan validation checks duplicate IDs, bounds and aligned arena fit without altering the published phase. Only after success does a new generation invalidate old handles. Each step copies one bounded chunk synchronously and verifies stream CRC before publishing the entire phase. Failed I/O, short read (SDK adapter), CRC mismatch or abort exposes no partial assets. Note: CRC is over incoming data, not a physical RAM readback test. A valid new load overwrites the old arena; failure is NOT rollback to old contents. Retry from the complete manifest. No second 4 MiB arena is assumed.

States: EMPTY -> LOADING -> READY -> COMBAT -> READY. Failure goes to FAILED; retry starts a new generation. During combat this store refuses any begin/step/abort that could modify residency and never calls the CD reader to satisfy missing assets. Reads check generation, slot and bounds, and call only cart read. This is a local policy, not a global CD scheduler lock. Application must ensure exclusive CD ownership during CDFS loading, including stopping/draining ADX or other streams beforehand. Step bounds transfer size, not wall-clock duration. No reentry, concurrent mutation, DMA or slave reads through this API. Before leave_combat or replacement, drain every outstanding renderer/DMA/slave consumer; fence enforcement belongs in integration. Never reset/init the store while old handles survive. Clock changes/cart reset require complete teardown and reload. Do not use Backup RAM.

## Memory

Arena size is configurable (up to 4,194,304 bytes); no fixed 2+2 MiB role partition. Physical banks remain two separate 2 MiB slices. Core metadata sizeof = 1,360 bytes on tested 64-bit host; target size and linker map still to be measured. Scratch <= 8,192 bytes, external/caller supplied. SDK CDFS mount has its own sector buffer. No dedicated per-asset allocation or heap. The phase may waste up to 31 bytes alignment per record. Full 4 MiB reservation is possible only when no other cart user has reserved it; reserve space for guest ROM pages/audio as required by the final liveness study.

## Test evidence

The local host test was compiled with cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror and ran successfully. It was also compiled with AddressSanitizer and UndefinedBehaviorSanitizer and passed. The synthetic cart reader/writer splits accesses at 2 MiB. Tests cover a 64-byte asset crossing the bank boundary, CRC known vector, alignment/capacity and source bounds, no partial publication, duplicate IDs, stale handles, zero-length read, I/O failures, CRC mismatch, abort and retry, generation exhaustion and zero source reads during simulated combat. Tests exercise core callbacks, not actual SDK MMIO. Adapter authored against the inspected pinned headers; not linked or run on target.

ROM-free tests: cmake -S . -B build; cmake --build build; ctest --test-dir build --output-on-failure. Optional cross-build library: configure with installed LibSaturn SDK/toolchain and -DKOF_BUILD_SATURN_ASSETS=ON. It builds a library, not a bootable game or disc.

## Next gates

1. Host synthetic pack/manifest generator with exact offsets and CRCs; connect local ROM sprite decoding into native tile packs (no copyrighted output committed). Measure actual sprite/stage/team residency from LSPC traces.
2. Add an installed-SDK cart probe application: reject absent/1 MiB carts, write/read/verify patterns at bank boundaries, measure read/write throughput by chunk size and capture actual link map and diagnostics. Test Ymir and real 4 MiB hardware separately.
3. Integrate phase transitions and explicit draw-completion fences with VDP1 native tile/palette cache. Keep CD scheduler and audio ownership explicit; verify no synchronous combat asset I/O on the whole target, not just this store.
4. Determine if all required combat assets fit. If not, use measured prefetch/residency policy, not silent missing tiles or unbounded cache misses.

No actual ROM inputs, SH-2 toolchain, linked target, emulator or physical cartridge run in this environment. Core tests establish policy and safe bounds, not target performance or playability.
