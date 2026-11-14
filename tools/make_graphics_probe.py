"""Generate original synthetic 4bpp fixtures, no commercial ROM data."""
import sys
import struct
import zlib
from pathlib import Path

def main(out):
    out.mkdir(parents=True, exist_ok=True)
    ids=struct.pack(">II",1,2)
    pixels=bytearray()
    for t in range(2):
        for y in range(16):
            def pen(x):
                if x in (0,15) or y in (0,15): return 15
                if x==y or (t and x+y==15): return 0
                if x<5: return 1
                if x<10: return 2
                return 3
            for x in range(0,16,2):
                pixels.append((pen(x)<<4)|pen(x+1))
    data=ids+bytes(32-len(ids))+pixels
    (out/"PROBE.BIN").write_bytes(data)
    (out/"probe_manifest.h").write_text(
        '#ifndef KOF_PROBE_MANIFEST_H\n#define KOF_PROBE_MANIFEST_H\n'
        '#include "kof98/asset_store.h"\n'
        'static const kof_asset_desc probe_assets[] = {\n'
        f'    {{1u, 0u, 8u, 0x{zlib.crc32(ids):08X}u}},\n'
        f'    {{2u, 32u, 256u, 0x{zlib.crc32(pixels):08X}u}},\n'
        '};\n#endif\n', encoding='ascii')

if __name__ == "__main__":
    main(Path(sys.argv[1]))
