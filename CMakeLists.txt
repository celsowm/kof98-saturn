cmake_minimum_required(VERSION 3.21)
project(kof98_saturn LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_library(kof_dispatch STATIC src/dispatch.c src/paged_dispatch.c)
target_include_directories(kof_dispatch PUBLIC include)
add_library(kof_asset_store STATIC src/asset_store.c src/tile_cache.c)
option(KOF_ASSET_CRC_TABLE "Use table-driven CRC32 for asset loading" ON)
if(KOF_ASSET_CRC_TABLE)
  target_compile_definitions(kof_asset_store PRIVATE KOF_ASSET_CRC_TABLE=1)
else()
  target_compile_definitions(kof_asset_store PRIVATE KOF_ASSET_CRC_TABLE=0)
endif()
add_library(kof_lspc_ir STATIC src/lspc_ir.c src/strip.c src/strip_emit.c)
target_include_directories(kof_lspc_ir PUBLIC include)
option(KOF_LIVE_COMPACT_BUILD "Use active slot lists and single-write strip construction" OFF)
target_compile_definitions(kof_lspc_ir PRIVATE KOF_LIVE_COMPACT_BUILD=$<BOOL:${KOF_LIVE_COMPACT_BUILD}>)
target_include_directories(kof_asset_store PUBLIC include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(kof_dispatch PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(kof_asset_store PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(kof_lspc_ir PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  add_executable(test_dispatch tests/test_dispatch.c)
  target_link_libraries(test_dispatch PRIVATE kof_dispatch)
  add_test(NAME sparse_dispatch COMMAND test_dispatch)
  add_executable(test_paged_dispatch tests/test_paged_dispatch.c)
  target_link_libraries(test_paged_dispatch PRIVATE kof_dispatch)
  add_test(NAME paged_dispatch COMMAND test_paged_dispatch)
  add_executable(test_asset_store tests/test_asset_store.c)
  target_link_libraries(test_asset_store PRIVATE kof_asset_store)
  add_test(NAME asset_store COMMAND test_asset_store)
  add_executable(test_tile_cache tests/test_tile_cache.c)
  target_link_libraries(test_tile_cache PRIVATE kof_asset_store)
  add_test(NAME tile_cache COMMAND test_tile_cache)
  add_executable(test_sprite_epoch tests/test_sprite_epoch.c)
  target_include_directories(test_sprite_epoch PRIVATE include)
  add_test(NAME sprite_epoch COMMAND test_sprite_epoch)
  add_executable(test_strip_hash tests/test_strip_hash.c)
  target_include_directories(test_strip_hash PRIVATE include)
  add_test(NAME strip_hash COMMAND test_strip_hash)
  add_executable(test_zoom_memo tests/test_zoom_memo.c)
  target_include_directories(test_zoom_memo PRIVATE include)
  add_test(NAME zoom_memo COMMAND test_zoom_memo)
  add_executable(test_fix_vdp2 tests/test_fix_vdp2.c)
  target_include_directories(test_fix_vdp2 PRIVATE include)
  add_test(NAME fix_vdp2 COMMAND test_fix_vdp2)
  add_executable(test_fix_reuse tests/test_fix_reuse.c)
  target_link_libraries(test_fix_reuse PRIVATE kof_lspc_ir)
  add_test(NAME fix_reuse COMMAND test_fix_reuse)
  add_executable(test_pattern_key tests/test_pattern_key.c)
  target_link_libraries(test_pattern_key PRIVATE kof_lspc_ir)
  add_test(NAME pattern_key COMMAND test_pattern_key)
  add_executable(test_lut_memo tests/test_lut_memo.c)
  target_include_directories(test_lut_memo PRIVATE include)
  add_test(NAME lut_memo COMMAND test_lut_memo)
  add_executable(test_lspc_ir tests/test_lspc_ir.c)
  target_link_libraries(test_lspc_ir PRIVATE kof_lspc_ir)
  add_test(NAME lspc_ir COMMAND test_lspc_ir)
  add_executable(test_strip tests/test_strip.c)
  target_link_libraries(test_strip PRIVATE kof_lspc_ir)
  add_test(NAME strip_coalescing COMMAND test_strip)
  add_executable(test_strip_cache_meta tests/test_strip_cache_meta.c)
  target_link_libraries(test_strip_cache_meta PRIVATE kof_lspc_ir)
  add_test(NAME strip_cache_metadata COMMAND test_strip_cache_meta)
  add_executable(test_strip_renderer tests/test_strip_renderer.c src/saturn/strip_renderer.c src/saturn/tile_cache_sdk.c)
  target_include_directories(test_strip_renderer PRIVATE tests/sdk_mock)
  target_link_libraries(test_strip_renderer PRIVATE kof_lspc_ir kof_asset_store)
  add_test(NAME strip_renderer_mock COMMAND test_strip_renderer)
  add_executable(test_ng_input_script tests/test_ng_input_script.c src/host/ng_input_script.c)
  target_include_directories(test_ng_input_script PRIVATE src/host)
  add_test(NAME input_script COMMAND test_ng_input_script "${CMAKE_CURRENT_SOURCE_DIR}")
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME pack_dispatch COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pack_dispatch.py)
    add_test(NAME host_exec_adapter COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_adapt_host_exec.py)
    add_test(NAME dedup_routines COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dedup_routines.py)
    add_test(NAME cfg_merge COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cfg_merge.py)
    add_test(NAME flag_liveness COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_flag_liveness.py)
    add_test(NAME cycle_batch COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cycle_batch.py)
    add_test(NAME register_cache COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_register_cache.py)
    add_test(NAME native_residency COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_select_native.py)
    add_test(NAME pack_tiles COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pack_tiles.py)
    add_test(NAME frame_sequence COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_frame_sequence.py)
    add_test(NAME resident_sequence COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_resident_sequence.py)
    add_test(NAME live_sequence COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_live_sequence.py)
    add_test(NAME convert_music COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_convert_music.py)
    add_test(NAME mk_recomp_pages COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mk_recomp_pages.py)
    add_test(NAME profile_guest COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_profile_guest.py)
    add_test(NAME patch_musashi_ops COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_patch_musashi_ops.py)
  endif()
endif()

# ROM-free ng_boot interpreter-core tests (e.g. the slice hook). Needs the
# upstream Musashi sources but never a BIOS ROM or game ROM: the test boots a
# synthetic program, so the suite still runs on a clean checkout.
option(KOF_BUILD_NG_CORE_TESTS "Build ROM-free ng_boot core tests (interpreter)" OFF)
if(KOF_BUILD_NG_CORE_TESTS)
  if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    set(KOF_UPSTREAM_DIR "" CACHE PATH "Pinned KOF98 reference checkout")
    get_filename_component(up "${KOF_UPSTREAM_DIR}" ABSOLUTE)
    set(mus "${up}/neogeorecomp/ext/musashi")
    if(NOT EXISTS "${mus}/m68kcpu.c")
      message(FATAL_ERROR "Musashi sources not found at ${mus}; set KOF_UPSTREAM_DIR")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(sgen "${CMAKE_CURRENT_BINARY_DIR}/ng_slice_gen")
    file(MAKE_DIRECTORY "${sgen}")
    add_custom_command(
      OUTPUT "${sgen}/m68kops.c" "${sgen}/m68kops.h"
      COMMAND "${CMAKE_C_COMPILER}" -O2 -o "${sgen}/m68kmake${CMAKE_EXECUTABLE_SUFFIX}" "${mus}/m68kmake.c"
      COMMAND "${sgen}/m68kmake${CMAKE_EXECUTABLE_SUFFIX}" "${sgen}" "${mus}/m68k_in.c"
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/patch_musashi_ops.py" "${sgen}/m68kops.c"
      DEPENDS "${mus}/m68kmake.c" "${mus}/m68k_in.c"
      COMMENT "Generating Musashi opcode tables (ng_slice_gen)" VERBATIM)
    add_executable(ng_boot_host_slice
      tests/test_ng_boot_slice.c
      src/ng/ng_boot.c src/ng/ng_boot_stats.c
      "${mus}/m68kcpu.c" "${mus}/m68kdasm.c" "${sgen}/m68kops.c" "${mus}/softfloat/softfloat.c")
    target_include_directories(ng_boot_host_slice PRIVATE src/ng include "${mus}" "${sgen}")
    target_compile_definitions(ng_boot_host_slice PRIVATE MUSASHI_CNF="ng_musashi_conf_boot.h")
    if(MSVC)
      target_compile_options(ng_boot_host_slice PRIVATE /w)
    else()
      target_compile_options(ng_boot_host_slice PRIVATE -w)
    endif()
    add_test(NAME ng_boot_slice COMMAND ng_boot_host_slice)
  endif()
endif()

option(KOF_BUILD_HOST_ORACLE "Build ROM-local host paged oracle" OFF)
if(KOF_BUILD_HOST_ORACLE)
  include(cmake/HostOracle.cmake)
endif()

option(KOF_BUILD_SATURN_ASSETS "Build the LibSaturn cartridge/CDFS adapter" OFF)
if(KOF_BUILD_SATURN_ASSETS)
  if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Use the installed LibSaturn SH-2 toolchain")
  endif()
  find_package(LibSaturn CONFIG REQUIRED)
  # Static libraries do not inherit usage requirements from their consumers.
  # Every target translation unit must use the same freestanding SH-2 ABI.
  foreach(core_target IN ITEMS kof_dispatch kof_asset_store kof_lspc_ir)
    target_link_libraries(${core_target} PUBLIC LibSaturn::Core)
  endforeach()
  add_library(kof_saturn_assets STATIC src/saturn/asset_store_sdk.c src/saturn/tile_cache_sdk.c src/saturn/strip_renderer.c)
  target_link_libraries(kof_saturn_assets PUBLIC kof_asset_store kof_lspc_ir LibSaturn::Saturn)
endif()

option(KOF_BUILD_GRAPHICS_PROBE "Build ROM-free Saturn graphics probe and disc target" OFF)
if(KOF_BUILD_GRAPHICS_PROBE)
  if(NOT KOF_BUILD_SATURN_ASSETS)
    message(FATAL_ERROR "Graphics probe requires KOF_BUILD_SATURN_ASSETS=ON")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(probe_data "${CMAKE_CURRENT_BINARY_DIR}/probe-data")
  add_custom_command(OUTPUT "${probe_data}/PROBE.BIN" "${probe_data}/probe_manifest.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_graphics_probe.py" "${probe_data}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_graphics_probe.py" VERBATIM)
  add_executable(kof_graphics_probe src/saturn/graphics_probe.c "${probe_data}/probe_manifest.h")
  target_include_directories(kof_graphics_probe PRIVATE "${probe_data}")
  target_link_libraries(kof_graphics_probe PRIVATE kof_saturn_assets)
  libsaturn_configure_executable(kof_graphics_probe)
  libsaturn_add_binary(kof_graphics_probe MAX_BYTES 983040)
  libsaturn_add_disc(kof_graphics_probe ROOT_FILES "${probe_data}/PROBE.BIN")
endif()

option(KOF_BUILD_MUSIC_PROBE "Build the real KOF98 soundtrack CD playback target" OFF)
if(KOF_BUILD_MUSIC_PROBE)
  if(NOT KOF_BUILD_SATURN_ASSETS)
    message(FATAL_ERROR "Music probe requires KOF_BUILD_SATURN_ASSETS=ON")
  endif()
  set(KOF_MUSIC_DIR "" CACHE PATH "Local real KOF98 music asset (MUSIC.BIN + kof_music_assets.h)")
  if(NOT KOF_MUSIC_DIR)
    set(KOF_MUSIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/local/kof-music")
  endif()
  if(NOT EXISTS "${KOF_MUSIC_DIR}/MUSIC.BIN" OR NOT EXISTS "${KOF_MUSIC_DIR}/kof_music_assets.h")
    message(FATAL_ERROR "Run tools/convert_music.py first")
  endif()
  add_executable(kof_music src/saturn/music_probe.c)
  target_include_directories(kof_music PRIVATE "${KOF_MUSIC_DIR}")
  libsaturn_configure_executable(kof_music)
  libsaturn_add_binary(kof_music MAX_BYTES 983040)
  libsaturn_add_disc(kof_music ROOT_FILES "${KOF_MUSIC_DIR}/MUSIC.BIN")
endif()

option(KOF_BUILD_NG_BOOT "Build the native 68000 Neo Geo BIOS boot target" OFF)
if(KOF_BUILD_NG_BOOT)
  if(NOT KOF_BUILD_SATURN_ASSETS)
    message(FATAL_ERROR "NG boot requires KOF_BUILD_SATURN_ASSETS=ON")
  endif()
  set(KOF_UPSTREAM_DIR "" CACHE PATH "Pinned KOF98 reference checkout")
  get_filename_component(up "${KOF_UPSTREAM_DIR}" ABSOLUTE)
  set(mus "${up}/neogeorecomp/ext/musashi")
  if(NOT EXISTS "${mus}/m68kcpu.c")
    message(FATAL_ERROR "Musashi sources not found at ${mus}; set KOF_UPSTREAM_DIR")
  endif()
  find_program(KOF_HOST_CC NAMES gcc cc clang)
  if(NOT KOF_HOST_CC)
    message(FATAL_ERROR "A host C compiler is required to generate Musashi opcode tables")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(m68kgen "${CMAKE_CURRENT_BINARY_DIR}/musashi_gen")
  file(MAKE_DIRECTORY "${m68kgen}")


  # The integrated boot target also carries the game's own soundtrack
  # (MUSIC.BIN on the disc, streamed through the SCSP), so the music asset
  # requirement applies here too. ROM-derived, never committed.
  set(KOF_MUSIC_DIR "" CACHE PATH "Local real KOF98 music asset (MUSIC.BIN + kof_music_assets.h)")
  if(NOT KOF_MUSIC_DIR)
    set(KOF_MUSIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/local/kof-music")
  endif()
  if(NOT EXISTS "${KOF_MUSIC_DIR}/MUSIC.BIN" OR NOT EXISTS "${KOF_MUSIC_DIR}/kof_music_assets.h")
    message(FATAL_ERROR "Run tools/convert_music.py first (kof_ng_boot streams MUSIC.BIN)")
  endif()
  add_custom_command(
    OUTPUT "${m68kgen}/m68kops.c" "${m68kgen}/m68kops.h"
    COMMAND "${KOF_HOST_CC}" -O2 -o "${m68kgen}/m68kmake" "${mus}/m68kmake.c"
    COMMAND "${m68kgen}/m68kmake" "${m68kgen}" "${mus}/m68k_in.c"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/patch_musashi_ops.py" "${m68kgen}/m68kops.c"
    DEPENDS "${mus}/m68kmake.c" "${mus}/m68k_in.c"
    COMMENT "Generating Musashi opcode tables (68000-only cycle rows)"
    VERBATIM)
  add_executable(kof_ng_boot
    src/saturn/ng_boot_main.c
    src/ng/ng_boot.c src/ng/ng_boot_stats.c
    src/ng/ng_live_strips.c
    src/ng/ng_setjmp_shim.c
    "${mus}/m68kcpu.c"
    "${m68kgen}/m68kops.c")
  target_include_directories(kof_ng_boot PRIVATE
    src/ng src/ng/shim src/saturn "${mus}" "${m68kgen}")
  target_compile_definitions(kof_ng_boot PRIVATE MUSASHI_CNF="ng_musashi_conf_boot.h" KOF_NG_SATURN=1)
  option(KOF_GUEST_FRAME_TRACE "Trace per-frame guest state in ng_boot.c (diagnostic)" OFF)
  if(KOF_GUEST_FRAME_TRACE)
    target_compile_definitions(kof_ng_boot PRIVATE NG_GUEST_FRAME_TRACE=1)
  endif()
  target_compile_options(kof_ng_boot PRIVATE -w)
  libsaturn_configure_executable(kof_ng_boot)
  libsaturn_add_binary(kof_ng_boot MAX_BYTES 983040)
  # Live-frame C ROM tile-residency pack (TILES.BIN + tile_manifest.h), built by
  # tools/pack_live_frame.py from the frame's tile IDs. Game-derived: local only.
  set(KOF_TILE_DIR "" CACHE PATH "Local live-frame tile-residency pack")
  if(NOT KOF_TILE_DIR)
    set(KOF_TILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/local/kof-music/kof-tiles")
  endif()
  if(NOT EXISTS "${KOF_TILE_DIR}/TILES.BIN" OR NOT EXISTS "${KOF_TILE_DIR}/tile_manifest.h")
    message(FATAL_ERROR "Run tools/convert_music.py and tools/pack_live_frame.py first")
  endif()
  target_include_directories(kof_ng_boot PRIVATE "${KOF_TILE_DIR}")
  target_include_directories(kof_ng_boot PRIVATE "${KOF_MUSIC_DIR}")
  target_link_libraries(kof_ng_boot PRIVATE kof_saturn_assets)
  # The AES system BIOS and game P ROMs are local ROMs (never committed); stage them.
  set(bios_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/neo-po.bin")
  set(p1_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/242-pn1.bin")
  set(p2_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/242-p2.bin")
  if(NOT EXISTS "${bios_rom}" OR NOT EXISTS "${p1_rom}" OR NOT EXISTS "${p2_rom}")
    message(FATAL_ERROR "roms/neo-po.bin, 242-pn1.bin and 242-p2.bin required for the NG boot disc")
  endif()
  set(ng_disc "${CMAKE_CURRENT_BINARY_DIR}/ng-boot-disc")
  set(bios_stage "${ng_disc}/BIOS.BIN")
  set(p1_stage "${ng_disc}/P1.BIN")
  set(p2_stage "${ng_disc}/P2.BIN")
  set(l0_stage "${ng_disc}/L0.BIN")
  set(tile_stage "${ng_disc}/TILES.BIN")
  set(l0_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/000-lo.lo")
  if(NOT EXISTS "${l0_rom}")
    set(l0_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/000-lo.bin")
  endif()
  if(NOT EXISTS "${l0_rom}")
    message(FATAL_ERROR "roms/000-lo.lo (L0 zoom ROM) required for the NG boot disc")
  endif()
  add_custom_command(OUTPUT "${bios_stage}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${bios_rom}" "${bios_stage}"
    DEPENDS "${bios_rom}" VERBATIM)
  add_custom_command(OUTPUT "${p1_stage}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${p1_rom}" "${p1_stage}"
    DEPENDS "${p1_rom}" VERBATIM)
  add_custom_command(OUTPUT "${p2_stage}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${p2_rom}" "${p2_stage}"
    DEPENDS "${p2_rom}" VERBATIM)
  add_custom_command(OUTPUT "${l0_stage}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${l0_rom}" "${l0_stage}"
    DEPENDS "${l0_rom}" VERBATIM)
  add_custom_command(OUTPUT "${tile_stage}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${KOF_TILE_DIR}/TILES.BIN" "${tile_stage}"
    DEPENDS "${KOF_TILE_DIR}/TILES.BIN" VERBATIM)
  set(music_stage "${CMAKE_CURRENT_BINARY_DIR}/disc_stage/MUSIC.BIN")
  add_custom_command(OUTPUT "${music_stage}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${KOF_MUSIC_DIR}/MUSIC.BIN" "${music_stage}"
    DEPENDS "${KOF_MUSIC_DIR}/MUSIC.BIN" VERBATIM)
  libsaturn_add_disc(kof_ng_boot ROOT_FILES "${bios_stage}" "${p1_stage}" "${p2_stage}" "${l0_stage}" "${tile_stage}" "${music_stage}")
endif()

option(KOF_BUILD_NG_RECOMP_HOST "Build the host native-recomp (X2) equivalence/speed harness" OFF)
if(KOF_BUILD_NG_RECOMP_HOST)
  set(KOF_UPSTREAM_DIR "" CACHE PATH "Pinned KOF98 reference checkout")
  get_filename_component(NG "${KOF_UPSTREAM_DIR}" ABSOLUTE)
  set(mus "${up}/neogeorecomp/ext/musashi")
  set(ngsrc "${up}/neogeorecomp/src")
  set(ngtools "${up}/neogeorecomp/tools/m68krecomp")
  set(nginc "${up}/neogeorecomp/include")
  if(NOT EXISTS "${mus}/m68kcpu.c")
    message(FATAL_ERROR "Musashi sources not found; set KOF_UPSTREAM_DIR")
  endif()
  set(recomp_bios_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/neo-po.bin")
  if(NOT EXISTS "${recomp_bios_rom}")
    message(FATAL_ERROR "roms/neo-po.bin (AES BIOS) required for the recomp harness")
  endif()

  # Host opcode tables (m68kmake).
  set(hgen "${CMAKE_CURRENT_BINARY_DIR}/ng_recomp_gen")
  file(MAKE_DIRECTORY "${hgen}")
  add_custom_command(
    OUTPUT "${hgen}/m68kops.c" "${hgen}/m68kops.h"
    COMMAND "${CMAKE_C_COMPILER}" -O2 -o "${hgen}/m68kmake${CMAKE_EXECUTABLE_SUFFIX}" "${mus}/m68kmake.c"
    COMMAND "${hgen}/m68kmake${CMAKE_EXECUTABLE_SUFFIX}" "${hgen}" "${mus}/m68k_in.c"
    DEPENDS "${mus}/m68kmake.c" "${mus}/m68k_in.c"
    COMMENT "Generating host Musashi opcode tables" VERBATIM)

  # m68krecomp (host) - reads the user's own ROM, emits C (never committed).
  add_executable(ng_m68krecomp
    "${ngtools}/main.c" "${ngtools}/recomp.c" "${ngtools}/decode.c" "${ngtools}/emit.c"
    "${ngsrc}/romload.c" "${ngsrc}/musashi_glue.c"
    "${mus}/m68kcpu.c" "${mus}/m68kdasm.c" "${hgen}/m68kops.c" "${mus}/softfloat/softfloat.c")
  target_include_directories(ng_m68krecomp PRIVATE "${mus}" "${hgen}" "${nginc}" "${ngsrc}" "${ngtools}")
  target_compile_definitions(ng_m68krecomp PRIVATE MUSASHI_CNF="ng_musashi_conf.h")
  if(MSVC)
    target_compile_options(ng_m68krecomp PRIVATE /w)
  else()
    target_compile_options(ng_m68krecomp PRIVATE -w)
    target_link_libraries(ng_m68krecomp PRIVATE m)
  endif()

  # Byte-swap the BIOS to 68000 order, then recompile its reset/init routines.
  set(recomp_dir "${CMAKE_CURRENT_BINARY_DIR}/ng_recomp_bios")
  set(bios68k "${recomp_dir}/bios-68k.bin")
  add_custom_command(
    OUTPUT "${bios68k}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${recomp_dir}"
    COMMAND "${Python3_EXECUTABLE}" -c
      "import sys;d=bytearray(open(sys.argv[1],'rb').read());d[0::2],d[1::2]=d[1::2],d[0::2];open(sys.argv[2],'wb').write(d)"
      "${recomp_bios_rom}" "${bios68k}"
    DEPENDS "${recomp_bios_rom}" VERBATIM)
  set(recomp_c "${recomp_dir}/bios_recomp_00.c")
  set(recomp_tab "${recomp_dir}/bios_recomp_table.c")
  add_custom_command(
    OUTPUT "${recomp_c}" "${recomp_tab}" "${recomp_dir}/bios_recomp_common.h"
    COMMAND ng_m68krecomp --raw "${bios68k}" --base 0xC00000 --entry 0xC00402
            --prefix bios_ --out "${recomp_dir}" --files 1
    DEPENDS ng_m68krecomp "${bios68k}"
    COMMENT "Recompiling BIOS reset/init routines" VERBATIM)

  add_executable(ng_recomp_host
    src/host/ng_recomp_host.c "${ngsrc}/cpu.c"
    "${mus}/m68kcpu.c" "${mus}/m68kdasm.c" "${hgen}/m68kops.c" "${mus}/softfloat/softfloat.c"
    "${recomp_c}" "${recomp_tab}")
  target_include_directories(ng_recomp_host PRIVATE src/ng "${mus}" "${hgen}" "${nginc}")
  target_compile_definitions(ng_recomp_host PRIVATE MUSASHI_CNF="ng_musashi_conf_boot.h")
  if(MSVC)
    target_compile_options(ng_recomp_host PRIVATE /w)
  else()
    target_compile_options(ng_recomp_host PRIVATE -w)
    target_link_libraries(ng_recomp_host PRIVATE m)
  endif()
  add_test(NAME ng_recomp_host COMMAND ng_recomp_host "${recomp_bios_rom}" 400000 200)
  set(KOF_OVERLAY_PAGES 8 CACHE STRING "Load units (pages) the BIOS recomp is split into for the overlay test")

  # Same recomp dispatch integrated into the real core (ng_boot.c): proves the
  # dispatcher (native entry vs Musashi fallback) is bit-exact vs the pure
  # interpreter over the full frame loop, on the real BIOS.
  add_executable(ng_boot_host_recomp
    src/host/ng_boot_host.c src/host/ng_input_script.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c src/ng/ng_live_strips.c
    src/lspc_ir.c src/strip.c src/strip_emit.c
    "${mus}/m68kcpu.c" "${mus}/m68kdasm.c" "${hgen}/m68kops.c" "${mus}/softfloat/softfloat.c"
    "${recomp_c}" "${recomp_tab}")
  target_include_directories(ng_boot_host_recomp PRIVATE
    src/ng src/host include "${mus}" "${hgen}" "${nginc}")
  target_compile_definitions(ng_boot_host_recomp PRIVATE NG_BOOT_RECOMP MUSASHI_CNF="ng_musashi_conf_boot.h")
  if(MSVC)
    target_compile_options(ng_boot_host_recomp PRIVATE /w)
  else()
    target_compile_options(ng_boot_host_recomp PRIVATE -w)
    target_link_libraries(ng_boot_host_recomp PRIVATE m)
  endif()
  add_test(NAME ng_boot_recomp COMMAND ng_boot_host_recomp "${recomp_bios_rom}" 1200)
  set_tests_properties(ng_boot_recomp PROPERTIES
    PASS_REGULAR_EXPRESSION "cycles=243302402.*vram_writes=205824.*last_pc=C15434")

  # Recomp-only core (NG_BOOT_RECOMP_ONLY): no Musashi at all, a dispatch miss is
  # a hard fault. Must produce the same counters as the interpreter, proving the
  # interpreter-free build (the one that fits the Saturn WRAM budget) is exact.
  add_executable(ng_boot_host_recomp_only
    src/host/ng_boot_host.c src/host/ng_input_script.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c src/ng/ng_live_strips.c
    src/lspc_ir.c src/strip.c src/strip_emit.c
    "${recomp_c}" "${recomp_tab}")
  target_include_directories(ng_boot_host_recomp_only PRIVATE
    src/ng src/host include "${nginc}")
  target_compile_definitions(ng_boot_host_recomp_only PRIVATE
    NG_BOOT_RECOMP NG_BOOT_RECOMP_ONLY)
  if(MSVC)
    target_compile_options(ng_boot_host_recomp_only PRIVATE /w)
  else()
    target_compile_options(ng_boot_host_recomp_only PRIVATE -w)
    target_link_libraries(ng_boot_host_recomp_only PRIVATE m)
  endif()
  add_test(NAME ng_boot_recomp_only COMMAND ng_boot_host_recomp_only "${recomp_bios_rom}" 1200)
  set_tests_properties(ng_boot_recomp_only PROPERTIES
    PASS_REGULAR_EXPRESSION "cycles=243302402.*vram_writes=205824.*last_pc=C15434.*interp_instrs=0 bad_dispatch=0")

  # ---- X1: guest PC profile (host-only) ----
  # Same interpreter core, with executed instruction words and 4 KB page hits
  # recorded. Used to size the native hot set; never built for Saturn.
  add_executable(ng_boot_host_prof
    src/host/ng_boot_host.c src/host/ng_input_script.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_profile.c
    src/ng/ng_live_strips.c
    src/lspc_ir.c src/strip.c src/strip_emit.c
    "${mus}/m68kcpu.c" "${mus}/m68kdasm.c" "${hgen}/m68kops.c" "${mus}/softfloat/softfloat.c")
  target_include_directories(ng_boot_host_prof PRIVATE
    src/ng src/host include "${mus}" "${hgen}" "${nginc}")
  target_compile_definitions(ng_boot_host_prof PRIVATE
    NG_BOOT_PC_PROFILE MUSASHI_CNF="ng_musashi_conf_boot.h")
  if(MSVC)
    target_compile_options(ng_boot_host_prof PRIVATE /w)
  else()
    target_compile_options(ng_boot_host_prof PRIVATE -w)
    target_link_libraries(ng_boot_host_prof PRIVATE m)
  endif()
  add_test(NAME ng_boot_profile COMMAND ng_boot_host_prof "${recomp_bios_rom}" 120)
  set_tests_properties(ng_boot_profile PROPERTIES
    ENVIRONMENT "NG_PROFILE_OUT=${CMAKE_CURRENT_BINARY_DIR}/ng_profile.txt"
    PASS_REGULAR_EXPRESSION "# kof-guest-profile-v1")

  # ---- recomp routine overlay/streaming ----
  # Same BIOS, split into 8 load units (pages). The core pages them in on demand;
  # the test runs with a 3-page budget and must stay bit-exact.
  set(pages_dir "${CMAKE_CURRENT_BINARY_DIR}/ng_recomp_pages")
  # m68krecomp names the page files %02d (two digits, wider past 99).
  set(page_srcs)
  math(EXPR page_last "${KOF_OVERLAY_PAGES} - 1")
  foreach(i RANGE 0 ${page_last})
    if(i LESS 10)
      set(page_name "0${i}")
    else()
      set(page_name "${i}")
    endif()
    list(APPEND page_srcs "${pages_dir}/bios_recomp_${page_name}.c")
  endforeach()
  add_custom_command(
    OUTPUT ${page_srcs} "${pages_dir}/bios_recomp_table.c" "${pages_dir}/bios_recomp_common.h"
    COMMAND ng_m68krecomp --raw "${bios68k}" --base 0xC00000 --entry 0xC00402
            --prefix bios_ --out "${pages_dir}" --files ${KOF_OVERLAY_PAGES}
    DEPENDS ng_m68krecomp "${bios68k}"
    COMMENT "Recompiling BIOS into ${KOF_OVERLAY_PAGES} overlay pages" VERBATIM)
  add_custom_command(
    OUTPUT "${pages_dir}/bios_recomp_pages.h" "${pages_dir}/bios_recomp_pages.c"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mk_recomp_pages.py"
            --dir "${pages_dir}" --prefix bios_ --out "${pages_dir}/bios_recomp_pages.h"
    DEPENDS ${page_srcs}
    COMMENT "Generating the PC -> overlay page map" VERBATIM)
  add_custom_target(ng_recomp_pages DEPENDS
    "${pages_dir}/bios_recomp_pages.h" "${pages_dir}/bios_recomp_pages.c")

  add_executable(ng_boot_host_overlay
    src/host/ng_boot_host.c src/host/ng_input_script.c src/host/ng_overlay_host.c
    src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c src/ng/ng_live_strips.c
    src/lspc_ir.c src/strip.c src/strip_emit.c
    "${mus}/m68kcpu.c" "${mus}/m68kdasm.c" "${hgen}/m68kops.c" "${mus}/softfloat/softfloat.c"
    ${page_srcs} "${pages_dir}/bios_recomp_pages.c")
  target_include_directories(ng_boot_host_overlay PRIVATE
    src/ng src/host include "${mus}" "${hgen}" "${nginc}" "${pages_dir}")
  target_compile_definitions(ng_boot_host_overlay PRIVATE
    NG_BOOT_RECOMP NG_BOOT_OVERLAY
    MUSASHI_CNF="ng_musashi_conf_boot.h")
  add_dependencies(ng_boot_host_overlay ng_recomp_pages)
  if(MSVC)
    target_compile_options(ng_boot_host_overlay PRIVATE /w)
  else()
    target_compile_options(ng_boot_host_overlay PRIVATE -w)
    target_link_libraries(ng_boot_host_overlay PRIVATE m)
  endif()
  add_test(NAME ng_boot_overlay COMMAND ng_boot_host_overlay "${recomp_bios_rom}" 1200)
  set_tests_properties(ng_boot_overlay PROPERTIES
    PASS_REGULAR_EXPRESSION "recomp: native_blocks=318020 interp_instrs=0 bad_dispatch=0"
    ENVIRONMENT "NG_OVERLAY_BUDGET=3")
  # Separate assertion that paging really happened; kept as its own test so a
  # regression in either dimension is reported separately.
  add_test(NAME ng_boot_overlay_paged COMMAND ng_boot_host_overlay "${recomp_bios_rom}" 1200)
  set_tests_properties(ng_boot_overlay_paged PROPERTIES
    PASS_REGULAR_EXPRESSION "cycles=243302402.*vram_writes=205824.*last_pc=C15434"
    FAIL_REGULAR_EXPRESSION "resident=[4-9]|errors=[1-9]|paging_ok=0|bad_dispatch=[1-9]|interp_instrs=[1-9]"
    ENVIRONMENT "NG_OVERLAY_BUDGET=3")
endif()

option(KOF_BUILD_NG_BENCH "Build the BIOS-only SH-2 speed bench (interpreter vs recomp-only)" OFF)
if(KOF_BUILD_NG_BENCH)
  if(NOT KOF_BUILD_SATURN_ASSETS)
    message(FATAL_ERROR "NG bench requires KOF_BUILD_SATURN_ASSETS=ON")
  endif()
  set(KOF_UPSTREAM_DIR "" CACHE PATH "Pinned KOF98 reference checkout")
  get_filename_component(up "${KOF_UPSTREAM_DIR}" ABSOLUTE)
  set(mus "${up}/neogeorecomp/ext/musashi")
  set(ngsrc "${up}/neogeorecomp/src")
  set(ngtools "${up}/neogeorecomp/tools/m68krecomp")
  set(nginc "${up}/neogeorecomp/include")
  if(NOT EXISTS "${mus}/m68kcpu.c")
    message(FATAL_ERROR "Musashi sources not found at ${mus}; set KOF_UPSTREAM_DIR")
  endif()
  find_program(KOF_HOST_CC NAMES gcc cc clang)
  if(NOT KOF_HOST_CC)
    message(FATAL_ERROR "A host C compiler is required to build the recomp generator")
  endif()
  # Only used to byte-swap the BIOS into 68000 order for the generator; the bench
  # program itself swaps at load time from the disc copy.
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  # Host tool: m68krecomp, built with the *host* compiler even in this cross
  # build (it reads the user's own ROM and emits C; never committed).
  set(bgen "${CMAKE_CURRENT_BINARY_DIR}/ng_bench_gen")
  file(MAKE_DIRECTORY "${bgen}")
  set(m68krecomp_exe "${bgen}/m68krecomp${CMAKE_EXECUTABLE_SUFFIX}")
  add_custom_command(
    OUTPUT "${bgen}/m68kops.c" "${bgen}/m68kops.h"
    COMMAND "${KOF_HOST_CC}" -O2 -o "${bgen}/m68kmake${CMAKE_EXECUTABLE_SUFFIX}" "${mus}/m68kmake.c"
    COMMAND "${bgen}/m68kmake${CMAKE_EXECUTABLE_SUFFIX}" "${bgen}" "${mus}/m68k_in.c"
    DEPENDS "${mus}/m68kmake.c" "${mus}/m68k_in.c"
    COMMENT "Generating Musashi opcode tables for m68krecomp" VERBATIM)
  add_custom_command(
    OUTPUT "${m68krecomp_exe}"
    COMMAND "${KOF_HOST_CC}" -O2 -w "-DMUSASHI_CNF=\"ng_musashi_conf.h\""
            -I "${mus}" -I "${bgen}" -I "${nginc}" -I "${ngsrc}" -I "${ngtools}"
            -o "${m68krecomp_exe}"
            "${ngtools}/main.c" "${ngtools}/recomp.c" "${ngtools}/decode.c" "${ngtools}/emit.c"
            "${ngsrc}/romload.c" "${ngsrc}/musashi_glue.c"
            "${mus}/m68kcpu.c" "${mus}/m68kdasm.c" "${bgen}/m68kops.c"
            "${mus}/softfloat/softfloat.c"
    DEPENDS "${bgen}/m68kops.c" "${mus}/m68kcpu.c" "${mus}/m68kdasm.c"
            "${ngtools}/main.c" "${ngtools}/recomp.c" "${ngtools}/decode.c" "${ngtools}/emit.c"
            "${ngsrc}/romload.c" "${ngsrc}/musashi_glue.c"
    COMMENT "Building m68krecomp (host tool)" VERBATIM)

  # Recompile the BIOS reset/init slice from the user's own BIOS ROM.
  set(bios_rom "${CMAKE_CURRENT_SOURCE_DIR}/roms/neo-po.bin")
  if(NOT EXISTS "${bios_rom}")
    message(FATAL_ERROR "roms/neo-po.bin (AES BIOS) required for the NG bench")
  endif()
  set(bios68k "${bgen}/bios-68k.bin")
  add_custom_command(
    OUTPUT "${bios68k}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${bgen}"
    COMMAND "${CMAKE_COMMAND}" -E env
            "${Python3_EXECUTABLE}" -c
            "import sys;d=bytearray(open(sys.argv[1],'rb').read());d[0::2],d[1::2]=d[1::2],d[0::2];open(sys.argv[2],'wb').write(d)"
            "${bios_rom}" "${bios68k}"
    DEPENDS "${bios_rom}" VERBATIM)
  set(recomp_c "${bgen}/bios_recomp_00.c")
  set(recomp_tab "${bgen}/bios_recomp_table.c")
  set(recomp_common "${bgen}/bios_recomp_common.h")
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BiosEntries.cmake")
  add_custom_command(
    OUTPUT "${recomp_c}" "${recomp_tab}" "${recomp_common}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${bgen}"
    COMMAND "${m68krecomp_exe}" --raw "${bios68k}" --base 0xC00000 ${BIOS_EXTRA_ENTRIES}
            --prefix bios_ --out "${bgen}" --files 1
    DEPENDS "${m68krecomp_exe}" "${bios68k}"
    COMMENT "Recompiling BIOS reset/init routines for SH-2" VERBATIM)

  # Disc payload for both bench targets: just the BIOS (identical workload).
  set(ng_bench_disc "${CMAKE_CURRENT_BINARY_DIR}/ng-bench-disc")
  set(bios_stage "${ng_bench_disc}/BIOS.BIN")
  add_custom_command(OUTPUT "${bios_stage}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${bios_rom}" "${bios_stage}"
    DEPENDS "${bios_rom}" VERBATIM)

  # (1) Interpreter reference: same bench main, Musashi core.
  add_executable(kof_ng_bench_interp
    src/saturn/ng_bench_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_setjmp_shim.c
    "${mus}/m68kcpu.c" "${bgen}/m68kops.c")
  target_include_directories(kof_ng_bench_interp PRIVATE src/ng src/ng/shim "${mus}" "${bgen}")
  target_compile_definitions(kof_ng_bench_interp PRIVATE MUSASHI_CNF="ng_musashi_conf_boot.h" KOF_NG_SATURN=1)
  target_compile_options(kof_ng_bench_interp PRIVATE -w)
  libsaturn_configure_executable(kof_ng_bench_interp)
  libsaturn_add_binary(kof_ng_bench_interp MAX_BYTES 983040)
  target_link_libraries(kof_ng_bench_interp PRIVATE kof_saturn_assets)
  libsaturn_add_disc(kof_ng_bench_interp ALL ROOT_FILES "${bios_stage}")

  # (2) Recomp-only: native BIOS routines, no interpreter linked.
  add_executable(kof_ng_bench_recomp
    src/saturn/ng_bench_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c "${recomp_c}" "${recomp_tab}")
  target_include_directories(kof_ng_bench_recomp PRIVATE src/ng "${mus}" "${bgen}" "${nginc}")
  target_compile_definitions(kof_ng_bench_recomp PRIVATE NG_BOOT_RECOMP_ONLY KOF_NG_SATURN=1)
  target_compile_options(kof_ng_bench_recomp PRIVATE -w)
  libsaturn_configure_executable(kof_ng_bench_recomp)
  libsaturn_add_binary(kof_ng_bench_recomp MAX_BYTES 983040)
  target_link_libraries(kof_ng_bench_recomp PRIVATE kof_saturn_assets)
  libsaturn_add_disc(kof_ng_bench_recomp ALL ROOT_FILES "${bios_stage}")

  # (3) Rawcode game bench: native game routines paged from disc, no renderer/
  # music/tiles so the recomp runtime + overlay fit WRAM. Opt-in because it
  # needs locally generated rawcode pages (PAGES.BIN/RELOCS.BIN/map).
  option(KOF_BUILD_RAWCODE_BENCH "Build the rawcode-overlay game speed bench" OFF)
  if(KOF_BUILD_RAWCODE_BENCH)
    set(KOF_RAWCODE_DIR "" CACHE PATH "Local rawcode pack output (PAGES.BIN/RELOCS.BIN/rawcode_map.c)")
    set(KOF_GAME_MAP_DIR "" CACHE PATH "Local game recomp page map (game_recomp_pages.h, stub_map.c)")
    set(KOF_RAWCODE_MAP_SRC "" CACHE FILEPATH "Pass-2 hot map source (default: stub_map.c in KOF_GAME_MAP_DIR)")
    if(NOT KOF_RAWCODE_DIR OR NOT KOF_GAME_MAP_DIR)
      message(FATAL_ERROR "Set KOF_RAWCODE_DIR and KOF_GAME_MAP_DIR for the rawcode bench")
    endif()
    set(raw_map_src "${KOF_GAME_MAP_DIR}/stub_map.c")
    if(KOF_RAWCODE_MAP_SRC)
      set(raw_map_src "${KOF_RAWCODE_MAP_SRC}")
    endif()
    set(ng_raw_disc "${CMAKE_CURRENT_BINARY_DIR}/ng-rawcode-disc")
    set(raw_bios "${ng_raw_disc}/BIOS.BIN")
    set(raw_p1 "${ng_raw_disc}/P1.BIN")
    set(raw_p2 "${ng_raw_disc}/P2.BIN")
    set(raw_pages "${ng_raw_disc}/PAGES.BIN")
    set(raw_relocs "${ng_raw_disc}/RELOCS.BIN")
    set(raw_l0 "${ng_raw_disc}/L0.BIN")
    set(raw_tiles "${ng_raw_disc}/TILES.BIN")
    add_custom_command(OUTPUT "${raw_bios}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${ng_raw_disc}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${bios_rom}" "${raw_bios}" VERBATIM)
    add_custom_command(OUTPUT "${raw_p1}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${ng_raw_disc}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/roms/242-pn1.bin" "${raw_p1}" VERBATIM)
    add_custom_command(OUTPUT "${raw_p2}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${ng_raw_disc}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/roms/242-p2.bin" "${raw_p2}" VERBATIM)
    add_custom_command(OUTPUT "${raw_pages}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${ng_raw_disc}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${KOF_RAWCODE_DIR}/PAGES.BIN" "${raw_pages}" VERBATIM)
    add_custom_command(OUTPUT "${raw_relocs}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${ng_raw_disc}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${KOF_RAWCODE_DIR}/RELOCS.BIN" "${raw_relocs}" VERBATIM)
    set(raw_l0_src "${CMAKE_CURRENT_SOURCE_DIR}/roms/000-lo.lo")
    if(NOT EXISTS "${raw_l0_src}")
      set(raw_l0_src "${CMAKE_CURRENT_SOURCE_DIR}/roms/000-lo.bin")
    endif()
    add_custom_command(OUTPUT "${raw_l0}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${ng_raw_disc}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${raw_l0_src}" "${raw_l0}" VERBATIM)
    add_custom_command(OUTPUT "${raw_tiles}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${ng_raw_disc}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${KOF_TILE_DIR}/TILES.BIN" "${raw_tiles}" VERBATIM)
    add_executable(kof_ng_rawcode_bench
      src/saturn/ng_rawcode_bench_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c
      src/ng/ng_live_strips.c
      src/saturn/ng_overlay_saturn.c src/saturn/rawcode_loader.c
      src/ng/libgcc_stubs.c
      "${recomp_c}" "${recomp_tab}" "${raw_map_src}")
    # Fully-resident native hot routines (no disc paging) - opt-in.
    option(KOF_RESIDENT_NATIVE "Link profile's hot routines directly (no paging)" OFF)
    # Renderer-enabled bench: link the self-contained dense handlers + verified
    # binary-search dispatch with a minimal 9-entry map. The full 371-routine
    # resident blob (~451 KiB) cannot coexist with the strip renderer in 1 MiB
    # WRAM-H; the dense handlers alone cover the measured SH-2 hotspots.
    option(KOF_DENSE_ONLY "Link only dense handlers (renderer-enabled bench)" OFF)
    if(KOF_RESIDENT_NATIVE AND KOF_DENSE_ONLY)
      target_sources(kof_ng_rawcode_bench PRIVATE
        src/saturn/resident_dispatch.c
        src/ng/dense_routines.c
        src/ng/wram_rescue_routines.c
        "${CMAKE_CURRENT_SOURCE_DIR}/local/ymir/dense_only_map.c")
      target_include_directories(kof_ng_rawcode_bench BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/local/ng_recomp_game256")
    elseif(KOF_RESIDENT_NATIVE)
      target_sources(kof_ng_rawcode_bench PRIVATE
        src/saturn/resident_dispatch.c
        src/ng/dense_routines.c
        "${CMAKE_CURRENT_SOURCE_DIR}/local/ymir/resident_routines.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/local/ymir/resident_map.c")
      target_include_directories(kof_ng_rawcode_bench BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/local/ng_recomp_game256")
    endif()
    target_include_directories(kof_ng_rawcode_bench PRIVATE src/ng src/ng/shim src/saturn
      include "${mus}" "${bgen}" "${nginc}" "${KOF_GAME_MAP_DIR}" "${KOF_TILE_DIR}")
    target_compile_definitions(kof_ng_rawcode_bench PRIVATE NG_BOOT_RECOMP_ONLY KOF_NG_SATURN=1 NG_BOOT_GAME_OVERLAY=1 LIVE_MAX_STRIPS=512)
    if(KOF_GUEST_FRAME_TRACE)
      target_compile_definitions(kof_ng_rawcode_bench PRIVATE NG_GUEST_FRAME_TRACE=1)
    endif()
    option(KOF_32BIT_CYCLES "Use 32-bit guest cycle counters in recompiled code" OFF)
    if(KOF_32BIT_CYCLES)
      target_compile_definitions(kof_ng_rawcode_bench PRIVATE KOF_32BIT_CYCLES=1)
      target_include_directories(kof_ng_rawcode_bench BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/local/ymir/c32)
    endif()
    target_compile_options(kof_ng_rawcode_bench PRIVATE -w)
    target_link_options(kof_ng_rawcode_bench PRIVATE
      -Wl,-u,___ashldi3 -Wl,-u,___ashrdi3 -Wl,-u,___lshrdi3)
    libsaturn_configure_executable(kof_ng_rawcode_bench)
    libsaturn_add_binary(kof_ng_rawcode_bench MAX_BYTES 983040)
    target_link_libraries(kof_ng_rawcode_bench PRIVATE kof_saturn_assets)
    libsaturn_add_disc(kof_ng_rawcode_bench ALL ROOT_FILES
      "${raw_bios}" "${raw_p1}" "${raw_p2}" "${raw_pages}" "${raw_relocs}"
      "${raw_l0}" "${raw_tiles}")
  endif()

  # Hybrid core: native dense handlers for the profiled BIOS hotspots, plus a
  # linked Musashi interpreter for guest-written low RAM/WRAM code (PC < 0x200000)
  # that a static recomp overlay cannot represent. Same validated renderer as the
  # rawcode bench. NG_BOOT_RECOMP (not _ONLY) activates recomp_step's Musashi
  # fallback; NG_BOOT_GUEST_MUSASHI routes live-RAM PCs to it.
  option(KOF_BUILD_HYBRID_BENCH "Build hybrid native+Musashi renderer bench" OFF)
  if(KOF_BUILD_HYBRID_BENCH)
    # Built on the proven kof_ng_boot renderer/cart base. Native recomp for the
    # BIOS + dense handlers; Musashi fallback for guest-written RAM (< 0x200000)
    # that static recomp cannot represent. Dropping the rawcode overlay/game map
    # keeps the image inside WRAM-H (Musashi tables ~600 KiB + BIOS recomp ~472 KiB).
    add_executable(kof_ng_hybrid_bench
      src/saturn/ng_boot_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c
      src/ng/ng_live_strips.c src/ng/ng_setjmp_shim.c
      src/ng/libgcc_stubs.c src/ng/bios_table_stub.c
      "${mus}/m68kcpu.c" "${m68kgen}/m68kops.c")
    set(KOF_HYBRID_MAP "${CMAKE_CURRENT_SOURCE_DIR}/local/ymir/dense_only_map.c" CACHE FILEPATH "Resident map for hybrid bench")
    # Live Z80 + YM2610 sound (upstream audio.c/ymfm, BSD-3) with minimal
    # freestanding C++ shims because the SH-2 toolchain has no libstdc++.
    set(KOF_AUDIO_SOURCES
      src/audio_shims/audio_saturn.c "${ngsrc}/ym2610.cpp"
      "${up}/neogeorecomp/ext/ymfm/src/ymfm_opn.cpp"
      "${up}/neogeorecomp/ext/ymfm/src/ymfm_adpcm.cpp"
      "${up}/neogeorecomp/ext/ymfm/src/ymfm_ssg.cpp"
      "${up}/neogeorecomp/ext/ymfm/src/ymfm_misc.cpp"
      "${up}/neogeorecomp/ext/z80/z80.c"
      src/audio_shims/shim_alloc.c src/audio_shims/shim_alloc.cpp)
    target_sources(kof_ng_hybrid_bench PRIVATE
      src/saturn/resident_dispatch.c
      src/ng/dense_routines.c
      "${KOF_HYBRID_MAP}" ${KOF_AUDIO_SOURCES})
    set_source_files_properties(
      src/audio_shims/shim_alloc.cpp
      "${ngsrc}/ym2610.cpp"
      "${up}/neogeorecomp/ext/ymfm/src/ymfm_opn.cpp"
      "${up}/neogeorecomp/ext/ymfm/src/ymfm_adpcm.cpp"
      "${up}/neogeorecomp/ext/ymfm/src/ymfm_ssg.cpp"
      "${up}/neogeorecomp/ext/ymfm/src/ymfm_misc.cpp"
      PROPERTIES COMPILE_OPTIONS "-std=c++17;-fno-exceptions;-fno-rtti")
    target_include_directories(kof_ng_hybrid_bench BEFORE PRIVATE
      src/audio_shims
      "${CMAKE_CURRENT_SOURCE_DIR}/local/ymir/c32"
      "${CMAKE_CURRENT_SOURCE_DIR}/local/ng_recomp_game256")
    target_include_directories(kof_ng_hybrid_bench PRIVATE
      src/ng src/ng/shim src/saturn include "${mus}" "${m68kgen}" "${nginc}"
      "${ngsrc}" "${up}/neogeorecomp/ext/ymfm/src" "${up}/neogeorecomp/ext/z80"
      "${bgen}" "${KOF_TILE_DIR}" "${KOF_MUSIC_DIR}")
    target_compile_definitions(kof_ng_hybrid_bench PRIVATE
      NG_BOOT_RECOMP NG_BOOT_GUEST_MUSASHI NG_BOOT_LIVE_AUDIO
      KOF_NG_SATURN=1 LIVE_MAX_STRIPS=512 KOF_32BIT_CYCLES=1
      MUSASHI_CNF="ng_musashi_conf_boot.h")
    target_compile_options(kof_ng_hybrid_bench PRIVATE -w)
    target_link_options(kof_ng_hybrid_bench PRIVATE
      -Wl,-u,___ashldi3 -Wl,-u,___ashrdi3 -Wl,-u,___lshrdi3)
    libsaturn_configure_executable(kof_ng_hybrid_bench)
    libsaturn_add_binary(kof_ng_hybrid_bench MAX_BYTES 983040)
    target_link_libraries(kof_ng_hybrid_bench PRIVATE kof_saturn_assets)
    set(sm1_stage "${CMAKE_CURRENT_BINARY_DIR}/ng-hybrid-disc/SM1.BIN")
    set(m1_stage "${CMAKE_CURRENT_BINARY_DIR}/ng-hybrid-disc/M1.BIN")
    add_custom_command(OUTPUT "${sm1_stage}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/ng-hybrid-disc"
      COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/roms/sm1.sm1" "${sm1_stage}" VERBATIM)
    add_custom_command(OUTPUT "${m1_stage}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/ng-hybrid-disc"
      COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/roms/242-s1.bin" "${m1_stage}" VERBATIM)
    libsaturn_add_disc(kof_ng_hybrid_bench ALL ROOT_FILES
      "${bios_stage}" "${p1_stage}" "${p2_stage}" "${l0_stage}" "${tile_stage}" "${music_stage}" "${sm1_stage}" "${m1_stage}")
  endif()

  # Fully-resident native speed bench (opt-in, no renderer/paging).
  option(KOF_BUILD_RESIDENT_BENCH "Build the fully-resident native speed bench" OFF)
  if(KOF_BUILD_RESIDENT_BENCH)
    add_executable(kof_resident_bench
      src/saturn/resident_bench_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c src/ng/libgcc_stubs.c
      src/saturn/resident_dispatch.c
      src/ng/dense_routines.c
      "${CMAKE_CURRENT_SOURCE_DIR}/local/ymir/resident_routines.c"
      "${CMAKE_CURRENT_SOURCE_DIR}/local/ymir/resident_map.c"
      "${recomp_c}" "${recomp_tab}")
    target_include_directories(kof_resident_bench BEFORE PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/local/ymir/c32"
      "${CMAKE_CURRENT_SOURCE_DIR}/local/ng_recomp_game256"
      src/ng src/ng/shim src/saturn include "${mus}" "${m68kgen}" "${nginc}")
    target_compile_definitions(kof_resident_bench PRIVATE
      KOF_NG_SATURN=1 NG_BOOT_RECOMP=1 NG_BOOT_RECOMP_ONLY=1
      KOF_32BIT_CYCLES=1)
    target_compile_options(kof_resident_bench PRIVATE -w)
    libsaturn_configure_executable(kof_resident_bench)
    libsaturn_add_binary(kof_resident_bench MAX_BYTES 983040)
    target_link_libraries(kof_resident_bench PRIVATE kof_saturn_assets)
    libsaturn_add_disc(kof_resident_bench ALL ROOT_FILES "${bios_stage}" "${p1_stage}" "${p2_stage}")

    # Profile-pruned native core (architecture redesign P2). Same RECOMP_ONLY
    # guest as kof_resident_bench, but the two generated recomp blobs and their
    # dispatch tables are replaced by the pruned selection produced locally by
    # tools/make_pruned_native.py. Dense handlers and the binary-search + tagged
    # cache dispatch are unchanged. Local build artifact only; no source of
    # game-derived code is committed.
    option(KOF_BUILD_PRUNED_BENCH "Build the profile-pruned RECOMP_ONLY bench" OFF)
    if(KOF_BUILD_PRUNED_BENCH)
      set(KOF_PRUNED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/local/pruned" CACHE PATH "Local pruned native artifacts")
      foreach(_pf pruned_native.c pruned_resident_map.c pruned_bios_table.c)
        if(NOT EXISTS "${KOF_PRUNED_DIR}/${_pf}")
          message(FATAL_ERROR "Missing ${KOF_PRUNED_DIR}/${_pf}; run tools/make_pruned_native.py first")
        endif()
      endforeach()
      add_executable(kof_resident_pruned
        src/saturn/resident_bench_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c src/ng/libgcc_stubs.c
        src/saturn/resident_dispatch.c
        src/ng/dense_routines.c src/ng/wram_rescue_routines.c
        "${KOF_PRUNED_DIR}/pruned_native.c"
        "${KOF_PRUNED_DIR}/pruned_resident_map.c"
        "${KOF_PRUNED_DIR}/pruned_bios_table.c")
      target_include_directories(kof_resident_pruned BEFORE PRIVATE
        "${KOF_PRUNED_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/local/ymir/c32"
        "${CMAKE_CURRENT_SOURCE_DIR}/local/ng_recomp_game256"
        "${CMAKE_CURRENT_SOURCE_DIR}/build-ng/ng_bench_gen"
        src/ng src/ng/shim src/saturn include "${mus}" "${m68kgen}" "${nginc}")
      target_compile_definitions(kof_resident_pruned PRIVATE
        KOF_NG_SATURN=1 NG_BOOT_RECOMP=1 NG_BOOT_RECOMP_ONLY=1
        KOF_32BIT_CYCLES=1)
      target_compile_options(kof_resident_pruned PRIVATE -w)
      libsaturn_configure_executable(kof_resident_pruned)
      libsaturn_add_binary(kof_resident_pruned MAX_BYTES 983040)
      target_link_libraries(kof_resident_pruned PRIVATE kof_saturn_assets)
      libsaturn_add_disc(kof_resident_pruned ALL ROOT_FILES "${bios_stage}" "${p1_stage}" "${p2_stage}")

      # Unified target (architecture redesign P3/P4/P5): the SAME pruned
      # RECOMP_ONLY native guest plus the validated gated renderer and the
      # live Z80/YM2610 audio path. unified_main.c is a flat-out bench main
      # based on the validated ng_rawcode_bench_main.c: guest frames advance
      # as fast as the SH-2 can execute them, VDP1 work is gated to the render
      # frame, and audio is serviced from the core slice hook. The 4 MiB cart
      # stays data-only; all executable native code is WRAM-H resident.
      option(KOF_BUILD_UNIFIED "Build unified native + renderer + live audio target" OFF)
      if(KOF_BUILD_UNIFIED)
        set(KOF_AUDIO_SOURCES_U
          src/audio_shims/audio_saturn.c "${ngsrc}/ym2610.cpp"
          "${up}/neogeorecomp/ext/ymfm/src/ymfm_opn.cpp"
          "${up}/neogeorecomp/ext/ymfm/src/ymfm_adpcm.cpp"
          "${up}/neogeorecomp/ext/ymfm/src/ymfm_ssg.cpp"
          "${up}/neogeorecomp/ext/ymfm/src/ymfm_misc.cpp"
          "${up}/neogeorecomp/ext/z80/z80.c"
          src/audio_shims/shim_alloc.c src/audio_shims/shim_alloc.cpp)
        add_executable(kof_unified
          src/saturn/unified_main.c src/ng/ng_boot.c src/ng/ng_boot_stats.c src/ng/ng_boot_recomp.c
          src/ng/ng_live_strips.c src/ng/libgcc_stubs.c
          src/saturn/resident_dispatch.c
          src/ng/dense_routines.c src/ng/wram_rescue_routines.c
          "${KOF_PRUNED_DIR}/pruned_native.c"
          "${KOF_PRUNED_DIR}/pruned_resident_map.c"
          "${KOF_PRUNED_DIR}/pruned_bios_table.c"
          ${KOF_AUDIO_SOURCES_U})
        set_source_files_properties(
          src/audio_shims/shim_alloc.cpp "${ngsrc}/ym2610.cpp"
          "${up}/neogeorecomp/ext/ymfm/src/ymfm_opn.cpp"
          "${up}/neogeorecomp/ext/ymfm/src/ymfm_adpcm.cpp"
          "${up}/neogeorecomp/ext/ymfm/src/ymfm_ssg.cpp"
          "${up}/neogeorecomp/ext/ymfm/src/ymfm_misc.cpp"
          PROPERTIES COMPILE_OPTIONS "-std=c++17;-fno-exceptions;-fno-rtti")
        target_include_directories(kof_unified BEFORE PRIVATE
          src/audio_shims "${KOF_PRUNED_DIR}"
          "${CMAKE_CURRENT_SOURCE_DIR}/local/ymir/c32"
          "${CMAKE_CURRENT_SOURCE_DIR}/local/ng_recomp_game256"
          "${CMAKE_CURRENT_SOURCE_DIR}/build-ng/ng_bench_gen")
        target_include_directories(kof_unified PRIVATE
          src/ng src/ng/shim src/saturn include "${mus}" "${m68kgen}" "${nginc}"
          "${ngsrc}" "${up}/neogeorecomp/ext/ymfm/src" "${up}/neogeorecomp/ext/z80"
          "${bgen}" "${KOF_TILE_DIR}" "${KOF_MUSIC_DIR}")
        target_compile_definitions(kof_unified PRIVATE
          KOF_NG_SATURN=1 NG_BOOT_RECOMP=1 NG_BOOT_RECOMP_ONLY=1 NG_BOOT_LIVE_AUDIO=1
          LIVE_MAX_STRIPS=512 KOF_32BIT_CYCLES=1
          MUSASHI_CNF="ng_musashi_conf_boot.h")
        target_compile_options(kof_unified PRIVATE -w)
        target_link_options(kof_unified PRIVATE
          -Wl,-u,___ashldi3 -Wl,-u,___ashrdi3 -Wl,-u,___lshrdi3)
        libsaturn_configure_executable(kof_unified)
        libsaturn_add_binary(kof_unified MAX_BYTES 983040)
        target_link_libraries(kof_unified PRIVATE kof_saturn_assets)
        set(sm1_stage_u "${CMAKE_CURRENT_BINARY_DIR}/ng-unified-disc/SM1.BIN")
        set(m1_stage_u "${CMAKE_CURRENT_BINARY_DIR}/ng-unified-disc/M1.BIN")
        add_custom_command(OUTPUT "${sm1_stage_u}"
          COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/ng-unified-disc"
          COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/roms/sm1.sm1" "${sm1_stage_u}" VERBATIM)
        add_custom_command(OUTPUT "${m1_stage_u}"
          COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/ng-unified-disc"
          COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/roms/242-s1.bin" "${m1_stage_u}" VERBATIM)
        libsaturn_add_disc(kof_unified ALL ROOT_FILES
          "${bios_stage}" "${p1_stage}" "${p2_stage}" "${l0_stage}" "${tile_stage}"
          "${music_stage}" "${sm1_stage_u}" "${m1_stage_u}")
      endif()
    endif()
  endif()
endif()

option(KOF_BUILD_FRAME_CAPTURE "Build pinned interpreter-only frame capture" OFF)
if(KOF_BUILD_FRAME_CAPTURE)
  include(cmake/FrameCapture.cmake)
endif()
set(KOF_FRAME_DIR "" CACHE PATH "Local validated static frame pack")
if(KOF_FRAME_DIR)
  if(NOT KOF_BUILD_SATURN_ASSETS)
    message(FATAL_ERROR "Frame replay requires KOF_BUILD_SATURN_ASSETS=ON")
  endif()
  if(NOT EXISTS "${KOF_FRAME_DIR}/frame_manifest.h" OR NOT EXISTS "${KOF_FRAME_DIR}/FRAME.BIN")
    message(FATAL_ERROR "Run tools/pack_frame.py first")
  endif()
  add_executable(kof_frame_replay src/saturn/frame_replay.c)
  target_include_directories(kof_frame_replay PRIVATE "${KOF_FRAME_DIR}")
  target_link_libraries(kof_frame_replay PRIVATE kof_saturn_assets)
  libsaturn_configure_executable(kof_frame_replay)
  libsaturn_add_binary(kof_frame_replay MAX_BYTES 983040)
  libsaturn_add_disc(kof_frame_replay ROOT_FILES "${KOF_FRAME_DIR}/FRAME.BIN")
endif()

option(KOF_LIVE_INDEPENDENT_SPRITES "Advance sprites independently of FIX-only raster changes" OFF)
option(KOF_LIVE_ZOOM_RUNS "Memoize contiguous source tile runs and flipped rows" OFF)
option(KOF_LIVE_SEGMENT_SCHEDULE "Schedule visibility by constant vertical coverage segments" OFF)
option(KOF_LIVE_SLAVE_HASH "Verify completed live strips on Slave SH-2 while Master emits VDP1" OFF)
option(KOF_LIVE_FAST_HASH "Unroll exact ordered KFR2 strip hashing" OFF)
option(KOF_LIVE_FIX_MAP_REUSE "Reuse published VDP2 map and CRAM on exact FIX state reuse" OFF)
option(KOF_LIVE_ZOOM_MEMO "Memoize L0 vertical source rows and skip contiguous visibility checks" OFF)
option(KOF_LIVE_FIX_VDP2 "Render constant live FIX snapshots through VDP2, fallback for raster changes" OFF)
option(KOF_LIVE_FIX_REUSE "Build FIX on independent raster intervals and reuse exact constant frames" ON)
option(KOF_LIVE_EMIT_CACHE "Reuse exact per-strip pattern and LUT cache slots" OFF)
option(KOF_LIVE_FRAME_CACHE "Reuse exact repeated live raster frames from a content-keyed cart cache" OFF)
option(KOF_LIVE_FRAME_CACHE_VBLANK_WAIT "Pace the frame cache with sat_wait_vblank" OFF)
option(KOF_LIVE_STAGE_VDP2 "Offload the first 25 live strips to VDP2 NBG1" OFF)
option(KOF_LIVE_PACKED_KEYS "Use compact exact pattern keys and single-pass strip validation" ON)
option(KOF_LIVE_LUT_MEMO "Memoize raw palettes to generation-checked resident LUT slots" ON)
option(KOF_LIVE_RANGE_SCHEDULE "Schedule only the valid control-state interval in epoch mode" ON)
option(KOF_LIVE_EPOCHS "Build strips in constant raster-state epochs" ON)
option(KOF_LIVE_VERSIONED_STRIPS "Use exact versioned palette keys during strip coalescing" ON)
option(KOF_LIVE_SCHEDULED "Use cached LSPC scanline schedule in live replay" ON)
option(KOF_SEQUENCE_BATCH "Emit replay sprites through SDK direct batch window" ON)
option(KOF_SEQUENCE_BATCH_PRECLIP_OPT "Enable SDK safe pre-clipping skip for batch sprites" OFF)
set(KOF_SEQUENCE_DIR "" CACHE PATH "Local validated replay sequence")
set(KOF_SEQUENCE_LOAD_CHUNK_BYTES 131072 CACHE STRING "Sequence CD read scratch bytes (sector multiple, 8192..131072)")
if(KOF_SEQUENCE_DIR)
  if(NOT KOF_BUILD_SATURN_ASSETS)
    message(FATAL_ERROR "Sequence replay requires KOF_BUILD_SATURN_ASSETS=ON")
  endif()
  if(NOT EXISTS "${KOF_SEQUENCE_DIR}/sequence_manifest.h" OR NOT EXISTS "${KOF_SEQUENCE_DIR}/SEQUENCE.BIN")
    message(FATAL_ERROR "Run tools/pack_sequence.py first")
  endif()
  add_executable(kof_sequence_replay src/saturn/sequence_replay.c)
  target_compile_definitions(kof_sequence_replay PRIVATE KOF_SEQUENCE_LOAD_CHUNK_BYTES=${KOF_SEQUENCE_LOAD_CHUNK_BYTES}
    KOF_LIVE_SCHEDULED=$<BOOL:${KOF_LIVE_SCHEDULED}>
    KOF_LIVE_VERSIONED_STRIPS=$<BOOL:${KOF_LIVE_VERSIONED_STRIPS}>
    KOF_LIVE_EPOCHS=$<BOOL:${KOF_LIVE_EPOCHS}>
    KOF_LIVE_RANGE_SCHEDULE=$<BOOL:${KOF_LIVE_RANGE_SCHEDULE}>
    KOF_LIVE_LUT_MEMO=$<BOOL:${KOF_LIVE_LUT_MEMO}>
    KOF_LIVE_EMIT_CACHE=$<BOOL:${KOF_LIVE_EMIT_CACHE}>
    KOF_LIVE_FRAME_CACHE=$<BOOL:${KOF_LIVE_FRAME_CACHE}>
    KOF_LIVE_FRAME_CACHE_VBLANK_WAIT=$<BOOL:${KOF_LIVE_FRAME_CACHE_VBLANK_WAIT}>
    KOF_LIVE_STAGE_VDP2=$<BOOL:${KOF_LIVE_STAGE_VDP2}>
    KOF_LIVE_PACKED_KEYS=$<BOOL:${KOF_LIVE_PACKED_KEYS}>
    KOF_LIVE_FIX_REUSE=$<BOOL:${KOF_LIVE_FIX_REUSE}>
    KOF_LIVE_FIX_VDP2=$<BOOL:${KOF_LIVE_FIX_VDP2}>
    KOF_LIVE_ZOOM_MEMO=$<BOOL:${KOF_LIVE_ZOOM_MEMO}>
    KOF_LIVE_ZOOM_RUNS=$<BOOL:${KOF_LIVE_ZOOM_RUNS}>
    KOF_LIVE_COMPACT_BUILD=$<BOOL:${KOF_LIVE_COMPACT_BUILD}>
    KOF_LIVE_INDEPENDENT_SPRITES=$<BOOL:${KOF_LIVE_INDEPENDENT_SPRITES}>
    KOF_LIVE_FAST_HASH=$<BOOL:${KOF_LIVE_FAST_HASH}>
    KOF_LIVE_SLAVE_HASH=$<BOOL:${KOF_LIVE_SLAVE_HASH}>
    KOF_LIVE_SEGMENT_SCHEDULE=$<BOOL:${KOF_LIVE_SEGMENT_SCHEDULE}>
    KOF_LIVE_FIX_MAP_REUSE=$<BOOL:${KOF_LIVE_FIX_MAP_REUSE}>
    KOF_SEQUENCE_BATCH=$<BOOL:${KOF_SEQUENCE_BATCH}>
    KOF_SEQUENCE_BATCH_PRECLIP_OPT=$<BOOL:${KOF_SEQUENCE_BATCH_PRECLIP_OPT}>)
  target_include_directories(kof_sequence_replay PRIVATE "${KOF_SEQUENCE_DIR}")
  target_link_libraries(kof_sequence_replay PRIVATE kof_saturn_assets)
  libsaturn_configure_executable(kof_sequence_replay)
  libsaturn_add_binary(kof_sequence_replay MAX_BYTES 983040)
  libsaturn_add_disc(kof_sequence_replay ROOT_FILES "${KOF_SEQUENCE_DIR}/SEQUENCE.BIN")
endif()
