# ROM-free Saturn graphics probe

A standalone bootable acceptance target, not the KOF game. Requires a mounted disc and a 4 MiB expansion RAM cartridge. Generates two original synthetic patterns in PROBE.BIN, loads them via CDFS and the phase store with CRC checks, reads/writes 64 bytes across the cart bank boundary, opens the tile view, locks combat residency, and emits a moving 6 by 12 grid of sprite strips. Full and reduced patterns alternate, with reversed rows, H flips, shared LUTs, white pen 15 borders and transparent diagonals. Expected 36 native + 36 variant sprites per submitted frame; no synchronous CD reads in the draw loop. The dark gray background is intentional. A panic screen indicates a failure. The probe feeds synthetic scanline IR, not live game LSPC/CPU state.

## Windows

With installed LibSaturn and SH-2 tools on PATH, in the checkout:

    cmake -S . -B build-saturn -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/lsp-new/share/libsaturn/toolchains/sh2eb-elf.cmake -DCMAKE_PREFIX_PATH=C:/lsp-new -DCMAKE_BUILD_TYPE=Release -DKOF_BUILD_SATURN_ASSETS=ON -DKOF_BUILD_GRAPHICS_PROBE=ON -DBUILD_TESTING=OFF
    cmake --build build-saturn --target kof_graphics_probe_disc --parallel 6

Open build-saturn/disc/kof_graphics_probe.cue in Ymir with 4 MiB RAM expansion and a suitable BIOS/region. The disc uses the installed SDK's native boot default (local test only). Python and mkisofs/genisoimage/xorrisofs must be available. The ELF, map and flat binary remain in build-saturn. If disc tool discovery fails, supply LIBSATURN_MKISOFS. Do not distribute local boot payloads.

Read kof_probe_stage, kof_probe_frames, kof_probe_native and kof_probe_variants via the ELF map/debugger. Stage 7 and increasing frame count indicate the draw loop progresses, not pixel-perfect proof; inspect the output and compare screenshots. The actual cross-build, link, disc build and Ymir run have not yet been executed for this probe. Earlier library-only build succeeded on Windows GCC 13.2.0. FIX, real KOF ROM data, sound and CPU execution are out of scope.
