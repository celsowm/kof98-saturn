"""Insert a read-only capture hook into the pinned desktop video source."""
import sys
from pathlib import Path

def instrument(text):
    anchor = '    draw_sprites(scanline, row);\n    draw_fix(scanline, row);'
    if text.count(anchor) != 1:
        raise ValueError("unexpected upstream render path")
    declaration = ('extern void kof_capture_line(int, int, const uint16_t *, unsigned, '
                   'const uint8_t *, const uint16_t *, int, const uint32_t *);\n')
    return '#include <stdint.h>\n' + declaration + text.replace(anchor, '''    draw_sprites(scanline, row);
    draw_fix(scanline, row);
    kof_capture_line(line, ng_screen_w, lspc_vram, lspc_anim_counter,
                     s_zoom_rom, s_palram[s_pal_bank], s_fix_use_bios, row);''')

if __name__ == "__main__":
    src, dst = map(Path, sys.argv[1:])
    dst.write_text(instrument(src.read_text()), encoding="utf-8")
