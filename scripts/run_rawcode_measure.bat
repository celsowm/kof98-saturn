@echo off
REM Pass-2: generate the hot map from the 256-page pack, rebuild the bench
REM with real symbol addresses (stub -> same-size hot map), then run Ymir.
cd /d E:\projetos\kof98-saturn

echo === generate hot map (4166 entries, matching stub) ===
echo     includes no-input profile so guest low-RAM routines (0x122 etc.) are covered
python tools/make_hot_rawcode_map.py local/ymir/rawpackcart/rawcode_map.c 4166 local/ymir/hotmap256.c local/profiles/fight.csv local/profiles/playlong.csv local/profiles/noinput1200.csv
if errorlevel 1 exit /b 1

echo === reconfigure pass 2 (real map source) ===
cmake -S . -B build-ng -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=C:/lsp-new/share/libsaturn/toolchains/sh2eb-elf.cmake ^
  -DLibSaturn_DIR=C:/lsp-new/lib/cmake/LibSaturn ^
  -DKOF_BUILD_SATURN_ASSETS=ON -DKOF_BUILD_NG_BENCH=ON -DKOF_BUILD_RAWCODE_BENCH=ON -DKOF_32BIT_CYCLES=ON ^
  -DKOF_UPSTREAM_DIR=E:/projetos/kof98-saturn/deps/kof98recomp ^
  -DKOF_RAWCODE_DIR=E:/projetos/kof98-saturn/local/ymir/rawpackc32b ^
  -DKOF_GAME_MAP_DIR=E:/projetos/kof98-saturn/local/ng_recomp_game256 ^
  -DKOF_RAWCODE_MAP_SRC=E:/projetos/kof98-saturn/local/ymir/hotmap256.c
if errorlevel 1 exit /b 1

echo === rebuild bench + disc ===
cmake --build build-ng --target kof_ng_rawcode_bench_disc -j 4
if errorlevel 1 exit /b 1

echo === verify symbol addresses unchanged ===
C:\msys64\home\celso\saturn-tools\bin\sh2eb-elf-nm.exe build-ng/kof_ng_rawcode_bench.elf | findstr /R "_ng_cpu$ _ng_cycles$ _ng_icount$ _g_game_map$"

echo === Ymir gameplay measurement (native routines) ===
if exist local\ymir\rawcode-measure rmdir /s /q local\ymir\rawcode-measure
python scripts/kof_ymir_bench.py --target rawcode --frames 1200 --out local/ymir/rawcode-measure
