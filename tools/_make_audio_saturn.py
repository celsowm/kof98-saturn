#!/usr/bin/env python3
"""Create local Saturn copies of upstream audio.c/ng_internal.h (local tool).

The only patch: respect KOF_32BIT_CYCLES for ng_cycles so audio.c matches the
recomp guest ABI. Upstream is never modified; BSD-3 notice is preserved.
"""
from pathlib import Path

src = Path("deps/kof98recomp/neogeorecomp/src")
dst = Path("src/audio_shims")

internal = (src / "ng_internal.h").read_text()
old = "    extern int64_t ng_cycles;\n    return (int)((ng_cycles - ng_frame_start) / NG_CLOCKS_PER_LINE);"
new = ("#ifdef KOF_32BIT_CYCLES\n"
       "    extern int32_t ng_cycles;\n"
       "#else\n"
       "    extern int64_t ng_cycles;\n"
       "#endif\n"
       "    return (int)((ng_cycles - ng_frame_start) / NG_CLOCKS_PER_LINE);")
assert old in internal, "ng_cycles declaration not found"
internal = internal.replace(old, new)
(dst / "ng_internal.h").write_text(internal)

audio = (src / "audio.c").read_text()
old_rom = "static const uint8_t *rom(void) { return s_use_bios_rom ? s_sm1 : s_m1; }"
new_rom = "static const uint8_t *rom(void) { const uint8_t* r = s_use_bios_rom ? s_sm1 : s_m1; return r ? r : s_sm1; }"
assert old_rom in audio
audio = audio.replace(old_rom, new_rom)
notice = ("/* Saturn audio.c - local copy from kof98recomp/neogeorecomp (BSD-3).\n"
          " * Patches: ng_internal.h honors KOF_32BIT_CYCLES; rom() falls back to\n"
          " * SM1 when the game M1 ROM is not loaded. */\n")
(dst / "audio_saturn.c").write_text(notice + audio)
print("wrote audio_saturn.c + patched ng_internal.h")
