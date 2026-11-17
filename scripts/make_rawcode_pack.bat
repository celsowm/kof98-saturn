@echo off
REM Regenerate the rawcode pack for the no-input gameplay bench.
REM Pass 1 build (stub map) must exist first so the ELF has final symbol addresses:
REM   cmake -S . -B build-ng -G Ninja ... -DKOF_BUILD_RAWCODE_BENCH=ON ^
REM     -DKOF_GAME_MAP_DIR=.../local/ng_recomp_game256 -DKOF_RAWCODE_MAP_SRC=
REM   cmake --build build-ng --target kof_ng_rawcode_bench
cd /d E:\projetos\kof98-saturn

echo === build rescue page 0 (5 missing no-input PCs + 2 redundant) ===
python tools/make_cart_rescue.py local/ng_recomp_game256 local/ng_recomp_full256 local/ng_recomp_game256/cart_rescue_00.c r_000122 r_005BE4 r_00A842 r_00C9D8 r_010448 r_00C54E r_03523A
if errorlevel 1 exit /b 1

echo === pack 256 cart-only pages with rescue page 0 ===
python tools/rawcode_pack.py --gen local/ng_recomp_game256 --elf build-ng/kof_ng_rawcode_bench.elf --out local/ymir/rawpackcart --pages 256 --stride 196608 --jobs 8 --skip-pages 123,195,252 --rescue 0:local/ng_recomp_game256/cart_rescue_00.c --extra-entry 0x000122:r_000122 --extra-entry 0x005BE4:r_005BE4 --extra-entry 0x00A82A:r_00A842 --extra-entry 0x00C9D8:r_00C9D8 --extra-entry 0x010448:r_010448 --extra-entry 0x00C54E:r_00C54E --extra-entry 0x03523A:r_03523A
